# LoRa receiver

Projet ESP-IDF indépendant pour recevoir des paquets LoRa avec le LR1121 de la carte EoRa-HUB-900TB et les afficher dans la console série.

## Parametres

- Cible : ESP32-S3
- Frequence : 915 MHz
- Bande passante LoRa : 125 kHz
- Spreading factor : 7
- Coding rate : 4/
- Reception : continue

Les broches radio sont definies au debut de `main/lora_receiver_main.c`.

## Compiler et flasher

Depuis un terminal ESP-IDF :

```sh
cd /Users/nolanbailliet/Documents/esp32-projects/lora_receiver
idf.py -B build build
idf.py -B build -p /dev/tty.usbserial-1140 flash monitor
```

Quitter le moniteur avec `Ctrl-]`.

Chaque paquet recu apparait sous la forme :

```text
[LoRa] paquet recu: longueur=5 RSSI=-72 dBm SNR=8 dB | hello
```

L'emetteur doit utiliser les memes frequence, bande passante, spreading factor, coding rate et longueur d'en-tete implicite/explicite.
