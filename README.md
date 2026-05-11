# HikSADP for Linux

[![Platform](https://img.shields.io/badge/platform-Linux-blue)](https://www.kernel.org/)
[![Language](https://img.shields.io/badge/language-C%2B%2B23-00599C)](https://isocpp.org/)
[![UI](https://img.shields.io/badge/UI-Qt6-41CD52)](https://www.qt.io/)
[![Build](https://img.shields.io/badge/build-CMake-064F8C)](https://cmake.org/)
[![Status](https://img.shields.io/badge/status-active-success)](#)

HikSADP for Linux adalah aplikasi desktop Linux untuk discovery dan manajemen dasar perangkat Hikvision (kamera, NVR, DVR) dengan scope setara SADP tool.

## Fitur Utama
- SADP discovery (multicast + broadcast) dengan auto refresh.
- Device list, search/filter, dan panel detail device.
- Activate device (batch).
- Network config:
  - single device
  - batch sequential IP assignment.
- Reboot device (batch).
- Change admin password (jika password lama diketahui).
- Password reset via security-code XML:
  - export request XML
  - import response XML
  - apply security code ke device.
- Export CSV/XML.
- Open web login device.

## Arsitektur Singkat
- `hiksadp/src/core`: strong types, error/result model, domain device.
- `hiksadp/src/protocol`: SADP packet + discovery.
- `hiksadp/src/management`: ISAPI client + device orchestration.
- `hiksadp/src/ui`: Qt Widgets GUI.

Lihat diagram alur: [docs/hiksadp_flow.svg](docs/hiksadp_flow.svg)

Lihat PRD: [docs/PRD.md](docs/PRD.md)

## Build dan Run
Prasyarat utama:
- Linux
- CMake >= 3.22
- Compiler C++23 (GCC/Clang)
- Qt6 (Core, Network, Widgets)

```bash
cd hiksadp
cmake -S . -B build
cmake --build build -j
```

Jalankan GUI:
```bash
./build/src/hiksadp_gui
```

Jalankan CLI:
```bash
./build/src/hiksadp_cli --help
```

## Catatan Operasional
- Discovery bisa intermiten di jaringan ramai; aplikasi sudah memakai mekanisme stale/purge agar daftar device lebih stabil saat auto refresh.
- Akses web device lintas subnet membutuhkan routing/VPN/NAT yang benar. Mengganti browser eksternal ke embedded webview tidak menghilangkan kebutuhan route jaringan.

## Roadmap Berikutnya
- Konfigurasi `stale/purge TTL` dari UI.
- Security Questions flow end-to-end (per firmware/model matrix).
- Embedded webview opsional (UI convenience, bukan bypass routing).

## Lisensi
Internal project / sesuai kebijakan pemilik repository.
