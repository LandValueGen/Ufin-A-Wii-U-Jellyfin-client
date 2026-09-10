#include "SDL2/SDL.h"

static Uint32 g_ticks = 0;
static Uint32 g_queued = 0;
static Uint32 g_totalQueued = 0;
static bool g_paused = true;
static bool g_open = false;

extern "C" {
int SDL_InitSubSystem(Uint32) { return 0; }
void SDL_QuitSubSystem(Uint32) {}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec* desired, SDL_AudioSpec* obtained,
                                      int) {
    if (obtained) *obtained = *desired;
    g_open = true;
    g_paused = true;
    g_queued = 0;
    return 2;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID) { g_open = false; }
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int pause_on) { g_paused = pause_on != 0; }

int SDL_QueueAudio(SDL_AudioDeviceID, const void*, Uint32 len) {
    g_queued += len;
    g_totalQueued += len;
    return 0;
}

Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID) { return g_queued; }
const char* SDL_GetError(void) { return "fake sdl"; }
Uint32 SDL_GetTicks(void) { return g_ticks; }
void SDL_Delay(Uint32 ms) { g_ticks += ms; }
}

void fake_sdl_advance_ticks(Uint32 ms) { g_ticks += ms; }
void fake_sdl_consume_audio(Uint32 bytes) { g_queued = bytes >= g_queued ? 0 : g_queued - bytes; }
bool fake_sdl_device_paused() { return g_paused; }
Uint32 fake_sdl_total_queued() { return g_totalQueued; }
