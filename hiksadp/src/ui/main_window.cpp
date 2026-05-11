#include "ui/main_window.hpp"

#include "ui/device_table.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
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
    QLineEdit* search_edit{nullptr};
    QComboBox* status_filter{nullptr};

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
    impl_->search_edit = new QLineEdit(this);
    impl_->status_filter = new QComboBox(this);
    impl_->search_edit->setPlaceholderText("Search IP / Serial / Model...");
    impl_->search_edit->setMinimumWidth(240);
    impl_->status_filter->addItems({"All", "Active", "Inactive"});

    toolbar->addWidget(impl_->btn_scan);
    toolbar->addWidget(impl_->btn_activate);
    toolbar->addWidget(impl_->btn_network);
    toolbar->addWidget(impl_->btn_reboot);
    toolbar->addSeparator();
    toolbar->addWidget(impl_->search_edit);
    toolbar->addWidget(impl_->status_filter);
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
    connect(impl_->search_edit, &QLineEdit::textChanged,
            impl_->table, &DeviceTableWidget::set_filter_text);
    connect(impl_->status_filter, &QComboBox::currentTextChanged,
            impl_->table, &DeviceTableWidget::set_filter_status);

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
    const auto macs = selected_macs();
    if (macs.empty()) {
        show_info("Network Config", "Pilih satu device terlebih dulu.");
        return;
    }
    if (macs.size() != 1) {
        show_info("Network Config", "Untuk saat ini, Network Config hanya mendukung 1 device.");
        return;
    }

    auto dev = impl_->device_manager.find_by_mac(macs.front());
    if (!dev) {
        show_error("Network Config Error", "Device tidak ditemukan.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Network Configuration");
    dialog.setModal(true);

    auto* form = new QFormLayout(&dialog);

    auto* ip_edit = new QLineEdit(QString::fromStdString(dev->network.ip.get()), &dialog);
    auto* mask_edit = new QLineEdit(QString::fromStdString(dev->network.subnet_mask.get()), &dialog);
    auto* gateway_edit = new QLineEdit(QString::fromStdString(dev->network.gateway.get()), &dialog);
    auto* http_port_edit = new QLineEdit(QString::number(dev->network.http_port.get()), &dialog);
    auto* sdk_port_edit = new QLineEdit(QString::number(dev->network.sdk_port.get()), &dialog);
    auto* dhcp_check = new QCheckBox("Enable DHCP", &dialog);
    dhcp_check->setChecked(dev->network.dhcp_enabled);

    auto* password_edit = new QLineEdit(&dialog);
    password_edit->setEchoMode(QLineEdit::Password);

    form->addRow("IP Address", ip_edit);
    form->addRow("Subnet Mask", mask_edit);
    form->addRow("Gateway", gateway_edit);
    form->addRow("HTTP Port", http_port_edit);
    form->addRow("SDK Port", sdk_port_edit);
    form->addRow(dhcp_check);
    form->addRow("Admin Password", password_edit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto ip = ip_edit->text().trimmed().toStdString();
    const auto mask = mask_edit->text().trimmed().toStdString();
    const auto gateway = gateway_edit->text().trimmed().toStdString();
    const auto password = password_edit->text().toStdString();
    const auto dhcp_enabled = dhcp_check->isChecked();

    bool http_ok = false;
    const auto http_port_num = http_port_edit->text().trimmed().toUShort(&http_ok);
    bool sdk_ok = false;
    const auto sdk_port_num = sdk_port_edit->text().trimmed().toUShort(&sdk_ok);

    if (!http_ok || !sdk_ok || http_port_num == 0 || sdk_port_num == 0) {
        show_error("Network Config Error", "HTTP/SDK port tidak valid.");
        return;
    }
    if (password.empty()) {
        show_error("Network Config Error", "Password admin wajib diisi.");
        return;
    }
    if (!is_valid_ip(ip) || !is_valid_ip(mask) || !is_valid_ip(gateway)) {
        show_error("Network Config Error", "Format IP/Subnet/Gateway tidak valid.");
        return;
    }

    const auto confirm = QMessageBox::question(
        this,
        "Confirm Network Change",
        "Perubahan network dapat memutus koneksi sementara.\nLanjutkan?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    const auto result = impl_->device_manager.set_network_config(
        macs.front(),
        Password{password},
        IpAddress{ip},
        IpAddress{mask},
        IpAddress{gateway},
        Port{static_cast<std::uint16_t>(http_port_num)},
        Port{static_cast<std::uint16_t>(sdk_port_num)},
        dhcp_enabled);

    if (!result) {
        show_error("Network Config Error", QString::fromStdString(result.error().message()));
        return;
    }

    show_info("Network Config", "Konfigurasi network berhasil diterapkan.");
    impl_->lbl_status->setText("Network configuration applied");
}

void MainWindow::on_reboot_clicked()
{
    const auto macs = selected_macs();
    if (macs.empty()) {
        show_info("Reboot", "Pilih minimal satu device terlebih dulu.");
        return;
    }

    bool ok = false;
    const auto password = QInputDialog::getText(
        this,
        "Reboot Device",
        QString("Masukkan password admin untuk reboot %1 device:")
            .arg(macs.size()),
        QLineEdit::Password,
        {},
        &ok);

    if (!ok || password.isEmpty()) {
        return;
    }

    const auto confirm = QMessageBox::question(
        this,
        "Confirm Reboot",
        QString("Yakin reboot %1 device terpilih?").arg(macs.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    const auto result = impl_->device_manager.reboot_batch(
        macs, Password{password.toStdString()});

    const auto success = result.success_count();
    const auto failed = result.failure_count();

    QString message = QString("Reboot selesai.\nBerhasil: %1\nGagal: %2")
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
        show_error("Reboot Result", message);
    } else {
        show_info("Reboot Result", message);
    }

    impl_->lbl_status->setText(
        QString("Reboot done: %1 success, %2 failed").arg(success).arg(failed));
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
