#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <functional>

class GatewayClient final : public QObject {
    Q_OBJECT

public:
    explicit GatewayClient(QUrl baseUrl, QObject* parent = nullptr);

    [[nodiscard]] QUrl baseUrl() const;
    void setBaseUrl(QUrl baseUrl);

    void refreshHealth();
    void refreshMarkets();
    void refreshOrderState();
    void refreshAll();

    void submitLimitOrder(const QString& symbol, const QString& side, double price, int quantity);

signals:
    void healthReceived(const QJsonObject& payload);
    void marketsReceived(const QJsonArray& markets);
    void orderStateReceived(const QJsonObject& payload);
    void orderSubmitFinished(const QJsonObject& payload);
    void requestFailed(const QString& path, const QString& detail);

private:
    using JsonHandler = std::function<void(const QJsonDocument&)>;

    void getJson(const QString& path, JsonHandler handler);
    void postJson(const QString& path, const QJsonObject& body, JsonHandler handler);
    [[nodiscard]] QUrl makeUrl(const QString& path) const;

    QUrl baseUrl_;
    QNetworkAccessManager network_;
};
