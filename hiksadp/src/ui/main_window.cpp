#include "ui/main_window.hpp"

#include "ui/device_table.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>

namespace hiksadp::ui {

struct MainWindow::Impl {
    DeviceTableWidget* table{nullptr};

    QPushButton* btn_scan{nullptr};
    QPushButton* btn_activate{nullptr};
    QPushButton* btn_network{nullptr};
    QPushButton* btn_reboot{nullptr};
    QPushButton* btn_export_csv{nullptr};
    QPushButton* btn_export_xml{nullptr};

    QLabel* lbl_status{nullptr};

    DeviceManager device_manager;
    protocol::SadpDiscovery scanner;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), impl_{std::make_unique<Impl>()}
{
    setup_ui();
    setup_toolbar();
    setup_statusbar();
    setup_connections();

    setWindowTitle("HikSADP Linux");
    resize(1200, 700);

    update_action_states();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    impl_->scanner.stop();
    QMainWindow::closeEvent(event);
}

void MainWindow::setup_ui()
{
    impl_->table = new DeviceTableWidget(this);
    setCentralWidget(impl_->table);
}

void MainWindow::setup_toolbar()
{
    auto* toolbar = addToolBar("Main");

    impl_->btn_scan = new QPushButton("Scan", this);
    impl_->btn_activate = new QPushButton("Activate", this);
    impl_->btn_network = new QPushButton("Network", this);
    impl_->btn_reboot = new QPushButton("Reboot", this);
    impl_->btn_export_csv = new QPushButton("Export CSV", this);
    impl_->btn_export_xml = new QPushButton("Export XML", this);

    toolbar->addWidget(impl_->btn_scan);
    toolbar->addWidget(impl_->btn_activate);
    toolbar->addWidget(impl_->btn_network);
    toolbar->addWidget(impl_->btn_reboot);
    toolbar->addSeparator();
    toolbar->addWidget(impl_->btn_export_csv);
    toolbar->addWidget(impl_->btn_export_xml);
}

void MainWindow::setup_statusbar()
{
    impl_->lbl_status = new QLabel("Ready", this);
    statusBar()->addPermanentWidget(impl_->lbl_status);
}

void MainWindow::setup_connections()
{
    connect(impl_->btn_scan, &QPushButton::clicked,
            this, &MainWindow::on_scan_clicked);
    connect(impl_->btn_activate, &QPushButton::clicked,
            this, &MainWindow::on_activate_clicked);
    connect(impl_->btn_network, &QPushButton::clicked,
            this, &MainWindow::on_network_config_clicked);
    connect(impl_->btn_reboot, &QPushButton::clicked,
            this, &MainWindow::on_reboot_clicked);
    connect(impl_->btn_export_csv, &QPushButton::clicked,
            this, &MainWindow::on_export_csv_clicked);
    connect(impl_->btn_export_xml, &QPushButton::clicked,
            this, &MainWindow::on_export_xml_clicked);

    connect(impl_->table, &DeviceTableWidget::selection_changed,
            this, &MainWindow::on_selection_changed);

    impl_->device_manager.on_device_list_changed([this](const std::vector<Device>& devices) {
        impl_->table->set_devices(devices);
        impl_->lbl_status->setText(QString("Devices: %1").arg(devices.size()));
        update_action_states();
    });

    impl_->scanner.on_device_found([this](const Device& d) {
        on_device_found(d);
    });

    impl_->scanner.on_scan_complete([this](const protocol::DiscoveryResult& r) {
        on_scan_complete(r);
    });

    impl_->scanner.on_error([this](const AppError& err) {
        show_error("Scan Error", QString::fromStdString(err.message()));
    });
}

void MainWindow::update_action_states()
{
    const auto count = static_cast<int>(selected_macs().size());
    const bool has_selection = count > 0;

    impl_->btn_activate->setEnabled(has_selection);
    impl_->btn_network->setEnabled(has_selection);
    impl_->btn_reboot->setEnabled(has_selection);
}

std::vector<hiksadp::MacAddress> MainWindow::selected_macs() const
{
    return impl_->table->selected_macs();
}

void MainWindow::on_scan_clicked()
{
    impl_->lbl_status->setText("Scanning...");
    auto result = impl_->scanner.start_scan();
    if (!result) {
        show_error("Scan Error", QString::fromStdString(result.error().message()));
    }
}

void MainWindow::on_activate_clicked()
{
    const auto macs = selected_macs();
    if (macs.empty()) {
        show_info("Activate", "Pilih minimal satu device terlebih dulu.");
        return;
    }

    bool ok = false;
    const auto password = QInputDialog::getText(
        this,
        "Activate Device",
        QString("Masukkan password admin untuk %1 device:")
            .arg(macs.size()),
        QLineEdit::Password,
        {},
        &ok);

    if (!ok || password.isEmpty()) {
        return;
    }

    const auto password_std = password.toStdString();
    if (!is_strong_password(password_std)) {
        show_error("Activate Error",
                   "Password lemah. Gunakan minimal 8 karakter dan kombinasi >=3 kategori (huruf besar/kecil/angka/simbol).");
        return;
    }

    const auto result = impl_->device_manager.activate_batch(
        macs, Password{password_std});

    const auto success = result.success_count();
    const auto failed = result.failure_count();

    QString message = QString("Activation selesai.\nBerhasil: %1\nGagal: %2")
        .arg(success)
        .arg(failed);

    if (failed > 0) {
        QStringList lines;
        for (const auto& item : result.items) {
            if (!item.success) {
                lines << QString("- %1: %2")
                             .arg(QString::fromStdString(item.device_label))
                             .arg(QString::fromStdString(item.error_message));
            }
        }
        message += "\n\nDetail gagal:\n" + lines.join('\n');
        show_error("Activation Result", message);
    } else {
        show_info("Activation Result", message);
    }

    impl_->lbl_status->setText(
        QString("Activation done: %1 success, %2 failed").arg(success).arg(failed));
}

void MainWindow::on_network_config_clicked()
{
    show_info("Not Implemented", "Network config dialog akan diimplementasikan di M2.");
}

void MainWindow::on_reboot_clicked()
{
    show_info("Not Implemented", "Reboot operation UI akan diimplementasikan di M2.");
}

void MainWindow::on_export_csv_clicked()
{
    auto result = impl_->device_manager.export_csv();
    if (!result) {
        show_error("Export CSV Error", QString::fromStdString(result.error().message()));
        return;
    }

    const auto path = QFileDialog::getSaveFileName(this, "Export CSV", "devices.csv", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        show_error("Export CSV Error", "Gagal membuka file output.");
        return;
    }

    QTextStream out(&file);
    out << QString::fromStdString(result.value());
    impl_->lbl_status->setText("CSV exported");
}

void MainWindow::on_export_xml_clicked()
{
    auto result = impl_->device_manager.export_xml();
    if (!result) {
        show_error("Export XML Error", QString::fromStdString(result.error().message()));
        return;
    }

    const auto path = QFileDialog::getSaveFileName(this, "Export XML", "devices.xml", "XML Files (*.xml)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        show_error("Export XML Error", "Gagal membuka file output.");
        return;
    }

    QTextStream out(&file);
    out << QString::fromStdString(result.value());
    impl_->lbl_status->setText("XML exported");
}

void MainWindow::on_selection_changed()
{
    update_action_states();
}

void MainWindow::on_device_found(const hiksadp::Device& device)
{
    impl_->table->upsert_device(device);

    auto all = impl_->table->all_devices();
    impl_->device_manager.update_devices(all);
}

void MainWindow::on_scan_complete(const protocol::DiscoveryResult& result)
{
    impl_->lbl_status->setText(QString("Scan complete: %1 device(s)").arg(result.responses_received));
}

void MainWindow::show_error(const QString& title, const QString& message)
{
    QMessageBox::critical(this, title, message);
}

void MainWindow::show_info(const QString& title, const QString& message)
{
    QMessageBox::information(this, title, message);
}

} // namespace hiksadp::ui
