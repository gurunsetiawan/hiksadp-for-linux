#include "device_table.hpp"

#include <algorithm>
#include <QColor>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QFont>

namespace hiksadp::ui {

static constexpr int COL_COUNT = static_cast<int>(DeviceTableWidget::Column::COUNT);

static const QStringList COLUMN_HEADERS = {
    "No.", "IP Address", "Status", "HTTP Port",
    "Device Type", "Serial Number", "MAC Address",
    "Firmware Version", "SDK Port", "Gateway", "Subnet Mask"
};

static const int COLUMN_WIDTHS[] = {
    40, 130, 80, 90, 110, 160, 150, 160, 80, 130, 130
};

DeviceTableWidget::DeviceTableWidget(QWidget* parent)
    : QTableWidget{parent}
{
    setup_columns();
    connect(this, &QTableWidget::itemSelectionChanged,
            this, &DeviceTableWidget::on_item_selection_changed);
}

void DeviceTableWidget::setup_columns()
{
    setColumnCount(COL_COUNT);
    setHorizontalHeaderLabels(COLUMN_HEADERS);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    setSortingEnabled(true);
    verticalHeader()->setVisible(false);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

    for (int i = 0; i < COL_COUNT; ++i) {
        setColumnWidth(i, COLUMN_WIDTHS[i]);
    }

    // Font monospace untuk IP dan MAC
    QFont mono_font{"Monospace", 9};
    mono_font.setStyleHint(QFont::TypeWriter);
    // Ini diset per-item di store_device_in_row
}

// Buat QTableWidgetItem yang tidak bisa di-edit
static QTableWidgetItem* make_item(const QString& text,
                                    Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

void DeviceTableWidget::store_device_in_row(int row, const Device& device)
{
    const int no = row + 1;
    QFont mono{"Monospace", 9};
    mono.setStyleHint(QFont::TypeWriter);

    auto set_col = [&](Column col, const QString& text,
                        bool monospace = false,
                        Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter)
    {
        auto* item = make_item(text, align);
        if (monospace) item->setFont(mono);
        // Simpan MAC di UserRole untuk lookup
        if (col == Column::MacAddress) {
            item->setData(Qt::UserRole,
                          QString::fromStdString(device.mac_address.get()));
        }
        setItem(row, static_cast<int>(col), item);
    };

    set_col(Column::No,           QString::number(no),
            false, Qt::AlignCenter | Qt::AlignVCenter);
    set_col(Column::IpAddress,    QString::fromStdString(device.network.ip.get()),
            true);
    set_col(Column::Status,       QString::fromStdString(device.status_string()));
    set_col(Column::HttpPort,     QString::number(device.network.http_port.get()),
            false, Qt::AlignCenter | Qt::AlignVCenter);
    set_col(Column::DeviceType,   QString::fromStdString(device.device_type));
    set_col(Column::SerialNumber, QString::fromStdString(device.serial_number.get()),
            true);
    set_col(Column::MacAddress,   QString::fromStdString(device.mac_address.get()),
            true);
    set_col(Column::Firmware,     QString::fromStdString(device.firmware_version.get()));
    set_col(Column::SdkPort,      QString::number(device.network.sdk_port.get()),
            false, Qt::AlignCenter | Qt::AlignVCenter);
    set_col(Column::Gateway,      QString::fromStdString(device.network.gateway.get()),
            true);
    set_col(Column::SubnetMask,   QString::fromStdString(device.network.subnet_mask.get()),
            true);

    apply_row_style(row, device);
}

void DeviceTableWidget::apply_row_style(int row, const Device& device)
{
    const bool inactive = is_inactive(device.state);
    const bool error    = is_error(device.state);

    QColor fg = inactive ? QColor{180, 100, 0}   // amber untuk inactive
              : error    ? QColor{180, 40,  40}   // merah untuk error
              :            QColor{};               // default

    QColor bg = inactive ? QColor{255, 250, 235}
              : error    ? QColor{255, 235, 235}
              :            QColor{};

    for (int col = 0; col < COL_COUNT; ++col) {
        auto* item = this->item(row, col);
        if (!item) continue;
        if (fg.isValid()) item->setForeground(fg);
        if (bg.isValid()) item->setBackground(bg);
    }
}

void DeviceTableWidget::set_devices(const std::vector<Device>& devices)
{
    setSortingEnabled(false);
    clearContents();
    setRowCount(static_cast<int>(devices.size()));
    all_devices_ = devices;

    for (int row = 0; row < static_cast<int>(devices.size()); ++row) {
        store_device_in_row(row, devices[static_cast<std::size_t>(row)]);
    }

    setSortingEnabled(true);
    // Re-number setelah sort
    sortByColumn(static_cast<int>(Column::IpAddress), Qt::AscendingOrder);
}

void DeviceTableWidget::upsert_device(const Device& device)
{
    // Update all_devices_
    auto it = std::find_if(all_devices_.begin(), all_devices_.end(),
        [&](const Device& d) {
            return d.mac_address == device.mac_address;
        });
    if (it == all_devices_.end()) {
        all_devices_.push_back(device);
    } else {
        *it = device;
    }

    const int existing_row = find_row_by_mac(device.mac_address);
    if (existing_row >= 0) {
        store_device_in_row(existing_row, device);
    } else {
        const int new_row = rowCount();
        setRowCount(new_row + 1);
        store_device_in_row(new_row, device);
    }
}

int DeviceTableWidget::find_row_by_mac(const MacAddress& mac) const
{
    const auto target = QString::fromStdString(mac.get());
    for (int row = 0; row < rowCount(); ++row) {
        auto* item = this->item(row, static_cast<int>(Column::MacAddress));
        if (item && item->data(Qt::UserRole).toString() == target) {
            return row;
        }
    }
    return -1;
}

std::vector<MacAddress> DeviceTableWidget::selected_macs() const
{
    std::vector<MacAddress> result;
    const auto selected_rows = selectionModel()->selectedRows();
    result.reserve(static_cast<std::size_t>(selected_rows.size()));

    for (const auto& idx : selected_rows) {
        auto* item = this->item(idx.row(),
                                 static_cast<int>(Column::MacAddress));
        if (item) {
            result.emplace_back(item->data(Qt::UserRole).toString().toStdString());
        }
    }
    return result;
}

std::vector<Device> DeviceTableWidget::all_devices() const
{
    return all_devices_;
}

void DeviceTableWidget::set_filter_text(const QString& text)
{
    filter_text_ = text.toLower();
    for (int row = 0; row < rowCount(); ++row) {
        bool text_match = filter_text_.isEmpty();
        if (!text_match) {
            for (int col = 0; col < COL_COUNT && !text_match; ++col) {
                auto* item = this->item(row, col);
                if (item && item->text().toLower().contains(filter_text_))
                    text_match = true;
            }
        }
        auto* status_item = this->item(row, static_cast<int>(Column::Status));
        const bool status_match = (filter_status_ == "All") ||
            (status_item && status_item->text() == filter_status_);
        setRowHidden(row, !(text_match && status_match));
    }
}

void DeviceTableWidget::set_filter_status(const QString& status)
{
    filter_status_ = status;
    for (int row = 0; row < rowCount(); ++row) {
        bool text_match = filter_text_.isEmpty();
        if (!text_match) {
            for (int col = 0; col < COL_COUNT && !text_match; ++col) {
                auto* item = this->item(row, col);
                if (item && item->text().toLower().contains(filter_text_)) {
                    text_match = true;
                }
            }
        }
        auto* item = this->item(row, static_cast<int>(Column::Status));
        const bool status_match = (status == "All") || (item && item->text() == status);
        setRowHidden(row, !(text_match && status_match));
    }
}

void DeviceTableWidget::on_item_selection_changed()
{
    emit selection_changed(
        static_cast<int>(selectionModel()->selectedRows().size()));
}

} // namespace hiksadp::ui
