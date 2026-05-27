#include "uart_stream.h"

#include "audio_pipeline.h"
#include <string.h>

#define UART_RX_RING_SIZE 4096U

static UART_HandleTypeDef *g_huart = NULL;
static uint8_t g_rx_byte = 0U;

static volatile uint16_t g_rx_head = 0U;
static volatile uint16_t g_rx_tail = 0U;
static uint8_t g_rx_ring[UART_RX_RING_SIZE];

static uint16_t ring_count(void)
{
    uint16_t head = g_rx_head;
    uint16_t tail = g_rx_tail;
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }
    return (uint16_t)(UART_RX_RING_SIZE - tail + head);
}

static uint8_t ring_pop(uint8_t *out)
{
    if (g_rx_head == g_rx_tail)
    {
        return 0U;
    }
    *out = g_rx_ring[g_rx_tail];
    g_rx_tail = (uint16_t)((g_rx_tail + 1U) % UART_RX_RING_SIZE);
    return 1U;
}

static uint8_t ring_peek(uint16_t idx, uint8_t *out)
{
    uint16_t count = ring_count();
    uint16_t pos;
    if (idx >= count)
    {
        return 0U;
    }
    pos = (uint16_t)((g_rx_tail + idx) % UART_RX_RING_SIZE);
    *out = g_rx_ring[pos];
    return 1U;
}

static uint8_t ring_discard(uint16_t n)
{
    uint16_t count = ring_count();
    if (n > count)
    {
        return 0U;
    }
    g_rx_tail = (uint16_t)((g_rx_tail + n) % UART_RX_RING_SIZE);
    return 1U;
}

static uint8_t parser_try_pop(uart_packet_t *pkt)
{
    uint8_t b = 0U;

    while (ring_count() > 0U)
    {
        (void)ring_peek(0U, &b);
        if (b == STREAM_START_BYTE)
        {
            break;
        }
        (void)ring_discard(1U);
    }

    if (ring_count() < 8U)
    {
        return 0U;
    }

    uint8_t h[7];
    for (uint16_t i = 0U; i < 7U; i++)
    {
        (void)ring_peek(i, &h[i]);
    }

    if (h[0] != STREAM_START_BYTE || h[1] != STREAM_VERSION)
    {
        (void)ring_discard(1U);
        return 0U;
    }

    pkt->type = h[2];
    pkt->seq = (uint16_t)((uint16_t)h[3] | ((uint16_t)h[4] << 8U));
    pkt->len = (uint16_t)((uint16_t)h[5] | ((uint16_t)h[6] << 8U));

    if (pkt->len > UART_STREAM_MAX_PAYLOAD)
    {
        (void)ring_discard(1U);
        return 0U;
    }

    if (ring_count() < (uint16_t)(8U + pkt->len))
    {
        return 0U;
    }

    for (uint16_t i = 0U; i < pkt->len; i++)
    {
        (void)ring_peek((uint16_t)(7U + i), &pkt->payload[i]);
    }

    (void)ring_peek((uint16_t)(7U + pkt->len), &b);
    if (b != STREAM_END_BYTE)
    {
        (void)ring_discard(1U);
        return 0U;
    }

    (void)ring_discard((uint16_t)(8U + pkt->len));
    return 1U;
}

void uart_stream_init(UART_HandleTypeDef *huart)
{
    g_huart = huart;
    g_rx_head = 0U;
    g_rx_tail = 0U;
    (void)HAL_UART_Receive_IT(g_huart, &g_rx_byte, 1U);
}

void uart_stream_on_rx_complete(UART_HandleTypeDef *huart)
{
    uint16_t next_head;
    if (huart != g_huart)
    {
        return;
    }

    next_head = (uint16_t)((g_rx_head + 1U) % UART_RX_RING_SIZE);
    if (next_head != g_rx_tail)
    {
        g_rx_ring[g_rx_head] = g_rx_byte;
        g_rx_head = next_head;
    }
    (void)HAL_UART_Receive_IT(g_huart, &g_rx_byte, 1U);
}

void uart_stream_send_packet(uint8_t type, uint16_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t header[7];
    uint8_t end = STREAM_END_BYTE;

    if ((g_huart == NULL) || (len > UART_STREAM_MAX_PAYLOAD))
    {
        return;
    }

    header[0] = STREAM_START_BYTE;
    header[1] = STREAM_VERSION;
    header[2] = type;
    header[3] = (uint8_t)(seq & 0xFFU);
    header[4] = (uint8_t)((seq >> 8U) & 0xFFU);
    header[5] = (uint8_t)(len & 0xFFU);
    header[6] = (uint8_t)((len >> 8U) & 0xFFU);

    (void)HAL_UART_Transmit(g_huart, header, sizeof(header), HAL_MAX_DELAY);
    if ((payload != NULL) && (len > 0U))
    {
        (void)HAL_UART_Transmit(g_huart, (uint8_t *)payload, len, HAL_MAX_DELAY);
    }
    (void)HAL_UART_Transmit(g_huart, &end, 1U, HAL_MAX_DELAY);
}

void uart_stream_send_mel(uint16_t seq, const uint16_t *mels, uint8_t n_mels)
{
    uint8_t payload[1U + AUDIO_N_MELS * 2U];
    uint16_t len = (uint16_t)(1U + (uint16_t)n_mels * 2U);

    if (n_mels > AUDIO_N_MELS)
    {
        return;
    }

    payload[0] = n_mels;
    for (uint16_t i = 0U; i < (uint16_t)n_mels; i++)
    {
        payload[1U + i * 2U] = (uint8_t)(mels[i] & 0xFFU);
        payload[2U + i * 2U] = (uint8_t)((mels[i] >> 8U) & 0xFFU);
    }
    uart_stream_send_packet(PKT_MEL_FRAME, seq, payload, len);
}

void uart_stream_send_pong(uint16_t seq)
{
    uart_stream_send_packet(PKT_PONG, seq, NULL, 0U);
}

__weak void uart_stream_on_audio_frame(const int16_t *samples, uint16_t sample_count, uint16_t seq)
{
    uint16_t mels[AUDIO_N_MELS];
    (void)sample_count;

    audio_pipeline_process_frame(samples, mels);
    uart_stream_send_mel(seq, mels, AUDIO_N_MELS);
}

void uart_stream_process(void)
{
    uart_packet_t pkt;
    while (parser_try_pop(&pkt) == 1U)
    {
        if ((pkt.type == PKT_AUDIO_FRAME) && (pkt.len == (AUDIO_FRAME_SAMPLES * 2U)))
        {
            uart_stream_on_audio_frame((const int16_t *)pkt.payload, AUDIO_FRAME_SAMPLES, pkt.seq);
        }
        else if (pkt.type == PKT_PING)
        {
            uart_stream_send_pong(pkt.seq);
        }
        else
        {
            uint8_t err = 1U;
            uart_stream_send_packet(PKT_ERROR, pkt.seq, &err, 1U);
        }
    }
}
