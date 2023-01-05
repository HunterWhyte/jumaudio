/* Copyright (c) 2022  Hunter Whyte */

#include "jumaudio.h"

#include <math.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "pffft/pffft.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

void dataCallback(ma_device* p_device, void* p_output, const void* p_input, ma_uint32 frame_count);
void readIntoFFTBuffer(const float* samples_in, ma_int32 in_pos, ma_int32 in_size,
                       float* samples_out, ma_int32 out_size, const float* hamming,
                       ma_int32 channels);
void readIntoBins(float* out, const float* freqs, ma_int32 out_sz, const float* fft,
                  ma_int32 fft_sz, float sample_rate);
void applyWeighting(float* data, const float* weights, ma_int32 size);
void applyAveraging(const float* current, float* averaged, ma_int32 size);
void applySmoothing(const float* in, float* out, ma_int32 size);
float normalizeArray(float* array, ma_int32 size, float max);
void initPFFFT(PFFFTInfo* info, ma_int32 size);
void deinitPFFFT(PFFFTInfo* info);
void buildWeightTable(const float* freq_bins, ma_int32 num_bins, const float in_weights[][2],
                      ma_int32 num_weights, float* out_weights);
void buildFreqTable(float* freq_bins, ma_int32 num_bins, const float in_freqs[][2],
                    ma_int32 num_freqs);
void buildHammingWindow(float* hamming_lut, ma_int32 sample_size);
float lerpArray(const float array[][2], ma_int32 size, float x);

// miniaudio data callback for both capture and playback
void dataCallback(ma_device* p_device, void* p_output, const void* p_input, ma_uint32 frame_count) {
  jum_AudioSetup* setup;
  ma_uint64 frames_read;
  ma_int32 reader_pos;
  ma_int32 writer_pos;
  ma_decoder p_decoder;
  ma_uint32 remaining;
  float* input;
  float* output;
  ma_uint8* temp;
  ma_uint32 i;

  setup = (jum_AudioSetup*)p_device->pUserData;
  writer_pos = setup->control.writer_pos;
  remaining = (setup->buffer.sz - writer_pos) / setup->info.channels;

  if (setup->mode == AUDIO_MODE_CAPTURE && p_input) {
    input = (float*)p_input;
    if (remaining > frame_count) {
      memcpy(&setup->buffer.buf[writer_pos], input,
             frame_count * setup->info.channels * sizeof(float));
    } else {
      memcpy(&setup->buffer.buf[writer_pos], input,
             remaining * setup->info.channels * sizeof(float));
      memcpy(&setup->buffer.buf[0], &input[remaining * setup->info.channels],
             (frame_count - remaining) * setup->info.channels * sizeof(float));
    }
    reader_pos = writer_pos;

  } else if (setup->mode == AUDIO_MODE_PLAYBACK && p_output) {

    p_decoder = setup->decoder[setup->current_decoder];
    // read samples in original decoder format into a temporary buffer
    if (frame_count * setup->info.bytes_per_frame > setup->conversion_sz) {
      printf("error, requesting more frames than we have room for in temp buffer\n");
      return;
    } else {
      ma_decoder_read_pcm_frames(&p_decoder, setup->conversion_buf, frame_count, &frames_read);
    }

    // pause playback if reach end of file TODO add looping, next song etc
    if (frames_read == 0) {
      sem_wait(&setup->control.mutex);
      setup->control.playing = false;
      sem_post(&setup->control.mutex);
      return;
    }

    // cast conversion buf to uint8* so that we can index it by byte
    temp = (ma_uint8*)setup->conversion_buf;
    // convert from decoder output format to 32 bit float and copy to the audio buffer
    if (remaining > frame_count) {
      ma_convert_pcm_frames_format(&setup->buffer.buf[writer_pos], ma_format_f32, temp,
                                   setup->info.format, frame_count, setup->info.channels,
                                   ma_dither_mode_none);
    } else {
      ma_convert_pcm_frames_format(&setup->buffer.buf[writer_pos], ma_format_f32, temp,
                                   setup->info.format, remaining, setup->info.channels,
                                   ma_dither_mode_none);
      ma_convert_pcm_frames_format(
          &setup->buffer.buf[0], ma_format_f32, &temp[remaining * setup->info.bytes_per_frame],
          setup->info.format, (frame_count - remaining), setup->info.channels, ma_dither_mode_none);
    }

    // determine where the reader pointer should be based on the writer
    reader_pos = writer_pos - (setup->predecode_bufs * setup->info.period * setup->info.channels);
    if (reader_pos < 0)
      reader_pos = setup->buffer.sz + reader_pos;

    output = (float*)p_output;
    // copy from reader pointer to output stream
    for (i = 0; i < frame_count * setup->info.channels; i++) {
      output[i] = setup->buffer.buf[(reader_pos + i) % setup->buffer.sz];  // TODO * by amplitude
    }
  } else {
    printf("warning, device started but no configuration\n");
    return;
  }

  // printf("writer: %d reader: %d frames %d\n", audio_control.writer_pos, reader_pos, frame_count);
  // increment writer pointer
  writer_pos += frame_count * setup->info.channels;
  if (writer_pos >= setup->buffer.sz) {
    writer_pos -= setup->buffer.sz;
  }

  sem_wait(&setup->control.mutex);
  setup->control.writer_pos = writer_pos;
  setup->control.reader_pos = reader_pos;
  setup->control.new_data_flag = true;
  sem_post(&setup->control.mutex);
}

// create jum_AudioSetup, initialize miniaudio context, enumerate devices
jum_AudioSetup* jum_initAudio(ma_uint32 buffer_size, ma_uint32 predecode_bufs, ma_uint32 period) {
  ma_result result;
  jum_AudioSetup* setup = (jum_AudioSetup*)malloc(sizeof(jum_AudioSetup));
  // allocate enough for max of 2 channels, if we are decoding 1 channel only half will be used
  setup->buffer.buf = (float*)malloc(buffer_size * 2 * sizeof(float));
  setup->buffer.allocated_sz = buffer_size * 2;
  setup->predecode_bufs = predecode_bufs;
  // allocate conversion buffer based on period * max possible bytes per frame (32 bits*2 channels)
  setup->conversion_sz = period * sizeof(float) * 2;
  setup->conversion_buf = (void*)malloc(period * sizeof(float) * 2);

  setup->mode = AUDIO_MODE_NONE;
  setup->control.new_data_flag = false;
  setup->control.playing = true;
  setup->control.writer_pos = 0;
  setup->control.reader_pos = 0;
  sem_init(&setup->control.mutex, 0, 1);

  setup->info.sample_rate = 0;
  setup->info.bytes_per_frame = 0;
  setup->info.channels = 0;
  setup->info.format = (ma_format)0;
  setup->info.period = period;

  result = ma_context_init(NULL, 0, NULL, &setup->context);
  if (result != MA_SUCCESS) {
    printf("Failed to initialize context.\n");
    return NULL;
  }
  result = ma_context_get_devices(&setup->context, &setup->playback_device_info,
                                  &setup->playback_device_count, &setup->capture_device_info,
                                  &setup->capture_device_count);
  if (result != MA_SUCCESS) {
    printf("Failed to retrieve device information.\n");
    return NULL;
  }

  return setup;
}

void jum_deinitAudio(jum_AudioSetup* setup) {
  ma_device_uninit(&setup->device);
  ma_decoder_uninit(&setup->decoder[0]);
  ma_decoder_uninit(&setup->decoder[1]);

  free(setup->buffer.buf);
  free(setup->conversion_buf);
  setup = NULL;
}

ma_int32 jum_startPlayback(jum_AudioSetup* setup, const char* filepath, ma_int32 device_index) {
  ma_result result;
  ma_device_config config;

  // check if device has already been initialized
  if (setup->mode != AUDIO_MODE_NONE) {
    ma_device_uninit(&setup->device);
    ma_decoder_uninit(&setup->decoder[0]);
    ma_decoder_uninit(&setup->decoder[1]);
    setup->mode = AUDIO_MODE_NONE;
  }

  result = ma_decoder_init_file(filepath, NULL, &setup->decoder[0]);
  if (result != MA_SUCCESS) {
    printf("Could not load file: %s\n", filepath);
    return -1;
  }
  config = ma_device_config_init(ma_device_type_playback);
  // if device index is valid then set it
  if (device_index >= 0 && device_index < (ma_int32)setup->playback_device_count) {
    config.playback.pDeviceID = &setup->playback_device_info[device_index].id;
  }
  // have to manually convert to float for fft so just use float for playback
  config.playback.format = ma_format_f32;
  config.playback.channels = setup->decoder[0].outputChannels;
  config.sampleRate = setup->decoder[0].outputSampleRate;
  config.dataCallback = dataCallback;
  config.pUserData = setup;
  config.periodSizeInFrames = setup->info.period;

  setup->info.channels = config.playback.channels;
  setup->info.sample_rate = config.sampleRate;
  setup->info.format = setup->decoder[0].outputFormat;
  setup->info.bytes_per_frame = ma_get_bytes_per_frame(setup->info.format, setup->info.channels);

  // set buffer size to appropriate value for given number of channels TODO look at how we are dealing with 1 vs 2 channels
  setup->buffer.sz =
      setup->info.channels == 2 ? setup->buffer.allocated_sz : setup->buffer.allocated_sz / 2;

  result = ma_device_init(&setup->context, &config, &setup->device);
  if (result != MA_SUCCESS) {
    printf("Failed to open device.\n");
    ma_decoder_uninit(&setup->decoder[0]);
    return -1;
  }

  result = ma_device_start(&setup->device);
  if (result != MA_SUCCESS) {
    ma_device_uninit(&setup->device);
    ma_decoder_uninit(&setup->decoder[0]);
    printf("Failed to start device.\n");
    return -1;
  }

  setup->mode = AUDIO_MODE_PLAYBACK;

#ifdef JUMAUDIO_DEBUG
  char* selected_device_name;
  if (device_index >= 0 && device_index < (ma_int32)setup->playback_device_count) {
    selected_device_name = setup->playback_device_info[device_index].name;
  } else {
    for (ma_uint32 i = 0; i < setup->playback_device_count; i++) {
      if (setup->playback_device_info[i].isDefault) {
        selected_device_name = setup->playback_device_info[i].name;
      }
    }
  }

  printf("Starting playback of '%s', on device [%d]'%s'\n", device_index, filepath, selected_device_name);
  jum_printAudioInfo(setup->info);
#endif

  return 0;
}

ma_int32 jum_startCapture(jum_AudioSetup* setup, ma_int32 device_index) {
  ma_result result;
  ma_device_config config;

  // check if there is already an active device, if there is then stop it and start a new one
  if (setup->mode != AUDIO_MODE_NONE) {
    ma_device_uninit(&setup->device);
    ma_decoder_uninit(&setup->decoder[0]);
    ma_decoder_uninit(&setup->decoder[1]);
    setup->mode = AUDIO_MODE_NONE;
  }

  config = ma_device_config_init(ma_device_type_capture);
  if (device_index >= 0 && device_index < (ma_int32)setup->capture_device_count) {
    config.capture.pDeviceID = &setup->capture_device_info[device_index].id;
  }
  config.capture.format = ma_format_f32;
  config.capture.channels = 2;
  config.sampleRate = 44100;
  config.dataCallback = dataCallback;
  config.pUserData = setup;

  setup->info.channels = config.capture.channels;
  setup->info.sample_rate = config.sampleRate;
  setup->info.format = config.capture.format;
  setup->info.bytes_per_frame = ma_get_bytes_per_frame(setup->info.format, setup->info.channels);

  // set buffer size to appropriate value for 2 channels
  setup->buffer.sz = setup->buffer.allocated_sz;

  result = ma_device_init(&setup->context, &config, &setup->device);
  if (result != MA_SUCCESS) {
    printf("Failed to open device.\n");
    return -1;
  }

  result = ma_device_start(&setup->device);
  if (result != MA_SUCCESS) {
    ma_device_uninit(&setup->device);
    printf("Failed to start device.\n");
    return -1;
  }

  setup->mode = AUDIO_MODE_CAPTURE;

#ifdef JUMAUDIO_DEBUG
  char* selected_device_name;
  if (device_index >= 0 && device_index < (ma_int32)setup->capture_device_count) {
    selected_device_name = setup->capture_device_info[device_index].name;
  } else {
    for (ma_uint32 i = 0; i < setup->capture_device_count; i++) {
      if (setup->capture_device_info[i].isDefault) {
        selected_device_name = setup->capture_device_info[i].name;
      }
    }
  }

  printf("Starting capture of device [%d]'%s'\n", device_index, selected_device_name);
  jum_printAudioInfo(setup->info);
#endif

  return 0;
}

void jum_printAudioInfo(AudioInfo info) {
  switch (info.format) {
    case (ma_format_u8):
      printf("ma_format_u8\n");
      break;
    case (ma_format_s16):
      printf("ma_format_s16\n");
      break;
    case (ma_format_s24):
      printf("ma_format_s24\n");
      break;
    case (ma_format_s32):
      printf("ma_format_s32\n");
      break;
    case (ma_format_f32):
      printf("ma_format_f32\n");
      break;
    default:
      printf("unknown\n");
      break;
  }
  printf("num channels %d\n", info.channels);
  printf("sample rate %d\n", info.sample_rate);
  printf("bytes per frame %d\n", info.bytes_per_frame);
}

// perform fft calculation, result is written to fft->result
void jum_analyze(jum_FFTSetup* fft, jum_AudioSetup* audio, ma_uint32 msec) {
  ma_int32 reader_pos;
  ma_int32 temp_pos;
  bool playing;

  // increment pointer position (in 32 bit float samples) based on given time
  fft->pos += ((audio->info.sample_rate * msec) / 1000L) * audio->info.channels;
  if (fft->pos >= audio->buffer.sz) {
    fft->pos -= audio->buffer.sz;
  }

  // this blocks end of audio data callback, so keep as short as possible
  // read necessary data from the datacallback thread
  sem_wait(&audio->control.mutex);
  if (audio->control.new_data_flag) {  // data written to audio stream
    reader_pos = audio->control.reader_pos;
    audio->control.new_data_flag = 0;
  } else {
    reader_pos = -1;  // no new reader pos
  }
  playing = audio->control.playing;
  sem_post(&audio->control.mutex);

  // if there was a new reader position
  if (reader_pos >= 0) {
    // if the fft position is lagging behind or too far ahead of reader, resync
    if (fft->pos < reader_pos || (fft->pos - reader_pos) > MAX_DESYNC) {
      fft->pos = reader_pos;
    }
  }

  if (playing) {
    // temp position since fft_pos keeps up with actual playback rate
    // if we are capturing, then delay one buffer size behind reader
    if (audio->mode == AUDIO_MODE_CAPTURE) {
      temp_pos = fft->pos - fft->pffft.sz * audio->info.channels;
    } else {
      temp_pos = fft->pos;
    }

    if (temp_pos < 0) {
      temp_pos = audio->buffer.sz + temp_pos;
    }

    readIntoFFTBuffer(audio->buffer.buf, temp_pos, audio->buffer.sz, fft->pffft.in, fft->pffft.sz,
                      fft->luts.hamming, audio->info.channels);
    pffft_transform_ordered(fft->pffft.setup, fft->pffft.in, fft->pffft.out, NULL, PFFFT_FORWARD);
  } else {
    memset(fft->pffft.out, 0, fft->pffft.sz * sizeof(float));  // TODO only have to clear this once
  }

  readIntoBins(fft->raw, fft->luts.freqs, fft->num_bins, fft->pffft.out, fft->pffft.sz,
               audio->info.sample_rate);
  applyWeighting(fft->raw, fft->luts.weights, fft->num_bins);
  applyAveraging(fft->raw, fft->averaged, fft->num_bins);
  applySmoothing(fft->averaged, fft->result, fft->num_bins);
  fft->max = normalizeArray(fft->result, fft->num_bins, fft->max);
}

// apply windowing and copy to fft buffer
void readIntoFFTBuffer(const float* samples_in, ma_int32 in_pos, ma_int32 in_size,
                       float* samples_out, ma_int32 out_size, const float* hamming,
                       ma_int32 channels) {
  ma_int32 i;
  for (i = 0; i < out_size; i++) {
    samples_out[i] = samples_in[in_pos % in_size] * hamming[i];
    in_pos++;
    if (channels == 2) {  // two channels, use average
      samples_out[i] += samples_in[in_pos % in_size] * hamming[i];
      samples_out[i] /= 2;
      in_pos++;
    }
  }
}

// take equally distributed fft samples and average into bins
void readIntoBins(float* out, const float* freqs, ma_int32 out_sz, const float* fft,
                  ma_int32 fft_sz, float sample_rate) {
  float re, im, freq;
  ma_int32 current_bin = 0;
  ma_int32 samples_in_bin = 0;
  ma_int32 i;
  out[0] = 0;

  // read from raw output into bins and average
  for (i = 0; i < fft_sz / 2; i++) {
    re = fft[i * 2];
    im = fft[i * 2 + 1];

    // frequency for this sample
    freq = (i * (sample_rate / 2)) / (fft_sz / 2);
    // fits into current bin
    if (freq > freqs[current_bin]) {       // passed current bin
      out[current_bin] /= samples_in_bin;  // take average
      samples_in_bin = 0;
      current_bin++;

      while (freq > freqs[current_bin]) {
        if (current_bin >= out_sz)
          break;
        // if skipping multiple bins then duplicate prev value TODO could smooth better?
        if (current_bin != 0)
          out[current_bin] = out[current_bin - 1];
        current_bin++;
      }
      if (current_bin >= out_sz)
        break;

      out[current_bin] = 0;  // set to 0 so that we can +=
    }

    out[current_bin] += sqrt((re * re) + (im * im));
    samples_in_bin++;
  }
}

void applyWeighting(float* data, const float* weights, ma_int32 size) {
  ma_int32 i;
  for (i = 0; i < size; i++) {
    // log scaling
    data[i] = log10f(data[i] + 0.5) + 0.31;  // prevent negative numbers
    // apply weighting
    data[i] *= weights[i];
  }
}

void applyAveraging(const float* current, float* averaged, ma_int32 size) {
  ma_int32 i;
  for (i = 0; i < size; i++) {
    // apply exponential moving average
    if (current[i] > averaged[i]) {
      // jump up quickly
      averaged[i] = (0.2F) * averaged[i] + ((1 - 0.2F) * current[i]);
    } else {
      // fall down slowly
      averaged[i] = (0.9F) * averaged[i] + ((1 - 0.9F) * current[i]);
    }
  }
}

void applySmoothing(const float* in, float* out, ma_int32 size) {
  ma_int32 i, j, sample_width;
  float x;
  for (i = 0; i < size; i++) {
    // smooth between frequency bins
    // number of surrounding bins in each direction to take avg of
    x = ((float)size - (float)i) / ((float)size);
    sample_width = (ma_int32)(x * 7) + 2;
    for (j = 0; j < sample_width; j++) {
      if (i - j > 0)
        out[i] += in[i - j] / (sample_width * 2);
      if (i + j < size)
        out[i] += in[i + j] / (sample_width * 2);
    }
  }
}

float normalizeArray(float* array, ma_int32 size, float max) {
  ma_int32 i;
  // first pass check if max provided has been exceeded
  for (i = 0; i < size; i++) {
    if (array[i] > max)
      max = array[i];
  }
  // second pass, normalize result based on max, min is always 0
  for (i = 0; i < size; i++) {
    array[i] = (array[i] - 0) / (max - 0);
  }
  return max;
}

jum_FFTSetup* jum_initFFT(const float freq_points[][2], ma_int32 freqs_sz,
                          const float weight_points[][2], ma_int32 weights_sz, ma_int32 fft_sz,
                          ma_int32 num_bins) {

  jum_FFTSetup* setup = (jum_FFTSetup*)malloc(sizeof(jum_FFTSetup));

  initPFFFT(&setup->pffft, fft_sz);
  setup->raw = (float*)malloc(num_bins * sizeof(float));
  setup->averaged = (float*)malloc(num_bins * sizeof(float));
  setup->result = (float*)malloc(num_bins * sizeof(float));

  setup->luts.freqs = (float*)malloc(num_bins * sizeof(float));
  buildFreqTable(setup->luts.freqs, num_bins, freq_points, freqs_sz);
  setup->luts.weights = (float*)malloc(num_bins * sizeof(float));
  buildWeightTable(setup->luts.freqs, num_bins, weight_points, weights_sz, setup->luts.weights);
  setup->luts.hamming = (float*)malloc(fft_sz * sizeof(float));
  buildHammingWindow(setup->luts.hamming, fft_sz);

  setup->max = 2.5;
  setup->pos = 0;
  setup->num_bins = num_bins;

  return setup;
}

void jum_deinitFFT(jum_FFTSetup* setup) {
  deinitPFFFT(&setup->pffft);

  free(setup->raw);
  free(setup->averaged);
  free(setup->result);

  free(setup->luts.weights);
  free(setup->luts.freqs);
  free(setup->luts.hamming);

  free(setup);
  setup = NULL;
}

void initPFFFT(PFFFTInfo* info, ma_int32 size) {
  info->sz = size;
  info->setup = pffft_new_setup(size, PFFFT_REAL);
  info->in = (float*)pffft_aligned_malloc(size * sizeof(float));
  info->out = (float*)pffft_aligned_malloc(size * 2 * sizeof(float));
}

void deinitPFFFT(PFFFTInfo* info) {
  if (info) {
    pffft_destroy_setup(info->setup);
    info->setup = NULL;
    pffft_aligned_free(info->in);
    info->in = NULL;
    pffft_aligned_free(info->out);
    info->out = NULL;
  }
}

// build lookup table of weights for each frequency bin
// must be done after building lookup table of frequency bins
void buildWeightTable(const float* freq_bins, ma_int32 num_bins, const float in_weights[][2],
                      ma_int32 num_weights, float* out_weights) {
  ma_int32 i;
  float db;
  for (i = 0; i < num_bins; i++) {
    db = lerpArray(in_weights, num_weights, freq_bins[i]);
    out_weights[i] = powf(10, (db / 30));  // use multiplication factor
  }
}

// build lookup table of frequency bins
void buildFreqTable(float* freq_bins, ma_int32 num_bins, const float in_freqs[][2],
                    ma_int32 num_freqs) {
  ma_int32 i;
  for (i = 0; i < num_bins; i++) {
    freq_bins[i] = lerpArray(in_freqs, num_freqs, ((float)i) / ((float)num_bins));
  }
}

// build lookup table of constants for each sample fed into fft
void buildHammingWindow(float* hamming_lut, ma_int32 sample_size) {
  ma_int32 i;
  for (i = 0; i < sample_size; i++) {
    hamming_lut[i] = 0.54 * (1 - cos(2 * M_PI * i / sample_size));
  }
}

// 2d array of x[0],y[1] points, sorted in increasing order of x ([0])
float lerpArray(const float array[][2], ma_int32 size, float x) {
  float ax, ay, bx, by, mix;
  ma_int32 i;
  // handle case where value is before first point
  if (array[0][0] > x) {
    return array[0][1];
  }
  for (i = 1; i < size; i++) {
    if (array[i][0] > x) {  // find which points we are between
      ax = array[i - 1][0];
      ay = array[i - 1][1];
      bx = array[i][0];
      by = array[i][1];
      mix = (x - ax) / (bx - ax);
      return ay + (by - ay) * mix;
    }
  }
  // handle case where value is after last point
  return array[size - 1][1];
}