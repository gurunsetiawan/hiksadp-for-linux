# HikSADP for Linux

[![Platform](https://img.shields.io/badge/platform-Linux-blue)](https://www.kernel.org/)
[![Language](https://img.shields.io/badge/language-C%2B%2B23-00599C)](https://isocpp.org/)
[![UI](https://img.shields.io/badge/UI-Qt6-41CD52)](https://www.qt.io/)
[![Build](https://img.shields.io/badge/build-CMake-064F8C)](https://cmake.org/)
[![Status](https://img.shields.io/badge/status-active-success)](#)
[![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![CI](https://github.com/gurunsetiawan/hiksadp-for-linux/actions/workflows/ci.yml/badge.svg)](https://github.com/gurunsetiawan/hiksadp-for-linux/actions/workflows/ci.yml)

HikSADP for Linux adalah aplikasi desktop Linux untuk discovery dan manajemen dasar perangkat Hikvision (kamera, NVR, DVR) dengan scope setara SADP tool.

## Fitur Utama
- SADP discovery (multicast + broadcast) dengan auto refresh.
- Device list, search/filter, dan panel detail device.
- Preserve selection saat auto refresh (berdasarkan MAC).
- Scan Settings untuk atur retention TTL:
  - stale after
  - purge after.
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
- Password reset via Security Questions (in-app flow):
  - input 3 jawaban
  - set password baru
  - submit ke endpoint ISAPI fallback (tergantung dukungan firmware).
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
- Catch2 v3 (opsional, untuk test target)
  - Jika tidak tersedia, project tetap build `hiksadp_smoke_tests` (tanpa dependency eksternal).

```bash
cd hiksadp
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake
```

Jalankan GUI:
```bash
./build/src/hiksadp_gui
```

Jalankan CLI:
```bash
./build/src/hiksadp_cli --help
```

## Release
Release normal berjalan otomatis setelah PR merge ke `main`.

Default bump adalah `patch`. Tambahkan label PR jika perlu:
- `release:major`: naikkan versi mayor, contoh `v1.4.2` -> `v2.0.0`.
- `release:minor`: naikkan versi minor, contoh `v1.4.2` -> `v1.5.0`.
- `release:patch`: naikkan versi patch, contoh `v1.4.2` -> `v1.4.3`.

Saat tag `v*` dibuat otomatis, workflow release akan build, test, package, lalu membuat GitHub Release dengan artefak `.deb` dan `.tar.gz`.

Workflow release juga bisa dijalankan manual dari GitHub Actions dengan input tag dan catatan tambahan. Push tag manual `v*` tetap didukung.

## Catatan Operasional
- Discovery bisa intermiten di jaringan ramai; aplikasi sudah memakai mekanisme stale/purge agar daftar device lebih stabil saat auto refresh.
- Mekanisme update daftar device memakai hasil scan terbaru per siklus (`scan_complete`) agar stale/purge bekerja konsisten.
- Akses web device lintas subnet membutuhkan routing/VPN/NAT yang benar. Mengganti browser eksternal ke embedded webview tidak menghilangkan kebutuhan route jaringan.
- Logging operasional disimpan ke:
  - GUI/CLI: `QStandardPaths::AppDataLocation/hiksadp.log`.

## Roadmap Berikutnya
- Hardening matrix Security Questions per firmware/model (karena endpoint bisa beda antar device).
- Embedded webview opsional (UI convenience, bukan bypass routing).

## Lisensi
GPLv3. Lihat [LICENSE](LICENSE).
