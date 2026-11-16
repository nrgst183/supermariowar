#pragma once
// SDL3_mixer compatibility for the original Xbox (platform/xbox/src/sdl_mixer.cpp):
// a software mixer (WAV and Ogg Vorbis via stb_vorbis) feeding one DirectSound
// stream. Only the track/tag API the game uses is provided.
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MIXER_MAJOR_VERSION 3
#define SDL_MIXER_MINOR_VERSION 2
#define SDL_MIXER_MICRO_VERSION 0

typedef struct MIX_Mixer MIX_Mixer;
typedef struct MIX_Audio MIX_Audio;
typedef struct MIX_Track MIX_Track;

typedef void (SDLCALL *MIX_TrackStoppedCallback)(void* userdata, MIX_Track* track);

#define MIX_PROP_PLAY_LOOPS_NUMBER "SDL_mixer.play.loops"

bool MIX_Init(void);
void MIX_Quit(void);
int MIX_Version(void);
int MIX_GetNumAudioDecoders(void);
const char* MIX_GetAudioDecoder(int index);

MIX_Mixer* MIX_CreateMixerDevice(SDL_AudioDeviceID devid, const SDL_AudioSpec* spec);
void MIX_DestroyMixer(MIX_Mixer* mixer);

MIX_Audio* MIX_LoadAudio(MIX_Mixer* mixer, const char* path, bool predecode);
void MIX_DestroyAudio(MIX_Audio* audio);

MIX_Track* MIX_CreateTrack(MIX_Mixer* mixer);
void MIX_DestroyTrack(MIX_Track* track);
bool MIX_TagTrack(MIX_Track* track, const char* tag);
bool MIX_SetTrackStoppedCallback(MIX_Track* track, MIX_TrackStoppedCallback cb, void* userdata);
bool MIX_SetTrackAudio(MIX_Track* track, MIX_Audio* audio);
bool MIX_PlayTrack(MIX_Track* track, SDL_PropertiesID options);
bool MIX_StopTrack(MIX_Track* track, Sint64 fade_out_frames);
bool MIX_PauseTrack(MIX_Track* track);
bool MIX_ResumeTrack(MIX_Track* track);
bool MIX_TrackPlaying(MIX_Track* track);
bool MIX_TrackPaused(MIX_Track* track);
bool MIX_SetTrackGain(MIX_Track* track, float gain);

bool MIX_StopTag(MIX_Mixer* mixer, const char* tag, Sint64 fade_out_ms);
bool MIX_SetTagGain(MIX_Mixer* mixer, const char* tag, float gain);

#ifdef __cplusplus
}
#endif
