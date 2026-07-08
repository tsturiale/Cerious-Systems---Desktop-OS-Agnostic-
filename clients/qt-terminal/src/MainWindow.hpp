#pragma once

#include "GatewayClient.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QSet>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QUrl gatewayUrl, QWidget* parent = nullptr);

private slots:
    void refreshNow();
    void submitOrder();
    void applyHealth(const QJsonObject& payload);
    void applyMarkets(const QJsonArray& markets);
    void applyOrderState(const QJsonObject& payload);
    void applyOrderSubmit(const QJsonObject& payload);
    void applyError(const QString& path, const QString& detail);

private:
    void buildUi();
    void buildStatusBar();
    QWidget* buildOrderTicket();
    QWidget* buildTables();
    void configureTable(QTableWidget* table, const QStringList& headers);
    void writeRows(QTableWidget* table, const QJsonArray& rows, const QStringList& fields);
    void appendLog(const QString& message);
    void rememberSymbols(const QJsonArray& markets);

    GatewayClient client_;
    QTimer* refreshTimer_ = nullptr;

    QLabel* gatewayStatus_ = nullptr;
    QLabel* marketStatus_ = nullptr;
    QLabel* executionStatus_ = nullptr;
    QLabel* revisionStatus_ = nullptr;

    QLineEdit* gatewayEdit_ = nullptr;
    QComboBox* symbolCombo_ = nullptr;
    QComboBox* sideCombo_ = nullptr;
    QDoubleSpinBox* priceInput_ = nullptr;
    QSpinBox* quantityInput_ = nullptr;
    QPushButton* submitButton_ = nullptr;

    QTableWidget* productsTable_ = nullptr;
    QTableWidget* ordersTable_ = nullptr;
    QTableWidget* fillsTable_ = nullptr;
    QTableWidget* positionsTable_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QSet<QString> knownSymbols_;
};
