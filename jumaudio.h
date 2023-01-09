/* Copyright (c) 2022  Hunter Whyte */

#ifndef JUMAUDIO_H_
#define JUMAUDIO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "miniaudio/miniaudio.h"
#include "pffft/pffft.h"

#define M_PI 3.14159265358979323846

// 1500 samples (~35ms)
#define MAX_DESYNC 1500
#define MAX_SOUND_FILES 32
#define SOUND_FLAGS (MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC)
// read only after setup
typedef struct audioInfo {
  ma_uint32 sample_rate;
  ma_uint32 bytes_per_frame;
  ma_uint32 channels;
  ma_format format;
  ma_uint32 period;
} AudioInfo;

// read/write for init and audio stream callback, read only for everything else
// all accesses after intialization should acquire mutex
typedef struct audioControl {
  sem_t mutex;
  ma_int32 writer_pos;
  ma_int32 reader_pos;
  bool new_data_flag;
  float music_volume;
  float other_volume;
} AudioControl;

typedef struct audioBuffer {
  // lock free, single producer, single consumer circular buffer
  float* buf;
  ma_int32 sz;
  ma_int32 allocated_sz;
} AudioBuffer;

typedef enum {
  AUDIO_MODE_NONE,
  AUDIO_MODE_PLAYBACK,
  AUDIO_MODE_CAPTURE,
} jum_AudioMode;

// absolute filepath along with its associated ma_audio sound
typedef struct sound_file {
  char* filepath;
  ma_sound sound;
} SoundFile;

// audio player/capturer setup
typedef struct jum_audio {
  AudioBuffer buffer;
  AudioControl control;
  AudioInfo info;

  float* conversion_buf;
  ma_uint32 conversion_sz;
  ma_int32 predecode_bufs;

  ma_context context;
  ma_resource_manager resource_manager;

  ma_engine music_engine;  // sounds played from this engine will have FFT performed
  SoundFile song_file;     // the single sound playing on the music engine

  ma_engine other_engine;  // sounds played from this engine will not contribute to FFT
  SoundFile sound_files[MAX_SOUND_FILES];
  ma_int32 num_sound_files;

  ma_device playback_device;
  bool playback_open;
  ma_device capture_device;
  bool capture_open;

  jum_AudioMode mode;

  ma_device_info* playback_device_info;
  ma_device_info* capture_device_info;
  ma_uint32 playback_device_count, capture_device_count;
} jum_AudioSetup;

// pffft data
typedef struct pffftinfo {
  ma_int32 sz;
  float* in;
  float* out;
  PFFFT_Setup* setup;
} PFFFTInfo;

typedef struct fft_tables {
  float* freqs;    // frequencies for each bin
  float* weights;  // weights applied for each frequency bin
  float* hamming;  // constants to multiple input by for hamming window
} FFTTables;

typedef struct jum_fft {
  PFFFTInfo pffft;    // pffft setup and inout buffers
  ma_int32 num_bins;  // number of output frequncy bins
  float* raw;         // raw fft output, num_bins size
  float* averaged;    // fft with averaging over time, num_bins size
  float* result;      // final fft with averaging and weighting, num_bins size
  FFTTables luts;     // lookup tables generated on init
  float max;          // max result ever output, keep track for normalizing output
  ma_int32 pos;       // last pos in audio buffer used for fft
  float level;        // average audio level of the audio buffer
} jum_FFTSetup;

jum_AudioSetup* jum_initAudio(ma_uint32 buffer_size, ma_uint32 predecode_bufs, ma_uint32 period);
void jum_deinitAudio(jum_AudioSetup* setup);
ma_int32 jum_openPlaybackDevice(jum_AudioSetup* setup, ma_int32 device_index);
ma_int32 jum_openCaptureDevice(jum_AudioSetup* setup, ma_int32 device_index);
void jum_printAudioInfo(AudioInfo info);
void jum_analyze(jum_FFTSetup* fft, jum_AudioSetup* audio, ma_uint32 msec);
jum_FFTSetup* jum_initFFT(const float freq_points[][2], ma_int32 freqs_sz,
                          const float weight_points[][2], ma_int32 weights_sz, ma_int32 fft_sz,
                          ma_int32 num_bins);
void jum_deinitFFT(jum_FFTSetup* setup);
void jum_setMusicVolume(jum_AudioSetup* setup, float volume);
void jum_setOtherVolume(jum_AudioSetup* setup, float volume);
ma_int32 jum_playSong(jum_AudioSetup* setup, const char* filepath);
float jum_getSongCursor(jum_AudioSetup* setup);
float jum_getSongLength(jum_AudioSetup* setup);
bool jum_isSongFinished(jum_AudioSetup* setup);
void jum_pauseSong(jum_AudioSetup* setup);
void jum_resumeSong(jum_AudioSetup* setup);
ma_int32 jum_loadSound(jum_AudioSetup* setup, const char* filepath);
ma_int32 jum_playSound(jum_AudioSetup* setup, ma_int32 handle, float repeat_delay);
void jum_clearSoundFiles(jum_AudioSetup* setup);
void jum_setFFTMode(jum_AudioSetup* setup, jum_AudioMode mode);

#ifdef __cplusplus
}
#endif

#endif  // JUMAUDIO_H_