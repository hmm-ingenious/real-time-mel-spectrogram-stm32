# Real-Time Mel Spectrogram on STM32F446RE

Real-time embedded DSP pipeline for audio feature extraction using STM32F446RE, CMSIS-DSP, UART communication, and Python visualization.

## Overview

This project implements a real-time mel spectrogram generation pipeline on the STM32F446RE microcontroller.

Audio frames are streamed from a host PC to the STM32 over UART, processed in real time using DSP algorithms, and mel spectrogram features are sent back to the host for visualization.

## Features

- Real-time audio frame processing
- UART-based host ↔ MCU communication
- Interrupt-driven ring buffer implementation
- Hann windowing
- FFT using CMSIS-DSP
- Magnitude spectrum calculation
- Mel filterbank implementation
- Log compression
- Real-time host visualization
- Low-latency embedded DSP pipeline

## System Architecture

Host PC:
- Audio framing
- Packetization
- UART transmission
- Visualization GUI

STM32F446RE:
- UART reception
- Ring buffer packet handling
- DSP feature extraction
- UART transmission back to host

Processing pipeline:

PCM Audio Frame
→ Hann Window
→ FFT
→ Magnitude Spectrum
→ Mel Filterbank
→ Log Compression
→ Mel Spectrogram Output

## Hardware

- STM32F446RE (ARM Cortex-M4)
- UART Serial Communication
- Host PC

## Software Stack

Embedded:
- C
- STM32CubeIDE
- STM32 HAL
- CMSIS-DSP

Host:
- Python
- NumPy
- PySerial
- Tkinter

## Performance

- Frame size: 512 samples
- Sample rate: 16 kHz
- Frame duration: 32 ms
- Mel bins: 40
- FFT complexity: O(N log N)

## Key Embedded Concepts Demonstrated

- Real-time signal processing
- Interrupt Service Routine (ISR) design
- Ring buffer implementation
- UART protocol handling
- Packet synchronization
- DSP optimization for embedded systems
- Spectral leakage reduction using windowing

## Challenges Solved

1. UART synchronization using packet framing
2. Preventing buffer overflow with ring buffer
3. Meeting real-time processing deadlines
4. Efficient FFT implementation using CMSIS-DSP
5. Reliable host-microcontroller communication

## Future Improvements

- DMA-based UART transfer
- Real-time microphone input
- MFCC extraction
- On-device ML inference
- Standalone embedded audio classification

## Repository Structure

```text
Core/              Embedded application code
Drivers/           STM32 HAL + CMSIS drivers
host/              Python host-side scripts
dsp_labhw_project.ioc
STM32F446RETX_FLASH.ld
STM32F446RETX_RAM.ld
```

## Author

Aryan Patgar
