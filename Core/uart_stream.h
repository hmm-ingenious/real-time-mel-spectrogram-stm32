#ifndef UART_STREAM_H
#define UART_STREAM_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_START_BYTE 0xAAU
#define STREAM_END_BYTE 0x55U
#define STREAM_VERSION 0x01U

#define PKT_AUDIO_FRAME 0x01U
#define PKT_MEL_FRAME 0x81U
#define PKT_PING 0xF0U
#define PKT_PONG 0xF1U
#define PKT_ERROR 0xFEU

#define UART_STREAM_MAX_PAYLOAD 1024U

typedef struct
{
    uint8_t type;
    uint16_t seq;
    uint16_t len;
    uint8_t payload[UART_STREAM_MAX_PAYLOAD];
} uart_packet_t;

void uart_stream_init(UART_HandleTypeDef *huart);
void uart_stream_process(void);
void uart_stream_on_rx_complete(UART_HandleTypeDef *huart);

void uart_stream_send_packet(uint8_t type, uint16_t seq, const uint8_t *payload, uint16_t len);
void uart_stream_send_mel(uint16_t seq, const uint16_t *mels, uint8_t n_mels);
void uart_stream_send_pong(uint16_t seq);

/* Weak hook; implement in user code if needed. */
void uart_stream_on_audio_frame(const int16_t *samples, uint16_t sample_count, uint16_t seq);

#ifdef __cplusplus
}
#endif

#endif
