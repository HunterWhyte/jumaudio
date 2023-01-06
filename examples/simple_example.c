/* Copyright (c) 2022  Hunter Whyte */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include "miniaudio/miniaudio.h"

#include "jumaudio.h"

#define WINDOW_HEIGHT 600
// buffer sizes must be multiples of 16 for pffft
#define FFT_BUF_SIZE 4096
// number of callback buffers to predecode ahead of writing to stream
#define PREDECODE_BUFS 5
// when changing this value, have to recompute constants in buildFreqTable!
#define NUM_BINS 1024
// weighting applied to frequency bins, [0] = freq, [1] = weight in db
#define NUM_WEIGHTS 15
const float weights[NUM_WEIGHTS][2] = {{63, -5},    {200, -5},   {250, -5},   {315, -5},
                                       {400, -4.8}, {500, -3.2}, {630, -1.9}, {800, -0.8},
                                       {1000, 0.0}, {1250, 0.6}, {1600, 1.0}, {2000, 1.2},
                                       {2500, 3.3}, {3150, 4.2}, {4000, 5.0}};

#define NUM_FREQS 10
const float freqs[NUM_FREQS][2] = {{0, 35},      {0.2, 450},  {0.3, 700},  {0.4, 1200},
                                   {0.5, 1700},  {0.6, 2600}, {0.7, 4100}, {0.8, 6500},
                                   {0.9, 10000}, {1.0, 20000}};

int main(int argc, char* argv[]) {
  // SDL
  SDL_Window* sdl_window;
  SDL_Renderer* renderer;
  SDL_Event event;

  jum_AudioSetup* audio;
  jum_FFTSetup* fft;
  uint32_t start, delta;
  int32_t input;
  uint32_t i;
  bool playback = false;
  char* filepath;

  if (argc == 2) {
    filepath = argv[1];
    playback = true;
  }

  if (SDL_Init(SDL_INIT_VIDEO) == -1) {
    fprintf(stderr, "Couldn't intialize renderer: %s.\n", SDL_GetError());
    exit(1);
  }

  sdl_window =
      SDL_CreateWindow("jumaudio visualizer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       NUM_BINS, WINDOW_HEIGHT, SDL_WINDOW_OPENGL);
  renderer =
      SDL_CreateRenderer(sdl_window, -1, (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
  SDL_RenderSetVSync(renderer, 1);

  fft = jum_initFFT(freqs, NUM_FREQS, weights, NUM_WEIGHTS, FFT_BUF_SIZE, NUM_BINS);
  audio = jum_initAudio(FFT_BUF_SIZE * (PREDECODE_BUFS + 5), PREDECODE_BUFS, FFT_BUF_SIZE);

  if (playback) {
    if (jum_startPlayback(audio, filepath, -1) != 0) {
      printf("failed to start playback\n");
      return -1;
    }
  } else {
    printf("Capture Devices\n");
    for (i = 0; i < audio->capture_device_count; ++i) {
      printf("  %u: %s\n", i, audio->capture_device_info[i].name);
    }
    // prompt user for which capture device to use
    printf("Select capture device\n");
    input = getchar() - 48;
    if (input < 0 || input >= (Sint32)audio->capture_device_count) {
      printf("Invalid device selected, exiting...\n");
      return -1;
    }
    printf("%d: %s \n", input, audio->capture_device_info[input].name);

    if (jum_startCapture(audio, input) != 0) {
      printf("failed to start capture\n");
      return -1;
    }
  }

  jum_printAudioInfo(audio->info);

  bool done = false;
  float amplitude = 1;
  while (1) {
    start = SDL_GetTicks();
    // printf("frame time: %dms\n", msec);
    if (SDL_PollEvent(&event) && (event.type == SDL_KEYDOWN)) {
      switch (event.key.keysym.sym) {
        case SDLK_LEFT:
          jum_pausePlayback(audio);
          break;
        case SDLK_RIGHT:
          jum_resumePlayback(audio);
          break;
        case SDLK_UP:
          amplitude += 0.1;
          printf("amplitude: %f\n", amplitude);
          jum_setAmplitude(audio, amplitude);
          break;
        case SDLK_DOWN:
          amplitude -= 0.1;
          printf("amplitude: %f\n", amplitude);
          jum_setAmplitude(audio, amplitude);
          break;
        default:
          done = true;
          break;
      }
      if(done)
        break;
    }

    jum_analyze(fft, audio, delta);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (ma_uint32 i = 1; i < NUM_BINS; i++) {
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
      SDL_RenderDrawLine(renderer, i - 1, WINDOW_HEIGHT - WINDOW_HEIGHT * (fft->result[i - 1]), i,
                         WINDOW_HEIGHT - WINDOW_HEIGHT * (fft->result[i]));
    }
    SDL_RenderDrawLine(renderer, 0, 0, fft->level * NUM_BINS, 0);
    SDL_RenderDrawLine(renderer, 0, WINDOW_HEIGHT - 1, jum_getCursor(audio) * NUM_BINS,
                       WINDOW_HEIGHT - 1);
    SDL_RenderPresent(renderer);
    delta = SDL_GetTicks() - start;
  }

  jum_deinitFFT(fft);
  jum_deinitAudio(audio);

  /* SDL cleanup */
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(sdl_window);

  SDL_Quit();

  return 0;
}