#include "DeterministicExchange.hpp"

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace cerious::exchange;

namespace {

namespace fs = std::filesystem;

std::string get_string(const std::string& json, const std::string& key, const std::string& fallback = "") {
    const auto pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return fallback;
    if (json[pos] != '"') {
        auto end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
        auto raw = json.substr(pos, end - pos);
        raw.erase(raw.find_last_not_of(" \t\r\n") + 1);
        raw.erase(0, raw.find_first_not_of(" \t\r\n"));
        return raw.empty() ? fallback : raw;
    }
    ++pos;
    std::string out;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') break;
        if (ch == '\\' && pos < json.size()) {
            const char escaped = json[pos++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(escaped); break;
            }
        } else {
            out.push_back(ch);
        }
    }
    return out.empty() ? fallback : out;
}

double get_double(const std::string& json, const std::string& key, double fallback = 0.0) {
    const auto raw = get_string(json, key, "");
    if (raw.empty()) return fallback;
    try { return std::stod(raw); } catch (...) { return fallback; }
}

std::optional<std::string> get_json_member(const std::string& json, const std::string& key) {
    const auto pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return std::nullopt;
    const char open = json[pos];
    if (open != '{' && open != '[') return std::nullopt;
    const char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = pos; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == open) ++depth;
        else if (ch == close) {
            --depth;
            if (depth == 0) return json.substr(pos, i - pos + 1);
        }
    }
    return std::nullopt;
}

std::vector<std::string> json_object_array(const std::string& array_json) {
    std::vector<std::string> objects;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    std::size_t start = std::string::npos;
    for (std::size_t i = 0; i < array_json.size(); ++i) {
        const char ch = array_json[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(array_json.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

std::vector<std::string> json_string_array(const std::string& array_json) {
    std::vector<std::string> values;
    bool in_string = false;
    bool escaped = false;
    std::string current;
    for (std::size_t i = 0; i < array_json.size(); ++i) {
        const char ch = array_json[i];
        if (!in_string) {
            if (ch == '"') {
                in_string = true;
                current.clear();
            }
            continue;
        }
        if (escaped) {
            switch (ch) {
                case '"': current.push_back('"'); break;
                case '\\': current.push_back('\\'); break;
                case 'n': current.push_back('\n'); break;
                case 'r': current.push_back('\r'); break;
                case 't': current.push_back('\t'); break;
                default: current.push_back(ch); break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            values.push_back(current);
            in_string = false;
            continue;
        }
        current.push_back(ch);
    }
    return values;
}

bool get_bool(const std::string& json, const std::string& key, bool fallback = false) {
    auto value = get_string(json, key, fallback ? "true" : "false");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value == "true" || value == "1" || value == "yes";
}

std::optional<std::string> read_text_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string env_string(const char* name);
int get_int(const std::string& json, const std::string& key, int fallback);

fs::path product_definitions_path(const fs::path& root) {
    const auto configured = env_string("CERIOUS_PRODUCT_DEFINITIONS_PATH");
    if (!configured.empty()) return fs::path(configured);
    return root / "data" / "product-definitions" / "product-definitions.json";
}

std::vector<ProductSpec> load_product_specs(const fs::path& root) {
    const auto raw = read_text_file(product_definitions_path(root));
    if (!raw) return DeterministicExchange::starter_products();
    const auto products_member = get_json_member(*raw, "products").value_or("[]");
    std::vector<ProductSpec> specs;
    for (const auto& object : json_object_array(products_member)) {
        ProductSpec spec;
        spec.symbol = upper_ascii(get_string(object, "symbol"));
        if (spec.symbol.empty()) continue;
        spec.exchange = get_string(object, "exchange", "SIM");
        spec.tick_size = get_double(object, "tickSize", get_double(object, "tick_size", 0.25));
        spec.tick_value = get_double(object, "tickValue", get_double(object, "tick_value", 1.0));
        spec.display_precision = static_cast<std::uint8_t>(std::clamp(get_int(object, "displayPrecision", get_int(object, "display_precision", 2)), 0, 12));
        spec.synthetic = get_bool(object, "synthetic", false);
        if (spec.tick_size > 0.0 && spec.tick_value > 0.0) specs.push_back(spec);
    }
    return specs.empty() ? DeterministicExchange::starter_products() : specs;
}

int get_int(const std::string& json, const std::string& key, int fallback = 0) {
    return static_cast<int>(std::llround(get_double(json, key, static_cast<double>(fallback))));
}

Milliseconds get_ms(const std::string& json, const std::string& key, Milliseconds fallback = 0) {
    const auto raw = get_string(json, key, "");
    if (raw.empty()) return fallback;
    try { return static_cast<Milliseconds>(std::stoull(raw)); } catch (...) { return fallback; }
}

Side parse_side(std::string raw) {
    std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return raw == "sell" || raw == "offer" || raw == "ask" || raw == "s" ? Side::Sell : Side::Buy;
}

OrderType parse_type(std::string raw) {
    std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return raw == "market" ? OrderType::Market : OrderType::Limit;
}

TimeInForce parse_tif(std::string raw) {
    std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (raw == "ioc") return TimeInForce::Ioc;
    if (raw == "gtc") return TimeInForce::Gtc;
    return TimeInForce::Day;
}

std::string env_string(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) == 0 && buffer != nullptr) {
        std::string value(buffer);
        std::free(buffer);
        return value;
    }
    return "";
#else
    const auto* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

void send_json(httplib::Response& res, const std::string& body, int status = 200) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body, "application/json");
}

OrderCommand parse_order(const std::string& body) {
    OrderCommand command;
    command.order_id = get_string(body, "orderId", get_string(body, "order_id"));
    command.symbol = get_string(body, "symbol", get_string(body, "marketKey"));
    command.side = parse_side(get_string(body, "side", "buy"));
    command.type = parse_type(get_string(body, "type", get_string(body, "orderType", "limit")));
    command.tif = parse_tif(get_string(body, "timeInForce", get_string(body, "tif", "day")));
    command.price = get_double(body, "price", 0.0);
    command.quantity = get_int(body, "quantity", get_int(body, "size", 0));
    command.timestamp_ms = get_ms(body, "timestampMs", 0);
    command.metadata.account = get_string(body, "account");
    command.metadata.operator_id = get_string(body, "operatorId");
    command.metadata.source = get_string(body, "source", "manual");
    command.metadata.strategy = get_string(body, "strategy", "manual");
    command.metadata.algo_id = get_string(body, "algoId");
    command.metadata.algo_name = get_string(body, "algoName");
    command.metadata.algo_role = get_string(body, "algoRole");
    command.metadata.order_tag = get_string(body, "orderTag", command.metadata.algo_id.empty() ? "MANUAL" : "ALGO");
    command.metadata.parent_order_id = get_string(body, "parentOrderId");
    command.metadata.trigger = get_string(body, "trigger");
    command.metadata.layer = get_int(body, "layer", 0);
    command.metadata.cover_ticks_from_fill = get_double(body, "coverTicksFromFill", 0.0);
    command.metadata.cover_tick_size = get_double(body, "coverTickSize", 0.0);
    return command;
}

MarketDataTick parse_market(const std::string& body) {
    MarketDataTick tick;
    tick.symbol = get_string(body, "symbol", get_string(body, "marketKey"));
    if (!get_string(body, "bestBid").empty()) tick.best_bid = get_double(body, "bestBid");
    else if (!get_string(body, "bid").empty()) tick.best_bid = get_double(body, "bid");
    if (!get_string(body, "bestAsk").empty()) tick.best_ask = get_double(body, "bestAsk");
    else if (!get_string(body, "ask").empty()) tick.best_ask = get_double(body, "ask");
    if (!get_string(body, "last").empty()) tick.last = get_double(body, "last");
    tick.last_size = get_int(body, "lastSize", 0);
    tick.timestamp_ms = get_ms(body, "timestampMs", 0);
    if (tick.timestamp_ms == 0) {
        const auto timestamp_ns = get_ms(body, "timestampNs", 0);
        if (timestamp_ns > 0) tick.timestamp_ms = timestamp_ns / 1000000ULL;
    }
    return tick;
}

std::string status_string(ExecStatus status) {
    switch (status) {
        case ExecStatus::Accepted: return "accepted";
        case ExecStatus::Resting: return "working";
        case ExecStatus::PartialFill: return "partial";
        case ExecStatus::Filled: return "filled";
        case ExecStatus::Canceled: return "cancelled";
        case ExecStatus::Rejected: return "rejected";
        case ExecStatus::Replaced: return "replaced";
    }
    return "unknown";
}

std::string side_token(Side side) {
    return side == Side::Buy ? "bid" : "offer";
}

std::string display_side(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

double finite_or_zero(double value) {
    return std::isfinite(value) ? value : 0.0;
}

class ExchangeServerState {
public:
    ExchangeServerState(std::vector<ProductSpec> products, fs::path root)
        : root_(std::move(root)) {
        exchange_.register_products(products.empty() ? DeterministicExchange::starter_products() : products);
        for (const auto& product : exchange_.products()) {
            products_[product.symbol] = product;
        }
        session_id_ = current_globex_trading_day();
        session_dir_ = root_ / "data" / "exchange" / "sessions" / session_id_;
        state_path_ = session_dir_ / "state.json";
        load_session_state();
    }

    std::string health_json() const {
        return "{\"ok\":true,\"service\":\"cerious.exchange\",\"runtime\":\"cpp\",\"products\":" + std::to_string(products_.size())
            + ",\"sessionId\":" + ExchangeJson::q(session_id_)
            + ",\"persistencePath\":" + ExchangeJson::q(state_path_.string()) + "}";
    }

    std::string products_json() const {
        return ExchangeJson::products(exchange_.products());
    }

    std::string send_order(const std::string& body) {
        const auto batch = exchange_.submit_order_batch(parse_order(body));
        apply_batch(batch);
        persist_session_state();
        return ExchangeJson::event_batch(batch);
    }

    std::string cancel_order(const std::string& body) {
        CancelCommand command;
        command.order_id = get_string(body, "orderId", get_string(body, "order_id"));
        command.reason = get_string(body, "reason", "user_cancel");
        command.timestamp_ms = get_ms(body, "timestampMs", 0);
        const auto batch = exchange_.cancel_order_batch(command);
        apply_batch(batch);
        persist_session_state();
        return ExchangeJson::event_batch(batch);
    }

    std::string replace_order(const std::string& body) {
        ReplaceCommand command;
        command.order_id = get_string(body, "orderId", get_string(body, "order_id"));
        if (!get_string(body, "price").empty()) command.price = get_double(body, "price");
        if (!get_string(body, "quantity").empty()) command.quantity = get_int(body, "quantity");
        command.timestamp_ms = get_ms(body, "timestampMs", 0);
        const auto batch = exchange_.replace_order_batch(command);
        apply_batch(batch);
        persist_session_state();
        return ExchangeJson::event_batch(batch);
    }

    std::string apply_market(const std::string& body) {
        const auto tick = parse_market(body);
        update_mark(tick);
        const auto batch = exchange_.apply_market_data_batch(tick);
        apply_batch(batch);
        if (!batch.reports.empty()) persist_session_state();
        return ExchangeJson::event_batch(batch);
    }

    std::string snapshot_json(const std::string& symbol, std::size_t levels) const {
        return ExchangeJson::snapshot(exchange_.snapshot(symbol, levels));
    }

    std::string orders_json() const {
        return ExchangeJson::working_orders(exchange_);
    }

    std::string state_json() const {
        std::ostringstream out;
        out << "{\"service\":\"cerious.exchange\""
            << ",\"fetchedAt\":" << current_ms()
            << ",\"sessionId\":" << ExchangeJson::q(session_id_)
            << ",\"persistencePath\":" << ExchangeJson::q(state_path_.string())
            << ",\"persistedAt\":" << persisted_at_ms_
            << ",\"sessionMetrics\":" << session_metrics_json()
            << ",\"simOrders\":" << working_orders_json()
            << ",\"simPositions\":" << positions_json()
            << ",\"fills\":" << fills_json()
            << ",\"simMessages\":[";
        for (std::size_t i = 0; i < messages_.size(); ++i) {
            if (i) out << ",";
            out << ExchangeJson::q(messages_[i]);
        }
        out << "]}";
        return out.str();
    }

    void reset(bool clear_fills) {
        exchange_.reset();
        messages_.push_front(clear_fills ? "Cerious Exchange reset: orders, fills, and positions cleared." : "Cerious Exchange reset: working orders cleared.");
        while (messages_.size() > 50) messages_.pop_back();
        if (clear_fills) {
            fills_.clear();
            positions_.clear();
            session_peak_pnl_ = 0.0;
            session_low_pnl_ = 0.0;
            session_max_drawdown_ = 0.0;
            session_current_pnl_ = 0.0;
            session_metrics_updated_ms_ = current_ms();
        }
        persist_session_state();
    }

private:
    struct FillState {
        std::string id;
        std::string order_id;
        std::string symbol;
        Side side = Side::Buy;
        int qty = 0;
        double price = 0.0;
        Milliseconds timestamp_ms = 0;
        OrderMetadata metadata;
    };

    struct PositionState {
        std::string symbol;
        int qty = 0;
        int buy_qty = 0;
        int sell_qty = 0;
        double avg_price = 0.0;
        double mark_price = 0.0;
        double open_pnl = 0.0;
        double realized_pnl = 0.0;
    };

    DeterministicExchange exchange_;
    fs::path root_;
    fs::path session_dir_;
    fs::path state_path_;
    std::string session_id_;
    Milliseconds persisted_at_ms_ = 0;
    std::unordered_map<std::string, ProductSpec> products_;
    std::unordered_map<std::string, double> marks_;
    std::map<std::string, PositionState> positions_;
    std::vector<FillState> fills_;
    std::deque<std::string> messages_;
    double session_peak_pnl_ = 0.0;
    double session_low_pnl_ = 0.0;
    double session_max_drawdown_ = 0.0;
    double session_current_pnl_ = 0.0;
    Milliseconds session_metrics_updated_ms_ = 0;
    Milliseconds last_metrics_persist_ms_ = 0;

    static std::string current_globex_trading_day() {
        std::time_t raw = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &raw);
#else
        localtime_r(&raw, &local);
#endif
        if (local.tm_hour >= 17) {
            raw += 24 * 60 * 60;
#ifdef _WIN32
            localtime_s(&local, &raw);
#else
            localtime_r(&raw, &local);
#endif
        }
        char buffer[16]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
        return buffer;
    }

    static Milliseconds current_ms() {
        return static_cast<Milliseconds>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    const ProductSpec& product(const std::string& symbol) const {
        static const ProductSpec fallback{"UNKNOWN", "SIM", 0.25, 1.0, 2, false};
        const auto it = products_.find(symbol);
        return it == products_.end() ? fallback : it->second;
    }

    double order_price(const Order& order) const {
        return static_cast<double>(order.price_ticks) * product(order.symbol).tick_size;
    }

    static double tick_pnl(double entry_price, double mark_price, int signed_contracts, const ProductSpec& spec) {
        if (!std::isfinite(entry_price) || !std::isfinite(mark_price) || signed_contracts == 0) return 0.0;
        if (!(spec.tick_size > 0.0) || !(spec.tick_value > 0.0)) return 0.0;
        const double ticks_moved = (mark_price - entry_price) / spec.tick_size;
        return ticks_moved * spec.tick_value * static_cast<double>(signed_contracts);
    }

    double session_total_pnl() const {
        double total = 0.0;
        for (const auto& [_, pos] : positions_) {
            total += finite_or_zero(pos.open_pnl) + finite_or_zero(pos.realized_pnl);
        }
        return total;
    }

    void record_session_pnl_sample(bool allow_persist) {
        const auto pnl = session_total_pnl();
        session_current_pnl_ = pnl;
        session_peak_pnl_ = std::max(session_peak_pnl_, pnl);
        session_low_pnl_ = std::min(session_low_pnl_, pnl);
        session_max_drawdown_ = std::max(session_max_drawdown_, std::max(0.0, -session_low_pnl_));
        session_metrics_updated_ms_ = current_ms();

        if (allow_persist && session_metrics_updated_ms_ > last_metrics_persist_ms_ + 5000) {
            last_metrics_persist_ms_ = session_metrics_updated_ms_;
            persist_session_state();
        }
    }

    void update_mark(const MarketDataTick& tick) {
        double mark = std::nan("");
        if (tick.last) mark = *tick.last;
        else if (tick.best_bid && tick.best_ask) mark = (*tick.best_bid + *tick.best_ask) / 2.0;
        else if (tick.best_bid) mark = *tick.best_bid;
        else if (tick.best_ask) mark = *tick.best_ask;
        if (!std::isfinite(mark)) return;
        marks_[tick.symbol] = mark;
        auto pos_it = positions_.find(tick.symbol);
        if (pos_it != positions_.end()) {
            pos_it->second.mark_price = mark;
            recalc_open_pnl(pos_it->second);
            record_session_pnl_sample(true);
        }
    }

    void apply_batch(const GatewayEventBatch& batch) {
        for (const auto& report : batch.reports) {
            if (report.fill_quantity <= 0) continue;
            record_fill(report);
        }
    }

    void record_fill(const ExecutionReport& report) {
        FillState fill;
        fill.id = "CERX-FILL-" + std::to_string(report.sequence);
        fill.order_id = report.order_id;
        fill.symbol = report.symbol;
        fill.side = report.side;
        fill.qty = report.fill_quantity;
        fill.price = report.execution_price;
        fill.timestamp_ms = report.timestamp_ms ? report.timestamp_ms : current_ms();
        fill.metadata = report.metadata;
        fills_.push_back(fill);
        if (fills_.size() > 5000) {
            fills_.erase(fills_.begin(), fills_.begin() + static_cast<std::ptrdiff_t>(fills_.size() - 5000));
        }
        update_position(fill);
    }

    void update_position(const FillState& fill) {
        auto& pos = positions_[fill.symbol];
        pos.symbol = fill.symbol;
        if (fill.side == Side::Buy) pos.buy_qty += fill.qty;
        else pos.sell_qty += fill.qty;

        const int signed_qty = fill.side == Side::Buy ? fill.qty : -fill.qty;
        if (pos.qty == 0 || (pos.qty > 0) == (signed_qty > 0)) {
            const int next_qty = pos.qty + signed_qty;
            pos.avg_price = next_qty == 0
                ? 0.0
                : ((pos.avg_price * std::abs(pos.qty)) + (fill.price * fill.qty)) / std::abs(next_qty);
            pos.qty = next_qty;
        } else {
            const int closing_qty = std::min(std::abs(pos.qty), std::abs(signed_qty));
            const double direction = pos.qty > 0 ? 1.0 : -1.0;
            pos.realized_pnl += tick_pnl(pos.avg_price, fill.price, static_cast<int>(closing_qty * direction), product(fill.symbol));
            const int remaining = pos.qty + signed_qty;
            if (remaining == 0) {
                pos.qty = 0;
                pos.avg_price = 0.0;
            } else if ((remaining > 0) == (pos.qty > 0)) {
                pos.qty = remaining;
            } else {
                pos.qty = remaining;
                pos.avg_price = fill.price;
            }
        }
        const auto mark_it = marks_.find(fill.symbol);
        pos.mark_price = mark_it == marks_.end() ? fill.price : mark_it->second;
        recalc_open_pnl(pos);
        record_session_pnl_sample(false);
    }

    void recalc_open_pnl(PositionState& pos) const {
        pos.open_pnl = tick_pnl(pos.avg_price, pos.mark_price, pos.qty, product(pos.symbol));
    }

    std::string fill_state_json(const FillState& fill) const {
        std::ostringstream out;
        out << "{\"id\":" << ExchangeJson::q(fill.id)
            << ",\"orderId\":" << ExchangeJson::q(fill.order_id)
            << ",\"symbol\":" << ExchangeJson::q(fill.symbol)
            << ",\"side\":" << ExchangeJson::q(fill.side == Side::Buy ? "buy" : "sell")
            << ",\"qty\":" << fill.qty
            << ",\"price\":" << fill.price
            << ",\"timestampMs\":" << fill.timestamp_ms
            << ",\"account\":" << ExchangeJson::q(fill.metadata.account)
            << ",\"operatorId\":" << ExchangeJson::q(fill.metadata.operator_id)
            << ",\"source\":" << ExchangeJson::q(fill.metadata.source)
            << ",\"strategy\":" << ExchangeJson::q(fill.metadata.strategy)
            << ",\"algoId\":" << ExchangeJson::q(fill.metadata.algo_id)
            << ",\"algoName\":" << ExchangeJson::q(fill.metadata.algo_name)
            << ",\"algoRole\":" << ExchangeJson::q(fill.metadata.algo_role)
            << ",\"orderTag\":" << ExchangeJson::q(fill.metadata.order_tag)
            << ",\"parentOrderId\":" << ExchangeJson::q(fill.metadata.parent_order_id)
            << ",\"trigger\":" << ExchangeJson::q(fill.metadata.trigger)
            << ",\"layer\":" << fill.metadata.layer
            << ",\"coverTicksFromFill\":" << fill.metadata.cover_ticks_from_fill
            << ",\"coverTickSize\":" << fill.metadata.cover_tick_size
            << "}";
        return out.str();
    }

    std::string working_order_state_json(const Order& order) const {
        std::ostringstream out;
        out << "{\"orderId\":" << ExchangeJson::q(order.id)
            << ",\"symbol\":" << ExchangeJson::q(order.symbol)
            << ",\"side\":" << ExchangeJson::q(order.side == Side::Buy ? "buy" : "sell")
            << ",\"type\":" << ExchangeJson::q(order.type == OrderType::Market ? "market" : "limit")
            << ",\"tif\":" << ExchangeJson::q(order.tif == TimeInForce::Gtc ? "gtc" : order.tif == TimeInForce::Ioc ? "ioc" : "day")
            << ",\"price\":" << order_price(order)
            << ",\"quantity\":" << order.remaining_quantity
            << ",\"timestampMs\":" << order.timestamp_ms
            << ",\"account\":" << ExchangeJson::q(order.metadata.account)
            << ",\"operatorId\":" << ExchangeJson::q(order.metadata.operator_id)
            << ",\"source\":" << ExchangeJson::q(order.metadata.source)
            << ",\"strategy\":" << ExchangeJson::q(order.metadata.strategy)
            << ",\"algoId\":" << ExchangeJson::q(order.metadata.algo_id)
            << ",\"algoName\":" << ExchangeJson::q(order.metadata.algo_name)
            << ",\"algoRole\":" << ExchangeJson::q(order.metadata.algo_role)
            << ",\"orderTag\":" << ExchangeJson::q(order.metadata.order_tag)
            << ",\"parentOrderId\":" << ExchangeJson::q(order.metadata.parent_order_id)
            << ",\"trigger\":" << ExchangeJson::q(order.metadata.trigger)
            << ",\"layer\":" << order.metadata.layer
            << ",\"coverTicksFromFill\":" << order.metadata.cover_ticks_from_fill
            << ",\"coverTickSize\":" << order.metadata.cover_tick_size
            << "}";
        return out.str();
    }

    void persist_session_state() {
        try {
            fs::create_directories(session_dir_);
            const auto now = current_ms();
            std::ostringstream out;
            out << "{\"version\":1"
                << ",\"service\":\"cerious.exchange\""
                << ",\"sessionId\":" << ExchangeJson::q(session_id_)
                << ",\"savedAt\":" << now
                << ",\"sessionMetrics\":" << session_metrics_json()
                << ",\"workingOrders\":[";
            const auto orders = exchange_.working_orders();
            for (std::size_t i = 0; i < orders.size(); ++i) {
                if (i) out << ",";
                out << working_order_state_json(orders[i]);
            }
            out << "],\"fills\":[";
            for (std::size_t i = 0; i < fills_.size(); ++i) {
                if (i) out << ",";
                out << fill_state_json(fills_[i]);
            }
            out << "],\"messages\":[";
            for (std::size_t i = 0; i < messages_.size(); ++i) {
                if (i) out << ",";
                out << ExchangeJson::q(messages_[i]);
            }
            out << "]}";

            const auto tmp = state_path_.string() + ".tmp";
            {
                std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
                file << out.str();
                file.flush();
                if (!file) return;
            }
            std::error_code ec;
            fs::rename(tmp, state_path_, ec);
            if (ec) {
                fs::remove(state_path_, ec);
                ec.clear();
                fs::rename(tmp, state_path_, ec);
            }
            if (!ec) persisted_at_ms_ = now;
        } catch (...) {
        }
    }

    void load_session_state() {
        const auto raw = read_text_file(state_path_);
        if (!raw) {
            messages_.push_front("Cerious Exchange session journal initialized: " + session_id_);
            persist_session_state();
            return;
        }
        persisted_at_ms_ = get_ms(*raw, "savedAt", 0);

        const auto metrics_member = get_json_member(*raw, "sessionMetrics").value_or("{}");
        session_peak_pnl_ = get_double(metrics_member, "sessionPeakPnl", 0.0);
        session_low_pnl_ = get_double(metrics_member, "sessionLowPnl", 0.0);
        session_max_drawdown_ = get_double(metrics_member, "maxDrawdown", 0.0);
        session_current_pnl_ = get_double(metrics_member, "currentPnl", 0.0);
        session_metrics_updated_ms_ = get_ms(metrics_member, "updatedAt", persisted_at_ms_);

        const auto orders_member = get_json_member(*raw, "workingOrders").value_or("[]");
        for (const auto& object : json_object_array(orders_member)) {
            OrderCommand command;
            command.order_id = get_string(object, "orderId");
            command.symbol = get_string(object, "symbol");
            command.side = parse_side(get_string(object, "side", "buy"));
            command.type = parse_type(get_string(object, "type", "limit"));
            command.tif = parse_tif(get_string(object, "tif", "day"));
            command.price = get_double(object, "price", 0.0);
            command.quantity = get_int(object, "quantity", 0);
            command.timestamp_ms = get_ms(object, "timestampMs", current_ms());
            command.metadata.account = get_string(object, "account");
            command.metadata.operator_id = get_string(object, "operatorId");
            command.metadata.source = get_string(object, "source", "manual");
            command.metadata.strategy = get_string(object, "strategy", "manual");
            command.metadata.algo_id = get_string(object, "algoId");
            command.metadata.algo_name = get_string(object, "algoName");
            command.metadata.algo_role = get_string(object, "algoRole");
            command.metadata.order_tag = get_string(object, "orderTag", command.metadata.algo_id.empty() ? "MANUAL" : "ALGO");
            command.metadata.parent_order_id = get_string(object, "parentOrderId");
            command.metadata.trigger = get_string(object, "trigger");
            command.metadata.layer = get_int(object, "layer", 0);
            command.metadata.cover_ticks_from_fill = get_double(object, "coverTicksFromFill", 0.0);
            command.metadata.cover_tick_size = get_double(object, "coverTickSize", 0.0);
            if (!command.order_id.empty() && !command.symbol.empty() && command.quantity > 0) {
                exchange_.submit_order_batch(command);
            }
        }

        const auto fills_member = get_json_member(*raw, "fills").value_or("[]");
        for (const auto& object : json_object_array(fills_member)) {
            FillState fill;
            fill.id = get_string(object, "id");
            fill.order_id = get_string(object, "orderId");
            fill.symbol = get_string(object, "symbol");
            fill.side = parse_side(get_string(object, "side", "buy"));
            fill.qty = get_int(object, "qty", 0);
            fill.price = get_double(object, "price", 0.0);
            fill.timestamp_ms = get_ms(object, "timestampMs", current_ms());
            fill.metadata.account = get_string(object, "account");
            fill.metadata.operator_id = get_string(object, "operatorId");
            fill.metadata.source = get_string(object, "source", "manual");
            fill.metadata.strategy = get_string(object, "strategy", "manual");
            fill.metadata.algo_id = get_string(object, "algoId");
            fill.metadata.algo_name = get_string(object, "algoName");
            fill.metadata.algo_role = get_string(object, "algoRole");
            fill.metadata.order_tag = get_string(object, "orderTag", fill.metadata.source == "algo" ? "ALGO" : "MANUAL");
            fill.metadata.parent_order_id = get_string(object, "parentOrderId");
            fill.metadata.trigger = get_string(object, "trigger");
            fill.metadata.layer = get_int(object, "layer", 0);
            fill.metadata.cover_ticks_from_fill = get_double(object, "coverTicksFromFill", 0.0);
            fill.metadata.cover_tick_size = get_double(object, "coverTickSize", 0.0);
            if (fill.id.empty()) fill.id = "CERX-FILL-RESTORED-" + std::to_string(fills_.size() + 1);
            if (!fill.symbol.empty() && fill.qty > 0) {
                fills_.push_back(fill);
                update_position(fill);
            }
        }

        const auto messages_member = get_json_member(*raw, "messages").value_or("[]");
        for (const auto& message : json_string_array(messages_member)) {
            if (!message.empty()) messages_.push_back(message);
            if (messages_.size() >= 50) break;
        }
        messages_.push_front("Cerious Exchange restored session journal: " + session_id_);
        while (messages_.size() > 50) messages_.pop_back();
        record_session_pnl_sample(false);
    }

    std::string session_metrics_json() const {
        std::ostringstream out;
        out << "{\"currentPnl\":" << finite_or_zero(session_current_pnl_)
            << ",\"sessionPeakPnl\":" << finite_or_zero(session_peak_pnl_)
            << ",\"sessionLowPnl\":" << finite_or_zero(session_low_pnl_)
            << ",\"maxDrawdown\":" << finite_or_zero(session_max_drawdown_)
            << ",\"drawdown\":" << finite_or_zero(std::max(0.0, -session_current_pnl_))
            << ",\"updatedAt\":" << session_metrics_updated_ms_
            << ",\"method\":\"server session PnL low versus zero: realized plus open PnL sampled on fills and mark updates\"}";
        return out.str();
    }

    std::string working_orders_json() const {
        const auto orders = exchange_.working_orders();
        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < orders.size(); ++i) {
            if (i) out << ",";
            const auto& order = orders[i];
            const auto& spec = product(order.symbol);
            out << "{\"id\":" << ExchangeJson::q(order.id)
                << ",\"marketKey\":" << ExchangeJson::q(order.symbol)
                << ",\"outcome\":\"yes\""
                << ",\"side\":" << ExchangeJson::q(side_token(order.side))
                << ",\"orderType\":" << ExchangeJson::q(order.type == OrderType::Market ? "market" : "limit")
                << ",\"price\":" << order_price(order)
                << ",\"size\":" << order.original_quantity
                << ",\"remaining\":" << order.remaining_quantity
                << ",\"filledSize\":" << (order.original_quantity - order.remaining_quantity)
                << ",\"matchedVolume\":" << (order.original_quantity - order.remaining_quantity)
                << ",\"status\":\"working\""
                << ",\"createdAt\":" << order.timestamp_ms
                << ",\"updatedAt\":" << order.timestamp_ms
                << ",\"operator\":\"tsturiale\""
                << ",\"source\":" << ExchangeJson::q(order.metadata.source)
                << ",\"strategy\":" << ExchangeJson::q(order.metadata.strategy)
                << ",\"legId\":" << ExchangeJson::q(order.id + "-L1")
                << ",\"orderTag\":" << ExchangeJson::q(order.metadata.order_tag)
                << ",\"algoRole\":" << ExchangeJson::q(order.metadata.algo_role)
                << ",\"algoId\":" << ExchangeJson::q(order.metadata.algo_id)
                << ",\"algoName\":" << ExchangeJson::q(order.metadata.algo_name)
                << ",\"parentOrderId\":" << ExchangeJson::q(order.metadata.parent_order_id)
                << ",\"layer\":" << order.metadata.layer
                << ",\"trigger\":" << ExchangeJson::q(order.metadata.trigger)
                << ",\"coverTicksFromFill\":" << order.metadata.cover_ticks_from_fill
                << ",\"coverTickSize\":" << order.metadata.cover_tick_size
                << ",\"tickSize\":" << spec.tick_size
                << ",\"tickValue\":" << spec.tick_value
                << "}";
        }
        out << "]";
        return out.str();
    }

    std::string fills_json() const {
        std::map<std::string, std::vector<const FillState*>> by_symbol;
        for (const auto& fill : fills_) by_symbol[fill.symbol].push_back(&fill);
        std::ostringstream out;
        out << "{";
        bool first_symbol = true;
        for (const auto& [symbol, rows] : by_symbol) {
            if (!first_symbol) out << ",";
            first_symbol = false;
            out << ExchangeJson::q(symbol) << ":[";
            const auto start = rows.size() > 250 ? rows.size() - 250 : 0U;
            for (std::size_t i = start; i < rows.size(); ++i) {
                if (i > start) out << ",";
                const auto& fill = *rows[i];
                out << "{\"timestamp\":" << fill.timestamp_ms
                    << ",\"marketKey\":" << ExchangeJson::q(fill.symbol)
                    << ",\"price\":" << fill.price
                    << ",\"size\":" << fill.qty
                    << ",\"side\":" << ExchangeJson::q(fill.side == Side::Buy ? "yes" : "no")
                    << ",\"displaySide\":" << ExchangeJson::q(display_side(fill.side))
                    << ",\"orderId\":" << ExchangeJson::q(fill.order_id)
                    << ",\"source\":" << ExchangeJson::q(fill.metadata.source)
                    << ",\"strategy\":" << ExchangeJson::q(fill.metadata.strategy)
                    << ",\"orderTag\":" << ExchangeJson::q(fill.metadata.order_tag)
                    << ",\"algoRole\":" << ExchangeJson::q(fill.metadata.algo_role)
                    << ",\"coverTicksFromFill\":" << fill.metadata.cover_ticks_from_fill
                    << ",\"coverTickSize\":" << fill.metadata.cover_tick_size
                    << "}";
            }
            out << "]";
        }
        out << "}";
        return out.str();
    }

    std::string positions_json() const {
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const auto& [symbol, pos] : positions_) {
            if (pos.qty == 0 && pos.realized_pnl == 0.0) continue;
            if (!first) out << ",";
            first = false;
            const auto& spec = product(symbol);
            const auto status = pos.qty == 0 ? "closed" : "open";
            out << "{\"id\":" << ExchangeJson::q("cerx-pos-" + symbol)
                << ",\"marketKey\":" << ExchangeJson::q(symbol)
                << ",\"outcome\":" << ExchangeJson::q(pos.qty >= 0 ? "yes" : "no")
                << ",\"size\":" << pos.qty
                << ",\"avgPrice\":" << finite_or_zero(pos.avg_price)
                << ",\"markPrice\":" << finite_or_zero(pos.mark_price)
                << ",\"openPnl\":" << finite_or_zero(pos.open_pnl)
                << ",\"realizedPnl\":" << finite_or_zero(pos.realized_pnl)
                << ",\"totalPnl\":" << finite_or_zero(pos.open_pnl + pos.realized_pnl)
                << ",\"status\":" << ExchangeJson::q(status)
                << ",\"openedAt\":0"
                << ",\"operator\":\"tsturiale\""
                << ",\"source\":\"cerious-exchange\""
                << ",\"strategy\":\"native-ledger\""
                << ",\"legId\":" << ExchangeJson::q(symbol + "-cerx-position")
                << ",\"tickSize\":" << spec.tick_size
                << ",\"tickValue\":" << spec.tick_value
                << "}";
        }
        out << "]";
        return out.str();
    }
};

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8011;
    fs::path root = fs::current_path();
    const auto env_host = env_string("CERIOUS_EXCHANGE_HOST");
    if (!env_host.empty()) host = env_host;
    const auto env_port = env_string("CERIOUS_EXCHANGE_PORT");
    if (!env_port.empty()) {
        try { port = std::stoi(env_port); } catch (...) {}
    } else {
        const auto compat_port = env_string("CERIOUS_EXCHANGE_HTTP_PORT");
        if (!compat_port.empty()) {
            try { port = std::stoi(compat_port); } catch (...) {}
        }
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            try { port = std::stoi(argv[++i]); } catch (...) {}
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--root" && i + 1 < argc) {
            root = fs::path(argv[++i]);
        }
    }

    ExchangeServerState state(load_product_specs(root), root);
    std::mutex mutex;
    std::atomic_bool shutdown_requested{false};

    httplib::Server server;

    server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.health_json());
    });

    server.Get("/products", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.products_json());
    });

    server.Post("/send", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.send_order(req.body));
    });

    server.Post("/cancel", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.cancel_order(req.body));
    });

    server.Post("/replace", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.replace_order(req.body));
    });

    server.Post("/market", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.apply_market(req.body));
    });

    server.Get(R"(/book/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        std::size_t levels = 20;
        if (req.has_param("levels")) {
            try { levels = static_cast<std::size_t>(std::clamp(std::stoi(req.get_param_value("levels")), 1, 200)); } catch (...) {}
        }
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.snapshot_json(req.matches[1].str(), levels));
    });

    server.Get("/orders", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.orders_json());
    });

    server.Get("/state", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        send_json(res, state.state_json());
    });

    server.Post("/reset", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto clear = get_string(req.body, "clearFills", "true");
        state.reset(clear != "false" && clear != "0");
        send_json(res, "{\"ok\":true,\"service\":\"cerious.exchange\",\"reset\":true,\"state\":" + state.state_json() + "}");
    });

    server.Post("/shutdown", [&](const httplib::Request&, httplib::Response& res) {
        send_json(res, "{\"ok\":true,\"service\":\"cerious.exchange\",\"shutdown\":\"requested\"}");
        shutdown_requested.store(true);
        std::thread([&server] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            server.stop();
        }).detach();
    });

    std::cerr << "cerious_exchange_server listening on " << host << ":" << port << "\n";
    server.listen(host, port);
    shutdown_requested.store(true);
    return 0;
}
