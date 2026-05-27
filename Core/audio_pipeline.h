#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>

#define AUDIO_FRAME_SAMPLES 512U
#define AUDIO_N_FFT 512U
#define AUDIO_N_MELS 40U
#define AUDIO_SAMPLE_RATE 16000U

void audio_pipeline_init(void);

/*
 * Input:  frame_pcm16 length AUDIO_FRAME_SAMPLES
 * Output: out_mels_u16 length AUDIO_N_MELS
 */
void audio_pipeline_process_frame(const int16_t *frame_pcm16, uint16_t *out_mels_u16);

#endif
