#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "machine/machine.h"

struct renderer_state
{
  TTF_Font *font;
  uint32_t ticks[10];
  uint32_t counter;
  uint32_t prev_ticks;
  SDL_Window *main_window;
  SDL_Window *debug_window;
  SDL_Palette *palette;
};

void audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
  // struct tnes_machine *m = (struct tnes_machine *)userdata;
  // bool success = SDL_PutAudioStreamData(stream, m->audio_buffer, m->audio_buffer_len * 4);
  // if (!success)
  // {
  //   SDL_Log("Failed to put audio into buffer: %s", SDL_GetError());
  // }
}

bool initialize_sdl_audio(struct tnes_machine *machine, struct renderer_state *state)
{
  SDL_AudioSpec desired = {
      .freq = 44100,
      .format = SDL_AUDIO_F32,
      .channels = 1,
      // .samples = 256,
      // .callback = ic_rp2a03_sdl_audio_callback,
      // .userdata = &machine,
  };

  SDL_AudioStream *audio_stream = SDL_CreateAudioStream(&desired, NULL);
  if (audio_stream == NULL)
  {
    SDL_Log("Failed to create audio stream: %s", SDL_GetError());
    return NULL;
  }
  bool success = SDL_SetAudioStreamGetCallback(audio_stream, audio_callback, machine);
  if (!success)
  {
    SDL_Log("Failed to set audio callback: %s", SDL_GetError());
    return NULL;
  }
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired);
  if (audio == 0)
  {
    SDL_Log("Failed to open audio: %s", SDL_GetError());
    return NULL;
  }
  SDL_AudioSpec preferred_spec;
  int sample_frames;
  success = SDL_GetAudioDeviceFormat(audio, &preferred_spec, &sample_frames);
  if (!success)
  {
    SDL_Log("Failed to read audio device format: %s", SDL_GetError());
    return NULL;
  }
  machine->sampling_count = sample_frames * desired.freq;

  success = SDL_PauseAudioDevice(audio);
  if (!success)
  {
    SDL_Log("Failed to pause audio device: %s", SDL_GetError());
    return NULL;
  }

  for (int i = 0; i < 10; ++i)
  {
    state->ticks[i] = 0;
  }
  state->counter = 0;
  state->prev_ticks = 0;

  return true;
}

bool initialize_sdl_video(struct tnes_machine *machine, struct renderer_state *state)
{
  state->font = TTF_OpenFont("cour.ttf", 14);
  if (!state->font)
  {
    SDL_Log("Failed to initialize Font: %s", SDL_GetError());
    return NULL;
  }

  state->main_window = SDL_CreateWindow("NES", 800, 740, 0);
  if (!state->main_window)
  {
    SDL_Log("Failed to initialize Main Window: %s", SDL_GetError());
    return NULL;
  }

  state->debug_window = SDL_CreateWindow("debug", 800, 740, 0);
  if (!state->debug_window)
  {
    SDL_Log("Failed to initialize Debug Window: %s", SDL_GetError());
    return NULL;
  }
  // bool success = SDL_HideWindow(state->debug_window);
  // if (!success)
  // {
  //   SDL_Log("Failed to hide window: %s", SDL_GetError());
  //   return NULL;
  // }

  state->palette = SDL_CreatePalette(128);
  SDL_Color colors[64] = {
      {.a = 0xff, .r = 0x7c, .g = 0x7c, .b = 0x7c},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0xfc},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0xbc},
      {.a = 0xff, .r = 0x44, .g = 0x28, .b = 0xbc},
      {.a = 0xff, .r = 0x94, .g = 0x00, .b = 0x84},
      {.a = 0xff, .r = 0xa8, .g = 0x00, .b = 0x20},
      {.a = 0xff, .r = 0xa8, .g = 0x10, .b = 0x00},
      {.a = 0xff, .r = 0x88, .g = 0x14, .b = 0x00},
      {.a = 0xff, .r = 0x50, .g = 0x30, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x78, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x68, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x58, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x40, .b = 0x58},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0xbc, .g = 0xbc, .b = 0xbc},
      {.a = 0xff, .r = 0x00, .g = 0x78, .b = 0xf8},
      {.a = 0xff, .r = 0x00, .g = 0x58, .b = 0xf8},
      {.a = 0xff, .r = 0x68, .g = 0x44, .b = 0xfc},
      {.a = 0xff, .r = 0xd8, .g = 0x00, .b = 0xcc},
      {.a = 0xff, .r = 0xe4, .g = 0x00, .b = 0x58},
      {.a = 0xff, .r = 0xf8, .g = 0x38, .b = 0x00},
      {.a = 0xff, .r = 0xe4, .g = 0x5c, .b = 0x10},
      {.a = 0xff, .r = 0xac, .g = 0x7c, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0xb8, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0xa8, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0xa8, .b = 0x44},
      {.a = 0xff, .r = 0x00, .g = 0x88, .b = 0x88},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0xf8, .g = 0xf8, .b = 0xf8},
      {.a = 0xff, .r = 0x3c, .g = 0xbc, .b = 0xfc},
      {.a = 0xff, .r = 0x68, .g = 0x88, .b = 0xfc},
      {.a = 0xff, .r = 0x98, .g = 0x78, .b = 0xf8},
      {.a = 0xff, .r = 0xf8, .g = 0x78, .b = 0xf8},
      {.a = 0xff, .r = 0xf8, .g = 0x58, .b = 0x98},
      {.a = 0xff, .r = 0xf8, .g = 0x78, .b = 0x58},
      {.a = 0xff, .r = 0xfc, .g = 0xa0, .b = 0x44},
      {.a = 0xff, .r = 0xf8, .g = 0xb8, .b = 0x00},
      {.a = 0xff, .r = 0xb8, .g = 0xf8, .b = 0x18},
      {.a = 0xff, .r = 0x58, .g = 0xd8, .b = 0x54},
      {.a = 0xff, .r = 0x58, .g = 0xf8, .b = 0x98},
      {.a = 0xff, .r = 0x00, .g = 0xe8, .b = 0xd8},
      {.a = 0xff, .r = 0x78, .g = 0x78, .b = 0x78},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0xfc, .g = 0xfc, .b = 0xfc},
      {.a = 0xff, .r = 0xa4, .g = 0xe4, .b = 0xfc},
      {.a = 0xff, .r = 0xb8, .g = 0xb8, .b = 0xf8},
      {.a = 0xff, .r = 0xd8, .g = 0xb8, .b = 0xf8},
      {.a = 0xff, .r = 0xf8, .g = 0xb8, .b = 0xf8},
      {.a = 0xff, .r = 0xf8, .g = 0xa4, .b = 0xc0},
      {.a = 0xff, .r = 0xf0, .g = 0xd0, .b = 0xb0},
      {.a = 0xff, .r = 0xfc, .g = 0xe0, .b = 0xa8},
      {.a = 0xff, .r = 0xf8, .g = 0xd8, .b = 0x78},
      {.a = 0xff, .r = 0xd8, .g = 0xf8, .b = 0x78},
      {.a = 0xff, .r = 0xb8, .g = 0xf8, .b = 0xb8},
      {.a = 0xff, .r = 0xb8, .g = 0xf8, .b = 0xd8},
      {.a = 0xff, .r = 0x00, .g = 0xfc, .b = 0xfc},
      {.a = 0xff, .r = 0xf8, .g = 0xd8, .b = 0xf8},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
      {.a = 0xff, .r = 0x00, .g = 0x00, .b = 0x00},
  };
  bool success = SDL_SetPaletteColors(state->palette, colors, 0, 128);
  if (!success)
  {
    SDL_Log("Failed to create palette: %s", SDL_GetError());
    return NULL;
  }
  // SDL_SetSurfacePalette(main_surface, state->palette);
  // SDL_SetSurfacePalette(debug_surface, state->palette);

  return true;
}

struct renderer_state *initialize_sdl(struct tnes_machine *machine)
{
  struct renderer_state *state = malloc(sizeof(struct renderer_state));
  bool success = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  if (!success)
  {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return NULL;
  }
  success = TTF_Init();
  if (!success)
  {
    SDL_Log("Failed to initialize TTF: %s", SDL_GetError());
    return NULL;
  }

  if (!initialize_sdl_video(machine, state))
  {
    return NULL;
  }

  if (!initialize_sdl_audio(machine, state))
  {
    return NULL;
  }

  return state;
}

void render(struct renderer_state *state)
{
  // Update FPS counter.
  uint32_t nticks = SDL_GetTicks();
  state->ticks[state->counter] = nticks - state->prev_ticks;
  state->prev_ticks = nticks;
  state->counter = (state->counter + 1) % 10;

  SDL_Surface *main_surface = SDL_GetWindowSurface(state->main_window);
  if (!main_surface)
  {
    SDL_Log("Failed to initialize Main Surface: %s", SDL_GetError());
  }
  SDL_PixelFormatDetails const *details = SDL_GetPixelFormatDetails(main_surface->format);
  if (details == NULL)
  {
    SDL_Log("Failed to get surface details: %s", SDL_GetError());
  }
  bool success = SDL_FillSurfaceRect(main_surface, NULL, SDL_MapRGB(details, NULL, 0x00, 0x00, 0x00));
  if (!success)
  {
    SDL_Log("Failed to fill surface: %s", SDL_GetError());
  }

  SDL_Surface *s = SDL_CreateSurface(16, 4, SDL_PIXELFORMAT_INDEX8);
  if (!s)
  {
    SDL_Log("Failed to fill surface: %s", SDL_GetError());
  }

  success = SDL_SetSurfacePalette(s, state->palette);
  if (!success)
  {
    SDL_Log("Failed to fill surface: %s", SDL_GetError());
  }

  for (int j = 0; j < 64; j++)
  {
    char *pixels = s->pixels;
    pixels[j] = j;
  }
  // for (int i = 0; i < 240; i++)
  // {
  //   for (int j = 0; j < 256; j++)
  //   {
  //     ((uint32_t *)s->pixels)[(i * s->w + j)] = palette_table[ppu.screen[i][j]];
  //   }
  // }

  SDL_Rect gameSrc = {
      .x = 0,
      .y = 0,
      .w = 16,
      .h = 4,
  };
  SDL_Rect gameDst = {
      .x = 0,
      .y = 0,
      .w = 256,
      .h = 64,
  };
  success = SDL_BlitSurfaceScaled(s, &gameSrc, main_surface, &gameDst, SDL_SCALEMODE_NEAREST);
  if (!success)
  {
    SDL_Log("Failed to blit surface: %s", SDL_GetError());
  }
  SDL_DestroySurface(s);

  success = SDL_UpdateWindowSurface(state->main_window);
  if (!success)
  {
    SDL_Log("Failed to fill surface: %s", SDL_GetError());
  }

  // Render Debug screen
  if (!(SDL_GetWindowFlags(state->debug_window) & SDL_WINDOW_HIDDEN))
  {
    SDL_Surface *debug_surface = SDL_GetWindowSurface(state->debug_window);
    if (!debug_surface)
    {
      SDL_Log("Failed to initialize Debug Surface: %s", SDL_GetError());
    }

    SDL_Color fg = {
        0xff,
        0xff,
        0xff,
        0xff,
    };

    success = SDL_FillSurfaceRect(debug_surface, NULL, SDL_MapRGB(details, NULL, 0x00, 0x00, 0x00));
    if (!success)
    {
      SDL_Log("Failed to fill surface: %s", SDL_GetError());
    }

    // FPS
    uint32_t total_ticks = 1; // Avoid division by 0
    for (int i = 0; i < 10; i++)
    {
      total_ticks += state->ticks[i];
    }

    char fps_string[10];
    sprintf(fps_string, "%4.0f FPS", 1000.0f / (total_ticks / 10.0f));
    SDL_Surface *textSurface = TTF_RenderText_Solid(state->font, fps_string, strlen(fps_string), fg);
    if (!textSurface)
    {
      SDL_Log("Failed to fill surface: %s", SDL_GetError());
    }
    SDL_Rect textLocation = {
        .x = 2,
        .y = 256,
        .w = 0,
        .h = 0,
    };
    success = SDL_BlitSurface(textSurface, NULL, debug_surface, &textLocation);
    if (!success)
    {
      SDL_Log("Failed to fill surface: %s", SDL_GetError());
    }
    SDL_DestroySurface(textSurface);

    SDL_Surface *s = SDL_CreateSurface(16, 4, SDL_PIXELFORMAT_INDEX8);
    if (!s)
    {
      SDL_Log("Failed to fill surface: %s", SDL_GetError());
    }

    success = SDL_SetSurfacePalette(s, state->palette);
    if (!success)
    {
      SDL_Log("Failed to fill surface: %s", SDL_GetError());
    }

    for (int j = 0; j < 64; j++)
    {
      char *pixels = s->pixels;
      pixels[j] = j;
    }

    SDL_Rect paletteSrc = {
        .x = 0,
        .y = 0,
        .w = 16,
        .h = 4,
    };
    SDL_Rect paletteDst = {
        .x = 0,
        .y = 0,
        .w = 256,
        .h = 64,
    };
    success = SDL_BlitSurfaceScaled(s, &paletteSrc, debug_surface, &paletteDst, SDL_SCALEMODE_NEAREST);
    if (!success)
    {
      SDL_Log("Failed to blit surface: %s", SDL_GetError());
    }
    SDL_DestroySurface(s);

    // debug_draw(&machine, s);
    success = SDL_UpdateWindowSurface(state->debug_window);
    if (!success)
    {
      SDL_Log("Failed to update surface: %s", SDL_GetError());
    }
  }
}

int handle_inputs(struct renderer_state *state)
{
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    //   controllers_handle(&controllers, &event);
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
      return 1;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
      SDL_WindowEvent *we = (SDL_WindowEvent *)&event;
      if (we->windowID == SDL_GetWindowID(state->main_window))
      {
        return 1;
      }
      else
      {
        SDL_HideWindow(state->debug_window);
      }
    }
    case SDL_EVENT_KEY_DOWN:
    {
      SDL_KeyboardEvent *ke = (SDL_KeyboardEvent *)&event;
      switch (ke->key)
      {
      case SDLK_F7:
        if (SDL_GetWindowFlags(state->debug_window) & SDL_WINDOW_HIDDEN)
        {
          SDL_ShowWindow(state->debug_window);
        }
        else
        {
          SDL_HideWindow(state->debug_window);
        }
        break;

      default:
        break;
      }
    }
    }
  }

  return 0;
}