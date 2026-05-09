# HikSADP Linux — Implementation Checklist (Toward PRD v1.0)

Status baseline: discovery CLI sudah berjalan stabil dan mendeteksi device setara pembanding.
Target dokumen ini: menutup gap antara implementasi saat ini dengan PRD v1.0.

## 1. Milestone & Sequence

1. M1 — GUI Shell + Wiring Dasar (wajib dulu)
2. M2 — Operasi Inti dari GUI (activation, network config, reboot, export)
3. M3 — Password Reset Flow (request/export/import)
4. M4 — Reliability, Logging, Testing Coverage
5. M5 — Packaging & Release (AppImage/.deb/.rpm)

## 2. Current State Summary

Done:
- Core type-safe domain (`StrongType`, `Result`, `DeviceState`)
- SADP discovery (multicast + broadcast, per-interface)
- ISAPI client dasar (activate/config/reboot/info)
- DeviceManager batch operation dasar
- CLI scanner usable
- Unit tests: types + packet

Not done / partial:
- Main GUI app belum runnable end-to-end
- Password reset flow belum ada
- Export column selection belum ada
- Logging terstruktur ke file belum ada
- Test matrix sesuai PRD belum lengkap
- Packaging artifact belum ada

## 3. Work Breakdown (Executable)

## M1 — GUI Shell + Wiring Dasar

Goal: aplikasi GUI bisa dibuka, scan, tampil tabel, dan pilih device.

Tasks:
- [ ] Implement `src/ui/main_window.cpp`
- [ ] Tambah `hiksadp_gui` executable di `src/CMakeLists.txt`
- [ ] Wire `SadpDiscovery` -> `DeviceManager` -> `DeviceTableWidget`
- [ ] Toolbar actions: Scan, Activate, Network Config, Reboot, Export CSV/XML
- [ ] Status bar: total devices, last scan status, selected count
- [ ] Auto refresh toggle (default off/on sesuai keputusan produk)

Acceptance:
- [ ] GUI launch sukses
- [ ] Scan menampilkan device setara CLI
- [ ] Multi-select row berfungsi

## M2 — Operasi Inti dari GUI

Goal: fitur SADP utama bisa dipakai langsung dari GUI.

Tasks:
- [ ] Dialog Activate (single + batch)
- [ ] Validasi password strength realtime
- [ ] Dialog Network Config (single)
- [ ] Dialog Batch Sequential IP + preview assignment
- [ ] DHCP toggle per device
- [ ] Reboot single + batch dengan progress
- [ ] Export CSV/XML via file save dialog
- [ ] Filter status + search (IP/serial/model)

Acceptance:
- [ ] Aktivasi 1 dan batch sukses pada device nyata
- [ ] Network config dan batch IP bekerja
- [ ] Reboot dan export bekerja

## M3 — Password Reset Flow

Goal: menutup fitur PRD reset password berbasis XML support flow.

Tasks:
- [ ] Desain data model reset request/response
- [ ] Generate request XML (serial + timestamp + metadata)
- [ ] Import reset XML dari support
- [ ] Apply reset token/code ke device (jika endpoint tersedia)
- [ ] Error handling & user guidance message

Acceptance:
- [ ] Bisa export request XML
- [ ] Bisa import file reset XML
- [ ] Jalur proses end-to-end tervalidasi sesuai model device yang didukung

## M4 — Reliability, Logging, Testing

Goal: kualitas produksi dan maintainability.

Tasks:
- [ ] Integrasi `spdlog` + rolling file logs
- [ ] Replace `std::cout` debug path dengan logger
- [ ] Unit test `DeviceManager` (state transition + batch errors)
- [ ] Unit/integration test `IsapiClient` dengan mock HTTP server
- [ ] Test parser `ProbeMatch` dengan fixture multi-vendor firmware variations
- [ ] Tambah tag test `.integration` dan exclusion default CI
- [ ] CI workflow: build + test matrix Linux

Acceptance:
- [ ] Test coverage modul high-priority sesuai PRD
- [ ] Error diagnosis dari log file memadai

## M5 — Packaging & Release

Goal: distribusi siap pakai user Linux.

Tasks:
- [ ] AppImage build pipeline
- [ ] .deb packaging
- [ ] .rpm packaging
- [ ] Release notes template + artifact checksums
- [ ] Runtime dependency audit

Acceptance:
- [ ] Artifact install/run di Ubuntu/Fedora minimal

## 4. PRD Traceability Matrix (Ringkas)

- Device Discovery: Done (CLI), Partial (GUI)
- Device Activation: Partial (service ready, GUI flow belum selesai)
- Network Configuration: Partial (service ready, GUI flow belum selesai)
- Password Reset: Not started
- Export & Reporting: Partial (export engine ada, column selection belum)
- Device Management/Reboot: Partial (service ready, GUI flow belum selesai)
- User Experience: Partial/Not started (status/progress/theme/logging belum komplet)

## 5. Suggested Execution Plan (2-Week Blocks)

Sprint A:
- M1 complete + smoke test device nyata

Sprint B:
- M2 complete (activate/config/reboot/export + search/filter)

Sprint C:
- M3 complete (reset flow) + M4 logging + tests high-priority

Sprint D:
- M5 packaging + CI hardening + v1.0 RC

## 6. Definition of Done (v1.0)

- [ ] Semua fitur in-scope PRD section 3 tercapai atau ditandai explicit exception
- [ ] Build reproducible
- [ ] Test high-priority pass
- [ ] GUI usable untuk workflow teknisi harian tanpa CLI fallback
- [ ] Packaging artifacts tersedia
