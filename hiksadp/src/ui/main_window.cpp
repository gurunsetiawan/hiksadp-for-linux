#include "ui/main_window.hpp"

#include "management/password_reset_service.hpp"
#include "ui/device_table.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QDateTime>

namespace hiksadp::ui {

struct MainWindow::Impl {
    DeviceTableWidget* table{nullptr};

    QPushButton* btn_scan{nullptr};
    QPushButton* btn_activate{nullptr};
    QPushButton* btn_network{nullptr};
    QPushButton* btn_reboot{nullptr};
    QPushButton* btn_password_reset{nullptr};
    QPushButton* btn_export_csv{nullptr};
    QPushButton* btn_export_xml{nullptr};
    QLineEdit* search_edit{nullptr};
    QComboBox* status_filter{nullptr};
    QCheckBox* auto_refresh_check{nullptr};

    QLabel* lbl_status{nullptr};
    QProgressBar* progress{nullptr};

    DeviceManager device_manager;
    PasswordResetService password_reset_service;
    protocol::SadpDiscovery scanner;
};

static std::vector<std::pair<QString, QString>> export_columns()
{
    return {
        {"IP Address", "ip"},
        {"Subnet Mask", "subnet"},
        {"Gateway", "gateway"},
        {"HTTP Port", "http"},
        {"SDK Port", "sdk"},
        {"MAC Address", "mac"},
        {"Serial Number", "serial"},
        {"Model", "model"},
        {"Device Type", "type"},
        {"Firmware", "firmware"},
        {"DHCP", "dhcp"},
        {"Status", "status"},
    };
}

static QString escape_xml(QString s)
{
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    s.replace("'", "&apos;");
    return s;
}

static std::optional<std::vector<QString>>
prompt_export_columns(QWidget* parent, const QString& title)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    auto* info = new QLabel("Pilih kolom yang akan diexport:", &dialog);
    layout->addWidget(info);

    auto* list = new QListWidget(&dialog);
    for (const auto& [label, key] : export_columns()) {
        auto* item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, key);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return std::nullopt;

    std::vector<QString> cols;
    for (int i = 0; i < list->count(); ++i) {
        const auto* item = list->item(i);
        if (item->checkState() == Qt::Checked) {
            cols.push_back(item->data(Qt::UserRole).toString());
        }
    }
    if (cols.empty()) return std::nullopt;
    return cols;
}

static QString device_field_value(const Device& dev, const QString& key)
{
    if (key == "ip") return QString::fromStdString(dev.network.ip.get());
    if (key == "subnet") return QString::fromStdString(dev.network.subnet_mask.get());
    if (key == "gateway") return QString::fromStdString(dev.network.gateway.get());
    if (key == "http") return QString::number(dev.network.http_port.get());
    if (key == "sdk") return QString::number(dev.network.sdk_port.get());
    if (key == "mac") return QString::fromStdString(dev.mac_address.get());
    if (key == "serial") return QString::fromStdString(dev.serial_number.get());
    if (key == "model") return QString::fromStdString(dev.model);
    if (key == "type") return QString::fromStdString(dev.device_type);
    if (key == "firmware") return QString::fromStdString(dev.firmware_version.get());
    if (key == "dhcp") return dev.network.dhcp_enabled ? "Yes" : "No";
    if (key == "status") return QString::fromStdString(dev.status_string());
    return {};
}

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
    impl_->btn_password_reset = new QPushButton("Password Reset", this);
    impl_->btn_export_csv = new QPushButton("Export CSV", this);
    impl_->btn_export_xml = new QPushButton("Export XML", this);
    impl_->search_edit = new QLineEdit(this);
    impl_->status_filter = new QComboBox(this);
    impl_->auto_refresh_check = new QCheckBox("Auto refresh (15s)", this);
    impl_->search_edit->setPlaceholderText("Search IP / Serial / Model...");
    impl_->search_edit->setMinimumWidth(240);
    impl_->status_filter->addItems({"All", "Active", "Inactive"});

    toolbar->addWidget(impl_->btn_scan);
    toolbar->addWidget(impl_->btn_activate);
    toolbar->addWidget(impl_->btn_network);
    toolbar->addWidget(impl_->btn_reboot);
    toolbar->addWidget(impl_->btn_password_reset);
    toolbar->addSeparator();
    toolbar->addWidget(impl_->search_edit);
    toolbar->addWidget(impl_->status_filter);
    toolbar->addWidget(impl_->auto_refresh_check);
    toolbar->addSeparator();
    toolbar->addWidget(impl_->btn_export_csv);
    toolbar->addWidget(impl_->btn_export_xml);
}

void MainWindow::setup_statusbar()
{
    impl_->lbl_status = new QLabel("Ready", this);
    impl_->progress = new QProgressBar(this);
    impl_->progress->setMinimum(0);
    impl_->progress->setMaximum(100);
    impl_->progress->setValue(0);
    impl_->progress->setVisible(false);
    impl_->progress->setFixedWidth(260);
    statusBar()->addPermanentWidget(impl_->lbl_status);
    statusBar()->addPermanentWidget(impl_->progress);
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
    connect(impl_->btn_password_reset, &QPushButton::clicked,
            this, &MainWindow::on_password_reset_clicked);
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
    connect(impl_->auto_refresh_check, &QCheckBox::toggled, this, [this](bool enabled) {
        impl_->scanner.set_auto_refresh(enabled);
        impl_->lbl_status->setText(enabled ? "Auto refresh enabled (15s)" : "Auto refresh disabled");
    });

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

    impl_->device_manager.on_progress([this](int current, int total, const std::string& label) {
        if (total <= 0) return;
        impl_->progress->setVisible(true);
        impl_->progress->setMaximum(total);
        impl_->progress->setValue(current);
        impl_->lbl_status->setText(QString::fromStdString(label));
        if (current >= total) {
            impl_->progress->setVisible(false);
        }
    });
}

void MainWindow::update_action_states()
{
    const auto count = static_cast<int>(selected_macs().size());
    const bool has_selection = count > 0;

    impl_->btn_activate->setEnabled(has_selection);
    impl_->btn_network->setEnabled(has_selection);
    impl_->btn_reboot->setEnabled(has_selection);
    impl_->btn_password_reset->setEnabled(count == 1);
}

std::vector<hiksadp::MacAddress> MainWindow::selected_macs() const
{
    return impl_->table->selected_macs();
}

void MainWindow::on_scan_clicked()
{
    if (impl_->scanner.is_scanning()) {
        show_info("Scan", "Scan sedang berjalan.");
        return;
    }
    impl_->btn_scan->setEnabled(false);
    impl_->lbl_status->setText("Scanning...");
    auto result = impl_->scanner.start_scan();
    if (!result) {
        impl_->btn_scan->setEnabled(true);
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
    if (macs.size() > 1) {
        QDialog dialog(this);
        dialog.setWindowTitle("Batch Sequential IP Configuration");
        dialog.setModal(true);

        auto* form = new QFormLayout(&dialog);
        auto* start_ip_edit = new QLineEdit("192.168.1.64", &dialog);
        auto* mask_edit = new QLineEdit("255.255.255.0", &dialog);
        auto* gateway_edit = new QLineEdit("192.168.1.1", &dialog);
        auto* http_port_edit = new QLineEdit("80", &dialog);
        auto* sdk_port_edit = new QLineEdit("8000", &dialog);
        auto* dhcp_check = new QCheckBox("Enable DHCP", &dialog);
        auto* password_edit = new QLineEdit(&dialog);
        password_edit->setEchoMode(QLineEdit::Password);

        form->addRow("Start IP", start_ip_edit);
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

        const auto start_ip = start_ip_edit->text().trimmed().toStdString();
        const auto mask = mask_edit->text().trimmed().toStdString();
        const auto gateway = gateway_edit->text().trimmed().toStdString();
        const auto password = password_edit->text().toStdString();
        const auto dhcp_enabled = dhcp_check->isChecked();

        bool http_ok = false;
        const auto http_port_num = http_port_edit->text().trimmed().toUShort(&http_ok);
        bool sdk_ok = false;
        const auto sdk_port_num = sdk_port_edit->text().trimmed().toUShort(&sdk_ok);

        if (!http_ok || !sdk_ok || http_port_num == 0 || sdk_port_num == 0) {
            show_error("Batch Network Config Error", "HTTP/SDK port tidak valid.");
            return;
        }
        if (password.empty()) {
            show_error("Batch Network Config Error", "Password admin wajib diisi.");
            return;
        }
        if (!is_valid_ip(start_ip) || !is_valid_ip(mask) || !is_valid_ip(gateway)) {
            show_error("Batch Network Config Error", "Format Start IP/Subnet/Gateway tidak valid.");
            return;
        }

        IpSequenceConfig cfg{
            IpAddress{start_ip},
            IpAddress{mask},
            IpAddress{gateway},
            Port{static_cast<std::uint16_t>(http_port_num)},
            Port{static_cast<std::uint16_t>(sdk_port_num)},
            dhcp_enabled
        };

        const auto preview = impl_->device_manager.preview_sequential_ips(macs, cfg);
        QStringList lines;
        for (const auto& [mac, ip] : preview) {
            lines << QString("%1 -> %2")
                         .arg(QString::fromStdString(mac.get()))
                         .arg(QString::fromStdString(ip.get()));
        }

        const auto confirm = QMessageBox::question(
            this,
            "Confirm Batch IP Assignment",
            "IP preview:\n" + lines.join('\n') + "\n\nLanjutkan apply?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (confirm != QMessageBox::Yes) {
            return;
        }

        const auto result = impl_->device_manager.assign_sequential_ips(
            macs, Password{password}, cfg);

        const auto success = result.success_count();
        const auto failed = result.failure_count();

        QString message = QString("Batch IP selesai.\nBerhasil: %1\nGagal: %2")
            .arg(success)
            .arg(failed);

        if (failed > 0) {
            QStringList failed_lines;
            for (const auto& item : result.items) {
                if (!item.success) {
                    failed_lines << QString("- %1: %2")
                                       .arg(QString::fromStdString(item.device_label))
                                       .arg(QString::fromStdString(item.error_message));
                }
            }
            message += "\n\nDetail gagal:\n" + failed_lines.join('\n');
            show_error("Batch Network Config Result", message);
        } else {
            show_info("Batch Network Config Result", message);
        }

        impl_->lbl_status->setText(
            QString("Batch IP done: %1 success, %2 failed").arg(success).arg(failed));
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

void MainWindow::on_password_reset_clicked()
{
    const auto macs = selected_macs();
    if (macs.size() != 1) {
        show_info("Password Reset", "Pilih tepat satu device untuk password reset.");
        return;
    }

    auto dev = impl_->device_manager.find_by_mac(macs.front());
    if (!dev) {
        show_error("Password Reset Error", "Device tidak ditemukan.");
        return;
    }

    const auto action = QMessageBox::question(
        this,
        "Password Reset",
        "Pilih Yes untuk generate request XML.\nPilih No untuk import response XML.",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Yes);

    if (action == QMessageBox::Cancel) {
        return;
    }

    if (action == QMessageBox::Yes) {
        const auto path = QFileDialog::getSaveFileName(
            this, "Save Reset Request XML", "reset_request.xml", "XML Files (*.xml)");
        if (path.isEmpty()) return;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            show_error("Password Reset Error", "Gagal membuat file request XML.");
            return;
        }

        const auto now = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        const auto xml_result = impl_->password_reset_service.build_request_xml(*dev, now);
        if (!xml_result) {
            show_error("Password Reset Error", QString::fromStdString(xml_result.error().message()));
            return;
        }

        QTextStream out(&file);
        out << QString::fromStdString(xml_result.value());

        show_info("Password Reset", "Request XML berhasil dibuat.");
        impl_->lbl_status->setText("Password reset request exported");
        return;
    }

    const auto path = QFileDialog::getOpenFileName(
        this, "Import Reset Response XML", {}, "XML Files (*.xml)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        show_error("Password Reset Error", "Gagal membaca response XML.");
        return;
    }
    const auto parse_result =
        impl_->password_reset_service.parse_response_xml(QString::fromUtf8(file.readAll()).toStdString());
    if (!parse_result) {
        show_error("Password Reset Error", QString::fromStdString(parse_result.error().message()));
        return;
    }

    const auto& parsed = parse_result.value();
    QString summary = "Response XML berhasil diimport.\n";
    summary += "Serial: " + QString::fromStdString(parsed.serial) + "\n";
    if (!parsed.timestamp.empty()) summary += "Date/Timestamp: " + QString::fromStdString(parsed.timestamp) + "\n";
    if (!parsed.reset_code.empty()) summary += "Reset Code: " + QString::fromStdString(parsed.reset_code) + "\n";
    summary += "\nCatatan: apply reset code ke device belum diimplementasikan (M3 next step).";

    show_info("Password Reset", summary);
    impl_->lbl_status->setText("Password reset response imported");
}

void MainWindow::on_export_csv_clicked()
{
    const auto selected = prompt_export_columns(this, "Export CSV Columns");
    if (!selected.has_value()) return;

    const auto path = QFileDialog::getSaveFileName(this, "Export CSV", "devices.csv", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        show_error("Export CSV Error", "Gagal membuka file output.");
        return;
    }

    const auto cols = selected.value();
    QStringList headers;
    for (const auto& key : cols) {
        for (const auto& [label, map_key] : export_columns()) {
            if (map_key == key) {
                headers << label;
                break;
            }
        }
    }

    QTextStream out(&file);
    out << headers.join(',') << "\n";
    const auto devices = impl_->device_manager.devices();
    for (const auto& dev : devices) {
        QStringList row;
        for (const auto& key : cols) {
            row << device_field_value(dev, key);
        }
        out << row.join(',') << "\n";
    }

    impl_->lbl_status->setText("CSV exported");
}

void MainWindow::on_export_xml_clicked()
{
    const auto selected = prompt_export_columns(this, "Export XML Columns");
    if (!selected.has_value()) return;

    const auto path = QFileDialog::getSaveFileName(this, "Export XML", "devices.xml", "XML Files (*.xml)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        show_error("Export XML Error", "Gagal membuka file output.");
        return;
    }

    const auto cols = selected.value();
    QTextStream out(&file);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<DeviceList generatedAt=\""
        << QDateTime::currentDateTime().toString(Qt::ISODate)
        << "\">\n";
    const auto devices = impl_->device_manager.devices();
    for (const auto& dev : devices) {
        out << "  <Device>\n";
        for (const auto& key : cols) {
            QString tag;
            for (const auto& [label, map_key] : export_columns()) {
                if (map_key == key) {
                    tag = label;
                    break;
                }
            }
            tag.remove(' ');
            out << "    <" << tag << ">"
                << escape_xml(device_field_value(dev, key))
                << "</" << tag << ">\n";
        }
        out << "  </Device>\n";
    }
    out << "</DeviceList>\n";
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
    impl_->btn_scan->setEnabled(true);
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
