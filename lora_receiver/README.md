# LoRa receiver

Standalone ESP-IDF project for receiving LoRa packets with the LR1121 on the
EoRa-HUB-900TB board and displaying them in the serial console.

## Parameters

- Target: ESP32-S3
- Frequency: 915 MHz
- LoRa bandwidth: 125 kHz
- Spreading factor : 7
- Coding rate: 4/
- Reception: continuous

The radio pins are defined at the top of `main/lora_receiver_main.c`.

## Build and flash

From an ESP-IDF terminal:

```sh
cd lora_receiver
idf.py -B build build
idf.py -B build -p PORT flash monitor
```

Exit the monitor with `Ctrl-]`.

Each received packet is displayed as:

```text
[LoRa] packet received: length=5 RSSI=-72 dBm SNR=8 dB | hello
```

The transmitter must use the same frequency, bandwidth, spreading factor,
coding rate, and implicit/explicit header length.
