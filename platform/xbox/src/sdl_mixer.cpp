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
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <map>
#include <string>
#include <vector>

struct MIX_Audio {
    bool vorbis = false;
    // PCM: interleaved 16-bit samples
    std::vector<Sint16> pcm;
    // 8-bit WAVs: their samples as they are (unsigned), half the memory of widening them (the
    // sound effects: 64 MB)
    std::vector<Uint8> pcm8;
    ~MIX_Audio() { g_sdlx_sound_bytes -= pcm.size() * sizeof(Sint16) + pcm8.size(); }    //(counted: the memory watch)

    int sample(size_t i) const { return pcm8.empty() ? pcm[i] : (static_cast<int>(pcm8[i]) - 128) << 8; }
    int channels = 2;
    int rate = 44100;
    size_t frames = 0;
    // Vorbis: streamed from this file (from `offset`), a little at a time: a match's music is 3-4 MB
    // compressed, too much to keep in the 64 MB
    std::string oggFile;
    long oggOffset = 0;
};

// A Vorbis track's decoding, ahead of its mixing. The decoder reads its file as it goes (a disk read
// now and then, a seek at each loop), so it runs on the mixer thread outside the lock (refillStreams),
// into this ring of decoded frames that the mixing reads under the lock: the game's thread, taking
// the lock to play a sound or ask about a track, never waits for the disk. Made on the game's thread
// with its first frames decoded, before a track has it; from then on only the mixer thread uses it.
// The track and a refill under way each hold a reference; the last one closes it.
struct VorbisStream {
    static constexpr int k_frames = 4096;    // ~93 ms (a power of two)

    stb_vorbis* decoder = nullptr;    // (it closes the file)
    int rate = 44100;
    Sint64 loops = 0;     // the extra loops left (-1: forever): the decoder starts over itself
    bool ended = false;   // nothing more to decode
    int read = 0;         // the ring's first frame not mixed yet
    int count = 0;        // and how many are decoded from there
    volatile LONG refs = 1;
    Sint16 ring[k_frames * 2];
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

    // Vorbis decoding (decoded ahead, VorbisStream)
    VorbisStream* stream = nullptr;
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
constexpr int k_ring_frames = VorbisStream::k_frames;

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

void releaseStream(VorbisStream* stream)
{
    if (stream && InterlockedDecrement(&stream->refs) == 0) {
        stb_vorbis_close(stream->decoder);
        delete stream;
    }
}

void closeDecoder(MIX_Track* track)
{
    releaseStream(track->stream);
    track->stream = nullptr;
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

// A Vorbis stream's decoder, reading its file (closed with the decoder)
stb_vorbis* openVorbis(const MIX_Audio& audio, int* error)
{
    std::string path = audio.oggFile;
    std::replace(path.begin(), path.end(), '/', '\\');
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        *error = -1;
        return nullptr;
    }
    //(reads in big blocks: the C library's own buffer is 512 bytes, a system call each)
    setvbuf(file, nullptr, _IOFBF, 32 * 1024);
    if (audio.oggOffset)
        fseek(file, audio.oggOffset, SEEK_SET);
    stb_vorbis* decoder = stb_vorbis_open_file(file, 1, error, nullptr);
    if (!decoder)
        fclose(file);
    return decoder;
}

// Decodes into the ring until it holds `upTo` frames (or is full), straight into its free part,
// starting over at the end while loops are left. Not under the lock: the mixer thread's (a stream a
// track has), or the game's (one no track has yet)
void decodeAhead(VorbisStream* stream, int upTo)
{
    if (upTo > k_ring_frames)
        upTo = k_ring_frames;
    int restarts = 0;
    while (!stream->ended && stream->count < upTo) {
        const int write = (stream->read + stream->count) & (k_ring_frames - 1);
        int n = SDL_min(k_ring_frames - write, upTo - stream->count);
        n = SDL_min(n, k_vorbis_chunk_frames);
        const int frames = stb_vorbis_get_samples_short_interleaved(stream->decoder, 2, &stream->ring[write * 2], n * 2);
        if (frames > 0) {
            stream->count += frames;
            restarts = 0;
            continue;
        }
        //(the end: from the start again while loops are left; one that gives nothing even from
        //its start has ended)
        if (stream->loops == 0 || ++restarts > 1) {
            stream->ended = true;
            break;
        }
        if (stream->loops > 0)
            stream->loops--;
        stb_vorbis_seek_start(stream->decoder);
    }
}

// A Vorbis track's stream, its file opened and its first frames decoded (the game's thread, before
// the lock: the mixing doesn't wait for that, and starts with them, not a packet of silence)
VorbisStream* openStream(const MIX_Audio& audio, Sint64 loops, int* error)
{
    stb_vorbis* decoder = openVorbis(audio, error);
    if (!decoder)
        return nullptr;
    auto* stream = new VorbisStream();
    stream->decoder = decoder;
    stream->rate = static_cast<int>(stb_vorbis_get_info(decoder).sample_rate);
    stream->loops = loops < -1 ? -1 : loops;
    decodeAhead(stream, k_vorbis_chunk_frames * 2);
    return stream;
}

// Refills the Vorbis tracks' rings (the mixer thread): the streams are taken under the lock, and
// decoded outside it
void refillStreams()
{
    VorbisStream* streams[16];
    int n = 0;
    {
        Lock lock;
        if (!g_mixer)
            return;
        for (MIX_Track* track : g_mixer->tracks) {
            VorbisStream* stream = track->stream;
            if (stream && !stream->ended && k_ring_frames - stream->count >= k_vorbis_chunk_frames && n < 16) {
                InterlockedIncrement(&stream->refs);
                streams[n++] = stream;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        decodeAhead(streams[i], k_ring_frames);
        releaseStream(streams[i]);
    }
}

// `stream`: the track's Vorbis stream if its audio is Vorbis (openStream), taken over
bool startLocked(MIX_Track* track, Sint64 loops, VorbisStream* stream, int error)
{
    closeDecoder(track);
    track->generation++;
    track->playing = false;
    track->paused = false;
    track->pos = 0;
    track->loops = loops < -1 ? -1 : loops;

    MIX_Audio* audio = track->audio;
    if (!audio || !audio->vorbis)
        releaseStream(stream);
    if (!audio)
        return SDL_SetError("The track has no audio");

    if (audio->vorbis) {
        if (!stream)
            return SDL_SetError("Couldn't decode Ogg Vorbis audio (error %d)", error);
        track->stream = stream;
        track->step = static_cast<Uint32>((static_cast<Uint64>(stream->rate) << 16) / k_rate);
    } else {
        if (audio->frames == 0)
            return SDL_SetError("The audio is empty");
        track->step = static_cast<Uint32>((static_cast<Uint64>(audio->rate) << 16) / k_rate);
    }
    track->playing = true;
    return true;
}

// Loops back to the start or stops at the end (PCM; a Vorbis stream loops itself); false if the
// track stopped
bool handleEnd(MIX_Track* track)
{
    if (track->loops == 0) {
        stopLocked(track);
        return false;
    }
    if (track->loops > 0)
        track->loops--;
    track->pos = 0;
    return true;
}

void mixTrack(MIX_Track* track, int* out, int frames)
{
    const int gain = static_cast<int>(effectiveGain(track) * 256.0f + 0.5f);
    MIX_Audio* audio = track->audio;

    for (int i = 0; i < frames && track->playing; i++) {
        int left = 0, right = 0;

        if (audio->vorbis) {
            //(the frames decoded ahead, from the ring's first; pos is from there)
            const VorbisStream* stream = track->stream;
            if (!stream)
                break;
            const size_t idx = static_cast<size_t>(track->pos >> 16);
            if (idx >= static_cast<size_t>(stream->count)) {
                //(all mixed: the end if there's no more, else not decoded yet (silence until it is))
                if (stream->ended)
                    stopLocked(track);
                break;
            }
            const int at = (stream->read + static_cast<int>(idx)) & (k_ring_frames - 1);
            left = stream->ring[at * 2];
            right = stream->ring[at * 2 + 1];
        } else {
            size_t idx = static_cast<size_t>(track->pos >> 16);
            if (idx >= audio->frames) {
                if (!handleEnd(track))
                    break;
                idx = 0;
            }
            if (audio->channels == 2) {
                left = audio->sample(idx * 2);
                right = audio->sample(idx * 2 + 1);
            } else {
                left = right = audio->sample(idx);
            }
        }

        out[i * 2] += (left * gain) >> 8;
        out[i * 2 + 1] += (right * gain) >> 8;
        track->pos += track->step;
    }

    // The ring's frames mixed are free for decoding again
    if (VorbisStream* stream = track->stream) {
        const int used = static_cast<int>(std::min<Uint64>(track->pos >> 16, static_cast<Uint64>(stream->count)));
        stream->read = (stream->read + used) & (k_ring_frames - 1);
        stream->count -= used;
        track->pos -= static_cast<Uint64>(used) << 16;
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

            //(the music decoded ahead, outside the lock: its file is read here, not while the
            //game's thread may be waiting for the lock)
            refillStreams();
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

// (the system's reads, in big blocks: sdlx_open)
bool readFile(const char* path, std::vector<unsigned char>& out)
{
    void* file = sdlx_open(path, false);
    if (!file)
        return SDL_SetError("Couldn't open %s", path);
    const Sint64 size = sdlx_size(file);
    if (size <= 0) {
        sdlx_close(file);
        return SDL_SetError("Couldn't read %s", path);
    }
    out.resize(static_cast<size_t>(size));
    const bool ok = sdlx_read(file, out.data(), out.size());
    sdlx_close(file);
    if (!ok)
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

    SDL_XboxLoadStats& stats = g_sdlx_sound_stats;

    //(its size from its folder's listing: no DVD seek to the file)
    Uint64 t = sdlx_ticks_ns();
    long size = SDL_XboxDiscFileSize(path);
    if (size <= 0) {
        void* file = sdlx_open(path, false);
        size = static_cast<long>(sdlx_size(file));
        sdlx_close(file);
    }
    stats.sizeChecks++;
    stats.sizeCheckNs += sdlx_ticks_ns() - t;
    if (size <= 0)
        return readFile(path, out);

    t = sdlx_ticks_ns();
    const std::string cached = soundCacheFile(path);
    if (void* in = sdlx_open(cached.c_str(), false)) {
        Uint32 header[2] = {};
        bool ok = sdlx_read(in, header, sizeof(header)) && header[0] == SOUND_CACHE_MAGIC && header[1] == static_cast<Uint32>(size);
        if (ok) {
            out.resize(static_cast<size_t>(size));
            ok = sdlx_read(in, out.data(), out.size());
        }
        sdlx_close(in);
        stats.cacheReadNs += sdlx_ticks_ns() - t;
        if (ok) {
            stats.cacheHits++;
            stats.cacheReadBytes += out.size();
            return true;
        }
    }

    t = sdlx_ticks_ns();
    if (!readFile(path, out))
        return false;
    stats.discReads++;
    stats.discReadNs += sdlx_ticks_ns() - t;
    stats.discReadBytes += out.size();
    t = sdlx_ticks_ns();
    const Uint64 bytes = sizeof(Uint32) * 2 + out.size();
    if (g_sound_cache_used < 0)
        g_sound_cache_used = static_cast<Sint64>(sdlx_dir_bytes(soundCacheDir().c_str()));
    if (static_cast<Uint64>(g_sound_cache_used) + bytes > SOUND_CACHE_BUDGET || !sdlx_make_dir(soundCacheDir().c_str())) {
        stats.cacheWritesSkipped++;
        return true;
    }
    bool written = false;
    if (void* copy = sdlx_open(cached.c_str(), true)) {
        const Uint32 header[2] = {SOUND_CACHE_MAGIC, static_cast<Uint32>(size)};
        const bool ok = sdlx_write(copy, header, sizeof(header)) && sdlx_write(copy, out.data(), out.size());
        sdlx_close(copy);
        if (ok)
            g_sound_cache_used += static_cast<Sint64>(bytes);
        else
            std::remove(cached.c_str());    //(the drive is full: not kept half written)
        written = ok;
    }
    if (written) {
        stats.cacheWrites++;
        stats.cacheWriteBytes += bytes;
    } else {
        stats.cacheWritesSkipped++;
    }
    stats.cacheWriteNs += sdlx_ticks_ns() - t;
    return true;
}

// The file a Vorbis stream reads: the cache drive's copy of a disc file (made now if there's none:
// copied a block at a time, never all in memory) after its header, or the file itself
void streamSource(const char* path, std::string& file, long& offset)
{
    file = path;
    offset = 0;
    if (!((path[0] == 'D' || path[0] == 'd') && path[1] == ':'))
        return;
    long size = SDL_XboxDiscFileSize(path);
    if (size <= 0) {
        void* in = sdlx_open(path, false);
        size = in ? static_cast<long>(sdlx_size(in)) : 0;
        sdlx_close(in);
    }
    if (size <= 0)
        return;
    const std::string cached = soundCacheFile(path);
    if (void* in = sdlx_open(cached.c_str(), false)) {
        Uint32 header[2] = {};
        const bool ok = sdlx_read(in, header, sizeof(header)) && header[0] == SOUND_CACHE_MAGIC &&
                        header[1] == static_cast<Uint32>(size) && sdlx_size(in) == static_cast<Sint64>(sizeof(header)) + size;
        sdlx_close(in);
        if (ok) {
            file = cached;
            offset = sizeof(header);
            return;
        }
    }
    const Uint64 bytes = sizeof(Uint32) * 2 + static_cast<Uint64>(size);
    if (g_sound_cache_used < 0)
        g_sound_cache_used = static_cast<Sint64>(sdlx_dir_bytes(soundCacheDir().c_str()));
    if (static_cast<Uint64>(g_sound_cache_used) + bytes > SOUND_CACHE_BUDGET || !sdlx_make_dir(soundCacheDir().c_str()))
        return;
    void* in = sdlx_open(path, false);
    void* out = in ? sdlx_open(cached.c_str(), true) : nullptr;
    bool ok = in && out;
    if (ok) {
        const Uint32 header[2] = {SOUND_CACHE_MAGIC, static_cast<Uint32>(size)};
        ok = sdlx_write(out, header, sizeof(header));
        std::vector<unsigned char> block(256 * 1024);
        for (long left = size; ok && left > 0;) {
            const long n = left < static_cast<long>(block.size()) ? left : static_cast<long>(block.size());
            ok = sdlx_read(in, block.data(), static_cast<Uint64>(n)) && sdlx_write(out, block.data(), static_cast<Uint64>(n));
            left -= n;
        }
    }
    sdlx_close(out);
    sdlx_close(in);
    if (ok) {
        g_sound_cache_used += static_cast<Sint64>(bytes);
        file = cached;
        offset = sizeof(Uint32) * 2;
    } else if (out) {
        std::remove(cached.c_str());    //(the drive is full: not kept half written)
    }
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
    if (bits == 16 && channels == 2) {
        //(stereo sound effects as mono, both sides mixed: the original game's were mono, these are
        //twice their memory for nothing heard; the game doesn't pan its sounds)
        audio.pcm.resize(count / 2);
        for (size_t i = 0; i < count / 2; i++)
            audio.pcm[i] = static_cast<Sint16>((static_cast<int>(static_cast<Sint16>(le16(samples + i * 4))) +
                                                static_cast<int>(static_cast<Sint16>(le16(samples + i * 4 + 2)))) / 2);
        g_sdlx_sound_bytes += audio.pcm.size() * sizeof(Sint16);
        audio.channels = 1;
        audio.rate = rate;
        audio.frames = audio.pcm.size();
        return true;
    } else if (bits == 16) {
        audio.pcm.resize(count);
        for (size_t i = 0; i < count; i++)
            audio.pcm[i] = static_cast<Sint16>(le16(samples + i * 2));
    } else {
        audio.pcm8.assign(samples, samples + count);
    }
    g_sdlx_sound_bytes += audio.pcm.size() * sizeof(Sint16) + audio.pcm8.size();
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

    //(Vorbis, music: streamed from its file, not read whole)
    const size_t length = strlen(path);
    if (length > 4 && strcasecmp(path + length - 4, ".ogg") == 0) {
        auto* audio = new MIX_Audio();
        audio->vorbis = true;
        streamSource(path, audio->oggFile, audio->oggOffset);
        int error = 0;
        stb_vorbis* probe = openVorbis(*audio, &error);
        if (!probe) {
            delete audio;
            SDL_SetError("%s: couldn't decode Ogg Vorbis (error %d)", path, error);
            return nullptr;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(probe);
        stb_vorbis_close(probe);
        audio->rate = static_cast<int>(info.sample_rate);
        audio->channels = info.channels;
        return audio;
    }

    std::vector<unsigned char> data;
    if (!readSoundFile(path, data))
        return nullptr;

    const Uint64 decodeStart = sdlx_ticks_ns();
    struct Timed {
        Uint64 start;
        ~Timed() {
            g_sdlx_sound_stats.decodes++;
            g_sdlx_sound_stats.decodeNs += sdlx_ticks_ns() - start;
        }
    } timed {decodeStart};
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
        //(an Ogg file not named .ogg: streamed from the file too)
        audio->vorbis = true;
        audio->rate = static_cast<int>(info.sample_rate);
        audio->channels = info.channels;
        streamSource(path, audio->oggFile, audio->oggOffset);
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
    //(Vorbis: its file opened and its first frames decoded before the lock, which the mixer thread
    //would wait on meanwhile. The audio is the game thread's to change, as is this call)
    MIX_Audio* audio = nullptr;
    {
        Lock lock;
        audio = track->audio;
    }
    int error = 0;
    VorbisStream* stream = audio && audio->vorbis ? openStream(*audio, loops, &error) : nullptr;
    Lock lock;
    if (track->audio != audio) {
        releaseStream(stream);
        stream = track->audio && track->audio->vorbis ? openStream(*track->audio, loops, &error) : nullptr;
    }
    return startLocked(track, loops, stream, error);
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
