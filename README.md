# ESP32-S3 NAT Router

A single-purpose ESP32-S3 firmware focused on Wi-Fi APSTA routing and IPv4 NAPT throughput.

The firmware keeps the application layer deliberately small so CPU time, internal RAM, PSRAM, Wi-Fi driver buffers, and lwIP resources remain focused on forwarding traffic.

## Build

GitHub Actions builds the ESP32-S3 target and publishes:

- bootloader.bin
- partition-table.bin
- nat_router.bin
- merged.bin
- flash_args

## Hardware target

ESP32-S3 with 16 MB flash and 8 MB Octal PSRAM.

## Network model

The station interface is the upstream connection. The SoftAP interface is the downstream network. IPv4 NAPT is enabled only after the station receives an IPv4 address.

## Performance design

The configuration follows Espressif's documented high-performance ESP32-S3 Wi-Fi profile where applicable: 24 static RX buffers, 85 dynamic RX buffers, 32 static TX buffers, 32 cached TX buffers, 32 RX BA window, 64 KiB-class or larger TCP buffers, Wi-Fi IRAM optimization, RX IRAM optimization, LWIP IRAM optimization, dual-core operation, 240 MHz CPU, and 80 MHz Octal PSRAM.

The TX BA window is intentionally kept at a conservative high-performance value rather than forcing the maximum value. Espressif's current Wi-Fi Kconfig notes that larger TX BA windows consume memory and recommends lower values for maximum-throughput testing unless a specific workload demonstrates benefit.

## Limits

ESP32-S3 has one 2.4 GHz Wi-Fi radio. APSTA routing therefore shares radio airtime between the upstream and downstream wireless links. No software setting can remove that physical constraint.

Actual Internet throughput depends on upstream AP, channel utilization, signal quality, negotiated PHY rate, retransmissions, client capabilities, TCP behavior, and the forwarding workload.
