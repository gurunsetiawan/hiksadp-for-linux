#pragma once

#ifndef Q_MOC_RUN
#include "core/device.hpp"
#else
namespace hiksadp {
struct Device;
struct MacAddress;
} // namespace hiksadp
#endif

#include <QTableWidget>
#include <vector>

namespace hiksadp::ui {

// ── DeviceTableWidget ──────────────────────────────────────────────────────
//
// Tabel utama yang menampilkan daftar device hasil scan.
// Kolom sesuai SADP asli: No, IP, Status, HTTP Port, Device Type,
// Serial Number, MAC, Firmware, SDK Port, Gateway, Subnet Mask.
//
class DeviceTableWidget : public QTableWidget {
    Q_OBJECT

public:
    enum class Column : int {
        No           = 0,
        IpAddress    = 1,
        Status       = 2,
        HttpPort     = 3,
        DeviceType   = 4,
        SerialNumber = 5,
        MacAddress   = 6,
        Firmware     = 7,
        SdkPort      = 8,
        Gateway      = 9,
        SubnetMask   = 10,
        COUNT        = 11
    };

    explicit DeviceTableWidget(QWidget* parent = nullptr);

    // Update seluruh tabel dengan list device baru
    void set_devices(const std::vector<Device>& devices);

    // Tambah atau update satu device (untuk live update saat scan)
    void upsert_device(const Device& device);

    // Ambil MAC address dari row yang dipilih
    [[nodiscard]] std::vector<MacAddress> selected_macs() const;

    // Ambil semua device yang ditampilkan
    [[nodiscard]] std::vector<Device> all_devices() const;

    // Filter tampilan
    void set_filter_text(const QString& text);
    void set_filter_status(const QString& status); // "All", "Active", "Inactive"

Q_SIGNALS:
    void selection_changed(int selected_count);

private Q_SLOTS:
    void on_item_selection_changed();

private:
    void setup_columns();
    void apply_row_style(int row, const Device& device);
    void store_device_in_row(int row, const Device& device);
    [[nodiscard]] int find_row_by_mac(const MacAddress& mac) const;

    std::vector<Device> all_devices_; // data lengkap untuk filtering
    QString filter_text_;
    QString filter_status_{"All"};
};

} // namespace hiksadp::ui
