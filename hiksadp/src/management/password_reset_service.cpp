#include "management/password_reset_service.hpp"

#include <QRegularExpression>

namespace hiksadp {

static QString escape_xml(QString s)
{
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    s.replace("'", "&apos;");
    return s;
}

Result<std::string>
PasswordResetService::build_request_xml(const Device& device, const std::string& iso_timestamp) const
{
    if (device.serial_number.get().empty()) {
        return make_error<std::string>(ErrorCode::EmptyInput, "serial number kosong");
    }
    if (device.mac_address.get().empty()) {
        return make_error<std::string>(ErrorCode::EmptyInput, "MAC address kosong");
    }

    QString xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<PasswordResetRequest>\n";
    xml += "  <DeviceSN>" + escape_xml(QString::fromStdString(device.serial_number.get())) + "</DeviceSN>\n";
    xml += "  <MAC>" + escape_xml(QString::fromStdString(device.mac_address.get())) + "</MAC>\n";
    xml += "  <IPv4Address>" + escape_xml(QString::fromStdString(device.network.ip.get())) + "</IPv4Address>\n";
    xml += "  <Timestamp>" + escape_xml(QString::fromStdString(iso_timestamp)) + "</Timestamp>\n";
    xml += "</PasswordResetRequest>\n";
    return make_ok(xml.toStdString());
}

Result<PasswordResetResponse>
PasswordResetService::parse_response_xml(const std::string& xml) const
{
    const QString qxml = QString::fromStdString(xml);

    const auto sn_match =
        QRegularExpression("<(DeviceSN|SerialNumber)>([^<]+)</(DeviceSN|SerialNumber)>").match(qxml);
    if (!sn_match.hasMatch()) {
        return make_error<PasswordResetResponse>(ErrorCode::XmlParseFailed, "serial number tidak ditemukan");
    }

    const auto date_match =
        QRegularExpression("<(Date|Timestamp)>([^<]+)</(Date|Timestamp)>").match(qxml);
    const auto code_match =
        QRegularExpression("<(ResetCode|SecurityCode|Code)>([^<]+)</(ResetCode|SecurityCode|Code)>").match(qxml);

    PasswordResetResponse out;
    out.serial = sn_match.captured(2).toStdString();
    if (date_match.hasMatch()) out.timestamp = date_match.captured(2).toStdString();
    if (code_match.hasMatch()) out.reset_code = code_match.captured(2).toStdString();

    return make_ok(std::move(out));
}

} // namespace hiksadp

