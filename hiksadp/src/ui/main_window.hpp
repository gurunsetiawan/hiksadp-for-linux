#pragma once

#ifndef Q_MOC_RUN
#include "management/device_manager.hpp"
#include "protocol/sadp_discovery.hpp"
#else
namespace hiksadp {
struct Device;
struct MacAddress;
namespace protocol { struct DiscoveryResult; }
} // namespace hiksadp
#endif

#include <memory>
#include <vector>

// Qt forward declarations — menghindari include berat di header
class QCloseEvent;
class QLabel;
class QMainWindow;
class QPushButton;
class QStatusBar;
class QTableWidget;
class QTimer;
class QToolBar;

// Kita pakai QMainWindow sebagai base — definisi lengkap di .cpp via pimpl
#include <QMainWindow>

namespace hiksadp::ui {

class DeviceTableWidget;
class ActivationDialog;
class NetworkConfigDialog;

// ── MainWindow ─────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void on_scan_clicked();
    void on_activate_clicked();
    void on_network_config_clicked();
    void on_reboot_clicked();
    void on_change_password_clicked();
    void on_open_web_clicked();
    void on_scan_settings_clicked();
    void on_toggle_detail_clicked();
    void on_device_detail_clicked();
    void on_password_reset_clicked();
    void on_export_csv_clicked();
    void on_export_xml_clicked();
    void on_selection_changed();
    void on_device_found(const hiksadp::Device& device);
    void on_scan_complete(const hiksadp::protocol::DiscoveryResult& result);

private:
    void setup_ui();
    void setup_toolbar();
    void setup_statusbar();
    void setup_connections();
    void update_action_states();
    void show_error(const QString& title, const QString& message);
    void show_info(const QString& title, const QString& message);

    [[nodiscard]] std::vector<hiksadp::MacAddress> selected_macs() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hiksadp::ui
