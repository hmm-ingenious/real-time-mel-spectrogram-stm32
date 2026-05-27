import argparse
import struct
import threading
import time
import tkinter as tk
from tkinter import filedialog

import numpy as np
import resampy
import serial
import soundfile as sf


START_BYTE = 0xAA
END_BYTE = 0x55
VERSION = 0x01

PKT_AUDIO = 0x01
PKT_MEL = 0x81
PKT_PING = 0xF0
PKT_PONG = 0xF1

SAMPLE_RATE = 16000
FRAME_SAMPLES = 512
DEFAULT_MELS = 40


def build_packet(pkt_type, seq, payload):
    header = struct.pack("<BBBHH", START_BYTE, VERSION, pkt_type, seq & 0xFFFF, len(payload))
    return header + payload + bytes([END_BYTE])


class PacketParser:
    def __init__(self):
        self.buf = bytearray()

    def push(self, data):
        self.buf.extend(data)
        packets = []
        while True:
            if len(self.buf) < 8:
                break
            start_idx = self.buf.find(bytes([START_BYTE]))
            if start_idx < 0:
                self.buf.clear()
                break
            if start_idx > 0:
                del self.buf[:start_idx]
            if len(self.buf) < 8:
                break

            _, ver, pkt_type, seq, length = struct.unpack("<BBBHH", self.buf[:7])
            full_len = 7 + length + 1
            if len(self.buf) < full_len:
                break
            if self.buf[full_len - 1] != END_BYTE or ver != VERSION:
                del self.buf[0]
                continue

            payload = bytes(self.buf[7 : 7 + length])
            packets.append((pkt_type, seq, payload))
            del self.buf[:full_len]
        return packets


class App:
    def __init__(self, port, baud, loop_audio):
        self.root = tk.Tk()
        self.root.title("STM32 Real-Time Mel Spectrogram")

        self.canvas_w = 900
        self.canvas_h = 400
        self.canvas = tk.Canvas(self.root, width=self.canvas_w, height=self.canvas_h, bg="black")
        self.canvas.pack()

        self.open_btn = tk.Button(self.root, text="Open Audio File", command=self.load_file)
        self.open_btn.pack(pady=8)
        self.status_var = tk.StringVar(value="Idle: load an audio file")
        self.status_lbl = tk.Label(self.root, textvariable=self.status_var)
        self.status_lbl.pack(pady=4)

        self.serial_port = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=0.01,
            write_timeout=0.5,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        self.parser = PacketParser()
        self.seq = 0
        self.running = True
        self.audio = None
        self.audio_pos = 0
        self.loop_audio = loop_audio

        self.n_mels = DEFAULT_MELS
        self.spec = np.zeros((self.n_mels, self.canvas_w), dtype=np.float32)
        self.spec_lock = threading.Lock()

        self.tx_frames = 0
        self.rx_bytes = 0
        self.rx_mel_frames = 0
        self.last_mel_time = 0.0
        self.last_debug_print = 0.0

        self.rx_thread = threading.Thread(target=self.rx_worker, daemon=True)
        self.tx_thread = threading.Thread(target=self.tx_worker, daemon=True)
        self.rx_thread.start()
        self.tx_thread.start()

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.after(30, self.draw_loop)

    def load_file(self):
        path = filedialog.askopenfilename(filetypes=[("Audio Files", "*.wav *.flac *.ogg *.mp3"), ("All", "*.*")])
        if not path:
            return
        x, sr = sf.read(path, dtype="float32")
        if x.ndim > 1:
            x = np.mean(x, axis=1)
        if sr != SAMPLE_RATE:
            x = resampy.resample(x, sr, SAMPLE_RATE)
        peak = float(np.max(np.abs(x))) if x.size else 0.0
        if peak > 1e-6:
            # Keep levels healthy if source is very quiet.
            target_peak = 0.9
            gain = min(target_peak / peak, 20.0)
            x = x * gain
        x = np.clip(x, -1.0, 1.0)
        self.audio = (x * 32767.0).astype(np.int16)
        self.audio_pos = 0
        self.tx_frames = 0
        self.rx_mel_frames = 0
        self.last_mel_time = 0.0
        self.status_var.set(f"Loaded {len(self.audio)} samples @ {SAMPLE_RATE} Hz")

    def rx_worker(self):
        while self.running:
            data = self.serial_port.read(1024)
            if not data:
                continue
            self.rx_bytes += len(data)
            for pkt_type, _, payload in self.parser.push(data):
                if pkt_type == PKT_MEL:
                    self.handle_mel(payload)

    def handle_mel(self, payload):
        if not payload:
            return
        n = payload[0]
        if len(payload) != 1 + n * 2:
            return
        vals = np.frombuffer(payload[1:], dtype="<u2").astype(np.float32)
        if n != self.n_mels:
            self.n_mels = n
            with self.spec_lock:
                self.spec = np.zeros((self.n_mels, self.canvas_w), dtype=np.float32)
        vals = np.log1p(vals * 0.001)
        with self.spec_lock:
            self.spec = np.roll(self.spec, -1, axis=1)
            self.spec[:, -1] = vals
        self.rx_mel_frames += 1
        self.last_mel_time = time.time()

    def tx_worker(self):
        while self.running:
            if self.audio is None:
                time.sleep(0.01)
                continue
            if self.audio_pos >= len(self.audio):
                if self.loop_audio:
                    self.audio_pos = 0
                else:
                    time.sleep(0.05)
                    continue

            end = self.audio_pos + FRAME_SAMPLES
            frame = self.audio[self.audio_pos : end]
            self.audio_pos = end
            if len(frame) < FRAME_SAMPLES:
                padded = np.zeros(FRAME_SAMPLES, dtype=np.int16)
                padded[: len(frame)] = frame
                frame = padded
            payload = frame.tobytes()
            pkt = build_packet(PKT_AUDIO, self.seq, payload)
            self.seq = (self.seq + 1) & 0xFFFF
            self.serial_port.write(pkt)
            self.tx_frames += 1
            time.sleep(FRAME_SAMPLES / SAMPLE_RATE)

    def draw_loop(self):
        with self.spec_lock:
            img = np.copy(self.spec)
        vmin = float(np.percentile(img, 5))
        vmax = float(np.percentile(img, 99))
        if vmax <= vmin + 1e-6:
            vmax = vmin + 1e-6
        img = np.clip((img - vmin) / (vmax - vmin), 0.0, 1.0)

        self.canvas.delete("all")
        cols = self.canvas_w
        rows = self.n_mels
        px_h = self.canvas_h / max(rows, 1)

        for x in range(cols):
            col = img[:, x]
            for y in range(rows):
                v = col[rows - 1 - y]
                r = int(255 * v)
                g = int(140 * (v ** 0.7))
                b = int(255 * (1.0 - v) * 0.2)
                color = f"#{r:02x}{g:02x}{b:02x}"
                y0 = int(y * px_h)
                y1 = int((y + 1) * px_h) + 1
                self.canvas.create_line(x, y0, x, y1, fill=color)

        now = time.time()
        mel_age = now - self.last_mel_time if self.last_mel_time > 0 else -1
        if mel_age < 0:
            mel_status = "no mel frames yet"
        else:
            mel_status = f"last mel {mel_age:.2f}s ago"
        self.status_var.set(
            f"tx_frames={self.tx_frames} rx_mel={self.rx_mel_frames} rx_bytes={self.rx_bytes} | {mel_status}"
        )
        if (self.tx_frames > 20) and (self.rx_mel_frames == 0) and (now - self.last_debug_print > 1.0):
            print("WARN: TX active but no MEL packets received. Check UART baud/port/MCU packet parser.")
            self.last_debug_print = now

        self.root.after(30, self.draw_loop)

    def on_close(self):
        self.running = False
        time.sleep(0.05)
        self.serial_port.close()
        self.root.destroy()

    def run(self):
        self.root.mainloop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="UART serial port, e.g. /dev/ttyACM0 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=921600, help="Must match USART2 baud in CubeMX")
    parser.add_argument("--loop", action="store_true", help="Loop audio continuously")
    args = parser.parse_args()

    app = App(args.port, args.baud, args.loop)
    app.run()


if __name__ == "__main__":
    main()

