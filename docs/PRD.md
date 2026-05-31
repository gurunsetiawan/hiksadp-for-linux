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
- Scan SADP pada semua interface aktif (multicast + broadcast) sebagai default.
- Menampilkan device info: IP, MAC, serial, model, firmware, status.
- Auto refresh periodik.
- Device retention policy:
  - stale setelah 30 detik tidak terlihat
  - purge setelah 90 detik tidak terlihat.
- Retention policy dapat diatur dari UI (Scan Settings).
- Catatan keputusan: selector interface spesifik tidak menjadi alur utama v1, untuk menjaga kemudahan teknisi lapangan. Mode spesifik interface dipertimbangkan sebagai advanced option di versi berikutnya.

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
- Security Questions in-app flow:
  - input 3 jawaban
  - set password baru
  - submit ke endpoint fallback sesuai firmware.
- Recovery method selector.
- Capability display:
  - PasswordResetModeSecond
  - Support.

### 5.4 UI/UX
- Tabel device + search/filter.
- Panel detail kanan realtime.
- Toggle show/hide detail panel.
- Preserve selection saat auto refresh (berdasarkan MAC).
- Scan Settings untuk konfigurasi retention TTL:
  - stale after
  - purge after.
- Export CSV/XML.
- Open web login device.
- Logging operasional ke file aplikasi (`QStandardPaths::AppDataLocation/hiksadp.log`) untuk aksi penting dan error.

## 6. Non-Functional Requirements
- Build di Linux dengan CMake + Qt6 + C++23.
- Test harus tetap runnable di environment tanpa Catch2 v3 melalui smoke test executable (`hiksadp_smoke_tests`).
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
- Security Questions hardening per firmware matrix (beberapa model butuh endpoint/payload khusus).
- Advanced mode: optional single-interface scan selector (non-default).
- Theme policy eksplisit + opsi override dark/light.
- Packaging release AppImage/.rpm (TGZ/.deb sudah tersedia).
- Peningkatan test depth sesuai matriks test decisions PRD v1.0.
