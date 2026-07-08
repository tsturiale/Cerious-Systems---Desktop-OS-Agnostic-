#include "GatewayClient.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>

#include <utility>

GatewayClient::GatewayClient(QUrl baseUrl, QObject* parent)
    : QObject(parent), baseUrl_(std::move(baseUrl)) {
}

QUrl GatewayClient::baseUrl() const {
    return baseUrl_;
}

void GatewayClient::setBaseUrl(QUrl baseUrl) {
    baseUrl_ = std::move(baseUrl);
}

void GatewayClient::refreshHealth() {
    getJson(QStringLiteral("/api/health"), [this](const QJsonDocument& doc) {
        emit healthReceived(doc.object());
    });
}

void GatewayClient::refreshMarkets() {
    getJson(QStringLiteral("/api/markets"), [this](const QJsonDocument& doc) {
        const auto root = doc.object();
        emit marketsReceived(root.value(QStringLiteral("markets")).toArray());
    });
}

void GatewayClient::refreshOrderState() {
    getJson(QStringLiteral("/api/cerious/order-state"), [this](const QJsonDocument& doc) {
        emit orderStateReceived(doc.object());
    });
}

void GatewayClient::refreshAll() {
    refreshHealth();
    refreshMarkets();
    refreshOrderState();
}

void GatewayClient::submitLimitOrder(const QString& symbol, const QString& side, double price, int quantity) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QJsonObject payload{
        {QStringLiteral("orderId"), QStringLiteral("QT-%1-%2-%3").arg(symbol).arg(side).arg(now)},
        {QStringLiteral("symbol"), symbol},
        {QStringLiteral("side"), side},
        {QStringLiteral("type"), QStringLiteral("limit")},
        {QStringLiteral("price"), price},
        {QStringLiteral("quantity"), quantity},
        {QStringLiteral("source"), QStringLiteral("qt-terminal")},
        {QStringLiteral("strategy"), QStringLiteral("manual")},
        {QStringLiteral("operatorId"), QStringLiteral("tsturiale")}
    };

    postJson(QStringLiteral("/api/order"), payload, [this](const QJsonDocument& doc) {
        emit orderSubmitFinished(doc.object());
    });
}

void GatewayClient::getJson(const QString& path, JsonHandler handler) {
    QNetworkRequest request(makeUrl(path));
    request.setRawHeader("Accept", "application/json");

    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, handler = std::move(handler)]() mutable {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
        Q_UNUSED(cleanup);
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(path, reply->errorString());
            return;
        }

        QJsonParseError parseError{};
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit requestFailed(path, parseError.errorString());
            return;
        }

        handler(doc);
    });
}

void GatewayClient::postJson(const QString& path, const QJsonObject& body, JsonHandler handler) {
    QNetworkRequest request(makeUrl(path));
    request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    auto* reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, handler = std::move(handler)]() mutable {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
        Q_UNUSED(cleanup);
        const auto bodyBytes = reply->readAll();
        QJsonParseError parseError{};
        const auto doc = QJsonDocument::fromJson(bodyBytes, &parseError);

        if (reply->error() != QNetworkReply::NoError) {
            const auto detail = parseError.error == QJsonParseError::NoError && doc.isObject()
                ? QString::fromUtf8(QJsonDocument(doc.object()).toJson(QJsonDocument::Compact))
                : reply->errorString();
            emit requestFailed(path, detail);
            return;
        }

        if (parseError.error != QJsonParseError::NoError) {
            emit requestFailed(path, parseError.errorString());
            return;
        }

        handler(doc);
    });
}

QUrl GatewayClient::makeUrl(const QString& path) const {
    auto url = baseUrl_;
    const auto cleanBase = url.path().endsWith(u'/') ? url.path().chopped(1) : url.path();
    const auto cleanPath = path.startsWith(u'/') ? path : QStringLiteral("/") + path;
    url.setPath(cleanBase + cleanPath);
    return url;
}
