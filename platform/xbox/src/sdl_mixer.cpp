// Xbox SDL3 compatibility layer: SDL3_mixer on DirectSound.
//
// A software mixer adds up the playing tracks (16-bit PCM from WAV files, Ogg
// Vorbis decoded on the fly with stb_vorbis) into 44.1 kHz stereo, on its own
// thread, and feeds the result to one DirectSound stream in small packets.
//
// "Track stopped" callbacks are collected on the mixer thread and delivered on
// the game's thread (in SDL_PollEvent), where the game expects to run. A
// callback is dropped if the track was given new audio or restarted meanwhile.

#include <xtl.h>
#include <dsound.h>

#include <SDL3_mixer/SDL_mixer.h>

#include "sdl_internal.h"

extern "C" {
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct MIX_Audio {
    bool vorbis = false;
    // PCM: interleaved 16-bit samples
    std::vector<Sint16> pcm;
    int channels = 2;
    int rate = 44100;
    size_t frames = 0;
    // Vorbis: the whole compressed file
    std::vector<unsigned char> ogg;
};

struct MIX_Track {
    MIX_Mixer* mixer = nullptr;
    MIX_Audio* audio = nullptr;
    std::vector<std::string> tags;
    MIX_TrackStoppedCallback callback = nullptr;
    void* callback_userdata = nullptr;

    bool playing = false;
    bool paused = false;
    Sint64 loops = 0;     // remaining extra loops; -1: forever
    float gain = 1.0f;
    Uint32 generation = 0;

    // Playback position, 16.16 fixed point frames
    Uint64 pos = 0;
    Uint32 step = 1u << 16;

    // Vorbis decoding
    stb_vorbis* decoder = nullptr;
    std::vector<Sint16> chunk;  // decoded stereo frames
    int chunk_frames = 0;
};

struct MIX_Mixer {
    std::vector<MIX_Track*> tracks;
    std::map<std::string, float> tag_gains;
};

namespace {
constexpr int k_rate = 44100;
constexpr int k_packets = 8;
constexpr int k_packet_frames = 512;       // ~11.6 ms per packet
constexpr int k_vorbis_chunk_frames = 1024;

CRITICAL_SECTION g_lock;
bool g_lock_ready = false;
MIX_Mixer* g_mixer = nullptr;

LPDIRECTSOUND g_dsound = nullptr;
LPDIRECTSOUNDSTREAM g_stream = nullptr;
HANDLE g_thread = nullptr;
volatile bool g_thread_running = false;

Sint16 g_packet_data[k_packets][k_packet_frames * 2];
DWORD g_packet_status[k_packets];
DWORD g_packet_completed[k_packets];
int g_mix_buffer[k_packet_frames * 2];

struct PendingStop {
    MIX_Track* track;
    Uint32 generation;
};
std::vector<PendingStop> g_pending_stops;

struct Lock {
    Lock() { EnterCriticalSection(&g_lock); }
    ~Lock() { LeaveCriticalSection(&g_lock); }
};

void ensureLock()
{
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }
}

bool hasTag(const MIX_Track* track, const char* tag)
{
    return std::find(track->tags.begin(), track->tags.end(), tag) != track->tags.end();
}

float effectiveGain(const MIX_Track* track)
{
    float gain = track->gain;
    for (const std::string& tag : track->tags) {
        auto it = track->mixer->tag_gains.find(tag);
        if (it != track->mixer->tag_gains.end())
            gain *= it->second;
    }
    return gain;
}

void closeDecoder(MIX_Track* track)
{
    if (track->decoder) {
        stb_vorbis_close(track->decoder);
        track->decoder = nullptr;
    }
    track->chunk_frames = 0;
}

// Stops a track and queues its callback; call with the lock held
void stopLocked(MIX_Track* track)
{
    if (!track->playing)
        return;
    track->playing = false;
    track->paused = false;
    closeDecoder(track);
    if (track->callback)
        g_pending_stops.push_back({track, track->generation});
}

bool startLocked(MIX_Track* track, Sint64 loops)
{
    closeDecoder(track);
    track->generation++;
    track->playing = false;
    track->paused = false;
    track->pos = 0;
    track->loops = loops < -1 ? -1 : loops;

    MIX_Audio* audio = track->audio;
    if (!audio)
        return SDL_SetError("The track has no audio");

    if (audio->vorbis) {
        int error = 0;
        track->decoder = stb_vorbis_open_memory(audio->ogg.data(), static_cast<int>(audio->ogg.size()), &error, nullptr);
        if (!track->decoder)
            return SDL_SetError("Couldn't decode Ogg Vorbis audio (error %d)", error);
        const stb_vorbis_info info = stb_vorbis_get_info(track->decoder);
        track->step = static_cast<Uint32>((static_cast<Uint64>(info.sample_rate) << 16) / k_rate);
        track->chunk.resize(static_cast<size_t>(k_vorbis_chunk_frames) * 2);
        track->chunk_frames = 0;
    } else {
        if (audio->frames == 0)
            return SDL_SetError("The audio is empty");
        track->step = static_cast<Uint32>((static_cast<Uint64>(audio->rate) << 16) / k_rate);
    }
    track->playing = true;
    return true;
}

// Decodes the next chunk of a Vorbis track; false at the end of the stream
bool refillVorbis(MIX_Track* track)
{
    const int frames = stb_vorbis_get_samples_short_interleaved(track->decoder, 2, track->chunk.data(), k_vorbis_chunk_frames * 2);
    track->chunk_frames = frames > 0 ? frames : 0;
    return frames > 0;
}

// Loops back to the start or stops at the end; false if the track stopped
bool handleEnd(MIX_Track* track)
{
    if (track->loops == 0) {
        stopLocked(track);
        return false;
    }
    if (track->loops > 0)
        track->loops--;
    track->pos = 0;
    if (track->decoder) {
        stb_vorbis_seek_start(track->decoder);
        track->chunk_frames = 0;
    }
    return true;
}

void mixTrack(MIX_Track* track, int* out, int frames)
{
    const int gain = static_cast<int>(effectiveGain(track) * 256.0f + 0.5f);
    MIX_Audio* audio = track->audio;

    for (int i = 0; i < frames && track->playing; i++) {
        int left = 0, right = 0;

        if (audio->vorbis) {
            size_t idx = static_cast<size_t>(track->pos >> 16);
            int attempts = 0;
            while (idx >= static_cast<size_t>(track->chunk_frames)) {
                track->pos -= static_cast<Uint64>(track->chunk_frames) << 16;
                if (!refillVorbis(track)) {
                    if (++attempts > 2 || !handleEnd(track))
                        break;
                    continue;
                }
                idx = static_cast<size_t>(track->pos >> 16);
            }
            if (!track->playing || idx >= static_cast<size_t>(track->chunk_frames))
                break;
            left = track->chunk[idx * 2];
            right = track->chunk[idx * 2 + 1];
        } else {
            size_t idx = static_cast<size_t>(track->pos >> 16);
            if (idx >= audio->frames) {
                if (!handleEnd(track))
                    break;
                idx = 0;
            }
            if (audio->channels == 2) {
                left = audio->pcm[idx * 2];
                right = audio->pcm[idx * 2 + 1];
            } else {
                left = right = audio->pcm[idx];
            }
        }

        out[i * 2] += (left * gain) >> 8;
        out[i * 2 + 1] += (right * gain) >> 8;
        track->pos += track->step;
    }
}

void mixPacket(Sint16* dst)
{
    memset(g_mix_buffer, 0, sizeof(g_mix_buffer));
    {
        Lock lock;
        if (g_mixer) {
            for (MIX_Track* track : g_mixer->tracks) {
                if (track->playing && !track->paused && track->audio)
                    mixTrack(track, g_mix_buffer, k_packet_frames);
            }
        }
    }
    for (int i = 0; i < k_packet_frames * 2; i++)
        dst[i] = static_cast<Sint16>(std::clamp(g_mix_buffer[i], -32768, 32767));
}

DWORD WINAPI mixerThread(LPVOID)
{
    while (g_thread_running) {
        const Uint64 start = sdlx_cycles();
        DirectSoundDoWork();
        for (int i = 0; i < k_packets; i++) {
            if (g_packet_status[i] == static_cast<DWORD>(XMEDIAPACKET_STATUS_PENDING))
                continue;

            mixPacket(g_packet_data[i]);

            XMEDIAPACKET packet;
            memset(&packet, 0, sizeof(packet));
            packet.pvBuffer = g_packet_data[i];
            packet.dwMaxSize = sizeof(g_packet_data[i]);
            packet.pdwCompletedSize = &g_packet_completed[i];
            packet.pdwStatus = &g_packet_status[i];
            g_packet_status[i] = static_cast<DWORD>(XMEDIAPACKET_STATUS_PENDING);
            if (FAILED(IDirectSoundStream_Process(g_stream, &packet, NULL)))
                g_packet_status[i] = 0;
        }
        g_sdlx_profile.mixer = g_sdlx_profile.mixer + (sdlx_cycles() - start);
        Sleep(2);
    }
    return 0;
}

bool startOutput()
{
    HRESULT hr = DirectSoundCreate(NULL, &g_dsound, NULL);
    if (FAILED(hr))
        return SDL_SetError("DirectSoundCreate failed (0x%08x)", static_cast<unsigned>(hr));

    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = k_rate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = k_rate * 4;

    DSSTREAMDESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.dwMaxAttachedPackets = k_packets;
    desc.lpwfxFormat = &wfx;

    hr = IDirectSound_CreateSoundStream(g_dsound, &desc, &g_stream, NULL);
    if (FAILED(hr) || !g_stream)
        return SDL_SetError("CreateSoundStream failed (0x%08x)", static_cast<unsigned>(hr));

    IDirectSoundStream_SetVolume(g_stream, DSBVOLUME_MAX);
    IDirectSoundStream_SetHeadroom(g_stream, 0);

    DSMIXBINVOLUMEPAIR pairs[2];
    pairs[0].dwMixBin = DSMIXBIN_FRONT_LEFT;
    pairs[0].lVolume = DSBVOLUME_MAX;
    pairs[1].dwMixBin = DSMIXBIN_FRONT_RIGHT;
    pairs[1].lVolume = DSBVOLUME_MAX;
    DSMIXBINS mixbins;
    mixbins.dwMixBinCount = 2;
    mixbins.lpMixBinVolumePairs = pairs;
    IDirectSoundStream_SetMixBins(g_stream, &mixbins);

    memset(g_packet_status, 0, sizeof(g_packet_status));
    memset(g_packet_completed, 0, sizeof(g_packet_completed));

    g_thread_running = true;
    g_thread = CreateThread(NULL, 64 * 1024, mixerThread, NULL, 0, NULL);
    if (!g_thread) {
        g_thread_running = false;
        return SDL_SetError("Couldn't start the audio thread");
    }
    SetThreadPriority(g_thread, THREAD_PRIORITY_HIGHEST);
    return true;
}

void stopOutput()
{
    if (g_thread) {
        g_thread_running = false;
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stream) {
        IDirectSoundStream_Release(g_stream);
        g_stream = nullptr;
    }
    if (g_dsound) {
        IDirectSound_Release(g_dsound);
        g_dsound = nullptr;
    }
}

bool readFile(const char* path, std::vector<unsigned char>& out)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return SDL_SetError("Couldn't open %s", path);
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return SDL_SetError("Couldn't read %s", path);
    }
    out.resize(static_cast<size_t>(size));
    const size_t read = fread(out.data(), 1, out.size(), file);
    fclose(file);
    if (read != out.size())
        return SDL_SetError("Couldn't read %s", path);
    return true;
}

// ---- Sounds and music from the disc, copied to the cache drive
//
// The sound effects (50 WAVs, 6 MB) are read at startup and a match's music when it starts: each a
// DVD seek and a slow read. The first time, a file from the disc is copied to the cache drive as it
// is, and read from there afterwards (as the decoded images are, in sdl_image.cpp). An entry is the
// file's path (hashed) and its size (a changed file: copied again).
constexpr Uint32 SOUND_CACHE_MAGIC = 0x53574D53;    // "SMWS"
constexpr Uint64 SOUND_CACHE_BUDGET = 200ull << 20;

Sint64 g_sound_cache_used = -1;    // (bytes in the cache folder: counted at the first write)

std::string soundCacheDir()
{
    return std::string(SDL_XboxGetCachePath()) + "snd/";
}

std::string soundCacheFile(const char* file)
{
    //(FNV-1a of the path, its case and slashes evened out)
    Uint64 hash = 1469598103934665603ull;
    for (const char* c = file; *c; c++) {
        char ch = *c == '\\' ? '/' : *c;
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
        hash = (hash ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
    }
    char name[32];
    snprintf(name, sizeof(name), "%016llx.snd", static_cast<unsigned long long>(hash));
    return soundCacheDir() + name;
}

bool readSoundFile(const char* path, std::vector<unsigned char>& out)
{
    if (!((path[0] == 'D' || path[0] == 'd') && path[1] == ':'))
        return readFile(path, out);

    long size = -1;
    if (FILE* file = fopen(path, "rb")) {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        fclose(file);
    }
    if (size <= 0)
        return readFile(path, out);

    const std::string cached = soundCacheFile(path);
    if (FILE* in = fopen(cached.c_str(), "rb")) {
        Uint32 header[2] = {};
        bool ok = fread(header, sizeof(header), 1, in) == 1 && header[0] == SOUND_CACHE_MAGIC && header[1] == static_cast<Uint32>(size);
        if (ok) {
            out.resize(static_cast<size_t>(size));
            ok = fread(out.data(), 1, out.size(), in) == out.size();
        }
        fclose(in);
        if (ok)
            return true;
    }

    if (!readFile(path, out))
        return false;
    const Uint64 bytes = sizeof(Uint32) * 2 + out.size();
    if (g_sound_cache_used < 0)
        g_sound_cache_used = static_cast<Sint64>(sdlx_dir_bytes(soundCacheDir().c_str()));
    if (static_cast<Uint64>(g_sound_cache_used) + bytes > SOUND_CACHE_BUDGET || !sdlx_make_dir(soundCacheDir().c_str()))
        return true;
    if (FILE* copy = fopen(cached.c_str(), "wb")) {
        const Uint32 header[2] = {SOUND_CACHE_MAGIC, static_cast<Uint32>(size)};
        const bool ok = fwrite(header, sizeof(header), 1, copy) == 1 && fwrite(out.data(), 1, out.size(), copy) == out.size();
        fclose(copy);
        if (ok)
            g_sound_cache_used += static_cast<Sint64>(bytes);
        else
            std::remove(cached.c_str());    //(the drive is full: not kept half written)
    }
    return true;
}

Uint32 le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<Uint32>(p[3]) << 24); }
Uint16 le16(const unsigned char* p) { return static_cast<Uint16>(p[0] | (p[1] << 8)); }

bool parseWav(const std::vector<unsigned char>& data, MIX_Audio& audio, const char* path)
{
    if (data.size() < 12 || memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "WAVE", 4) != 0)
        return SDL_SetError("%s is not a WAV file", path);

    int format = 0, channels = 0, rate = 0, bits = 0;
    const unsigned char* samples = nullptr;
    size_t samples_size = 0;

    size_t offset = 12;
    while (offset + 8 <= data.size()) {
        const unsigned char* chunk = data.data() + offset;
        const Uint32 size = le32(chunk + 4);
        const size_t body = offset + 8;
        const size_t available = data.size() - body;
        if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16 && available >= 16) {
            format = le16(chunk + 8);
            channels = le16(chunk + 10);
            rate = static_cast<int>(le32(chunk + 12));
            bits = le16(chunk + 22);
        } else if (memcmp(chunk, "data", 4) == 0) {
            samples = chunk + 8;
            samples_size = std::min<size_t>(size, available);
        }
        offset = body + size + (size & 1);
    }

    if (format != 1 || (bits != 16 && bits != 8) || (channels != 1 && channels != 2) || rate <= 0 || !samples)
        return SDL_SetError("%s: unsupported WAV format (format %d, %d bit, %d channels)", path, format, bits, channels);

    const size_t count = bits == 16 ? samples_size / 2 : samples_size;
    audio.pcm.resize(count);
    if (bits == 16) {
        for (size_t i = 0; i < count; i++)
            audio.pcm[i] = static_cast<Sint16>(le16(samples + i * 2));
    } else {
        for (size_t i = 0; i < count; i++)
            audio.pcm[i] = static_cast<Sint16>((static_cast<int>(samples[i]) - 128) << 8);
    }
    audio.channels = channels;
    audio.rate = rate;
    audio.frames = count / static_cast<size_t>(channels);
    return true;
}
} // namespace


void sdlx_mixer_dispatch_callbacks()
{
    if (!g_lock_ready)
        return;

    std::vector<std::pair<MIX_Track*, Uint32>> calls;
    {
        Lock lock;
        if (g_pending_stops.empty())
            return;
        for (const PendingStop& stop : g_pending_stops)
            calls.emplace_back(stop.track, stop.generation);
        g_pending_stops.clear();
    }

    for (const auto& [track, generation] : calls) {
        MIX_TrackStoppedCallback callback = nullptr;
        void* userdata = nullptr;
        {
            Lock lock;
            // Skip tracks that were destroyed, restarted or given new audio since
            const bool alive = g_mixer && std::find(g_mixer->tracks.begin(), g_mixer->tracks.end(), track) != g_mixer->tracks.end();
            if (!alive || track->generation != generation || track->playing)
                continue;
            callback = track->callback;
            userdata = track->callback_userdata;
        }
        if (callback)
            callback(userdata, track);
    }
}


/* ---- Library ------------------------------------------------------------- */

bool MIX_Init(void)
{
    ensureLock();
    return true;
}

void MIX_Quit(void)
{
}

int MIX_Version(void)
{
    return SDL_VERSIONNUM(SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_MICRO_VERSION);
}

int MIX_GetNumAudioDecoders(void)
{
    return 2;
}

const char* MIX_GetAudioDecoder(int index)
{
    switch (index) {
        case 0: return "WAV";
        case 1: return "VORBIS";
        default: return nullptr;
    }
}


/* ---- Mixer --------------------------------------------------------------- */

MIX_Mixer* MIX_CreateMixerDevice(SDL_AudioDeviceID devid, const SDL_AudioSpec* spec)
{
    (void)devid;
    (void)spec;  // always 44.1 kHz stereo 16-bit
    ensureLock();
    if (g_mixer) {
        SDL_SetError("Only one mixer device is supported");
        return nullptr;
    }

    g_mixer = new MIX_Mixer();
    if (!startOutput()) {
        stopOutput();
        delete g_mixer;
        g_mixer = nullptr;
        return nullptr;
    }
    return g_mixer;
}

void MIX_DestroyMixer(MIX_Mixer* mixer)
{
    if (!mixer || mixer != g_mixer)
        return;
    stopOutput();
    {
        Lock lock;
        for (MIX_Track* track : g_mixer->tracks) {
            closeDecoder(track);
            delete track;
        }
        g_pending_stops.clear();
        delete g_mixer;
        g_mixer = nullptr;
    }
}


/* ---- Audio --------------------------------------------------------------- */

MIX_Audio* MIX_LoadAudio(MIX_Mixer* mixer, const char* path, bool predecode)
{
    (void)mixer;
    (void)predecode;  // WAV is always decoded; Vorbis always streams
    if (!path) {
        SDL_SetError("Parameter 'path' is invalid");
        return nullptr;
    }

    std::vector<unsigned char> data;
    if (!readSoundFile(path, data))
        return nullptr;

    auto* audio = new MIX_Audio();
    if (data.size() >= 4 && memcmp(data.data(), "OggS", 4) == 0) {
        int error = 0;
        stb_vorbis* probe = stb_vorbis_open_memory(data.data(), static_cast<int>(data.size()), &error, nullptr);
        if (!probe) {
            delete audio;
            SDL_SetError("%s: couldn't decode Ogg Vorbis (error %d)", path, error);
            return nullptr;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(probe);
        stb_vorbis_close(probe);
        audio->vorbis = true;
        audio->rate = static_cast<int>(info.sample_rate);
        audio->channels = info.channels;
        audio->ogg = std::move(data);
    } else if (!parseWav(data, *audio, path)) {
        delete audio;
        return nullptr;
    }
    return audio;
}

void MIX_DestroyAudio(MIX_Audio* audio)
{
    if (!audio)
        return;
    {
        Lock lock;
        if (g_mixer) {
            for (MIX_Track* track : g_mixer->tracks) {
                if (track->audio == audio) {
                    closeDecoder(track);
                    track->playing = false;
                    track->audio = nullptr;
                    track->generation++;
                }
            }
        }
    }
    delete audio;
}


/* ---- Tracks -------------------------------------------------------------- */

MIX_Track* MIX_CreateTrack(MIX_Mixer* mixer)
{
    if (!mixer) {
        SDL_SetError("Parameter 'mixer' is invalid");
        return nullptr;
    }
    auto* track = new MIX_Track();
    track->mixer = mixer;
    Lock lock;
    mixer->tracks.push_back(track);
    return track;
}

void MIX_DestroyTrack(MIX_Track* track)
{
    if (!track)
        return;
    Lock lock;
    MIX_Mixer* mixer = track->mixer;
    if (mixer)
        mixer->tracks.erase(std::remove(mixer->tracks.begin(), mixer->tracks.end(), track), mixer->tracks.end());
    g_pending_stops.erase(std::remove_if(g_pending_stops.begin(), g_pending_stops.end(),
        [track](const PendingStop& stop) { return stop.track == track; }), g_pending_stops.end());
    closeDecoder(track);
    delete track;
}

bool MIX_TagTrack(MIX_Track* track, const char* tag)
{
    if (!track || !tag)
        return SDL_SetError("Invalid track or tag");
    Lock lock;
    if (!hasTag(track, tag))
        track->tags.emplace_back(tag);
    return true;
}

bool MIX_SetTrackStoppedCallback(MIX_Track* track, MIX_TrackStoppedCallback cb, void* userdata)
{
    if (!track)
        return SDL_SetError("Parameter 'track' is invalid");
    Lock lock;
    track->callback = cb;
    track->callback_userdata = userdata;
    return true;
}

bool MIX_SetTrackAudio(MIX_Track* track, MIX_Audio* audio)
{
    if (!track)
        return SDL_SetError("Parameter 'track' is invalid");
    Lock lock;
    closeDecoder(track);
    track->playing = false;
    track->paused = false;
    track->audio = audio;
    track->generation++;
    return true;
}

bool MIX_PlayTrack(MIX_Track* track, SDL_PropertiesID options)
{
    if (!track)
        return SDL_SetError("Parameter 'track' is invalid");
    const Sint64 loops = options ? SDL_GetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, 0) : 0;
    Lock lock;
    return startLocked(track, loops);
}

bool MIX_StopTrack(MIX_Track* track, Sint64 fade_out_frames)
{
    (void)fade_out_frames;
    if (!track)
        return SDL_SetError("Parameter 'track' is invalid");
    Lock lock;
    stopLocked(track);
    return true;
}

bool MIX_PauseTrack(MIX_Track* track)
{
    if (!track)
        return SDL_SetError("Parameter 'track' is invalid");
    Lock lock;
    if (track->playing)
        track->paused = true;
    return true;
}

bool MIX_ResumeTrack(MIX_Track* track)
{
    if (!track)
        return SDL_SetError("Parameter 'track' is invalid");
    Lock lock;
    track->paused = false;
    return true;
}

bool MIX_TrackPlaying(MIX_Track* track)
{
    if (!track)
        return false;
    Lock lock;
    return track->playing && !track->paused;
}

bool MIX_TrackPaused(MIX_Track* track)
{
    if (!track)
        return false;
    Lock lock;
    return track->playing && track->paused;
}

bool MIX_SetTrackGain(MIX_Track* track, float gain)
{
    if (!track)
        return SDL_SetError("Parameter 'track' is invalid");
    Lock lock;
    track->gain = gain < 0.0f ? 0.0f : gain;
    return true;
}


/* ---- Tags ---------------------------------------------------------------- */

bool MIX_StopTag(MIX_Mixer* mixer, const char* tag, Sint64 fade_out_ms)
{
    (void)fade_out_ms;
    if (!mixer || !tag)
        return SDL_SetError("Invalid mixer or tag");
    Lock lock;
    for (MIX_Track* track : mixer->tracks) {
        if (hasTag(track, tag))
            stopLocked(track);
    }
    return true;
}

bool MIX_SetTagGain(MIX_Mixer* mixer, const char* tag, float gain)
{
    if (!mixer || !tag)
        return SDL_SetError("Invalid mixer or tag");
    Lock lock;
    mixer->tag_gains[tag] = gain < 0.0f ? 0.0f : gain;
    return true;
}
