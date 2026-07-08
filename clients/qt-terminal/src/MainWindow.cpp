#include "MainWindow.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {
QString jsonText(const QJsonValue& value) {
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble(), 'f', 4).replace(QRegularExpression(QStringLiteral("\\.?0+$")), QString());
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isNull() || value.isUndefined()) return QString();
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return QString();
}

QLabel* statusCard(const QString& title) {
    auto* label = new QLabel(title + QStringLiteral("\nwaiting"));
    label->setMinimumWidth(180);
    label->setFrameShape(QFrame::StyledPanel);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setStyleSheet(QStringLiteral("QLabel { padding: 10px; font-weight: 600; }"));
    return label;
}
}

MainWindow::MainWindow(QUrl gatewayUrl, QWidget* parent)
    : QMainWindow(parent), client_(std::move(gatewayUrl), this) {
    buildUi();

    connect(&client_, &GatewayClient::healthReceived, this, &MainWindow::applyHealth);
    connect(&client_, &GatewayClient::marketsReceived, this, &MainWindow::applyMarkets);
    connect(&client_, &GatewayClient::orderStateReceived, this, &MainWindow::applyOrderState);
    connect(&client_, &GatewayClient::orderSubmitFinished, this, &MainWindow::applyOrderSubmit);
    connect(&client_, &GatewayClient::requestFailed, this, &MainWindow::applyError);

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshNow);
    refreshTimer_->start(1000);
    refreshNow();
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("Cerious Systems - Qt Native Terminal"));
    resize(1500, 950);

    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);

    auto* topRow = new QHBoxLayout();
    gatewayEdit_ = new QLineEdit(client_.baseUrl().toString());
    auto* applyGateway = new QPushButton(QStringLiteral("Set Gateway"));
    auto* refresh = new QPushButton(QStringLiteral("Refresh"));
    connect(applyGateway, &QPushButton::clicked, this, [this]() {
        client_.setBaseUrl(QUrl(gatewayEdit_->text()));
        refreshNow();
    });
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshNow);

    topRow->addWidget(new QLabel(QStringLiteral("Gateway")));
    topRow->addWidget(gatewayEdit_, 1);
    topRow->addWidget(applyGateway);
    topRow->addWidget(refresh);
    layout->addLayout(topRow);

    buildStatusBar();
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(gatewayStatus_);
    statusRow->addWidget(marketStatus_);
    statusRow->addWidget(executionStatus_);
    statusRow->addWidget(revisionStatus_);
    layout->addLayout(statusRow);

    auto* splitter = new QSplitter(Qt::Horizontal, root);
    splitter->addWidget(buildOrderTicket());
    splitter->addWidget(buildTables());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    setCentralWidget(root);
}

void MainWindow::buildStatusBar() {
    gatewayStatus_ = statusCard(QStringLiteral("Gateway"));
    marketStatus_ = statusCard(QStringLiteral("Market Data"));
    executionStatus_ = statusCard(QStringLiteral("Execution"));
    revisionStatus_ = statusCard(QStringLiteral("Snapshot"));
}

QWidget* MainWindow::buildOrderTicket() {
    auto* group = new QGroupBox(QStringLiteral("Manual Order Ticket"));
    group->setMinimumWidth(300);
    auto* form = new QFormLayout(group);

    symbolCombo_ = new QComboBox(group);
    symbolCombo_->setEditable(true);
    for (const auto& symbol : {QStringLiteral("ES"), QStringLiteral("NQ"), QStringLiteral("RTY"), QStringLiteral("YM"), QStringLiteral("ES_NQ"), QStringLiteral("YM_ES"), QStringLiteral("RTY_ES")}) {
        symbolCombo_->addItem(symbol);
        knownSymbols_.insert(symbol);
    }

    sideCombo_ = new QComboBox(group);
    sideCombo_->addItems({QStringLiteral("buy"), QStringLiteral("sell")});

    priceInput_ = new QDoubleSpinBox(group);
    priceInput_->setDecimals(4);
    priceInput_->setRange(-1000000.0, 1000000.0);
    priceInput_->setSingleStep(0.25);

    quantityInput_ = new QSpinBox(group);
    quantityInput_->setRange(1, 1000);
    quantityInput_->setValue(1);

    submitButton_ = new QPushButton(QStringLiteral("Place Order"), group);
    connect(submitButton_, &QPushButton::clicked, this, &MainWindow::submitOrder);

    form->addRow(QStringLiteral("Product"), symbolCombo_);
    form->addRow(QStringLiteral("Side"), sideCombo_);
    form->addRow(QStringLiteral("Limit Price"), priceInput_);
    form->addRow(QStringLiteral("Quantity"), quantityInput_);
    form->addRow(submitButton_);

    log_ = new QPlainTextEdit(group);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(500);
    form->addRow(QStringLiteral("Audit"), log_);
    return group;
}

QWidget* MainWindow::buildTables() {
    auto* panel = new QWidget(this);
    auto* layout = new QGridLayout(panel);

    productsTable_ = new QTableWidget(panel);
    ordersTable_ = new QTableWidget(panel);
    fillsTable_ = new QTableWidget(panel);
    positionsTable_ = new QTableWidget(panel);

    configureTable(productsTable_, {QStringLiteral("Symbol"), QStringLiteral("Provider"), QStringLiteral("Status"), QStringLiteral("Tick"), QStringLiteral("Tick Value"), QStringLiteral("Last")});
    configureTable(ordersTable_, {QStringLiteral("Time"), QStringLiteral("Product"), QStringLiteral("Side"), QStringLiteral("Price"), QStringLiteral("Remaining"), QStringLiteral("Status"), QStringLiteral("Order ID")});
    configureTable(fillsTable_, {QStringLiteral("Time"), QStringLiteral("Product"), QStringLiteral("Side"), QStringLiteral("Price"), QStringLiteral("Qty"), QStringLiteral("Order ID")});
    configureTable(positionsTable_, {QStringLiteral("Product"), QStringLiteral("Side"), QStringLiteral("Qty"), QStringLiteral("Avg"), QStringLiteral("Mark"), QStringLiteral("Open PnL"), QStringLiteral("Closed PnL")});

    layout->addWidget(new QLabel(QStringLiteral("Products")), 0, 0);
    layout->addWidget(new QLabel(QStringLiteral("Working Orders")), 0, 1);
    layout->addWidget(productsTable_, 1, 0);
    layout->addWidget(ordersTable_, 1, 1);
    layout->addWidget(new QLabel(QStringLiteral("Fills")), 2, 0);
    layout->addWidget(new QLabel(QStringLiteral("Positions")), 2, 1);
    layout->addWidget(fillsTable_, 3, 0);
    layout->addWidget(positionsTable_, 3, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setRowStretch(1, 1);
    layout->setRowStretch(3, 1);
    return panel;
}

void MainWindow::configureTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

void MainWindow::writeRows(QTableWidget* table, const QJsonArray& rows, const QStringList& fields) {
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const auto object = rows.at(row).toObject();
        for (int col = 0; col < fields.size(); ++col) {
            auto* item = new QTableWidgetItem(jsonText(object.value(fields.at(col))));
            table->setItem(row, col, item);
        }
    }
}

void MainWindow::refreshNow() {
    client_.refreshAll();
}

void MainWindow::submitOrder() {
    client_.submitLimitOrder(
        symbolCombo_->currentText().trimmed().toUpper(),
        sideCombo_->currentText(),
        priceInput_->value(),
        quantityInput_->value());
}

void MainWindow::applyHealth(const QJsonObject& payload) {
    const auto ok = payload.value(QStringLiteral("ok")).toBool();
    gatewayStatus_->setText(QStringLiteral("Gateway\n%1").arg(ok ? QStringLiteral("online") : QStringLiteral("offline")));

    const auto market = payload.value(QStringLiteral("marketData")).toObject();
    marketStatus_->setText(QStringLiteral("Market Data\n%1 / %2 records")
        .arg(market.value(QStringLiteral("status")).toString(QStringLiteral("unknown")))
        .arg(QString::number(static_cast<qlonglong>(market.value(QStringLiteral("records")).toDouble()))));

    const auto execution = payload.value(QStringLiteral("execution")).toObject();
    executionStatus_->setText(QStringLiteral("Execution\n%1 / %2")
        .arg(execution.value(QStringLiteral("destination")).toString(QStringLiteral("unknown")))
        .arg(execution.value(QStringLiteral("healthy")).toBool() ? QStringLiteral("healthy") : QStringLiteral("not healthy")));
}

void MainWindow::applyMarkets(const QJsonArray& markets) {
    rememberSymbols(markets);
    productsTable_->setRowCount(markets.size());
    for (int row = 0; row < markets.size(); ++row) {
        const auto item = markets.at(row).toObject();
        const QStringList values{
            jsonText(item.value(QStringLiteral("key"))),
            jsonText(item.value(QStringLiteral("provider"))),
            jsonText(item.value(QStringLiteral("marketStatus"))),
            jsonText(item.value(QStringLiteral("tickSize"))),
            jsonText(item.value(QStringLiteral("tickValue"))),
            jsonText(item.value(QStringLiteral("resolution_price")))
        };
        for (int col = 0; col < values.size(); ++col) {
            productsTable_->setItem(row, col, new QTableWidgetItem(values.at(col)));
        }
    }
}

void MainWindow::applyOrderState(const QJsonObject& payload) {
    const auto revision = static_cast<qlonglong>(payload.value(QStringLiteral("revision")).toDouble());
    revisionStatus_->setText(QStringLiteral("Snapshot\n%1").arg(revision));

    writeRows(ordersTable_, payload.value(QStringLiteral("orders")).toArray(), {
        QStringLiteral("createdAt"),
        QStringLiteral("marketKey"),
        QStringLiteral("side"),
        QStringLiteral("price"),
        QStringLiteral("remaining"),
        QStringLiteral("status"),
        QStringLiteral("id")
    });

    QJsonArray fills;
    const auto fillsObject = payload.value(QStringLiteral("fills")).toObject();
    for (auto it = fillsObject.begin(); it != fillsObject.end(); ++it) {
        const auto arr = it.value().toArray();
        for (const auto& row : arr) fills.append(row);
    }
    writeRows(fillsTable_, fills, {
        QStringLiteral("timestamp"),
        QStringLiteral("marketKey"),
        QStringLiteral("displaySide"),
        QStringLiteral("price"),
        QStringLiteral("size"),
        QStringLiteral("orderId")
    });

    writeRows(positionsTable_, payload.value(QStringLiteral("positions")).toArray(), {
        QStringLiteral("marketKey"),
        QStringLiteral("side"),
        QStringLiteral("position"),
        QStringLiteral("avgPrice"),
        QStringLiteral("markPrice"),
        QStringLiteral("openPnl"),
        QStringLiteral("closedPnl")
    });
}

void MainWindow::applyOrderSubmit(const QJsonObject& payload) {
    appendLog(QStringLiteral("order submit: %1").arg(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))));
    client_.refreshOrderState();
}

void MainWindow::applyError(const QString& path, const QString& detail) {
    appendLog(QStringLiteral("request failed %1: %2").arg(path, detail));
}

void MainWindow::appendLog(const QString& message) {
    log_->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), message));
}

void MainWindow::rememberSymbols(const QJsonArray& markets) {
    for (const auto& market : markets) {
        const auto symbol = market.toObject().value(QStringLiteral("key")).toString();
        if (symbol.isEmpty() || knownSymbols_.contains(symbol)) continue;
        knownSymbols_.insert(symbol);
        symbolCombo_->addItem(symbol);
    }
}
