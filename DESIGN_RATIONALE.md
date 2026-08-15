# Design Rationale

## Objective

The design is optimized for one purpose: move IPv4 traffic through the ESP32-S3 APSTA path with as little application overhead as practical.

## CPU

Dual-core operation is retained. The Wi-Fi task is pinned to one core and lwIP TCP/IP processing is pinned to the other. The goal is to reduce contention between radio-driver work and IP-stack work rather than simply maximizing task count.

## Memory

The N16R8 memory configuration uses Octal PSRAM at 80 MHz and directs Wi-Fi/lwIP allocations toward external RAM where the framework permits. Internal RAM remains important for DMA and hot paths, so the configuration does not attempt to place every possible object in PSRAM.

## Wi-Fi buffers

The RX/TX buffer counts are based on Espressif's ESP32-S3 high-performance PSRAM profile. More buffers can improve burst handling, but excessive buffers reduce free memory and can make a router less stable. The selected values therefore prioritize the documented high-performance profile instead of blindly maximizing every integer.

## Block acknowledgement

RX BA is set to the documented high-performance value of 32. TX BA is set to 12. Espressif's current Kconfig describes larger TX BA windows as a memory/performance tradeoff and cites 9-12 for maximum-throughput iperf testing. A maximum-size TX window is therefore not automatically superior for a NAT router.

## IRAM

Wi-Fi IRAM optimization, RX IRAM optimization, Wi-Fi extra IRAM optimization, and lwIP IRAM optimization are enabled. The application has no UI, storage service, statistics engine, OTA service, or other workload competing for that execution memory.

## TCP

TCP MSS is 1460. TCP send and receive buffers are sized above 64 KiB and receive-window scaling is enabled so the larger window can actually be advertised. SACK remains enabled.

## Power management

Wi-Fi power save is disabled. This sacrifices energy efficiency in favor of consistent latency and throughput.

## Radio configuration

The firmware enables 802.11g/n and requests HT40. The PHY transmit power is configured to the supported maximum value. The actual negotiated bandwidth and PHY rate remain subject to the upstream AP, RF conditions, coexistence rules, and regulatory constraints.

## NAT

NAPT is disabled until the upstream station has a valid IPv4 address and is disabled again when the upstream address is lost. This prevents stale NAT state from being treated as usable connectivity.

## Recovery

When the upstream link drops, the firmware first tries known 2.4 GHz primary channels and locks to the discovered BSSID. If that does not succeed, it falls back to normal scanning. Recovery scanning is not performed during normal forwarding, so its cost is outside the steady-state data path.

## Application path

The forwarding path contains no application-level packet processing loop. Traffic is handled by the ESP Wi-Fi driver, netif layer, lwIP, and NAPT implementation. This keeps the application from becoming an additional packet-processing bottleneck.

## CI output

The build publishes the bootloader, partition table, application binary, merged flash image, and flash arguments. The merge operation is performed from inside the build directory so the output path resolves correctly.

## What cannot be guaranteed

Software cannot guarantee a percentage of silicon capacity or a fixed Internet Mbps value. Espressif's own ESP32-S3 measurements show that throughput varies substantially between open-air and shielded conditions, and APSTA NAT adds a second wireless leg that shares the same radio. The firmware therefore targets the documented high-performance operating point rather than claiming a theoretical percentage such as 99.99%.
