# Product Requirements Document (PRD)
## HikSADP for Linux

Version: 1.1  
Date: 2026-05-11  
Owner: Project Maintainer

## 1. Problem Statement
Teknisi Linux membutuhkan tool native untuk discovery dan konfigurasi perangkat Hikvision setara SADP. Tool resmi SADP berbasis Windows dan workaround (Wine) tidak konsisten.

## 2. Goals
- Menyediakan pengalaman setara SADP untuk Linux.
- Mengurangi ketergantungan pada tool Windows.
- Menjaga keamanan dan keandalan (type-safe C++ + error handling eksplisit).

## 3. Non-Goals
- VMS lengkap (live view, playback recording kompleks).
- Cloud management enterprise.
- Dukungan seluruh endpoint recovery lintas semua firmware dalam v1.

## 4. Target Users
- Teknisi jaringan / CCTV installer.
- Tim IT internal yang mengelola perangkat Hikvision di LAN.

## 5. Functional Requirements
### 5.1 Discovery
- Scan SADP pada interface aktif (multicast + broadcast).
- Menampilkan device info: IP, MAC, serial, model, firmware, status.
- Auto refresh periodik.
- Device retention policy:
  - stale setelah 30 detik tidak terlihat
  - purge setelah 90 detik tidak terlihat.

### 5.2 Device Operations
- Activate single/batch.
- Network config single.
- Network config batch sequential IP.
- Reboot single/batch.
- Change admin password (known old password).

### 5.3 Recovery
- Security-code XML request export.
- Security-code XML response import.
- Apply security code ke device.
- Recovery method selector.
- Capability display:
  - PasswordResetModeSecond
  - Support.

### 5.4 UI/UX
- Tabel device + search/filter.
- Panel detail kanan realtime.
- Toggle show/hide detail panel.
- Preserve selection saat auto refresh (berdasarkan MAC).
- Export CSV/XML.
- Open web login device.

## 6. Non-Functional Requirements
- Build di Linux dengan CMake + Qt6 + C++23.
- Type safety:
  - strong types
  - expected-style result/error.
- Responsif untuk jumlah device kecil-menengah (hingga ratusan entri).

## 7. Technical Design
- Core layer: domain + validation + error codes.
- Protocol layer: SADP packet/discovery parser.
- Management layer: ISAPI client + orchestration.
- UI layer: Qt Widgets.

## 8. Risks and Mitigations
- Risk: response discovery intermiten.  
  Mitigasi: stale/purge retention + refresh.
- Risk: endpoint ISAPI beda firmware.  
  Mitigasi: fallback endpoint + pesan capability jelas.
- Risk: lintas subnet tidak reachable.  
  Mitigasi: dokumentasi kebutuhan routing/VPN/NAT.

## 9. Success Metrics
- Device terdeteksi konsisten di environment uji.
- Operasi inti (activate/network/reboot/password change) berjalan tanpa tool Windows.
- Recovery XML flow dapat dipakai teknisi lapangan.

## 10. Open Items
- Menjadikan stale/purge TTL configurable dari UI.
- Security Questions full flow per firmware matrix.
