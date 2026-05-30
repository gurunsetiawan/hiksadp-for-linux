// Qt headers di .cpp saja
#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QAuthenticator>

#include "isapi_client.hpp"

#include <array>
#include <format>

namespace hiksadp {

// ── IsapiClient::Impl ──────────────────────────────────────────────────────

struct IsapiClient::Impl {
    std::unique_ptr<QNetworkAccessManager> nam;
    int timeout_ms{IsapiClient::DEFAULT_TIMEOUT_MS};
    std::string auth_user;
    std::string auth_password;
    bool use_auth{false};

    Impl() : nam{std::make_unique<QNetworkAccessManager>()} {
        QObject::connect(
            nam.get(),
            &QNetworkAccessManager::authenticationRequired,
            [this](QNetworkReply*, QAuthenticator* auth) {
                if (!use_auth) {
                    return;
                }
                auth->setUser(QString::fromStdString(auth_user));
                auth->setPassword(QString::fromStdString(auth_password));
            });
    }

    // Eksekusi request secara synchronous menggunakan local event loop
    // (aman karena dijalankan di worker thread, bukan UI thread)
    Result<IsapiResponse> exec_request(QNetworkReply* reply) {
        QEventLoop loop;
        QTimer     timeout_guard;

        timeout_guard.setSingleShot(true);
        timeout_guard.setInterval(timeout_ms);

        QObject::connect(reply, &QNetworkReply::finished,
                         &loop, &QEventLoop::quit);
        QObject::connect(&timeout_guard, &QTimer::timeout,
                         &loop, &QEventLoop::quit);

        timeout_guard.start();
        loop.exec();

        if (!timeout_guard.isActive()) {
            // Timer habis — timeout
            reply->abort();
            reply->deleteLater();
            return make_error<IsapiResponse>(ErrorCode::OperationTimeout);
        }
        timeout_guard.stop();

        if (reply->error() != QNetworkReply::NoError) {
            const auto err_str = reply->errorString().toStdString();
            const auto http_status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto body = reply->readAll().toStdString();
            reply->deleteLater();

            if (http_status == 401) {
                return make_error<IsapiResponse>(ErrorCode::AuthenticationFailed,
                                                  "HTTP 401");
            }
            if (http_status > 0) {
                IsapiResponse resp;
                resp.status_code = http_status;
                resp.body = body;
                return make_ok(std::move(resp));
            }

            if (reply->error() == QNetworkReply::ConnectionRefusedError ||
                reply->error() == QNetworkReply::HostNotFoundError) {
                return make_error<IsapiResponse>(ErrorCode::NetworkUnreachable,
                                                  err_str);
            }
            return make_error<IsapiResponse>(ErrorCode::UnexpectedResponse,
                                              err_str);
        }

        IsapiResponse resp;
        resp.status_code = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        resp.body = reply->readAll().toStdString();
        reply->deleteLater();
        return make_ok(std::move(resp));
    }

    // Setup request dengan Digest Auth
    QNetworkRequest make_request(const std::string& url_str) {
        QNetworkRequest req{QUrl{QString::fromStdString(url_str)}};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        return req;
    }
};

// ── IsapiClient ────────────────────────────────────────────────────────────

IsapiClient::IsapiClient() : impl_{std::make_unique<Impl>()} {}
IsapiClient::~IsapiClient() = default;

void IsapiClient::set_timeout(int ms) { impl_->timeout_ms = ms; }

std::string IsapiClient::build_base_url(const IpAddress& ip, Port port) const {
    return std::format("http://{}:{}", ip.get(), port.get());
}

// ── HTTP helpers ───────────────────────────────────────────────────────────

Result<IsapiResponse>
IsapiClient::http_get(const IsapiCredential& cred, const std::string& path)
{
    const auto url = build_base_url(cred.ip, cred.http_port) + path;
    auto req = impl_->make_request(url);
    impl_->auth_user = cred.username;
    impl_->auth_password = cred.password.get();
    impl_->use_auth = true;

    auto* reply = impl_->nam->get(req);
    return impl_->exec_request(reply);
}

Result<IsapiResponse>
IsapiClient::http_put(const IsapiCredential& cred,
                       const std::string& path,
                       const std::string& xml_body)
{
    const auto url = build_base_url(cred.ip, cred.http_port) + path;
    auto req = impl_->make_request(url);
    impl_->auth_user = cred.username;
    impl_->auth_password = cred.password.get();
    impl_->use_auth = true;

    auto* reply = impl_->nam->put(req,
        QByteArray::fromStdString(xml_body));
    return impl_->exec_request(reply);
}

Result<IsapiResponse>
IsapiClient::http_put_noauth(const IpAddress& ip, Port port,
                               const std::string& path,
                               const std::string& xml_body)
{
    const auto url = build_base_url(ip, port) + path;
    auto req = impl_->make_request(url);
    impl_->use_auth = false;
    auto* reply = impl_->nam->put(req, QByteArray::fromStdString(xml_body));
    return impl_->exec_request(reply);
}

// ── Public API ─────────────────────────────────────────────────────────────

Result<void> IsapiClient::activate_device(const ActivationRequest& req)
{
    if (!is_valid_ip(req.ip.get())) {
        return make_error<void>(ErrorCode::InvalidIpAddress, req.ip.get());
    }
    if (!is_strong_password(req.new_password.get())) {
        return make_error<void>(ErrorCode::WeakPassword);
    }

    const std::string xml = std::format(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<ActivateInfo>)"
        R"(<password>{}</password>)"
        R"(</ActivateInfo>)",
        req.new_password.get()
    );

    auto resp = http_put_noauth(req.ip, req.http_port,
                                 "/ISAPI/Security/userCheck", xml);
    if (!resp) return std::unexpected(resp.error());

    if (resp.value().status_code == 200) return Result<void>{};
    if (resp.value().status_code == 401)
        return make_error<void>(ErrorCode::AuthenticationFailed);
    if (resp.value().status_code == 400)
        return make_error<void>(ErrorCode::WeakPassword);

    return make_error<void>(ErrorCode::UnexpectedResponse,
        std::format("HTTP {}", resp.value().status_code));
}

Result<std::string> IsapiClient::get_device_info(const IsapiCredential& cred)
{
    auto resp = http_get(cred, "/ISAPI/System/deviceInfo");
    if (!resp) return std::unexpected(resp.error());

    if (!resp.value().is_ok()) {
        return make_error<std::string>(ErrorCode::UnexpectedResponse,
            std::format("HTTP {}", resp.value().status_code));
    }

    return make_ok(resp.value().body);
}

Result<void> IsapiClient::set_network_config(const NetworkConfigRequest& req)
{
    if (!is_valid_ip(req.new_ip.get()))
        return make_error<void>(ErrorCode::InvalidIpAddress, req.new_ip.get());
    if (!is_valid_ip(req.new_mask.get()))
        return make_error<void>(ErrorCode::InvalidIpAddress,
                                 "subnet: " + req.new_mask.get());
    if (!is_valid_ip(req.new_gateway.get()))
        return make_error<void>(ErrorCode::InvalidIpAddress,
                                 "gateway: " + req.new_gateway.get());

    const std::string xml = std::format(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<NetworkInterface>)"
        R"(<id>1</id>)"
        R"(<IPAddress>)"
        R"(<ipVersion>v4</ipVersion>)"
        R"(<addressingType>{}</addressingType>)"
        R"(<ipAddress>{}</ipAddress>)"
        R"(<subnetMask>{}</subnetMask>)"
        R"(<DefaultGateway><ipAddress>{}</ipAddress></DefaultGateway>)"
        R"(</IPAddress>)"
        R"(<httpPort>{}</httpPort>)"
        R"(<sdkPort>{}</sdkPort>)"
        R"(</NetworkInterface>)",
        req.dhcp_enabled ? "dynamic" : "static",
        req.new_ip.get(),
        req.new_mask.get(),
        req.new_gateway.get(),
        req.new_http_port.get(),
        req.new_sdk_port.get()
    );

    auto resp = http_put(req.credential,
                          "/ISAPI/System/Network/interfaces/1", xml);
    if (!resp) return std::unexpected(resp.error());

    if (resp.value().is_ok()) return Result<void>{};
    if (resp.value().status_code == 401)
        return make_error<void>(ErrorCode::AuthenticationFailed);

    return make_error<void>(ErrorCode::UnexpectedResponse,
        std::format("HTTP {}", resp.value().status_code));
}

Result<void> IsapiClient::reboot_device(const IsapiCredential& cred)
{
    auto resp = http_put(cred, "/ISAPI/System/reboot", "");
    if (!resp) return std::unexpected(resp.error());

    if (resp.value().is_ok()) return Result<void>{};
    if (resp.value().status_code == 401)
        return make_error<void>(ErrorCode::AuthenticationFailed);

    return make_error<void>(ErrorCode::UnexpectedResponse,
        std::format("HTTP {}", resp.value().status_code));
}

Result<void> IsapiClient::change_password(const ChangePasswordRequest& req)
{
    if (req.target_username.empty()) {
        return make_error<void>(ErrorCode::EmptyInput, "username kosong");
    }
    if (!is_strong_password(req.new_password.get())) {
        return make_error<void>(ErrorCode::WeakPassword);
    }

    const std::string xml_user = std::format(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<User>)"
        R"(<id>1</id>)"
        R"(<userName>{}</userName>)"
        R"(<password>{}</password>)"
        R"(</User>)",
        req.target_username,
        req.new_password.get());

    const std::string xml_password = std::format(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Security>)"
        R"(<password>{}</password>)"
        R"(</Security>)",
        req.new_password.get());

    struct Candidate {
        std::string path;
        std::string body;
    };
    const std::array<Candidate, 3> candidates{{
        {"/ISAPI/Security/users/1", xml_user},
        {"/ISAPI/Security/users/1/", xml_user},
        {"/ISAPI/Security/users/1/password", xml_password},
    }};

    int last_status = 0;
    std::string last_detail;
    for (const auto& c : candidates) {
        auto resp = http_put(req.credential, c.path, c.body);
        if (!resp) {
            // Error transport/auth diteruskan langsung.
            if (resp.error().code == ErrorCode::AuthenticationFailed ||
                resp.error().code == ErrorCode::NetworkUnreachable ||
                resp.error().code == ErrorCode::OperationTimeout) {
                return std::unexpected(resp.error());
            }
            last_detail = resp.error().message();
            continue;
        }

        last_status = resp.value().status_code;
        if (resp.value().is_ok()) return Result<void>{};
        if (resp.value().status_code == 401) {
            return make_error<void>(ErrorCode::AuthenticationFailed, "password lama salah");
        }
        if (resp.value().status_code == 400) {
            return make_error<void>(ErrorCode::WeakPassword, "password baru ditolak oleh policy device");
        }
        // Endpoint tidak didukung: lanjut coba kandidat berikutnya.
        if (resp.value().status_code == 403 ||
            resp.value().status_code == 404 ||
            resp.value().status_code == 405 ||
            resp.value().status_code == 501) {
            continue;
        }
        last_detail = std::format("HTTP {} body={}", resp.value().status_code, resp.value().body);
    }

    if (last_status == 403 || last_status == 404 || last_status == 405 || last_status == 501) {
        return make_error<void>(
            ErrorCode::UnexpectedResponse,
            "fitur change password tidak didukung pada endpoint ISAPI device ini");
    }
    return make_error<void>(ErrorCode::UnexpectedResponse,
        last_detail.empty() ? std::format("HTTP {}", last_status) : last_detail);
}

Result<bool> IsapiClient::is_device_active(const IpAddress& ip, Port http_port)
{
    if (!is_valid_ip(ip.get()))
        return make_error<bool>(ErrorCode::InvalidIpAddress, ip.get());

    // Coba hit endpoint tanpa auth — kalau 401 berarti aktif, 404/timeout = inactive/unreachable
    const auto url = build_base_url(ip, http_port) + "/ISAPI/System/deviceInfo";
    auto req = impl_->make_request(url);
    auto* reply = impl_->nam->get(req);
    auto resp = impl_->exec_request(reply);

    if (!resp) {
        if (resp.error().code == ErrorCode::OperationTimeout ||
            resp.error().code == ErrorCode::NetworkUnreachable)
            return make_error<bool>(resp.error().code);
        return make_ok(false);
    }

    // 401 = device aktif tapi perlu auth
    // 200 tanpa auth tidak mungkin untuk device normal
    return make_ok(resp.value().status_code == 401 ||
                   resp.value().status_code == 200);
}

} // namespace hiksadp
