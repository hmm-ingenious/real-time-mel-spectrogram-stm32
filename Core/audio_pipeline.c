#include "audio_pipeline.h"

#include "arm_math.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define AUDIO_N_BINS (AUDIO_N_FFT / 2U + 1U)

static float g_window[AUDIO_N_FFT];
static float g_mel_weights[AUDIO_N_MELS][AUDIO_N_BINS];
static float g_fft_in[AUDIO_N_FFT];
static float g_fft_out[AUDIO_N_FFT];
static float g_mag[AUDIO_N_BINS];
static arm_rfft_fast_instance_f32 g_rfft;

static float hz_to_mel(float hz) { return 2595.0f * log10f(1.0f + hz / 700.0f); }
static float mel_to_hz(float mel) { return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f); }

void audio_pipeline_init(void)
{
    const float fs = (float)AUDIO_SAMPLE_RATE;
    const float f_min = 50.0f;
    const float f_max = fs * 0.5f;
    float mel_pts[AUDIO_N_MELS + 2U];
    float hz_pts[AUDIO_N_MELS + 2U];
    uint32_t bin_pts[AUDIO_N_MELS + 2U];

    (void)arm_rfft_fast_init_f32(&g_rfft, AUDIO_N_FFT);

    for (uint32_t n = 0; n < AUDIO_N_FFT; n++)
    {
        g_window[n] = 0.5f - 0.5f * cosf((2.0f * PI_F * (float)n) / (float)(AUDIO_N_FFT - 1U));
    }

    {
        const float mel_min = hz_to_mel(f_min);
        const float mel_max = hz_to_mel(f_max);
        const float mel_step = (mel_max - mel_min) / (float)(AUDIO_N_MELS + 1U);
        for (uint32_t i = 0; i < AUDIO_N_MELS + 2U; i++)
        {
            mel_pts[i] = mel_min + (float)i * mel_step;
            hz_pts[i] = mel_to_hz(mel_pts[i]);
            bin_pts[i] = (uint32_t)((hz_pts[i] * (float)AUDIO_N_FFT) / fs);
            if (bin_pts[i] > (AUDIO_N_BINS - 1U))
            {
                bin_pts[i] = AUDIO_N_BINS - 1U;
            }
        }
    }

    memset(g_mel_weights, 0, sizeof(g_mel_weights));
    for (uint32_t m = 0; m < AUDIO_N_MELS; m++)
    {
        uint32_t left = bin_pts[m];
        uint32_t center = bin_pts[m + 1U];
        uint32_t right = bin_pts[m + 2U];

        if (center <= left)
        {
            center = left + 1U;
        }
        if (right <= center)
        {
            right = center + 1U;
            if (right >= AUDIO_N_BINS)
            {
                right = AUDIO_N_BINS - 1U;
            }
        }

        for (uint32_t k = left; (k < center) && (k < AUDIO_N_BINS); k++)
        {
            g_mel_weights[m][k] = ((float)k - (float)left) / ((float)center - (float)left);
        }
        for (uint32_t k = center; (k <= right) && (k < AUDIO_N_BINS); k++)
        {
            g_mel_weights[m][k] = ((float)right - (float)k) / ((float)right - (float)center);
        }
    }
}

void audio_pipeline_process_frame(const int16_t *frame_pcm16, uint16_t *out_mels_u16)
{
    for (uint32_t n = 0; n < AUDIO_N_FFT; n++)
    {
        g_fft_in[n] = ((float)frame_pcm16[n] / 32768.0f) * g_window[n];
    }

    arm_rfft_fast_f32(&g_rfft, g_fft_in, g_fft_out, 0U);

    g_mag[0] = fabsf(g_fft_out[0]);
    for (uint32_t k = 1U; k < (AUDIO_N_FFT / 2U); k++)
    {
        float re = g_fft_out[2U * k];
        float im = g_fft_out[2U * k + 1U];
        g_mag[k] = sqrtf(re * re + im * im);
    }
    g_mag[AUDIO_N_FFT / 2U] = fabsf(g_fft_out[1]);

    for (uint32_t m = 0; m < AUDIO_N_MELS; m++)
    {
        float e = 0.0f;
        for (uint32_t k = 0; k < AUDIO_N_BINS; k++)
        {
            e += g_mag[k] * g_mel_weights[m][k];
        }
        e = logf(1.0f + e);
        if (e > 8.0f)
        {
            e = 8.0f;
        }
        out_mels_u16[m] = (uint16_t)(e * 4096.0f);
    }
}
