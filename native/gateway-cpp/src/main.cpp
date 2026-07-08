#include <httplib.h>

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <cmath>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
FILE* open_process_pipe(const std::string& command) {
    return _popen(command.c_str(), "r");
}

int close_process_pipe(FILE* pipe) {
    return _pclose(pipe);
}
#else
FILE* open_process_pipe(const std::string& command) {
    return popen(command.c_str(), "r");
}

int close_process_pipe(FILE* pipe) {
    return pclose(pipe);
}
#endif

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const auto ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out << "\\u"
                        << std::hex << std::uppercase
                        << static_cast<int>(static_cast<unsigned char>(ch));
                } else {
                    out << ch;
                }
        }
    }
    return out.str();
}

std::string q(const std::string& value) {
    return "\"" + json_escape(value) + "\"";
}

std::string json_number(double value, int precision = 9) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    auto text = out.str();
    while (text.find('.') != std::string::npos && !text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text.empty() ? "0" : text;
}

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream out;
    out << in.rdbuf();
    auto text = out.str();
    if (text.size() >= 3
        && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (text.rfind("\\uFEFF", 0) == 0) {
        text.erase(0, 6);
    }
    return text;
}

bool write_text(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << content;
    return static_cast<bool>(out);
}

bool write_text_atomic(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << content;
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    if (!ec) return true;
    fs::remove(path, ec);
    ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void set_env_if_missing(const std::string& name, const std::string& value) {
    if (name.empty() || value.empty()) return;
    const auto* current = std::getenv(name.c_str());
    if (current != nullptr && current[0] != '\0') return;
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 0);
#endif
}

void load_dotenv_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto name = trim_copy(line.substr(0, eq));
        auto value = trim_copy(line.substr(eq + 1));
        if (name.size() >= 3
            && static_cast<unsigned char>(name[0]) == 0xEF
            && static_cast<unsigned char>(name[1]) == 0xBB
            && static_cast<unsigned char>(name[2]) == 0xBF) {
            name.erase(0, 3);
        }
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        set_env_if_missing(name, value);
    }
}

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
        const auto first = raw.find_first_not_of(" \t\r\n");
        const auto last = raw.find_last_not_of(" \t\r\n");
        if (first == std::string::npos || last == std::string::npos) return fallback;
        return raw.substr(first, last - first + 1);
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

std::optional<std::string> get_object(const std::string& json, const std::string& key) {
    const auto pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '{') return std::nullopt;
    std::size_t start = pos;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        const char ch = json[pos];
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
        if (ch == '{') ++depth;
        if (ch == '}') {
            --depth;
            if (depth == 0) return json.substr(start, pos - start + 1);
        }
    }
    return std::nullopt;
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

    const auto start = pos;
    const char open = json[pos];
    if (open == '{' || open == '[') {
        const char close = open == '{' ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        for (; pos < json.size(); ++pos) {
            const char ch = json[pos];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\' && in_string) {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                in_string = !in_string;
                continue;
            }
            if (in_string) continue;
            if (ch == open) ++depth;
            if (ch == close) {
                --depth;
                if (depth == 0) return json.substr(start, pos - start + 1);
            }
        }
        return std::nullopt;
    }

    if (open == '"') {
        bool escaped = false;
        for (++pos; pos < json.size(); ++pos) {
            const char ch = json[pos];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') return json.substr(start, pos - start + 1);
        }
        return std::nullopt;
    }

    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') ++pos;
    auto raw = trim_copy(json.substr(start, pos - start));
    if (raw.empty()) return std::nullopt;
    return raw;
}

std::size_t json_array_count(const std::string& raw) {
    auto value = trim_copy(raw);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return 0;
    std::size_t count = 0;
    bool saw_value = false;
    int object_depth = 0;
    int array_depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        const char ch = value[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            saw_value = true;
            continue;
        }
        if (in_string) continue;
        if (ch == '{') {
            ++object_depth;
            saw_value = true;
            continue;
        }
        if (ch == '}') {
            if (object_depth > 0) --object_depth;
            continue;
        }
        if (ch == '[') {
            ++array_depth;
            saw_value = true;
            continue;
        }
        if (ch == ']') {
            if (array_depth > 0) --array_depth;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) continue;
        if (ch == ',' && object_depth == 0 && array_depth == 0) {
            if (saw_value) ++count;
            saw_value = false;
            continue;
        }
        saw_value = true;
    }
    return saw_value ? count + 1 : 0;
}

std::size_t json_key_occurrences(const std::string& raw, const std::string& key) {
    const auto needle = "\"" + key + "\"";
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = raw.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::vector<std::string> json_object_array(const std::string& raw) {
    std::vector<std::string> objects;
    auto value = trim_copy(raw);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return objects;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    std::size_t start = std::string::npos;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        const char ch = value[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
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
            continue;
        }
        if (ch == '}') {
            if (depth > 0) --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(value.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

std::optional<double> get_number(const std::string& json, const std::string& key) {
    const auto pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return std::nullopt;
    std::string raw;
    if (json[pos] == '"') {
        raw = get_string(json, key, "");
    } else {
        auto end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
        raw = json.substr(pos, end - pos);
    }
    const auto first = raw.find_first_not_of(" \t\r\n");
    const auto last = raw.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || last == std::string::npos) return std::nullopt;
    raw = raw.substr(first, last - first + 1);
    try {
        return std::stod(raw);
    } catch (...) {
        return std::nullopt;
    }
}

double csv_number(const std::string& value, double fallback = 0.0) {
    auto raw = trim_copy(value);
    raw.erase(std::remove(raw.begin(), raw.end(), '$'), raw.end());
    raw.erase(std::remove(raw.begin(), raw.end(), ','), raw.end());
    if (raw.size() >= 2 && raw.front() == '(' && raw.back() == ')') {
        raw = "-" + raw.substr(1, raw.size() - 2);
    }
    try {
        const auto parsed = std::stod(raw);
        return std::isfinite(parsed) ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::vector<std::vector<std::string>> parse_csv_table(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (ch == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                cell.push_back('"');
                ++i;
            } else if (ch == '"') {
                quoted = false;
            } else {
                cell.push_back(ch);
            }
            continue;
        }
        if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            row.push_back(cell);
            cell.clear();
        } else if (ch == '\n') {
            row.push_back(cell);
            cell.clear();
            if (!row.empty()) rows.push_back(row);
            row.clear();
        } else if (ch != '\r') {
            cell.push_back(ch);
        }
    }
    row.push_back(cell);
    if (!(row.size() == 1 && trim_copy(row[0]).empty())) rows.push_back(row);
    return rows;
}

int csv_header_index(const std::vector<std::string>& headers, std::initializer_list<const char*> names) {
    const auto normalized_header = [](std::string value) {
        value = trim_copy(std::move(value));
        std::string out;
        out.reserve(value.size());
        for (unsigned char ch : value) {
            if (ch == '_' || ch == '-' || std::isspace(ch)) continue;
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
        return out;
    };
    for (std::size_t i = 0; i < headers.size(); ++i) {
        const auto header = normalized_header(headers[i]);
        for (const auto* name : names) {
            const auto target = normalized_header(std::string(name));
            if (header == target) return static_cast<int>(i);
        }
    }
    return -1;
}

std::uint64_t parse_iso_utc_ms(const std::string& raw) {
    const auto value = trim_copy(raw);
    if (value.size() < 19) return 0;
    std::tm tm{};
    std::istringstream input(value.substr(0, 19));
    input >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (input.fail()) return 0;
#ifdef _WIN32
    const auto seconds = _mkgmtime(&tm);
#else
    const auto seconds = timegm(&tm);
#endif
    if (seconds < 0) return 0;
    std::uint64_t ms = static_cast<std::uint64_t>(seconds) * 1000ULL;
    if (value.size() > 20 && value[19] == '.') {
        std::uint64_t fraction = 0;
        std::uint64_t scale = 100;
        for (std::size_t i = 20; i < value.size() && std::isdigit(static_cast<unsigned char>(value[i])) && scale > 0; ++i) {
            fraction += static_cast<std::uint64_t>(value[i] - '0') * scale;
            scale /= 10;
        }
        ms += fraction;
    }
    return ms;
}

bool get_bool(const std::string& json, const std::string& key, bool fallback = false) {
    auto raw = trim_copy(get_string(json, key, fallback ? "true" : "false"));
    for (auto& ch : raw) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (raw == "TRUE" || raw == "1" || raw == "YES" || raw == "ON") return true;
    if (raw == "FALSE" || raw == "0" || raw == "NO" || raw == "OFF") return false;
    return fallback;
}

std::vector<std::string> get_string_array(const std::string& json, const std::string& key) {
    std::vector<std::string> values;
    const auto pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return values;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return values;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '[') return values;
    ++pos;
    while (pos < json.size()) {
        while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ',')) ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '"') {
            while (pos < json.size() && json[pos] != ',' && json[pos] != ']') ++pos;
            continue;
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
        if (!out.empty()) values.push_back(out);
    }
    return values;
}

std::uint64_t get_u64_number(const std::string& json, const std::string& key, std::uint64_t fallback = 0) {
    const auto value = get_number(json, key);
    if (!value || !std::isfinite(*value) || *value < 0) return fallback;
    return static_cast<std::uint64_t>(*value);
}

bool is_deleted_definition(const std::string& json) {
    const auto deleted = get_string(json, "deleted", "false");
    return deleted == "true" || deleted == "1";
}

std::string env_or(const char* key, const std::string& fallback) {
    if (const char* value = std::getenv(key)) {
        if (*value) return value;
    }
    return fallback;
}

std::string shell_quote(const fs::path& path) {
    return "\"" + path.string() + "\"";
}

std::string shell_quote_arg(const std::string& value) {
    return "\"" + value + "\"";
}

std::string pipe_command(const std::string& command) {
#ifdef _WIN32
    // _popen invokes a shell, but Windows path quoting with redirection is
    // fragile when the executable path contains spaces. Use a nested cmd with
    // the documented /S quote rules so the child stdout remains attached to
    // the pipe and stderr redirection still works.
    return "cmd.exe /d /s /c \"" + command + "\"";
#else
    return command;
#endif
}

std::string upper_ascii(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}

std::string lower_ascii(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

void replace_all(std::string& value, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string strip_cdata(std::string value) {
    value = trim_copy(std::move(value));
    constexpr const char* open = "<![CDATA[";
    constexpr const char* close = "]]>";
    if (value.rfind(open, 0) == 0 && value.size() >= std::strlen(open) + std::strlen(close)
        && value.substr(value.size() - std::strlen(close)) == close) {
        value = value.substr(std::strlen(open), value.size() - std::strlen(open) - std::strlen(close));
    }
    return value;
}

std::string strip_xml_tags(std::string value) {
    std::string out;
    out.reserve(value.size());
    bool in_tag = false;
    for (const char ch : value) {
        if (ch == '<') {
            in_tag = true;
            continue;
        }
        if (ch == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag) out.push_back(ch);
    }
    return trim_copy(out);
}

std::string xml_unescape(std::string value) {
    value = strip_xml_tags(strip_cdata(std::move(value)));
    replace_all(value, "&amp;", "&");
    replace_all(value, "&quot;", "\"");
    replace_all(value, "&apos;", "'");
    replace_all(value, "&#39;", "'");
    replace_all(value, "&lt;", "<");
    replace_all(value, "&gt;", ">");
    replace_all(value, "&#8217;", "'");
    replace_all(value, "&#8220;", "\"");
    replace_all(value, "&#8221;", "\"");
    replace_all(value, "&#8212;", "-");
    replace_all(value, "&#8211;", "-");
    replace_all(value, "&#x2019;", "'");
    replace_all(value, "&#x2018;", "'");
    replace_all(value, "&#x201C;", "\"");
    replace_all(value, "&#x201D;", "\"");
    replace_all(value, "&#x2014;", "-");
    replace_all(value, "&#x2013;", "-");
    replace_all(value, "&#x26;", "&");
    replace_all(value, "&#x27;", "'");
    replace_all(value, "&#xA0;", " ");
    replace_all(value, "&#038;", "&");
    return trim_copy(value);
}

std::vector<std::string> xml_blocks(const std::string& xml, const std::string& tag) {
    std::vector<std::string> blocks;
    const auto lower = lower_ascii(xml);
    const auto open = "<" + lower_ascii(tag);
    const auto close = "</" + lower_ascii(tag) + ">";
    std::size_t pos = 0;
    while (blocks.size() < 80) {
        const auto start = lower.find(open, pos);
        if (start == std::string::npos) break;
        const auto open_end = lower.find('>', start);
        if (open_end == std::string::npos) break;
        const auto end = lower.find(close, open_end + 1);
        if (end == std::string::npos) break;
        blocks.push_back(xml.substr(open_end + 1, end - open_end - 1));
        pos = end + close.size();
    }
    return blocks;
}

std::string xml_tag_value(const std::string& block, const std::string& tag) {
    const auto lower = lower_ascii(block);
    const auto open = "<" + lower_ascii(tag);
    const auto close = "</" + lower_ascii(tag) + ">";
    const auto start = lower.find(open);
    if (start == std::string::npos) return "";
    const auto open_end = lower.find('>', start);
    if (open_end == std::string::npos) return "";
    const auto end = lower.find(close, open_end + 1);
    if (end == std::string::npos) return "";
    return xml_unescape(block.substr(open_end + 1, end - open_end - 1));
}

std::vector<std::string> json_object_array_items(const std::string& raw) {
    std::vector<std::string> items;
    const auto value = trim_copy(raw);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return items;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    std::size_t start = std::string::npos;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        const char ch = value[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
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
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                items.push_back(value.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return items;
}

bool safe_news_url(const std::string& url) {
    const auto lower = lower_ascii(trim_copy(url));
    if (lower.rfind("https://", 0) != 0 && lower.rfind("http://", 0) != 0) return false;
    for (const char ch : url) {
        if (ch == '"' || ch == '\n' || ch == '\r' || ch == '<' || ch == '>') return false;
    }
    return true;
}

std::string canonical_market_symbol(const std::string& raw) {
    const auto value = upper_ascii(raw);
    if (value == "ES_NQ" || value == "YM_ES" || value == "RTY_ES") return value;
    if (value.rfind("RTY", 0) == 0) return "RTY";
    if (value.rfind("ES", 0) == 0) return "ES";
    if (value.rfind("NQ", 0) == 0) return "NQ";
    if (value.rfind("YM", 0) == 0) return "YM";
    if (value.rfind("CL", 0) == 0) return "CL";
    if (value.rfind("GC", 0) == 0) return "GC";
    if (value.rfind("ZM", 0) == 0) return "ZM";
    if (value.rfind("ZS", 0) == 0) return "ZS";
    return value;
}

std::string utc_iso(std::chrono::system_clock::time_point time) {
    const auto tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

struct ProductDef {
    std::string symbol;
    std::string exchange{"CME"};
    std::string kind{"future"};
    std::string label;
    double tick_size = 0.25;
    double tick_value = 0.0;
    int display_precision = 2;
    bool synthetic = false;
    std::string formula;
    std::string ratio_label;
    std::string left;
    std::string right;
    double coef = 1.0;
    int left_ratio = 0;
    int right_ratio = 0;
};

struct SpreadDef {
    std::string symbol;
    std::string left;
    std::string right;
    double coef = 1.0;
    std::string label;
    std::string formula;
    std::string ratio_label;
    int left_ratio = 0;
    int right_ratio = 0;
};

std::vector<ProductDef> fallback_product_definitions() {
    return {
        {"ES", "CME", "future", "E-mini S&P 500", 0.25, 12.50, 2, false},
        {"MES", "CME", "future", "Micro E-mini S&P 500", 0.25, 1.25, 2, false},
        {"NQ", "CME", "future", "E-mini Nasdaq-100", 0.25, 5.00, 2, false},
        {"MNQ", "CME", "future", "Micro E-mini Nasdaq-100", 0.25, 0.50, 2, false},
        {"YM", "CME", "future", "E-mini Dow Jones", 1.00, 5.00, 0, false},
        {"MYM", "CME", "future", "Micro E-mini Dow Jones", 1.00, 0.50, 0, false},
        {"RTY", "CME", "future", "E-mini Russell 2000", 0.10, 5.00, 2, false},
        {"M2K", "CME", "future", "Micro E-mini Russell 2000", 0.10, 0.50, 2, false},
        {"CL", "CME", "future", "Crude Oil", 0.01, 10.00, 2, false},
        {"GC", "CME", "future", "Gold", 0.10, 10.00, 1, false},
        {"ZM", "CME", "future", "Soybean Meal", 0.10, 10.00, 1, false},
        {"ZS", "CME", "future", "Soybeans", 0.25, 12.50, 2, false},
        {"ES_NQ", "CERIOUS", "synthetic-spread", "ES/NQ", 0.25, 37.50, 2, true, "ES - 0.2666667 * NQ", "3 ES : 2 NQ", "ES", "NQ", 0.2666667, 3, 2},
        {"YM_ES", "CERIOUS", "synthetic-spread", "YM/ES", 1.00, 15.00, 0, true, "YM - 6.6666667 * ES", "10 YM : 1 ES", "YM", "ES", 6.6666667, 10, 1},
        {"RTY_ES", "CERIOUS", "synthetic-spread", "RTY/ES", 0.10, 35.00, 2, true, "RTY - 0.4285714 * ES", "7 RTY : 3 ES", "RTY", "ES", 0.4285714, 7, 3},
    };
}

ProductDef fallback_product_def_for(const std::string& raw_symbol) {
    const auto symbol = canonical_market_symbol(raw_symbol);
    for (const auto& product : fallback_product_definitions()) {
        if (product.symbol == symbol) return product;
    }
    return {symbol, "UNKNOWN", "future", symbol, 0.25, 0.0, 2, false};
}

fs::path product_definitions_path() {
    const auto configured = env_or("CERIOUS_PRODUCT_DEFINITIONS_PATH", "");
    if (!configured.empty()) return fs::path(configured);
    return fs::current_path() / "data" / "product-definitions" / "product-definitions.json";
}

std::vector<ProductDef> load_product_definitions() {
    const auto path = product_definitions_path();
    const auto raw = read_text(path);
    if (!raw) return fallback_product_definitions();
    const auto products_member = get_json_member(*raw, "products").value_or("[]");
    const auto objects = json_object_array(products_member);
    std::vector<ProductDef> products;
    products.reserve(objects.size());
    for (const auto& object : objects) {
        auto symbol = canonical_market_symbol(get_string(object, "symbol", ""));
        if (symbol.empty()) continue;
        auto fallback = fallback_product_def_for(symbol);
        ProductDef def;
        def.symbol = symbol;
        def.exchange = get_string(object, "exchange", fallback.exchange);
        def.kind = get_string(object, "kind", fallback.kind);
        def.label = get_string(object, "label", fallback.label.empty() ? symbol : fallback.label);
        def.tick_size = get_number(object, "tickSize").value_or(get_number(object, "tick_size").value_or(fallback.tick_size));
        def.tick_value = get_number(object, "tickValue").value_or(get_number(object, "tick_value").value_or(fallback.tick_value));
        def.display_precision = static_cast<int>(get_number(object, "displayPrecision").value_or(get_number(object, "display_precision").value_or(fallback.display_precision)));
        def.synthetic = get_bool(object, "synthetic", fallback.synthetic || def.kind.find("synthetic") != std::string::npos);
        def.formula = get_string(object, "formula", fallback.formula);
        def.ratio_label = get_string(object, "ratio", fallback.ratio_label);
        if (const auto expression = get_json_member(object, "expression")) {
            def.left = canonical_market_symbol(get_string(*expression, "left", fallback.left));
            def.right = canonical_market_symbol(get_string(*expression, "right", fallback.right));
            def.coef = get_number(*expression, "coefficient").value_or(get_number(*expression, "coef").value_or(fallback.coef));
        } else {
            def.left = canonical_market_symbol(get_string(object, "left", fallback.left));
            def.right = canonical_market_symbol(get_string(object, "right", fallback.right));
            def.coef = get_number(object, "coefficient").value_or(get_number(object, "coef").value_or(fallback.coef));
        }
        if (const auto legs = get_json_member(object, "legs")) {
            const auto leg_objects = json_object_array(*legs);
            if (!leg_objects.empty()) {
                def.left_ratio = static_cast<int>(std::abs(get_number(leg_objects[0], "ratio").value_or(fallback.left_ratio)));
            }
            if (leg_objects.size() > 1) {
                def.right_ratio = static_cast<int>(std::abs(get_number(leg_objects[1], "ratio").value_or(fallback.right_ratio)));
            }
        }
        if (def.left_ratio <= 0) def.left_ratio = fallback.left_ratio;
        if (def.right_ratio <= 0) def.right_ratio = fallback.right_ratio;
        if (def.synthetic && def.formula.empty() && !def.left.empty() && !def.right.empty()) {
            def.formula = def.left + " - " + json_number(def.coef, 7) + " * " + def.right;
        }
        products.push_back(std::move(def));
    }
    return products.empty() ? fallback_product_definitions() : products;
}

const std::vector<ProductDef>& product_definitions() {
    static const std::vector<ProductDef> products = load_product_definitions();
    return products;
}

ProductDef product_def_for(const std::string& raw_symbol) {
    const auto symbol = canonical_market_symbol(raw_symbol);
    for (const auto& product : product_definitions()) {
        if (product.symbol == symbol) return product;
    }
    return fallback_product_def_for(symbol);
}

std::optional<SpreadDef> spread_def_for(const std::string& raw_symbol) {
    const auto symbol = canonical_market_symbol(raw_symbol);
    for (const auto& product : product_definitions()) {
        if (product.symbol != symbol || !product.synthetic || product.left.empty() || product.right.empty()) continue;
        return SpreadDef{
            product.symbol,
            product.left,
            product.right,
            product.coef,
            product.label.empty() ? product.symbol : product.label,
            product.formula,
            product.ratio_label,
            product.left_ratio,
            product.right_ratio,
        };
    }
    return std::nullopt;
}

std::vector<SpreadDef> spread_definitions() {
    std::vector<SpreadDef> spreads;
    for (const auto& product : product_definitions()) {
        if (!product.synthetic || product.left.empty() || product.right.empty()) continue;
        spreads.push_back({
            product.symbol,
            product.left,
            product.right,
            product.coef,
            product.label.empty() ? product.symbol : product.label,
            product.formula,
            product.ratio_label,
            product.left_ratio,
            product.right_ratio,
        });
    }
    return spreads;
}

int env_port(const char* key, int fallback) {
    try {
        return std::stoi(env_or(key, std::to_string(fallback)));
    } catch (...) {
        return fallback;
    }
}

std::string content_type_for(const fs::path& path) {
    const auto ext = path.extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".wasm") return "application/wasm";
    return "application/octet-stream";
}

struct Args {
    std::string host{"127.0.0.1"};
    int port{8000};
    std::string execution_host{"127.0.0.1"};
    int execution_port{8011};
    fs::path root;
};

Args parse_args(int argc, char** argv) {
    Args args;
    args.host = env_or("CERIOUS_BACKEND_HOST", "127.0.0.1");
    args.execution_host = env_or("CERIOUS_EXCHANGE_HOST", "127.0.0.1");
    args.port = env_port("CERIOUS_BACKEND_PORT", 8000);
    args.execution_port = env_port("CERIOUS_EXCHANGE_HTTP_PORT", 8011);
    args.root = fs::current_path();
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) return "";
            return argv[++i];
        };
        if (key == "--host") args.host = next();
        else if (key == "--port") args.port = std::stoi(next());
        else if (key == "--execution-host") args.execution_host = next();
        else if (key == "--execution-port") args.execution_port = std::stoi(next());
        else if (key == "--root") args.root = fs::path(next());
    }
    return args;
}

struct MarketBook {
    std::string symbol;
    double bid = std::nan("");
    double ask = std::nan("");
    double last = std::nan("");
    int bid_size = 0;
    int ask_size = 0;
    int bid_count = 0;
    int ask_count = 0;
    int last_size = 0;
    std::uint64_t sequence = 0;
    std::uint64_t ts_ms = 0;
    bool live = false;
};

struct SessionStats {
    std::string symbol;
    double open = std::nan("");
    double high = std::nan("");
    double low = std::nan("");
    double reference = std::nan("");
    double last = std::nan("");
    double net_change = std::nan("");
    double net_change_pct = std::nan("");
    std::uint64_t session_start_ms = 0;
    std::uint64_t updated_at_ms = 0;
    bool ok = false;
};

struct CachedSessionStats {
    SessionStats stats;
    std::uint64_t fetched_at_ms = 0;
};

struct MarketTrade {
    std::string symbol;
    double price = std::nan("");
    int size = 0;
    std::string side{"buy"};
    std::uint64_t ts_ms = 0;
    std::uint64_t sequence = 0;
};

struct MarketBar {
    std::uint64_t timestamp = 0;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
};

struct RegressionStudy {
    bool ok = false;
    std::string error;
    std::string symbol;
    std::string interval{"30m"};
    int lookback = 0;
    double standard_deviations = 0.0;
    int bars = 0;
    double mean = std::nan("");
    double upper = std::nan("");
    double lower = std::nan("");
    double sigma = std::nan("");
    double slope = std::nan("");
    double intercept = std::nan("");
    std::uint64_t updated_at = 0;
    bool includes_live_mark = false;
};

struct CachedRegressionStudy {
    RegressionStudy study;
    std::uint64_t fetched_at_ms = 0;
};

struct CachedMarketBars {
    std::vector<MarketBar> bars;
    std::uint64_t fetched_at_ms = 0;
};

struct AlgoCoverPolicy {
    std::string symbol;
    std::string strategy;
    std::string algo_id;
    std::string algo_name;
    int layer = 0;
    int quantity = 0;
    double cover_ticks = 0.0;
    double tick_size = 0.0;
};

struct CeriousAdvisorySnapshot {
    std::uint64_t fetched_at_ms = 0;
    std::uint64_t next_due_ms = 0;
    std::uint64_t persisted_at_ms = 0;
    bool ready = false;
    std::string intelligence;
    std::string daily_summary;
    std::string macro_regime;
    std::string opportunity_map;
};

struct NewsSource {
    std::string id;
    std::string name;
    std::string url;
    std::string category;
};

struct NewsHeadline {
    std::string id;
    std::string source;
    std::string title;
    std::string link;
    std::string pub_date;
    std::string description;
    std::string urgency{"normal"};
    std::string bias{"mixed"};
};

struct EconomicCalendarEvent {
    std::string id;
    std::string ticker;
    std::string event;
    std::string category;
    std::string date_time;
    std::string date_label;
    std::string time_label;
    std::string actual;
    std::string forecast;
    std::string previous;
    std::string reference;
    std::string importance{"low"};
};

struct Gateway {
    Args args;
    fs::path dist;
    fs::path data;
    std::atomic<bool> shutdown_requested{false};
    std::atomic<bool> market_data_running{false};
    mutable std::mutex market_mutex;
    mutable std::mutex study_cache_mutex;
    mutable std::mutex study_warmup_mutex;
    mutable std::mutex history_cache_mutex;
    mutable std::mutex session_stats_mutex;
    std::unordered_map<std::string, MarketBook> market_books;
    std::unordered_map<std::string, std::deque<MarketTrade>> market_trades;
    mutable std::unordered_map<std::string, CachedRegressionStudy> regression_study_cache;
    mutable std::unordered_set<std::string> regression_study_warmups;
    mutable std::unordered_map<std::string, CachedMarketBars> history_bars_cache;
    mutable std::unordered_map<std::string, CachedSessionStats> session_stats_cache;
    mutable std::mutex algo_cover_mutex;
    mutable std::unordered_map<std::string, AlgoCoverPolicy> algo_cover_policies;
    mutable std::unordered_set<std::string> processed_sim_fill_events;
    mutable std::unordered_map<std::string, int> covered_algo_entry_qty;
    mutable std::mutex cerious_advisory_mutex;
    mutable std::optional<CeriousAdvisorySnapshot> cerious_advisory_cache;
    mutable bool cerious_advisory_refreshing = false;
    mutable bool cerious_advisory_scheduler_started = false;
    mutable std::mutex news_cache_mutex;
    mutable std::string news_cache_json;
    mutable std::uint64_t news_cache_ms = 0;
    mutable std::mutex economic_calendar_cache_mutex;
    mutable std::string economic_calendar_cache_json;
    mutable std::uint64_t economic_calendar_cache_ms = 0;
    std::string market_data_status{"not-started"};
    std::string market_data_error;
    std::string market_data_detail;
    std::uint64_t market_data_last_status_ms = 0;
    std::uint64_t market_data_last_heartbeat_ms = 0;
    std::uint64_t market_data_last_record_ms = 0;
    int market_data_subscription_acks = 0;
    int market_data_mappings = 0;
    int market_data_definitions = 0;
    int market_data_records = 0;
    std::thread market_data_thread;
    FILE* market_data_pipe = nullptr;

    explicit Gateway(Args next)
        : args(std::move(next)),
          dist(args.root / "apps" / "terminal" / "dist"),
          data(args.root / "data") {}

    ~Gateway() {
        stop_market_data();
    }

    std::string session_token() const {
        return "cerious-local-cpp-" + std::to_string(now_ms());
    }

    std::string portal_username() const {
        return env_or("CERIOUS_PORTAL_USERNAME", "tsturiale");
    }

    std::string portal_password() const {
        return env_or("CERIOUS_PORTAL_PASSWORD", "");
    }

    std::string admin_username() const {
        return env_or("CERIOUS_ADMIN_USERNAME", "ADMIN");
    }

    std::string admin_password() const {
        return env_or("CERIOUS_ADMIN_PASSWORD", "12345678");
    }

    bool auth_pair_matches(const std::string& username, const std::string& password,
                           const std::string& expected_username, const std::string& expected_password) const {
        const auto clean_username = upper_ascii(trim_copy(username));
        const auto clean_expected_username = upper_ascii(trim_copy(expected_username));
        const auto clean_password = trim_copy(password);
        const auto clean_expected_password = trim_copy(expected_password);
        return !clean_expected_username.empty()
            && !clean_expected_password.empty()
            && clean_username == clean_expected_username
            && clean_password == clean_expected_password;
    }

    bool valid_login(const std::string& username, const std::string& password) const {
        return auth_pair_matches(username, password, portal_username(), portal_password())
            || auth_pair_matches(username, password, admin_username(), admin_password());
    }

    std::string auth_success_json(const std::string& username) const {
        const auto token = session_token();
        return "{\"ok\":true,\"username\":" + q(trim_copy(username))
            + ",\"sessionToken\":" + q(token)
            + ",\"expiresAt\":" + std::to_string(now_ms() + 86400000ULL) + "}";
    }

    fs::path price_feed_exe() const {
        auto path = args.root / "native" / "price-feed-cpp" / "build" / "cerious_price_feed.exe";
        if (fs::exists(path)) return path;
        return args.root / "native" / "price-feed-cpp" / "build" / "cerious_price_feed";
    }

    void cleanup_orphan_price_feed_processes() const {
#ifdef _WIN32
        // Forced service restarts can leave the Databento feed child alive.
        // Start each gateway-owned market data session from a single clean feed.
        std::system("taskkill /F /IM cerious_price_feed.exe >nul 2>nul");
#endif
    }

    fs::path price_history_exe() const {
        auto path = args.root / "native" / "price-feed-cpp" / "build" / "cerious_price_history.exe";
        if (fs::exists(path)) return path;
        return args.root / "native" / "price-feed-cpp" / "build" / "cerious_price_history";
    }

    std::string market_data_symbols() const {
        return env_or("CERIOUS_PRICE_FEED_SYMBOLS", "ES.v.0,NQ.v.0,YM.v.0,RTY.v.0,CL.v.0,GC.v.0,ZM.v.0,ZS.v.0");
    }

    std::string market_data_stype() const {
        return env_or("CERIOUS_PRICE_FEED_STYPE", "continuous");
    }

    std::string market_data_stale_ms() const {
        return env_or("CERIOUS_PRICE_FEED_STALE_MS", "30000");
    }

    std::string market_data_reconnect_ms() const {
        return env_or("CERIOUS_PRICE_FEED_RECONNECT_MS", "5000");
    }

    std::string market_data_max_reconnect_ms() const {
        return env_or("CERIOUS_PRICE_FEED_MAX_RECONNECT_MS", "60000");
    }

    void start_market_data() {
        if (env_or("CERIOUS_MARKET_DATA_ENABLED", "1") == "0") {
            std::lock_guard<std::mutex> lock(market_mutex);
            market_data_status = "disabled";
            market_data_error.clear();
            return;
        }
        if (market_data_running.exchange(true)) return;
        const auto exe = price_feed_exe();
        if (!fs::exists(exe)) {
            std::lock_guard<std::mutex> lock(market_mutex);
            market_data_status = "unavailable";
            market_data_error = "native price feed executable not found";
            market_data_running.store(false);
            return;
        }
        if (env_or("DATABENTO_API_KEY", "").empty()) {
            std::lock_guard<std::mutex> lock(market_mutex);
            market_data_status = "unavailable";
            market_data_error = "DATABENTO_API_KEY is not configured";
            market_data_running.store(false);
            return;
        }
        cleanup_orphan_price_feed_processes();

        market_data_thread = std::thread([this, exe]() {
            const auto log_dir = data / "logs";
            std::error_code ec;
            fs::create_directories(log_dir, ec);
            const auto command = pipe_command(shell_quote(exe)
                + " --symbols " + shell_quote_arg(market_data_symbols())
                + " --stype " + shell_quote_arg(market_data_stype())
                + " --stale-ms " + shell_quote_arg(market_data_stale_ms())
                + " --reconnect-ms " + shell_quote_arg(market_data_reconnect_ms())
                + " --max-reconnect-ms " + shell_quote_arg(market_data_max_reconnect_ms())
                + " 2>&1");
            {
                std::lock_guard<std::mutex> lock(market_mutex);
                market_data_status = "starting";
                market_data_error.clear();
                market_data_detail.clear();
                market_data_last_status_ms = now_ms();
            }
            FILE* pipe = open_process_pipe(command);
            market_data_pipe = pipe;
            if (!pipe) {
                std::lock_guard<std::mutex> lock(market_mutex);
                market_data_status = "unavailable";
                market_data_error = "failed to start native price feed";
                market_data_last_status_ms = now_ms();
                market_data_running.store(false);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(market_mutex);
                market_data_status = "process-running";
                market_data_last_status_ms = now_ms();
            }

            char buffer[8192];
            while (market_data_running.load() && std::fgets(buffer, sizeof(buffer), pipe)) {
                ingest_market_data_line(std::string(buffer));
            }
            close_process_pipe(pipe);
            market_data_pipe = nullptr;
            market_data_running.store(false);
            std::lock_guard<std::mutex> lock(market_mutex);
            if (market_data_status != "error") market_data_status = "stopped";
            market_data_last_status_ms = now_ms();
        });
        market_data_thread.detach();
    }

    void stop_market_data() {
        market_data_running.store(false);
    }

    static bool finite(double value) {
        return std::isfinite(value);
    }

    static double mid_or_last(const MarketBook& book) {
        if (finite(book.last)) return book.last;
        if (finite(book.bid) && finite(book.ask)) return (book.bid + book.ask) / 2.0;
        if (finite(book.bid)) return book.bid;
        if (finite(book.ask)) return book.ask;
        return std::nan("");
    }

    bool ingest_market_status_line(const std::string& line) {
        if (line.find("\"type\":\"market.status\"") == std::string::npos) return false;
        const auto status = get_string(line, "status", "system");
        const auto detail = get_string(line, "detail", "");
        const auto ts_ms = get_u64_number(line, "tsMs", now_ms());
        std::lock_guard<std::mutex> lock(market_mutex);
        market_data_status = status;
        market_data_detail = detail;
        market_data_last_status_ms = ts_ms ? ts_ms : now_ms();
        if (status == "heartbeat") {
            market_data_last_heartbeat_ms = market_data_last_status_ms;
        } else if (status == "subscription_ack") {
            ++market_data_subscription_acks;
        } else if (status == "symbol_mapping") {
            market_data_mappings = std::max(market_data_mappings, static_cast<int>(get_number(line, "mappings").value_or(market_data_mappings + 1)));
        } else if (status == "definition") {
            market_data_definitions = std::max(market_data_definitions, static_cast<int>(get_number(line, "definitions").value_or(market_data_definitions + 1)));
        } else if (status == "record") {
            market_data_records = std::max(market_data_records, static_cast<int>(get_number(line, "records").value_or(market_data_records + 1)));
            market_data_last_record_ms = market_data_last_status_ms;
        } else if (status == "error") {
            market_data_error = detail;
        } else if (status == "reconnecting") {
            market_data_error = detail;
        }
        if (status != "error" && status != "reconnecting" && status != "slow_reader_warning") {
            market_data_error.clear();
        }
        return true;
    }

    void ingest_market_data_line(const std::string& line) {
        if (ingest_market_status_line(line)) return;
        if (line.find("\"type\":\"market.mbp1\"") == std::string::npos) return;
        const auto raw_symbol = get_string(line, "symbol", "");
        const auto symbol = canonical_market_symbol(raw_symbol);
        if (symbol.empty()) return;
        auto bid = get_number(line, "bid").value_or(std::nan(""));
        auto ask = get_number(line, "ask").value_or(std::nan(""));
        const auto price = get_number(line, "price").value_or(std::nan(""));
        const auto action = get_string(line, "action", "");
        const auto side_code = get_string(line, "side", "");
        const auto size = static_cast<int>(get_number(line, "size").value_or(0));
        const auto bid_size = static_cast<int>(get_number(line, "bidSize").value_or(0));
        const auto ask_size = static_cast<int>(get_number(line, "askSize").value_or(0));
        const auto bid_count = static_cast<int>(get_number(line, "bidCount").value_or(0));
        const auto ask_count = static_cast<int>(get_number(line, "askCount").value_or(0));
        const auto sequence = get_u64_number(line, "sequence", now_ms());
        const auto ts_ns = get_u64_number(line, "tsEventNs", now_ms() * 1000000ULL);
        const auto ts_ms = ts_ns / 1000000ULL;

        std::vector<MarketBook> sim_updates;
        {
            std::lock_guard<std::mutex> lock(market_mutex);
            ++market_data_records;
            market_data_last_record_ms = now_ms();
            if (market_data_status != "error" && market_data_status != "reconnecting") {
                market_data_status = "streaming";
                market_data_detail = "MBP-1 records received";
                market_data_error.clear();
            }
            auto& book = market_books[symbol];
            if (book.symbol.empty()) book.symbol = symbol;
            if (finite(bid)) book.bid = bid;
            if (finite(ask)) book.ask = ask;
            if (bid_size >= 0) book.bid_size = bid_size;
            if (ask_size >= 0) book.ask_size = ask_size;
            if (bid_count >= 0) book.bid_count = bid_count;
            if (ask_count >= 0) book.ask_count = ask_count;
            book.sequence = sequence;
            book.ts_ms = ts_ms ? ts_ms : now_ms();
            book.live = true;
            if ((action == "T" || action == "F") && finite(price)) {
                book.last = price;
                book.last_size = std::max(0, size);
                MarketTrade trade;
                trade.symbol = symbol;
                trade.price = price;
                trade.size = std::max(0, size);
                trade.side = side_code == "A" ? "buy" : "sell";
                trade.ts_ms = book.ts_ms;
                trade.sequence = sequence;
                auto& tape = market_trades[symbol];
                tape.push_back(trade);
                while (tape.size() > 200) tape.pop_front();
            }
            if (finite(book.bid) && finite(book.ask)) {
                sim_updates.push_back(book);
            }
            for (const auto& spread : spread_definitions()) {
                if (spread.left != symbol && spread.right != symbol) continue;
                const auto spread_book = book_unlocked(spread.symbol);
                if (spread_book && finite(spread_book->bid) && finite(spread_book->ask)) {
                    sim_updates.push_back(*spread_book);
                }
            }
        }

        for (const auto& update : sim_updates) {
            publish_market_to_execution_exchange(update);
        }
    }

    void publish_market_to_execution_exchange(const MarketBook& book) const {
        std::ostringstream body;
        body << std::fixed << std::setprecision(9)
             << "{\"symbol\":" << q(book.symbol)
             << ",\"bid\":" << book.bid
             << ",\"ask\":" << book.ask
             << ",\"bidSize\":" << book.bid_size
             << ",\"askSize\":" << book.ask_size;
        if (finite(book.last)) {
            body << ",\"last\":" << book.last
                 << ",\"lastSize\":" << book.last_size;
        }
        body << ",\"sequence\":" << book.sequence
             << ",\"timestampNs\":" << (book.ts_ms * 1000000ULL)
             << "}";
        auto result = execution_post("/market", body.str());
        if (result && result->status >= 200 && result->status < 300) {
            process_exchange_fill_events(result->body);
        }
    }

    std::optional<MarketBook> book_unlocked(const std::string& raw_symbol) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        if (const auto spread = spread_def_for(symbol)) {
            const auto left_it = market_books.find(spread->left);
            const auto right_it = market_books.find(spread->right);
            if (left_it == market_books.end() || right_it == market_books.end()) return std::nullopt;
            const auto& left = left_it->second;
            const auto& right = right_it->second;
            if (!finite(left.bid) || !finite(left.ask) || !finite(right.bid) || !finite(right.ask)) return std::nullopt;
            MarketBook out;
            out.symbol = spread->symbol;
            out.bid = left.bid - spread->coef * right.ask;
            out.ask = left.ask - spread->coef * right.bid;
            if (finite(out.bid) && finite(out.ask)) out.last = (out.bid + out.ask) / 2.0;
            out.bid_size = std::min(left.bid_size, right.ask_size);
            out.ask_size = std::min(left.ask_size, right.bid_size);
            out.bid_count = std::min(left.bid_count, right.ask_count);
            out.ask_count = std::min(left.ask_count, right.bid_count);
            out.last_size = std::min(left.last_size, right.last_size);
            out.sequence = std::max(left.sequence, right.sequence);
            out.ts_ms = std::max(left.ts_ms, right.ts_ms);
            out.live = left.live && right.live;
            return out;
        }
        const auto it = market_books.find(symbol);
        if (it == market_books.end()) return std::nullopt;
        return it->second;
    }

    std::optional<MarketBook> current_book(const std::string& raw_symbol) const {
        std::lock_guard<std::mutex> lock(market_mutex);
        return book_unlocked(raw_symbol);
    }

    std::string market_book_json(const MarketBook& book) const {
        const auto def = product_def_for(book.symbol);
        const auto mid = (book.bid + book.ask) / 2.0;
        const auto spread = book.ask - book.bid;
        const auto session = session_stats_for_book(book);
        std::string market_status_detail;
        const auto market_status = market_status_for_book(std::optional<MarketBook>{book}, market_status_detail);
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\"ok\":true,\"symbol\":" << q(book.symbol)
            << ",\"venue\":\"CME\",\"source\":\"databento-live-cpp\""
            << ",\"live\":true"
            << ",\"marketStatus\":" << q(market_status)
            << ",\"marketStatusDetail\":" << q(market_status_detail)
            << ",\"synthetic\":" << (spread_def_for(book.symbol) ? "true" : "false")
            << ",\"bids\":[{\"price\":" << book.bid << ",\"size\":" << book.bid_size << ",\"count\":" << book.bid_count << "}]"
            << ",\"asks\":[{\"price\":" << book.ask << ",\"size\":" << book.ask_size << ",\"count\":" << book.ask_count << "}]"
            << ",\"bestBid\":" << book.bid
            << ",\"bestAsk\":" << book.ask
            << ",\"bidSize\":" << book.bid_size
            << ",\"askSize\":" << book.ask_size
            << ",\"bidCount\":" << book.bid_count
            << ",\"askCount\":" << book.ask_count
            << ",\"mid\":" << mid
            << ",\"spread\":" << spread;
        if (finite(book.last)) {
            out << ",\"ltp\":" << book.last
                << ",\"ltpSize\":" << book.last_size
                << ",\"ltpSource\":" << q(spread_def_for(book.symbol) ? "synthetic_mid" : "mbp1_trade");
        }
        if (session.ok) {
            out << ",\"sessionOpen\":" << json_number(session.open)
                << ",\"sessionHigh\":" << json_number(session.high)
                << ",\"sessionLow\":" << json_number(session.low)
                << ",\"sessionReference\":" << json_number(session.reference)
                << ",\"sessionLast\":" << json_number(session.last)
                << ",\"netChange\":" << json_number(session.net_change)
                << ",\"netChangePct\":" << json_number(session.net_change_pct)
                << ",\"sessionStartMs\":" << session.session_start_ms
                << ",\"sessionStatsMs\":" << session.updated_at_ms;
        }
        out << ",\"tsMs\":" << book.ts_ms
            << ",\"sequence\":" << book.sequence
            << ",\"tickSize\":" << def.tick_size
            << ",\"tickValue\":" << def.tick_value
            << "}";
        return out.str();
    }

    std::string market_session_scaffold_json(const std::string& raw_symbol) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        const auto def = product_def_for(symbol);
        MarketBook seed;
        seed.symbol = symbol;
        seed.ts_ms = now_ms();
        const auto session = session_stats_for_book(seed);
        const auto mark = finite(session.last) ? session.last : std::nan("");
        std::string market_status_detail;
        const auto market_status = market_status_for_book(std::nullopt, market_status_detail);
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\"ok\":true,\"symbol\":" << q(symbol)
            << ",\"venue\":\"CME\",\"source\":\"databento-history-rest-session\""
            << ",\"live\":false"
            << ",\"marketStatus\":" << q(market_status)
            << ",\"marketStatusDetail\":" << q(market_status_detail)
            << ",\"synthetic\":" << (spread_def_for(symbol) ? "true" : "false")
            << ",\"bids\":[],\"asks\":[]"
            << ",\"mid\":" << json_number(mark)
            << ",\"ltp\":" << json_number(mark)
            << ",\"ltpSource\":\"session_history\"";
        if (session.ok) {
            out << ",\"sessionOpen\":" << json_number(session.open)
                << ",\"sessionHigh\":" << json_number(session.high)
                << ",\"sessionLow\":" << json_number(session.low)
                << ",\"sessionReference\":" << json_number(session.reference)
                << ",\"sessionLast\":" << json_number(session.last)
                << ",\"netChange\":" << json_number(session.net_change)
                << ",\"netChangePct\":" << json_number(session.net_change_pct)
                << ",\"sessionStartMs\":" << session.session_start_ms
                << ",\"sessionStatsMs\":" << session.updated_at_ms;
        }
        out << ",\"tsMs\":" << (session.updated_at_ms ? session.updated_at_ms : seed.ts_ms)
            << ",\"sequence\":0"
            << ",\"tickSize\":" << def.tick_size
            << ",\"tickValue\":" << def.tick_value
            << ",\"detail\":\"waiting for first live MBP-1 book; session range seeded from REST bars\""
            << "}";
        return out.str();
    }

    std::string market_status_for_book(const std::optional<MarketBook>& book, std::string& detail) const {
        const auto current_ms = now_ms();
        if (book && finite(book->bid) && finite(book->ask)) {
            const auto age_ms = book->ts_ms > 0 && current_ms >= book->ts_ms ? current_ms - book->ts_ms : 0;
            if (book->ts_ms > 0 && age_ms > 180000ULL) {
                detail = "last live book is stale";
                return "STALE";
            }
            detail = "live top-of-book available";
            return "OPEN";
        }

        std::string status;
        std::string error;
        std::string feed_detail;
        std::uint64_t last_status_ms = 0;
        std::uint64_t last_heartbeat_ms = 0;
        std::uint64_t last_record_ms = 0;
        int subscription_acks = 0;
        int mappings = 0;
        int definitions = 0;
        int records = 0;
        {
            std::lock_guard<std::mutex> lock(market_mutex);
            status = market_data_status;
            error = market_data_error;
            feed_detail = market_data_detail;
            last_status_ms = market_data_last_status_ms;
            last_heartbeat_ms = market_data_last_heartbeat_ms;
            last_record_ms = market_data_last_record_ms;
            subscription_acks = market_data_subscription_acks;
            mappings = market_data_mappings;
            definitions = market_data_definitions;
            records = market_data_records;
        }

        const bool running = market_data_running.load();
        const bool subscribed = subscription_acks > 0;
        const auto recent_signal_ms = std::max({last_status_ms, last_heartbeat_ms, last_record_ms});
        const bool recent_signal = recent_signal_ms > 0 && current_ms >= recent_signal_ms
            && (current_ms - recent_signal_ms) < 180000ULL;
        const bool feed_connected = running
            && status != "error"
            && status != "unavailable"
            && status != "disabled"
            && (subscribed || mappings > 0 || definitions > 0 || records > 0 || recent_signal);

        if (!running || status == "disabled" || status == "unavailable") {
            detail = error.empty() ? "market data service is not connected" : error;
            return "WAITING";
        }
        if (status == "error") {
            detail = error.empty() ? "market data service error" : error;
            return "STALE";
        }
        if (feed_connected && subscribed && recent_signal) {
            detail = "feed is subscribed and heartbeating; no live book is currently published";
            return "CLOSED";
        }
        detail = feed_detail.empty() ? "market data service is connecting" : feed_detail;
        return "WAITING";
    }

    std::string market_trades_json(const std::string& raw_symbol) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        const auto def = product_def_for(symbol);
        std::deque<MarketTrade> tape;
        {
            std::lock_guard<std::mutex> lock(market_mutex);
            if (const auto spread = spread_def_for(symbol)) {
                if (const auto book = book_unlocked(symbol); book && finite(book->last)) {
                    MarketTrade synthetic;
                    synthetic.symbol = symbol;
                    synthetic.price = book->last;
                    synthetic.size = std::max(0, book->last_size);
                    synthetic.side = "buy";
                    synthetic.ts_ms = book->ts_ms;
                    synthetic.sequence = book->sequence;
                    tape.push_back(synthetic);
                }
            } else {
                const auto it = market_trades.find(symbol);
                if (it != market_trades.end()) tape = it->second;
            }
        }
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\"ok\":true,\"symbol\":" << q(symbol) << ",\"trades\":[";
        for (std::size_t i = 0; i < tape.size(); ++i) {
            const auto& trade = tape[i];
            if (i) out << ",";
            out << "{\"symbol\":" << q(symbol)
                << ",\"venue\":\"CME\",\"source\":\"databento-live-cpp\""
                << ",\"timestamp\":" << trade.ts_ms
                << ",\"price\":" << trade.price
                << ",\"size\":" << trade.size
                << ",\"side\":" << q(trade.side)
                << ",\"bestBid\":null,\"bestAsk\":null"
                << ",\"tickSize\":" << def.tick_size
                << ",\"tickValue\":" << def.tick_value
                << "}";
        }
        out << "]}";
        return out.str();
    }

    std::string market_catalog_json() const {
        const auto products = product_definitions();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\"ok\":true,\"runtime\":\"cpp\",\"source\":\"gateway.product_definitions\",\"markets\":[";
        for (std::size_t i = 0; i < products.size(); ++i) {
            const auto& def = products[i];
            const auto book = current_book(def.symbol);
            const auto spot = book ? mid_or_last(*book) : std::numeric_limits<double>::quiet_NaN();
            std::string market_status_detail;
            const auto market_status = market_status_for_book(book, market_status_detail);
            if (i) out << ",";
            out << "{\"key\":" << q(def.symbol)
                << ",\"asset\":" << q(def.symbol)
                << ",\"title\":" << q(def.label.empty() ? def.symbol : def.label)
                << ",\"provider\":" << q(lower_ascii(def.exchange == "CERIOUS" ? std::string("cme") : def.exchange))
                << ",\"timeframe\":\"live\""
                << ",\"question\":" << q(def.exchange + " " + def.symbol)
                << ",\"up_pct\":0"
                << ",\"down_pct\":0"
                << ",\"volume\":0"
                << ",\"expiry_ts\":0"
                << ",\"live\":true"
                << ",\"marketStatus\":" << q(market_status)
                << ",\"marketStatusDetail\":" << q(market_status_detail)
                << ",\"last_update_ms\":" << (book ? book->ts_ms : 0)
                << ",\"tickSize\":" << def.tick_size
                << ",\"tickValue\":" << def.tick_value
                << ",\"displayPrecision\":" << def.display_precision
                << ",\"synthetic\":" << (def.synthetic ? "true" : "false")
                << ",\"productKind\":" << q(def.kind);
            if (finite(spot)) {
                out << ",\"price_to_beat\":" << spot
                    << ",\"start_price\":" << spot
                    << ",\"resolution_price\":" << spot;
            }
            out << "}";
        }
        out << "]}";
        return out.str();
    }

    std::string product_definitions_json() const {
        const auto products = product_definitions();
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\"ok\":true,\"service\":\"cerious.product-library\",\"runtime\":\"cpp\",\"source\":\""
            << json_escape(product_definitions_path().string())
            << "\",\"products\":[";
        for (std::size_t i = 0; i < products.size(); ++i) {
            const auto& def = products[i];
            if (i) out << ",";
            out << "{\"symbol\":" << q(def.symbol)
                << ",\"exchange\":" << q(def.exchange)
                << ",\"kind\":" << q(def.kind)
                << ",\"label\":" << q(def.label.empty() ? def.symbol : def.label)
                << ",\"tickSize\":" << json_number(def.tick_size)
                << ",\"tickValue\":" << json_number(def.tick_value, 2)
                << ",\"displayPrecision\":" << def.display_precision
                << ",\"synthetic\":" << (def.synthetic ? "true" : "false");
            if (def.synthetic) {
                out << ",\"formula\":" << q(def.formula)
                    << ",\"ratio\":" << q(def.ratio_label)
                    << ",\"expression\":{\"left\":" << q(def.left)
                    << ",\"right\":" << q(def.right)
                    << ",\"coefficient\":" << json_number(def.coef, 7)
                    << "}"
                    << ",\"legs\":["
                    << "{\"symbol\":" << q(def.left) << ",\"side\":1,\"ratio\":" << def.left_ratio << "},"
                    << "{\"symbol\":" << q(def.right) << ",\"side\":-1,\"ratio\":" << def.right_ratio << "}"
                    << "]";
            }
            out << "}";
        }
        out << "]}";
        return out.str();
    }

    std::string market_data_status_json() const {
        const auto current_ms = now_ms();
        std::string status;
        std::string error;
        std::string detail;
        std::uint64_t last_status_ms = 0;
        std::uint64_t last_heartbeat_ms = 0;
        std::uint64_t last_record_ms = 0;
        int subscription_acks = 0;
        int mappings = 0;
        int definitions = 0;
        int records = 0;
        std::vector<std::string> symbols;
        {
            std::lock_guard<std::mutex> lock(market_mutex);
            status = market_data_status;
            error = market_data_error;
            detail = market_data_detail;
            last_status_ms = market_data_last_status_ms;
            last_heartbeat_ms = market_data_last_heartbeat_ms;
            last_record_ms = market_data_last_record_ms;
            subscription_acks = market_data_subscription_acks;
            mappings = market_data_mappings;
            definitions = market_data_definitions;
            records = market_data_records;
            symbols.reserve(market_books.size());
            for (const auto& [symbol, book] : market_books) {
                if (finite(book.bid) && finite(book.ask)) symbols.push_back(symbol);
            }
        }

        const bool running = market_data_running.load();
        const bool subscribed = subscription_acks > 0;
        const auto recent_signal_ms = std::max({last_status_ms, last_heartbeat_ms, last_record_ms});
        const bool recent_signal = recent_signal_ms > 0 && current_ms >= recent_signal_ms
            && (current_ms - recent_signal_ms) < 180000ULL;
        const bool connected = running
            && status != "error"
            && status != "unavailable"
            && status != "disabled"
            && (subscribed || mappings > 0 || definitions > 0 || records > 0 || recent_signal);
        const bool heartbeat_ok = connected
            && (last_heartbeat_ms > 0 || last_record_ms > 0 || subscribed)
            && recent_signal;
        const bool price_ready = !symbols.empty();

        std::ostringstream out;
        out << "{\"ok\":true"
            << ",\"provider\":\"databento\""
            << ",\"dataset\":\"GLBX.MDP3\""
            << ",\"schema\":\"mbp-1\""
            << ",\"status\":" << q(status)
            << ",\"detail\":" << q(detail)
            << ",\"running\":" << (running ? "true" : "false")
            << ",\"connected\":" << (connected ? "true" : "false")
            << ",\"subscribed\":" << (subscribed ? "true" : "false")
            << ",\"heartbeatOk\":" << (heartbeat_ok ? "true" : "false")
            << ",\"priceReady\":" << (price_ready ? "true" : "false")
            << ",\"subscriptionAcks\":" << subscription_acks
            << ",\"mappings\":" << mappings
            << ",\"definitions\":" << definitions
            << ",\"records\":" << records
            << ",\"lastStatusMs\":" << last_status_ms
            << ",\"lastHeartbeatMs\":" << last_heartbeat_ms
            << ",\"lastRecordMs\":" << last_record_ms
            << ",\"error\":" << q(error)
            << ",\"bookSymbols\":[";
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (i) out << ",";
            out << q(symbols[i]);
        }
        out << "]}";
        return out.str();
    }

    std::string execution_status_json() const {
        const auto exchange = execution_get("/health");
        const bool exchange_required = upper_ascii(env_or("CERIOUS_EXECUTION_DESTINATION", "cerious-exchange")) != "NONE";
        const bool exchange_ok = exchange && exchange->status >= 200 && exchange->status < 300;
        return "{\"ok\":true"
            ",\"destination\":\"cerious-exchange\""
            ",\"exchange\":\"cerious.exchange\""
            ",\"required\":" + std::string(exchange_required ? "true" : "false")
            + ",\"healthy\":" + std::string((!exchange_required || exchange_ok) ? "true" : "false")
            + ",\"stateOwner\":\"cerious.exchange\"}";
    }

    std::vector<std::string> command_lines(const std::string& command, std::size_t max_lines = 2000) const {
        std::vector<std::string> lines;
        FILE* pipe = open_process_pipe(command);
        if (!pipe) return lines;
        char buffer[8192];
        while (lines.size() < max_lines && std::fgets(buffer, sizeof(buffer), pipe)) {
            lines.emplace_back(buffer);
        }
        close_process_pipe(pipe);
        return lines;
    }

    struct ProcessResult {
        int exit_code = -1;
        std::string output;
    };

    static ProcessResult capture_process_result(const std::string& command) {
        ProcessResult result;
        FILE* pipe = open_process_pipe(pipe_command(command));
        if (!pipe) {
            result.output = "failed to start process";
            return result;
        }
        char buffer[8192];
        while (std::fgets(buffer, sizeof(buffer), pipe)) {
            result.output += buffer;
            if (result.output.size() > 16000) break;
        }
        result.exit_code = close_process_pipe(pipe);
        return result;
    }

    static std::string curl_config_quote(const std::string& value) {
        std::string out = "\"";
        for (const auto ch : value) {
            if (ch == '\\' || ch == '"') out.push_back('\\');
            if (ch == '\r' || ch == '\n') continue;
            out.push_back(ch);
        }
        out.push_back('"');
        return out;
    }

    static std::string strip_header_breaks(std::string value) {
        value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
        value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
        return trim_copy(value);
    }

    static bool email_destination_ok(const std::string& value) {
        if (value.empty() || value.size() > 254) return false;
        if (value.find_first_of(" \t\r\n<>") != std::string::npos) return false;
        const auto at = value.find('@');
        if (at == std::string::npos || at == 0 || at + 1 >= value.size()) return false;
        return value.find('.', at + 1) != std::string::npos;
    }

    bool alert_smtp_dry_run() const {
        const auto value = lower_ascii(env_or("CERIOUS_ALERT_SMTP_DRY_RUN", "1"));
        return value != "0" && value != "false" && value != "no" && value != "off";
    }

    std::string alert_smtp_status_json() const {
        const auto smtp_url = env_or("CERIOUS_ALERT_SMTP_URL", "");
        const auto smtp_from = env_or("CERIOUS_ALERT_SMTP_FROM", "");
        const auto smtp_user = env_or("CERIOUS_ALERT_SMTP_USERNAME", "");
        const auto smtp_password = env_or("CERIOUS_ALERT_SMTP_PASSWORD", "");
        const bool credentials_ok = smtp_user.empty() || !smtp_password.empty();
        const bool configured = !smtp_url.empty() && !smtp_from.empty() && credentials_ok;
        const bool dry_run = alert_smtp_dry_run();
        std::string body = "{\"ok\":true"
            ",\"enabled\":true"
            ",\"runtime\":\"cpp\""
            ",\"provider\":\"smtp-email-to-sms\""
            ",\"transports\":[\"smtp\"]"
            ",\"dryRun\":" + std::string(dry_run ? "true" : "false")
            + ",\"configured\":" + std::string(configured ? "true" : "false")
            + ",\"ready\":" + std::string((dry_run || configured) ? "true" : "false");
        if (!dry_run && !configured) {
            body += ",\"error\":";
            body += q(credentials_ok
                ? "SMTP text alerts require CERIOUS_ALERT_SMTP_URL and CERIOUS_ALERT_SMTP_FROM"
                : "SMTP username is set but CERIOUS_ALERT_SMTP_PASSWORD is missing");
        }
        body += "}";
        return body;
    }

    std::optional<std::string> send_smtp_text_alert(const std::string& request_body, int& status) const {
        const auto to = strip_header_breaks(get_string(request_body, "to", ""));
        const auto message = get_string(request_body, "message", "Cerious alert");
        if (!email_destination_ok(to)) {
            status = 400;
            return std::string("{\"ok\":false,\"error\":\"SMS destination must be an email-to-SMS address\"}");
        }

        const auto smtp_url = env_or("CERIOUS_ALERT_SMTP_URL", "");
        const auto smtp_from = strip_header_breaks(env_or("CERIOUS_ALERT_SMTP_FROM", ""));
        const auto smtp_user = env_or("CERIOUS_ALERT_SMTP_USERNAME", "");
        const auto smtp_password = env_or("CERIOUS_ALERT_SMTP_PASSWORD", "");
        const bool credentials_ok = smtp_user.empty() || !smtp_password.empty();
        const bool configured = !smtp_url.empty() && !smtp_from.empty() && credentials_ok;
        const bool dry_run = alert_smtp_dry_run();

        if (dry_run) {
            status = 200;
            return "{\"ok\":true,\"queued\":true,\"provider\":\"smtp-email-to-sms\",\"dryRun\":true,\"runtime\":\"cpp\",\"message\":\"SMTP dry-run accepted\"}";
        }
        if (!configured) {
            status = 503;
            return alert_smtp_status_json();
        }
        if (!email_destination_ok(smtp_from)) {
            status = 503;
            return std::string("{\"ok\":false,\"configured\":false,\"provider\":\"smtp-email-to-sms\",\"error\":\"CERIOUS_ALERT_SMTP_FROM must be an email address\"}");
        }

        const auto curl_path = env_or("CERIOUS_ALERT_CURL_PATH",
#ifdef _WIN32
            "curl.exe"
#else
            "curl"
#endif
        );

        static std::atomic<unsigned long long> smtp_counter{0};
        const auto nonce = std::to_string(now_ms()) + "-" + std::to_string(++smtp_counter);
        const auto temp_base = fs::temp_directory_path() / ("cerious-alert-" + nonce);
        const auto message_path = temp_base.string() + ".eml";
        const auto config_path = temp_base.string() + ".curl";

        std::ostringstream email;
        email << "From: " << smtp_from << "\r\n"
              << "To: " << to << "\r\n"
              << "Subject: Cerious Alert\r\n"
              << "Content-Type: text/plain; charset=utf-8\r\n"
              << "\r\n"
              << message << "\r\n";

        std::ostringstream curl_config;
        curl_config << "url = " << curl_config_quote(smtp_url) << "\n"
                    << "ssl-reqd\n"
                    << "mail-from = " << curl_config_quote("<" + smtp_from + ">") << "\n"
                    << "mail-rcpt = " << curl_config_quote("<" + to + ">") << "\n"
                    << "upload-file = " << curl_config_quote(message_path) << "\n"
                    << "connect-timeout = 15\n"
                    << "max-time = 30\n"
                    << "silent\n"
                    << "show-error\n";
        if (!smtp_user.empty()) {
            curl_config << "user = " << curl_config_quote(smtp_user + ":" + smtp_password) << "\n";
        }

        if (!write_text(fs::path(message_path), email.str()) || !write_text(fs::path(config_path), curl_config.str())) {
            status = 500;
            std::error_code ec;
            fs::remove(message_path, ec);
            fs::remove(config_path, ec);
            return std::string("{\"ok\":false,\"provider\":\"smtp-email-to-sms\",\"error\":\"failed to stage SMTP alert\"}");
        }

        const auto command = shell_quote(fs::path(curl_path)) + " --config " + shell_quote(fs::path(config_path)) + " 2>&1";
        const auto result = capture_process_result(command);
        std::error_code ec;
        fs::remove(message_path, ec);
        fs::remove(config_path, ec);
        if (result.exit_code == 0) {
            status = 200;
            return std::string("{\"ok\":true,\"queued\":true,\"provider\":\"smtp-email-to-sms\",\"dryRun\":false,\"runtime\":\"cpp\"}");
        }

        status = 502;
        const auto detail = result.output.substr(0, 1000);
        const auto detail_lower = lower_ascii(detail);
        std::string error = "SMTP send failed";
        std::string hint;
        if (result.exit_code == 67 || detail_lower.find("login denied") != std::string::npos
            || detail_lower.find("authentication") != std::string::npos) {
            error = "SMTP authentication failed";
            hint = "Gmail usually requires a Gmail App Password for SMTP; the normal account password is rejected.";
        } else if (detail_lower.find("could not resolve") != std::string::npos
            || detail_lower.find("couldn't resolve") != std::string::npos) {
            error = "SMTP host lookup failed";
            hint = "Check CERIOUS_ALERT_SMTP_URL.";
        } else if (detail_lower.find("timed out") != std::string::npos
            || detail_lower.find("timeout") != std::string::npos) {
            error = "SMTP connection timed out";
            hint = "Check network access to the SMTP host and port.";
        }

        std::string body = "{\"ok\":false,\"queued\":false,\"provider\":\"smtp-email-to-sms\",\"dryRun\":false,\"runtime\":\"cpp\""
            ",\"error\":" + q(error)
            + ",\"exitCode\":" + std::to_string(result.exit_code)
            + ",\"detail\":" + q(detail);
        if (!hint.empty()) body += ",\"hint\":" + q(hint);
        body += "}";
        return body;
    }

    std::string history_schema_for_interval(const std::string& interval) const {
        const auto value = upper_ascii(interval);
        if (value == "1H" || value == "60" || value == "60M") return "ohlcv-1h";
        if (value == "1D" || value == "D" || value == "1440") return "ohlcv-1d";
        return "ohlcv-1m";
    }

    int interval_minutes(const std::string& interval) const {
        const auto value = upper_ascii(interval);
        if (value == "1D" || value == "D" || value == "1440") return 1440;
        if (value == "1H" || value == "60" || value == "60M") return 60;
        if (value == "30M" || value == "30") return 30;
        if (value == "5M" || value == "5") return 5;
        return 1;
    }

    std::int64_t chart_time_seconds(std::uint64_t timestamp_ms, const std::string& interval) const {
        const auto seconds = static_cast<std::int64_t>(timestamp_ms / 1000ULL);
        const auto minutes = std::max(1, interval_minutes(interval));
        if (minutes >= 1440) return seconds - (seconds % 86400);
        const auto interval_seconds = static_cast<std::int64_t>(minutes) * 60;
        return seconds - (seconds % interval_seconds);
    }

    std::vector<MarketBar> aggregate_bars(std::vector<MarketBar> bars, const int minutes, const int limit) const {
        if (minutes <= 1 || bars.empty()) return bars;
        const std::uint64_t bucket_ms = static_cast<std::uint64_t>(minutes) * 60ULL * 1000ULL;
        std::sort(bars.begin(), bars.end(), [](const MarketBar& left, const MarketBar& right) {
            return left.timestamp < right.timestamp;
        });

        std::vector<MarketBar> out;
        std::uint64_t current_bucket = 0;
        for (const auto& bar : bars) {
            const auto bucket = (bar.timestamp / bucket_ms) * bucket_ms;
            if (out.empty() || bucket != current_bucket) {
                MarketBar next = bar;
                next.timestamp = bucket;
                out.push_back(next);
                current_bucket = bucket;
                continue;
            }
            auto& active = out.back();
            active.high = std::max(active.high, bar.high);
            active.low = std::min(active.low, bar.low);
            active.close = bar.close;
            active.volume += bar.volume;
        }
        if (out.size() > static_cast<std::size_t>(limit)) {
            out.erase(out.begin(), out.end() - limit);
        }
        return out;
    }

    std::vector<MarketBar> history_bars_for_symbol(const std::string& raw_symbol, const std::string& interval, int limit) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        const auto normalized_limit = std::max(1, limit);
        const auto cache_key = symbol + "|" + lower_ascii(interval) + "|" + std::to_string(normalized_limit);
        std::uint64_t ttl_ms = 20000;
        try {
            ttl_ms = std::stoull(env_or("CERIOUS_CHART_HISTORY_CACHE_MS", "20000"));
        } catch (...) {
            ttl_ms = 20000;
        }
        const auto request_started_ms = now_ms();
        auto cached_bars = [&]() -> std::optional<std::vector<MarketBar>> {
            std::lock_guard<std::mutex> lock(history_cache_mutex);
            const auto cached = history_bars_cache.find(cache_key);
            if (cached != history_bars_cache.end() && !cached->second.bars.empty()) return cached->second.bars;

            const auto prefix = symbol + "|" + lower_ascii(interval) + "|";
            const CachedMarketBars* best = nullptr;
            for (const auto& [key, value] : history_bars_cache) {
                if (key.rfind(prefix, 0) != 0 || value.bars.empty()) continue;
                if (value.bars.size() < static_cast<std::size_t>(normalized_limit)) continue;
                if (!best || value.bars.size() < best->bars.size()) best = &value;
            }
            if (!best) return std::nullopt;
            auto bars = best->bars;
            if (bars.size() > static_cast<std::size_t>(normalized_limit)) {
                bars.erase(bars.begin(), bars.end() - normalized_limit);
            }
            return bars;
        };
        {
            std::lock_guard<std::mutex> lock(history_cache_mutex);
            const auto cached = history_bars_cache.find(cache_key);
            if (cached != history_bars_cache.end() && !cached->second.bars.empty()) {
                const auto age_ms = request_started_ms >= cached->second.fetched_at_ms
                    ? request_started_ms - cached->second.fetched_at_ms
                    : 0;
                if (age_ms <= ttl_ms) return cached->second.bars;
            }
            const auto prefix = symbol + "|" + lower_ascii(interval) + "|";
            const CachedMarketBars* best = nullptr;
            for (const auto& [key, value] : history_bars_cache) {
                if (key.rfind(prefix, 0) != 0 || value.bars.empty()) continue;
                if (value.bars.size() < static_cast<std::size_t>(normalized_limit)) continue;
                const auto age_ms = request_started_ms >= value.fetched_at_ms
                    ? request_started_ms - value.fetched_at_ms
                    : 0;
                if (age_ms > ttl_ms) continue;
                if (!best || value.bars.size() < best->bars.size()) best = &value;
            }
            if (best) {
                auto bars = best->bars;
                if (bars.size() > static_cast<std::size_t>(normalized_limit)) {
                    bars.erase(bars.begin(), bars.end() - normalized_limit);
                }
                return bars;
            }
        }
        const auto exe = price_history_exe();
        if (!fs::exists(exe)) {
            std::cerr << "history unavailable: native price history executable not found at "
                      << exe.string() << std::endl;
            if (auto cached = cached_bars()) return *cached;
            return {};
        }
        if (env_or("DATABENTO_API_KEY", "").empty()) {
            std::cerr << "history unavailable: DATABENTO_API_KEY is not configured in gateway process" << std::endl;
            if (auto cached = cached_bars()) return *cached;
            return {};
        }
        const auto schema = history_schema_for_interval(interval);
        const auto minutes = std::max(1, interval_minutes(interval));
        const auto records_per_bar = schema == "ohlcv-1m" ? minutes : 1;
        const auto fetch_limit = std::max(normalized_limit * records_per_bar + records_per_bar * 4, normalized_limit + 20);
        const auto schema_minutes = schema == "ohlcv-1h" ? 60 : schema == "ohlcv-1d" ? 1440 : 1;
        const auto now = std::chrono::system_clock::now();
        std::chrono::system_clock::time_point end_time;
        if (schema == "ohlcv-1d") {
            const auto epoch_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            end_time = std::chrono::system_clock::time_point(
                std::chrono::seconds((epoch_seconds / 86400) * 86400));
        } else {
            int historical_lag_minutes = 20;
            try {
                historical_lag_minutes = std::clamp(
                    std::stoi(env_or("CERIOUS_CHART_HISTORY_END_LAG_MINUTES", "20")),
                    5,
                    120);
            } catch (...) {
                historical_lag_minutes = 20;
            }
            end_time = std::chrono::time_point_cast<std::chrono::minutes>(
                now - std::chrono::minutes(historical_lag_minutes));
        }
        const auto lookback_minutes = std::max(1, fetch_limit) * schema_minutes;
        const auto start = utc_iso(end_time - std::chrono::minutes(lookback_minutes));
        const auto end = utc_iso(end_time);
        const auto data_symbol = symbol + ".v.0";
        const auto log_dir = data / "logs";
        std::error_code ec;
        fs::create_directories(log_dir, ec);
        const auto safe_symbol = upper_ascii(symbol);
        const auto err_log = log_dir / ("cerious-price-history-" + safe_symbol + "-" + std::to_string(now_ms()) + ".err.log");
        const auto command = pipe_command(shell_quote(exe)
            + " --symbols " + shell_quote_arg(data_symbol)
            + " --stype " + shell_quote_arg(market_data_stype())
            + " --schema " + shell_quote_arg(schema)
            + " --start " + shell_quote_arg(start)
            + " --end " + shell_quote_arg(end)
            + " --limit " + std::to_string(std::max(1, fetch_limit))
            + " 2>>" + shell_quote(err_log));

        std::vector<MarketBar> bars;
        for (const auto& line : command_lines(command, static_cast<std::size_t>(std::max(1, fetch_limit + 100)))) {
            if (line.find("\"type\":\"market.ohlcv\"") == std::string::npos) continue;
            MarketBar bar;
            const auto ts_ns = get_u64_number(line, "tsEventNs", 0);
            bar.timestamp = ts_ns / 1000000ULL;
            bar.open = get_number(line, "open").value_or(std::nan(""));
            bar.high = get_number(line, "high").value_or(std::nan(""));
            bar.low = get_number(line, "low").value_or(std::nan(""));
            bar.close = get_number(line, "close").value_or(std::nan(""));
            bar.volume = get_number(line, "volume").value_or(0);
            if (bar.timestamp && finite(bar.open) && finite(bar.high) && finite(bar.low) && finite(bar.close)) {
                bars.push_back(bar);
            }
        }
        bars = aggregate_bars(std::move(bars), minutes, normalized_limit);
        if (bars.size() > static_cast<std::size_t>(normalized_limit)) {
            bars.erase(bars.begin(), bars.end() - normalized_limit);
        }
        if (!bars.empty()) {
            std::lock_guard<std::mutex> lock(history_cache_mutex);
            history_bars_cache[cache_key] = CachedMarketBars{bars, now_ms()};
        } else if (auto cached = cached_bars()) {
            return *cached;
        }
        return bars;
    }

    std::vector<MarketBar> history_bars(const std::string& raw_symbol, const std::string& interval, int limit) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        if (const auto spread = spread_def_for(symbol)) {
            auto left = history_bars_for_symbol(spread->left, interval, limit);
            auto right = history_bars_for_symbol(spread->right, interval, limit);
            std::unordered_map<std::uint64_t, MarketBar> right_by_ts;
            for (const auto& bar : right) right_by_ts[bar.timestamp] = bar;
            std::vector<MarketBar> out;
            for (const auto& left_bar : left) {
                const auto it = right_by_ts.find(left_bar.timestamp);
                if (it == right_by_ts.end()) continue;
                const auto& right_bar = it->second;
                MarketBar bar;
                bar.timestamp = left_bar.timestamp;
                bar.open = left_bar.open - spread->coef * right_bar.open;
                bar.high = left_bar.high - spread->coef * right_bar.low;
                bar.low = left_bar.low - spread->coef * right_bar.high;
                bar.close = left_bar.close - spread->coef * right_bar.close;
                bar.volume = std::min(left_bar.volume, right_bar.volume);
                out.push_back(bar);
            }
            if (out.size() > static_cast<std::size_t>(limit)) {
                out.erase(out.begin(), out.end() - limit);
            }
            return out;
        }
        return history_bars_for_symbol(symbol, interval, limit);
    }

    std::vector<MarketBar> cached_history_bars_for_symbol(const std::string& raw_symbol, const std::string& interval, int limit) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        const auto normalized_limit = std::max(1, limit);
        const auto interval_key = lower_ascii(interval);
        const auto cache_key = symbol + "|" + interval_key + "|" + std::to_string(normalized_limit);
        std::lock_guard<std::mutex> lock(history_cache_mutex);

        auto tail = [&](std::vector<MarketBar> bars) {
            if (bars.size() > static_cast<std::size_t>(normalized_limit)) {
                bars.erase(bars.begin(), bars.end() - normalized_limit);
            }
            return bars;
        };

        const auto exact = history_bars_cache.find(cache_key);
        if (exact != history_bars_cache.end() && !exact->second.bars.empty()) {
            return tail(exact->second.bars);
        }

        const auto prefix = symbol + "|" + interval_key + "|";
        const CachedMarketBars* best = nullptr;
        for (const auto& [key, value] : history_bars_cache) {
            if (key.rfind(prefix, 0) != 0 || value.bars.empty()) continue;
            if (value.bars.size() < static_cast<std::size_t>(normalized_limit)) continue;
            if (!best || value.bars.size() < best->bars.size()) best = &value;
        }
        return best ? tail(best->bars) : std::vector<MarketBar>{};
    }

    std::vector<MarketBar> cached_history_bars(const std::string& raw_symbol, const std::string& interval, int limit) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        if (const auto spread = spread_def_for(symbol)) {
            auto left = cached_history_bars_for_symbol(spread->left, interval, limit);
            auto right = cached_history_bars_for_symbol(spread->right, interval, limit);
            std::unordered_map<std::uint64_t, MarketBar> right_by_ts;
            for (const auto& bar : right) right_by_ts[bar.timestamp] = bar;
            std::vector<MarketBar> out;
            for (const auto& left_bar : left) {
                const auto it = right_by_ts.find(left_bar.timestamp);
                if (it == right_by_ts.end()) continue;
                const auto& right_bar = it->second;
                MarketBar bar;
                bar.timestamp = left_bar.timestamp;
                bar.open = left_bar.open - spread->coef * right_bar.open;
                bar.high = left_bar.high - spread->coef * right_bar.low;
                bar.low = left_bar.low - spread->coef * right_bar.high;
                bar.close = left_bar.close - spread->coef * right_bar.close;
                bar.volume = std::min(left_bar.volume, right_bar.volume);
                out.push_back(bar);
            }
            if (out.size() > static_cast<std::size_t>(limit)) {
                out.erase(out.begin(), out.end() - limit);
            }
            return out;
        }
        return cached_history_bars_for_symbol(symbol, interval, limit);
    }

    static std::uint64_t current_globex_session_start_ms() {
        const auto now = std::chrono::system_clock::now();
        const auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm local_time{};
#ifdef _WIN32
        localtime_s(&local_time, &now_time_t);
#else
        localtime_r(&now_time_t, &local_time);
#endif
        if (local_time.tm_hour < 17) {
            local_time.tm_mday -= 1;
        }
        local_time.tm_hour = 17;
        local_time.tm_min = 0;
        local_time.tm_sec = 0;
        local_time.tm_isdst = -1;
        const auto session_time_t = std::mktime(&local_time);
        if (session_time_t <= 0) return 0;
        return static_cast<std::uint64_t>(session_time_t) * 1000ULL;
    }

    SessionStats session_stats_from_bars(const std::string& symbol, const std::vector<MarketBar>& bars, std::uint64_t session_start_ms) const {
        SessionStats stats;
        stats.symbol = symbol;
        stats.session_start_ms = session_start_ms;
        MarketBar prior{};
        bool has_prior = false;
        for (const auto& bar : bars) {
            if (!finite(bar.open) || !finite(bar.high) || !finite(bar.low) || !finite(bar.close)) continue;
            if (bar.timestamp < session_start_ms) {
                prior = bar;
                has_prior = true;
                continue;
            }
            if (!stats.ok) {
                stats.open = bar.open;
                stats.high = bar.high;
                stats.low = bar.low;
                stats.ok = true;
            } else {
                stats.high = std::max(stats.high, bar.high);
                stats.low = std::min(stats.low, bar.low);
            }
            stats.last = bar.close;
            stats.updated_at_ms = std::max(stats.updated_at_ms, bar.timestamp);
        }
        if (has_prior) {
            stats.reference = prior.close;
        } else if (stats.ok) {
            stats.reference = stats.open;
        }
        return stats;
    }

    SessionStats overlay_live_mark(SessionStats stats, const MarketBook& book) const {
        const auto live_mark = mid_or_last(book);
        if (finite(live_mark)) {
            if (!stats.ok) {
                stats.open = live_mark;
                stats.high = live_mark;
                stats.low = live_mark;
                stats.reference = live_mark;
                stats.ok = true;
            } else {
                stats.high = std::max(stats.high, live_mark);
                stats.low = std::min(stats.low, live_mark);
            }
            stats.last = live_mark;
            stats.updated_at_ms = std::max(stats.updated_at_ms, book.ts_ms ? book.ts_ms : now_ms());
        }
        const auto ref = finite(stats.reference) ? stats.reference : stats.open;
        if (stats.ok && finite(ref)) {
            stats.net_change = stats.last - ref;
            if (ref != 0.0) stats.net_change_pct = stats.net_change / std::abs(ref);
        }
        return stats;
    }

    SessionStats session_stats_for_book(const MarketBook& book) const {
        const auto symbol = canonical_market_symbol(book.symbol);
        const auto session_start = current_globex_session_start_ms();
        const auto cache_key = symbol + "|" + std::to_string(session_start);
        const auto current_ms = now_ms();
        {
            std::lock_guard<std::mutex> lock(session_stats_mutex);
            const auto cached = session_stats_cache.find(cache_key);
            if (cached != session_stats_cache.end()) {
                const auto age_ms = current_ms >= cached->second.fetched_at_ms
                    ? current_ms - cached->second.fetched_at_ms
                    : 0;
                if (age_ms <= 60000ULL) {
                    auto overlaid = overlay_live_mark(cached->second.stats, book);
                    cached->second.stats = overlaid;
                    return overlaid;
                }
            }
        }

        auto bars = history_bars(symbol, "5m", 300);
        auto stats = session_stats_from_bars(symbol, bars, session_start);
        auto overlaid = overlay_live_mark(stats, book);
        {
            std::lock_guard<std::mutex> lock(session_stats_mutex);
            session_stats_cache[cache_key] = CachedSessionStats{overlaid, current_ms};
        }
        return overlaid;
    }

    std::vector<MarketBar> study_bars_with_live_mark(const std::string& raw_symbol, const std::string& interval, int limit, bool* includes_live_mark = nullptr) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        const auto minutes = std::max(1, interval_minutes(interval));
        const auto bucket_ms = static_cast<std::uint64_t>(minutes) * 60ULL * 1000ULL;
        auto bars = history_bars(symbol, interval, std::max(1, limit));
        if (includes_live_mark) *includes_live_mark = false;

        const auto book = current_book(symbol);
        if (book) {
            const auto live_mark = mid_or_last(*book);
            if (finite(live_mark)) {
                const auto source_ts = book->ts_ms ? book->ts_ms : now_ms();
                const auto live_bucket = (source_ts / bucket_ms) * bucket_ms;
                if (!bars.empty() && bars.back().timestamp == live_bucket) {
                    auto& active = bars.back();
                    active.high = std::max(active.high, live_mark);
                    active.low = std::min(active.low, live_mark);
                    active.close = live_mark;
                } else {
                    MarketBar active;
                    active.timestamp = live_bucket;
                    active.open = live_mark;
                    active.high = live_mark;
                    active.low = live_mark;
                    active.close = live_mark;
                    active.volume = 0;
                    bars.push_back(active);
                }
                if (includes_live_mark) *includes_live_mark = true;
            }
        }

        std::sort(bars.begin(), bars.end(), [](const MarketBar& left, const MarketBar& right) {
            return left.timestamp < right.timestamp;
        });
        if (bars.size() > static_cast<std::size_t>(limit)) {
            bars.erase(bars.begin(), bars.end() - limit);
        }
        return bars;
    }

    RegressionStudy calculate_regression_study(const std::string& raw_symbol, const std::string& raw_interval, int raw_lookback, double raw_standard_deviations) const {
        RegressionStudy study;
        study.symbol = canonical_market_symbol(raw_symbol);
        study.interval = raw_interval.empty() ? std::string("30m") : raw_interval;
        if (raw_lookback < 2) {
            study.error = "regression lookback is required";
            return study;
        }
        study.lookback = std::clamp(raw_lookback, 2, 2000);
        study.standard_deviations = std::clamp(raw_standard_deviations, 0.0, 20.0);
        const auto cache_key = study.symbol + "|" + lower_ascii(study.interval) + "|"
            + std::to_string(study.lookback) + "|" + json_number(study.standard_deviations, 4);
        const auto cache_now = now_ms();
        {
            std::lock_guard<std::mutex> lock(study_cache_mutex);
            const auto cached = regression_study_cache.find(cache_key);
            if (cached != regression_study_cache.end()) {
                const auto age_ms = cache_now >= cached->second.fetched_at_ms
                    ? cache_now - cached->second.fetched_at_ms
                    : 0;
                const auto ttl_ms = cached->second.study.ok ? 60000ULL : 5000ULL;
                if (age_ms <= ttl_ms) return cached->second.study;
            }
        }

        auto cache_result = [&](const RegressionStudy& result) {
            std::lock_guard<std::mutex> lock(study_cache_mutex);
            regression_study_cache[cache_key] = CachedRegressionStudy{result, now_ms()};
            return result;
        };

        bool includes_live = false;
        // Synthetic spreads can lose bars when leg timestamps do not overlap exactly.
        // Use the same deep history window that charts consume so every subscriber
        // resolves the regression from one server-side study source.
        const auto study_limit = std::clamp(std::max(study.lookback + 8, study.lookback * 6), study.lookback, 1200);
        const auto bars = study_bars_with_live_mark(study.symbol, study.interval, study_limit, &includes_live);
        study.includes_live_mark = includes_live;
        study.bars = static_cast<int>(bars.size());
        if (bars.size() < static_cast<std::size_t>(study.lookback)) {
            study.error = "not enough bars for requested regression lookback";
            return cache_result(study);
        }

        const auto first = bars.size() - static_cast<std::size_t>(study.lookback);
        const auto n = static_cast<double>(study.lookback);
        const auto x_mean = (n - 1.0) / 2.0;
        double y_sum = 0.0;
        for (std::size_t i = first; i < bars.size(); ++i) {
            if (!finite(bars[i].close)) {
                study.error = "non-finite close in regression sample";
                return cache_result(study);
            }
            y_sum += bars[i].close;
        }

        const auto y_mean = y_sum / n;
        double numerator = 0.0;
        double denominator = 0.0;
        for (int i = 0; i < study.lookback; ++i) {
            const auto y = bars[first + static_cast<std::size_t>(i)].close;
            const auto x_delta = static_cast<double>(i) - x_mean;
            numerator += x_delta * (y - y_mean);
            denominator += x_delta * x_delta;
        }
        study.slope = denominator != 0.0 ? numerator / denominator : 0.0;
        study.intercept = y_mean - study.slope * x_mean;

        double residual_sq_sum = 0.0;
        for (int i = 0; i < study.lookback; ++i) {
            const auto y = bars[first + static_cast<std::size_t>(i)].close;
            const auto fitted = study.intercept + study.slope * static_cast<double>(i);
            const auto residual = y - fitted;
            residual_sq_sum += residual * residual;
        }
        study.sigma = std::sqrt(residual_sq_sum / n);
        study.mean = study.intercept + study.slope * static_cast<double>(study.lookback - 1);
        study.upper = study.mean + study.standard_deviations * study.sigma;
        study.lower = study.mean - study.standard_deviations * study.sigma;
        study.updated_at = bars.back().timestamp;
        study.ok = finite(study.mean) && finite(study.upper) && finite(study.lower);
        if (!study.ok) study.error = "regression calculation unavailable";
        return cache_result(study);
    }

    std::string regression_study_json(const std::string& raw_symbol, const std::string& interval, int lookback, double standard_deviations) const {
        const auto study = calculate_regression_study(raw_symbol, interval, lookback, standard_deviations);
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\"ok\":" << (study.ok ? "true" : "false")
            << ",\"runtime\":\"cpp\",\"source\":\"cerious-study-service\",\"study\":\"linear-regression\""
            << ",\"symbol\":" << q(study.symbol)
            << ",\"interval\":" << q(study.interval)
            << ",\"lookback\":" << study.lookback
            << ",\"standardDeviations\":" << study.standard_deviations
            << ",\"bars\":" << study.bars
            << ",\"includesLiveMark\":" << (study.includes_live_mark ? "true" : "false")
            << ",\"updatedAt\":" << study.updated_at
            << ",\"updatedTime\":" << (study.updated_at ? chart_time_seconds(study.updated_at, study.interval) : 0);
        if (study.ok) {
            out << ",\"mean\":" << study.mean
                << ",\"upper\":" << study.upper
                << ",\"lower\":" << study.lower
                << ",\"sigma\":" << study.sigma
                << ",\"slope\":" << study.slope
                << ",\"intercept\":" << study.intercept
                << ",\"label\":" << q("Linear Regression lookback " + std::to_string(study.lookback) + " " + study.interval);
        } else {
            out << ",\"error\":" << q(study.error.empty() ? "regression unavailable" : study.error);
        }
        out << "}";
        return out.str();
    }

    std::string bars_json(const std::string& raw_symbol, const std::string& interval, int limit) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        bool includes_live = false;
        const auto requested_limit = std::max(1, limit);
        const auto warm_limit = interval_minutes(interval) >= 1440 ? 140 : 300;
        const auto internal_limit = std::clamp(std::max(requested_limit, warm_limit), requested_limit, 1200);
        auto bars = study_bars_with_live_mark(symbol, interval, internal_limit, &includes_live);
        if (bars.size() > static_cast<std::size_t>(requested_limit)) {
            bars.erase(bars.begin(), bars.end() - requested_limit);
        }
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\"ok\":true,\"runtime\":\"cpp\",\"source\":\"databento-historical-cpp\",\"symbol\":" << q(symbol)
            << ",\"includesLiveMark\":" << (includes_live ? "true" : "false")
            << ",\"lastBarTimestamp\":" << (bars.empty() ? 0 : bars.back().timestamp)
            << ",\"bars\":[";
        for (std::size_t i = 0; i < bars.size(); ++i) {
            const auto& bar = bars[i];
            const auto chart_seconds = chart_time_seconds(bar.timestamp, interval);
            if (i) out << ",";
            out << "{\"time\":" << chart_seconds
                << ",\"timestamp\":" << bar.timestamp
                << ",\"timestampMs\":" << bar.timestamp
                << ",\"open\":" << bar.open
                << ",\"high\":" << bar.high
                << ",\"low\":" << bar.low
                << ",\"close\":" << bar.close
                << ",\"volume\":" << bar.volume
                << "}";
        }
        out << "]}";
        return out.str();
    }

    struct AdvisorySpreadStat {
        std::string key;
        std::string label;
        double spread = std::nan("");
        double last = std::nan("");
        double bid = std::nan("");
        double ask = std::nan("");
        double mean20 = std::nan("");
        double mean30 = std::nan("");
        double weekly_mean = std::nan("");
        double short_term_mean = std::nan("");
        int short_term_bars = 0;
        std::string short_term_interval{"30m"};
        double prior_mean20 = std::nan("");
        double prior_settle = std::nan("");
        double atr3 = std::nan("");
        double atr20 = std::nan("");
        double atr30 = std::nan("");
        double blended_atr = std::nan("");
        double vwap_basis = std::nan("");
        double z = 0.0;
        double day_z = 0.0;
        int order_flow_score = 0;
        int score = 0;
        bool live = false;
        std::uint64_t rv_updated_at = 0;
        RegressionStudy regression;
        std::vector<MarketBar> bars;
    };

    static std::string spread_label(const std::string& symbol) {
        auto label = symbol;
        std::replace(label.begin(), label.end(), '_', '/');
        return label;
    }

    static std::string spread_expression(const std::string& symbol) {
        if (symbol == "ES_NQ") return "Buy ES / sell NQ when the spread re-enters from below the band.";
        if (symbol == "RTY_ES") return "Buy RTY / sell ES only if rates and credit do not deteriorate.";
        if (symbol == "YM_ES") return "Long YM / short ES if Dow leadership confirms.";
        return "Use the relative-value signal only after macro and order-flow confirmation.";
    }

    static std::string spread_risk_read(const std::string& symbol) {
        if (symbol == "ES_NQ") return "Avoid premature fades when Nasdaq momentum persists.";
        if (symbol == "RTY_ES") return "Small-cap cheap can stay cheap in risk-off regimes.";
        if (symbol == "YM_ES") return "Often defensive value, not true risk-on.";
        return "Respect volatility expansion and confirm liquidity before sizing.";
    }

    static std::string spread_signal(double z) {
        if (z <= -1.5) return "Buy spread setup";
        if (z >= 1.5) return "Sell spread setup";
        if (z <= -1.0) return "Cheap watch; wait for reclaim";
        if (z >= 1.0) return "Rich watch; wait for fade";
        if (std::abs(z) < 0.5) return "Neutral / fair value";
        return z > 0 ? "Rich, wait or fade" : "Cheap, wait or confirm";
    }

    static double close_mean(const std::vector<MarketBar>& bars, std::size_t count, std::size_t skip_tail = 0) {
        if (bars.empty() || skip_tail >= bars.size()) return std::nan("");
        const auto end = bars.size() - skip_tail;
        const auto begin = end > count ? end - count : 0;
        if (begin >= end) return std::nan("");
        double sum = 0.0;
        int n = 0;
        for (std::size_t i = begin; i < end; ++i) {
            if (!finite(bars[i].close)) continue;
            sum += bars[i].close;
            ++n;
        }
        return n ? sum / static_cast<double>(n) : std::nan("");
    }

    static double average_true_range(const std::vector<MarketBar>& bars, std::size_t count) {
        if (bars.empty()) return std::nan("");
        const auto begin = bars.size() > count ? bars.size() - count : 0;
        double sum = 0.0;
        int n = 0;
        for (std::size_t i = begin; i < bars.size(); ++i) {
            const auto prev_close = i > 0 ? bars[i - 1].close : bars[i].open;
            const auto tr = std::max({
                bars[i].high - bars[i].low,
                std::abs(bars[i].high - prev_close),
                std::abs(bars[i].low - prev_close),
            });
            if (!finite(tr)) continue;
            sum += std::abs(tr);
            ++n;
        }
        return n ? sum / static_cast<double>(n) : std::nan("");
    }

    std::uint64_t advisory_refresh_ms() const {
        try {
            return std::clamp<std::uint64_t>(
                static_cast<std::uint64_t>(std::stoull(env_or("CERIOUS_ADVISORY_REFRESH_MS", "1800000"))),
                60000ULL,
                86400000ULL);
        } catch (...) {
            return 1800000ULL;
        }
    }

    std::optional<int> advisory_regression_lookback() const {
        const auto configured = trim_copy(env_or("CERIOUS_ADVISORY_REGRESSION_LOOKBACK", ""));
        if (configured.empty()) return std::nullopt;
        try {
            return std::clamp(std::stoi(configured), 2, 2000);
        } catch (...) {
            return std::nullopt;
        }
    }

    double advisory_regression_std_dev() const {
        try {
            return std::clamp(std::stod(env_or("CERIOUS_ADVISORY_REGRESSION_STD_DEV", "2")), 0.0, 20.0);
        } catch (...) {
            return 2.0;
        }
    }

    std::string advisory_regression_interval() const {
        const auto value = trim_copy(env_or("CERIOUS_ADVISORY_REGRESSION_INTERVAL", "30m"));
        return value.empty() ? std::string("30m") : value;
    }

    fs::path advisory_state_dir() const {
        return data / "advisory";
    }

    bool persist_cerious_advisory_snapshot(CeriousAdvisorySnapshot snapshot) const {
        if (!snapshot.ready) return false;
        const auto dir = advisory_state_dir();
        const auto cadence_ms = advisory_refresh_ms();
        const auto persisted_at = now_ms();
        snapshot.persisted_at_ms = persisted_at;
        if (!snapshot.next_due_ms && snapshot.fetched_at_ms) {
            snapshot.next_due_ms = snapshot.fetched_at_ms + cadence_ms;
        }
        bool ok = true;
        ok = write_text_atomic(dir / "intelligence.json", snapshot.intelligence) && ok;
        ok = write_text_atomic(dir / "daily-summary.json", snapshot.daily_summary) && ok;
        ok = write_text_atomic(dir / "macro-regime.json", snapshot.macro_regime) && ok;
        ok = write_text_atomic(dir / "opportunity-map.json", snapshot.opportunity_map) && ok;
        std::ostringstream meta;
        meta << "{\"ok\":true,\"service\":\"cerious.advisory.scheduler\""
             << ",\"owner\":\"cerious-gateway-cpp\""
             << ",\"cadenceMs\":" << cadence_ms
             << ",\"fetchedAtMs\":" << snapshot.fetched_at_ms
             << ",\"nextDueMs\":" << snapshot.next_due_ms
             << ",\"persistedAtMs\":" << persisted_at
             << ",\"rules\":" << cerious_subscription_model_json()
             << "}";
        ok = write_text_atomic(dir / "meta.json", meta.str()) && ok;
        return ok;
    }

    std::optional<CeriousAdvisorySnapshot> load_persisted_cerious_advisory_snapshot() const {
        const auto dir = advisory_state_dir();
        const auto intelligence = read_text(dir / "intelligence.json");
        const auto daily_summary = read_text(dir / "daily-summary.json");
        const auto macro_regime = read_text(dir / "macro-regime.json");
        const auto opportunity_map = read_text(dir / "opportunity-map.json");
        if (!intelligence || !daily_summary || !macro_regime || !opportunity_map) return std::nullopt;
        CeriousAdvisorySnapshot snapshot;
        snapshot.intelligence = *intelligence;
        snapshot.daily_summary = *daily_summary;
        snapshot.macro_regime = *macro_regime;
        snapshot.opportunity_map = *opportunity_map;
        snapshot.ready = true;
        if (const auto meta = read_text(dir / "meta.json")) {
            snapshot.fetched_at_ms = get_u64_number(*meta, "fetchedAtMs", 0);
            snapshot.next_due_ms = get_u64_number(*meta, "nextDueMs", 0);
            snapshot.persisted_at_ms = get_u64_number(*meta, "persistedAtMs", 0);
        }
        return snapshot;
    }

    int advisory_daily_lookback_days() const {
        try {
            return std::clamp(std::stoi(env_or("CERIOUS_ADVISORY_DAILY_LOOKBACK_DAYS", "20")), 5, 252);
        } catch (...) {
            return 20;
        }
    }

    int advisory_long_lookback_days() const {
        try {
            return std::clamp(std::stoi(env_or("CERIOUS_ADVISORY_LONG_LOOKBACK_DAYS", "30")), 10, 252);
        } catch (...) {
            return 30;
        }
    }

    int advisory_short_lookback_bars() const {
        try {
            return std::clamp(std::stoi(env_or("CERIOUS_ADVISORY_SHORT_LOOKBACK_BARS", "13")), 2, 240);
        } catch (...) {
            return 13;
        }
    }

    std::optional<RegressionStudy> cached_regression_study(
        const std::string& raw_symbol,
        const std::string& raw_interval,
        int raw_lookback,
        double raw_standard_deviations,
        std::uint64_t max_age_ms = 60000ULL) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        const auto interval = raw_interval.empty() ? std::string("30m") : raw_interval;
        const auto lookback = std::clamp(raw_lookback, 2, 2000);
        const auto standard_deviations = std::clamp(raw_standard_deviations, 0.0, 20.0);
        const auto cache_key = symbol + "|" + lower_ascii(interval) + "|"
            + std::to_string(lookback) + "|" + json_number(standard_deviations, 4);
        std::lock_guard<std::mutex> lock(study_cache_mutex);
        const auto cached = regression_study_cache.find(cache_key);
        if (cached == regression_study_cache.end()) return std::nullopt;
        if (max_age_ms > 0) {
            const auto now = now_ms();
            const auto age_ms = now >= cached->second.fetched_at_ms ? now - cached->second.fetched_at_ms : 0;
            if (age_ms > max_age_ms) return std::nullopt;
        }
        return cached->second.study;
    }

    void request_regression_study_warmup(const std::string& raw_symbol, const std::string& raw_interval, int raw_lookback, double raw_standard_deviations) const {
        const auto symbol = canonical_market_symbol(raw_symbol);
        const auto interval = raw_interval.empty() ? std::string("30m") : raw_interval;
        const auto lookback = std::clamp(raw_lookback, 2, 2000);
        const auto standard_deviations = std::clamp(raw_standard_deviations, 0.0, 20.0);
        const auto cache_key = symbol + "|" + lower_ascii(interval) + "|"
            + std::to_string(lookback) + "|" + json_number(standard_deviations, 4);
        {
            std::lock_guard<std::mutex> lock(study_warmup_mutex);
            if (regression_study_warmups.count(cache_key)) return;
            regression_study_warmups.insert(cache_key);
        }
        std::thread([this, symbol, interval, lookback, standard_deviations, cache_key]() {
            (void)this->calculate_regression_study(symbol, interval, lookback, standard_deviations);
            std::lock_guard<std::mutex> lock(this->study_warmup_mutex);
            this->regression_study_warmups.erase(cache_key);
        }).detach();
    }

    AdvisorySpreadStat build_advisory_spread_stat(const SpreadDef& spread, bool allow_history_fetch) const {
        AdvisorySpreadStat stat;
        stat.key = spread.symbol;
        stat.label = spread_label(spread.symbol);
        const auto daily_lookback = advisory_daily_lookback_days();
        const auto long_lookback = advisory_long_lookback_days();
        const auto short_interval = advisory_regression_interval();
        const auto short_lookback = advisory_short_lookback_bars();
        const auto regression_lookback = advisory_regression_lookback();
        const auto regression_std_dev = advisory_regression_std_dev();
        stat.bars = allow_history_fetch
            ? history_bars(spread.symbol, "1d", std::max(120, long_lookback + 20))
            : cached_history_bars(spread.symbol, "1d", std::max(120, long_lookback + 20));
        const auto short_bar_limit = regression_lookback
            ? std::max(*regression_lookback + 8, 240)
            : 240;
        const auto short_bars = allow_history_fetch
            ? history_bars(spread.symbol, short_interval, short_bar_limit)
            : cached_history_bars(spread.symbol, short_interval, short_bar_limit);
        stat.short_term_interval = short_interval;
        stat.short_term_bars = static_cast<int>(short_bars.size());
        stat.short_term_mean = close_mean(short_bars, static_cast<std::size_t>(short_lookback));
        if (regression_lookback) {
            if (allow_history_fetch) {
                stat.regression = calculate_regression_study(spread.symbol, short_interval, *regression_lookback, regression_std_dev);
            } else if (auto cached = cached_regression_study(spread.symbol, short_interval, *regression_lookback, regression_std_dev)) {
                stat.regression = *cached;
            }
        }
        std::string rv_interval = "1d";
        if (stat.bars.size() < 20) {
            stat.bars = allow_history_fetch
                ? history_bars(spread.symbol, "30m", 240)
                : cached_history_bars(spread.symbol, "30m", 240);
            rv_interval = "30m";
        }
        if (stat.bars.size() > 60) {
            stat.bars.erase(stat.bars.begin(), stat.bars.end() - 60);
        }

        const auto book = current_book(spread.symbol);
        if (book) {
            stat.bid = book->bid;
            stat.ask = book->ask;
            stat.last = mid_or_last(*book);
            stat.live = book->live;
            stat.rv_updated_at = book->ts_ms;
        }
        if ((!finite(stat.last) || stat.last == 0.0) && !stat.bars.empty()) {
            stat.last = stat.bars.back().close;
        }
        stat.spread = stat.last;
        if (!stat.rv_updated_at && !stat.bars.empty()) stat.rv_updated_at = stat.bars.back().timestamp;
        if (finite(stat.last) && (stat.bars.empty() || stat.bars.back().close != stat.last)) {
            MarketBar live_bar;
            live_bar.timestamp = stat.rv_updated_at ? stat.rv_updated_at : now_ms();
            live_bar.open = stat.last;
            live_bar.high = stat.last;
            live_bar.low = stat.last;
            live_bar.close = stat.last;
            live_bar.volume = 0;
            stat.bars.push_back(live_bar);
            if (stat.bars.size() > 60) stat.bars.erase(stat.bars.begin());
        }

        stat.mean20 = close_mean(stat.bars, static_cast<std::size_t>(daily_lookback));
        stat.mean30 = close_mean(stat.bars, static_cast<std::size_t>(long_lookback));
        stat.weekly_mean = close_mean(stat.bars, 5);
        stat.prior_mean20 = close_mean(stat.bars, static_cast<std::size_t>(daily_lookback), 1);
        stat.prior_settle = stat.bars.size() > 1 ? stat.bars[stat.bars.size() - 2].close : close_mean(stat.bars, 1);
        stat.atr3 = average_true_range(stat.bars, 3);
        stat.atr20 = average_true_range(stat.bars, 20);
        stat.atr30 = average_true_range(stat.bars, 30);
        if (!finite(stat.mean20)) stat.mean20 = stat.last;
        if (!finite(stat.mean30)) stat.mean30 = stat.mean20;
        if (!finite(stat.weekly_mean)) stat.weekly_mean = stat.mean20;
        if (!finite(stat.short_term_mean)) stat.short_term_mean = stat.mean20;
        if (!finite(stat.prior_mean20)) stat.prior_mean20 = stat.mean20;
        if (!finite(stat.prior_settle)) stat.prior_settle = stat.last;
        if (!finite(stat.atr3)) stat.atr3 = 0.0;
        if (!finite(stat.atr20)) stat.atr20 = stat.atr3;
        if (!finite(stat.atr30)) stat.atr30 = stat.atr20;
        stat.blended_atr = finite(stat.atr20) && finite(stat.atr30)
            ? (stat.atr20 * 0.65 + stat.atr30 * 0.35)
            : std::max(stat.atr20, stat.atr30);
        const auto min_width = std::max(product_def_for(spread.symbol).tick_size * 4.0, 0.0001);
        if (!finite(stat.blended_atr) || stat.blended_atr < min_width) stat.blended_atr = min_width;
        stat.vwap_basis = finite(stat.prior_settle) ? stat.prior_settle : stat.mean20;
        stat.z = finite(stat.last) ? (stat.last - stat.mean20) / stat.blended_atr : 0.0;
        const auto half_atr = std::max(stat.blended_atr / 2.0, min_width);
        stat.day_z = finite(stat.last) ? (stat.last - stat.vwap_basis) / half_atr : stat.z;
        stat.order_flow_score = std::clamp(static_cast<int>(std::llround(std::abs(stat.day_z) * 38.0 + std::abs(stat.z) * 18.0)), 0, 100);
        stat.score = std::clamp(40 + static_cast<int>(std::llround(std::abs(stat.z) * 28.0 + std::abs(stat.day_z) * 18.0)) + (stat.live ? 8 : 0), 0, 100);
        (void)rv_interval;
        return stat;
    }

    std::vector<AdvisorySpreadStat> build_advisory_spread_stats(bool allow_history_fetch) const {
        std::vector<AdvisorySpreadStat> stats;
        for (const auto& spread : spread_definitions()) {
            stats.push_back(build_advisory_spread_stat(spread, allow_history_fetch));
        }
        std::sort(stats.begin(), stats.end(), [](const auto& left, const auto& right) {
            return left.score > right.score;
        });
        return stats;
    }

    std::string advisory_spread_json(const AdvisorySpreadStat& stat, bool include_bars = true) const {
        const auto move = finite(stat.last) && finite(stat.mean20) ? stat.last - stat.mean20 : std::nan("");
        const auto move_pct = stat.blended_atr > 0.0 && finite(move) ? move / stat.blended_atr : 0.0;
        const auto half_atr = stat.blended_atr / 2.0;
        const auto regression_lookback_json = stat.regression.ok && stat.regression.lookback > 0
            ? std::to_string(stat.regression.lookback)
            : std::string("null");
        const auto regression_bars_json = stat.regression.ok
            ? std::to_string(stat.regression.bars)
            : std::string("0");
        std::ostringstream out;
        out << "{\"key\":" << q(stat.key)
            << ",\"label\":" << q(stat.label)
            << ",\"spread\":" << json_number(stat.spread, 4)
            << ",\"lastTraded\":" << json_number(stat.last, 4)
            << ",\"mean\":" << json_number(stat.mean20, 4)
            << ",\"longTermMean\":" << json_number(stat.mean30, 4)
            << ",\"weeklyMean\":" << json_number(stat.weekly_mean, 4)
            << ",\"shortTermMean\":" << json_number(stat.short_term_mean, 4)
            << ",\"shortTermInterval\":" << q(stat.short_term_interval)
            << ",\"shortTermBars\":" << stat.short_term_bars
            << ",\"lookbackMean\":" << json_number(stat.mean20, 4)
            << ",\"priorLookbackMean\":" << json_number(stat.prior_mean20, 4)
            << ",\"lookbackDays\":" << advisory_daily_lookback_days()
            << ",\"priorSettle\":" << json_number(stat.prior_settle, 4)
            << ",\"moveFromMean\":" << json_number(move, 4)
            << ",\"movePctOfAtr\":" << json_number(move_pct, 4)
            << ",\"atr\":" << json_number(stat.blended_atr, 4)
            << ",\"atr3\":" << json_number(stat.atr3, 4)
            << ",\"atr20\":" << json_number(stat.atr20, 4)
            << ",\"atr30\":" << json_number(stat.atr30, 4)
            << ",\"blendedAtr\":" << json_number(stat.blended_atr, 4)
            << ",\"halfAtr\":" << json_number(half_atr, 4)
            << ",\"vwapBasis\":" << json_number(stat.vwap_basis, 4)
            << ",\"dayZ\":" << json_number(stat.day_z, 4)
            << ",\"z\":" << json_number(stat.z, 4)
            << ",\"rawZ\":" << json_number(stat.z, 4)
            << ",\"signalThreshold\":1.5"
            << ",\"bias\":" << q(stat.z <= -0.5 ? "buy" : stat.z >= 0.5 ? "sell" : "neutral")
            << ",\"orderFlowScore\":" << stat.order_flow_score
            << ",\"updateCadence\":\"Daily baseline plus completed 30m study bars; live last-trade overlay\""
            << ",\"rvInterval\":\"1d\""
            << ",\"rvBars\":" << stat.bars.size()
            << ",\"rvUpdatedAt\":" << stat.rv_updated_at
            << ",\"publishedAt\":" << q(utc_iso(std::chrono::system_clock::now()) + "Z")
            << ",\"publishReason\":\"Native Cerious advisory snapshot from live book and historical bars\""
            << ",\"linearRegressionMean\":" << json_number(stat.regression.mean, 4)
            << ",\"linearRegressionUpper\":" << json_number(stat.regression.upper, 4)
            << ",\"linearRegressionLower\":" << json_number(stat.regression.lower, 4)
            << ",\"linearRegressionSigma\":" << json_number(stat.regression.sigma, 4)
            << ",\"linearRegressionSlope\":" << json_number(stat.regression.slope, 8)
            << ",\"linearRegressionInterval\":" << q(stat.regression.interval.empty() ? advisory_regression_interval() : stat.regression.interval)
            << ",\"linearRegressionLookback\":" << regression_lookback_json
            << ",\"linearRegressionBars\":" << regression_bars_json
            << ",\"linearRegressionUpdatedAt\":" << stat.regression.updated_at
            << ",\"linearRegressionIsForming\":" << (stat.regression.includes_live_mark ? "true" : "false")
            << ",\"linearRegressionSource\":\"cerious-study-service\""
            << ",\"linearRegressionReady\":" << (stat.regression.ok ? "true" : "false")
            << ",\"linearRegressionError\":" << q(stat.regression.ok ? "" : stat.regression.error)
            << ",\"theoreticalBid\":" << json_number(stat.bid, 4)
            << ",\"theoreticalAsk\":" << json_number(stat.ask, 4)
            << ",\"signal\":" << q(spread_signal(stat.day_z))
            << ",\"volume\":0"
            << ",\"live\":" << (stat.live ? "true" : "false");
        if (include_bars) {
            out << ",\"bars\":[";
            for (std::size_t i = 0; i < stat.bars.size(); ++i) {
                if (i) out << ",";
                const auto& bar = stat.bars[i];
                out << "{\"timestamp\":" << bar.timestamp
                    << ",\"open\":" << json_number(bar.open, 4)
                    << ",\"high\":" << json_number(bar.high, 4)
                    << ",\"low\":" << json_number(bar.low, 4)
                    << ",\"close\":" << json_number(bar.close, 4)
                    << ",\"volume\":" << json_number(bar.volume, 2)
                    << "}";
            }
            out << "]";
        }
        out << "}";
        return out.str();
    }

    std::string advisory_spread_configs_json() const {
        std::ostringstream out;
        out << "[";
        const auto defs = spread_definitions();
        for (std::size_t i = 0; i < defs.size(); ++i) {
            const auto& spread = defs[i];
            const auto product = product_def_for(spread.symbol);
            if (i) out << ",";
            out << "{\"symbol\":" << q(spread.symbol)
                << ",\"label\":" << q(spread_label(spread.symbol))
                << ",\"meaning\":" << q(spread_expression(spread.symbol))
                << ",\"legA\":" << q(spread.left)
                << ",\"legB\":" << q(spread.right)
                << ",\"ttRatio\":" << q(spread.ratio_label.empty() ? "-" : spread.ratio_label)
                << ",\"displayFormula\":" << q(spread.formula.empty() ? spread.left + " - " + json_number(spread.coef, 7) + " * " + spread.right : spread.formula)
                << ",\"syntheticTickValue\":" << json_number(product.tick_value, 2)
                << ",\"leftRatio\":" << spread.left_ratio
                << ",\"rightRatio\":" << spread.right_ratio
                << ",\"ratio\":" << json_number(spread.coef, 7)
                << "}";
        }
        out << "]";
        return out.str();
    }

    CeriousAdvisorySnapshot build_cerious_advisory_snapshot(bool allow_history_fetch = true) const {
        CeriousAdvisorySnapshot snapshot;
        snapshot.fetched_at_ms = now_ms();
        snapshot.next_due_ms = snapshot.fetched_at_ms + advisory_refresh_ms();
        const auto fetched_at = utc_iso(std::chrono::system_clock::now()) + "Z";
        const auto cadence_ms = advisory_refresh_ms();
        const auto stats = build_advisory_spread_stats(allow_history_fetch);
        snapshot.ready = std::any_of(stats.begin(), stats.end(), [](const auto& row) {
            return !row.bars.empty() && finite(row.last) && finite(row.mean20);
        });
        AdvisorySpreadStat fallback_strongest;
        fallback_strongest.key = "ES_NQ";
        fallback_strongest.label = "ES/NQ";
        fallback_strongest.score = 50;
        fallback_strongest.regression.ok = false;
        const auto strongest = stats.empty() ? fallback_strongest : stats.front();
        const auto avg_score = stats.empty()
            ? 50.0
            : std::accumulate(stats.begin(), stats.end(), 0.0, [](double sum, const auto& row) { return sum + row.score; }) / stats.size();
        const auto avg_z = stats.empty()
            ? 0.0
            : std::accumulate(stats.begin(), stats.end(), 0.0, [](double sum, const auto& row) { return sum + row.z; }) / stats.size();
        const auto regime_strength = std::clamp(static_cast<int>(std::llround(avg_score)), 0, 100);
        const auto regime_label = regime_strength >= 60 ? "Selective Risk-On" : regime_strength <= 40 ? "Risk-Off" : "Mixed";
        const auto algo = regime_strength >= 55 ? "Mean reversion with confirmation" : "Reduce size until spread and macro confirm";
        const auto goose_direction = strongest.z <= -0.75 ? "Long " + strongest.label : strongest.z >= 0.75 ? "Short " + strongest.label : "Selective";
        const auto goose_risk = std::abs(strongest.z) >= 1.5 ? "High" : std::abs(strongest.z) >= 0.75 ? "Medium" : "Low";
        const auto goose_confidence = strongest.live && strongest.regression.ok ? "Medium" : "Low";
        const auto risk_polarity = std::string(regime_label) == "Risk-Off" ? "risk-off" : "risk-on";
        const auto advisory_order_flow_status = std::clamp(strongest.order_flow_score, 0, 100);

        std::ostringstream eligible;
        eligible << "[";
        for (std::size_t i = 0; i < stats.size(); ++i) {
            if (i) eligible << ",";
            const auto& row = stats[i];
            eligible << "{\"key\":" << q(row.key)
                << ",\"label\":" << q(row.label)
                << ",\"score\":" << row.score
                << ",\"z\":" << json_number(row.z, 4)
                << ",\"bias\":" << q(row.z <= -0.5 ? "Buy weakness when GOOSE agrees" : row.z >= 0.5 ? "Sell strength when GOOSE agrees" : "Neutral; wait for extension")
                << ",\"approach\":" << q(std::abs(row.z) >= 1.5
                    ? "Qualified extension: deploy only with macro and order-flow confirmation."
                    : "Watch list: wait for a cleaner band location before full-size deployment.")
                << "}";
        }
        eligible << "]";

        std::ostringstream daily;
        daily << "{\"service\":\"cerious.daily.summary\""
            << ",\"fetchedAt\":" << q(fetched_at)
            << ",\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"daily baseline plus completed 30m advisory refresh\",\"cadenceMs\":" << cadence_ms << ",\"sources\":[\"/api/bars\",\"/api/studies/regression\",\"live top-of-book/last-trade overlay\"],\"uiPolicy\":\"render endpoint payload only\"}"
            << ",\"summaryRead\":" << q(std::string("Native Cerious daily cockpit: suggested focus is ") + strongest.label + ". Use this as the operator read before opening GOOSE, relative value visuals, and macro regime. Advisory payloads refresh on the completed 30-minute cadence while live market data continues independently.")
            << ",\"top\":["
            << "{\"label\":\"Suggested Focus\",\"value\":" << q(strongest.label) << ",\"note\":" << q(spread_signal(strongest.day_z)) << "},"
            << "{\"label\":\"Trade Bias\",\"value\":" << q(regime_label) << ",\"note\":\"Favor only the spreads where z-location, macro regime, and order-flow status agree.\"},"
            << "{\"label\":\"Macro / News\",\"value\":" << q(goose_risk) << ",\"note\":\"GOOSE and streaming headlines remain confirmation layers before full-size layering.\"},"
            << "{\"label\":\"Data Quality\",\"value\":" << q(strongest.live ? "Live" : "Backfill") << ",\"note\":\"Endpoint is generated by the native C++ advisory model, not a copied dashboard file.\"}"
            << "],\"classification\":["
            << "{\"label\":\"Current Bias\",\"value\":" << q(regime_label) << ",\"note\":\"Derived from spread scores, z-location, live state, and macro factor pressure.\"},"
            << "{\"label\":\"Eligible Spreads\",\"value\":" << q(stats.empty() ? "Waiting" : spread_label(stats.front().key)) << ",\"note\":\"Ranked by the server-side relative value model.\"},"
            << "{\"label\":\"Algorithmic Approach\",\"value\":\"Mean Reversion First\",\"note\":\"Layer only from qualified bands; reduce size when ATR and macro pressure expand.\"},"
            << "{\"label\":\"Risk-On / Off Strength\",\"value\":" << q(std::to_string(regime_strength) + "/100") << ",\"note\":\"Formula scale is served by the advisory endpoint and rendered by the widget.\"}"
            << "],\"sourcePills\":["
            << "{\"label\":\"Server Endpoint\",\"tone\":\"blue\"},"
            << "{\"label\":\"Completed 30m Cadence\",\"tone\":\"blue\"},"
            << "{\"label\":\"Daily Baseline\",\"tone\":\"amber\"},"
            << "{\"label\":\"Live Overlay\",\"tone\":\"amber\"}"
            << "],\"eligibleSpreads\":" << eligible.str()
            << ",\"gooseComplement\":" << q("Daily Summary and GOOSE now consume the same native advisory snapshot. Daily Summary is the cockpit; GOOSE is the active confirmation read.")
            << "}";
        snapshot.daily_summary = daily.str();

        std::ostringstream macro;
        macro << "{\"service\":\"macro.regime\",\"fetchedAt\":" << q(fetched_at)
            << ",\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"completed 30m advisory refresh\",\"cadenceMs\":" << cadence_ms << ",\"sources\":[\"spread relative-value bars\",\"live market overlay\"],\"uiPolicy\":\"render endpoint payload only\"}"
            << ",\"label\":" << q(regime_label)
            << ",\"strength\":" << regime_strength
            << ",\"algo\":" << q(algo)
            << ",\"score\":" << regime_strength
            << ",\"factors\":{\"volatility\":" << json_number((100.0 - avg_score) / 100.0, 4)
            << ",\"rates\":" << json_number(avg_z / 3.0, 4)
            << ",\"credit\":" << json_number((strongest.key == "RTY_ES" ? strongest.z : avg_z) / 3.0, 4)
            << ",\"breadth\":" << json_number(avg_score / 100.0, 4)
            << ",\"smallCapLeadership\":" << json_number((strongest.key == "RTY_ES" ? -strongest.z : 0.0), 4)
            << ",\"news\":0}"
            << ",\"factorRows\":["
            << "{\"key\":\"Volatility\",\"value\":" << json_number((100.0 - avg_score) / 100.0, 4) << ",\"weight\":0.2,\"contribution\":" << json_number((100.0 - avg_score) * 0.2, 2) << "},"
            << "{\"key\":\"Rates\",\"value\":" << json_number(avg_z / 3.0, 4) << ",\"weight\":0.2,\"contribution\":" << json_number(avg_z * 6.7, 2) << "},"
            << "{\"key\":\"Credit\",\"value\":" << json_number((strongest.key == "RTY_ES" ? strongest.z : avg_z) / 3.0, 4) << ",\"weight\":0.18,\"contribution\":" << json_number((strongest.key == "RTY_ES" ? strongest.z : avg_z) * 6.0, 2) << "},"
            << "{\"key\":\"Breadth\",\"value\":" << json_number((avg_score - 50.0) / 50.0, 4) << ",\"weight\":0.18,\"contribution\":" << json_number((avg_score - 50.0) * 0.18, 2) << "},"
            << "{\"key\":\"SmallCap Leadership\",\"value\":" << json_number((strongest.key == "RTY_ES" ? -strongest.z : 0.0) / 2.0, 4) << ",\"weight\":0.14,\"contribution\":" << json_number((strongest.key == "RTY_ES" ? -strongest.z : 0.0) * 7.0, 2) << "},"
            << "{\"key\":\"Headlines\",\"value\":0,\"weight\":0.1,\"contribution\":0}"
            << "],\"newsRead\":{\"bias\":\"mixed\",\"score\":52,\"urgentCount\":0,\"summary\":\"Streaming news is handled separately; no urgent headline override applied to this advisory snapshot.\"}"
            << ",\"leadership\":{\"ES\":" << json_number(avg_z, 4) << ",\"NQ\":" << json_number(-avg_z, 4) << ",\"YM\":" << json_number(avg_z / 2.0, 4) << ",\"RTY\":" << json_number(strongest.key == "RTY_ES" ? -strongest.z : 0.0, 4) << "}"
            << ",\"rtyVolumeShare\":0.19"
            << ",\"read\":" << q("Macro regime is " + std::string(regime_label) + ". Let GOOSE confirm before full-size deployment; advisory numbers refresh on the low-frequency spread cadence while live LTP updates separately.")
            << "}";
        snapshot.macro_regime = macro.str();

        std::ostringstream rows;
        rows << "[";
        for (std::size_t i = 0; i < stats.size(); ++i) {
            if (i) rows << ",";
            const auto& row = stats[i];
            rows << "{\"key\":" << q(row.key)
                << ",\"label\":" << q(row.label)
                << ",\"score\":" << row.score
                << ",\"z\":" << json_number(row.z, 4)
                << ",\"spread\":" << json_number(row.last, 4)
                << ",\"signal\":" << q(spread_signal(row.day_z))
                << ",\"expression\":" << q(spread_expression(row.key))
                << ",\"risk\":" << q(spread_risk_read(row.key))
                << ",\"location\":" << std::clamp(static_cast<int>(std::llround(50.0 + row.z * 20.0)), 0, 100)
                << ",\"confirmation\":" << row.order_flow_score
                << ",\"regime\":" << regime_strength
                << ",\"liquidity\":" << (row.live ? 82 : 35)
                << "}";
        }
        rows << "]";

        std::ostringstream opportunity;
        opportunity << "{\"service\":\"signal.cross-spread\",\"fetchedAt\":" << q(fetched_at)
            << ",\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"completed 30m advisory refresh\",\"cadenceMs\":" << cadence_ms << ",\"sources\":[\"spread bars\",\"macro regime\",\"live market overlay\"],\"uiPolicy\":\"render endpoint payload only\"}"
            << ",\"rows\":" << rows.str()
            << ",\"playbookRows\":["
            << "{\"signalCombination\":\"RTY/ES up + ES/NQ up\",\"interpretation\":\"Small caps outperform while Nasdaq underperforms. Broadening or domestic cyclicals.\",\"expression\":\"Long RTY / short NQ\",\"risk\":\"Watch rates and credit. This can reverse violently on hawkish shocks.\"},"
            << "{\"signalCombination\":\"RTY/ES down + ES/NQ down\",\"interpretation\":\"Small caps lag while Nasdaq leads. Narrow mega-cap growth regime.\",\"expression\":\"Long NQ / short RTY\",\"risk\":\"Momentum can persist. Avoid premature fades.\"},"
            << "{\"signalCombination\":\"YM/ES up + RTY/ES up\",\"interpretation\":\"Value, cyclicals, and small caps all improving.\",\"expression\":\"Long YM and RTY basket / short ES\",\"risk\":\"Confirm with market breadth and regional banks.\"},"
            << "{\"signalCombination\":\"YM/ES up + RTY/ES down\",\"interpretation\":\"Dow/value outperforms, but small-cap credit beta remains suspect.\",\"expression\":\"Long YM / short RTY\",\"risk\":\"Often defensive value, not true risk-on.\"},"
            << "{\"signalCombination\":\"ES/NQ down + YM/ES down\",\"interpretation\":\"Nasdaq and S&P growth leadership over Dow value.\",\"expression\":\"Long NQ / short YM\",\"risk\":\"Size carefully around earnings concentration in mega-cap tech.\"}"
            << "],\"productRows\":[";
        const auto defs = spread_definitions();
        for (std::size_t i = 0; i < defs.size(); ++i) {
            if (i) opportunity << ",";
            const auto& def = defs[i];
            opportunity << "{\"spread\":" << q(spread_label(def.symbol))
                << ",\"label\":" << q(def.symbol == "ES_NQ" ? "S&P versus Nasdaq" : def.symbol == "RTY_ES" ? "Russell versus S&P" : "Dow versus S&P")
                << ",\"tag\":" << q(def.symbol == "ES_NQ" ? "Growth leadership" : def.symbol == "RTY_ES" ? "Small-cap beta" : "Value/cyclical leadership")
                << ",\"formula\":" << q(def.left + " - " + json_number(def.coef, 7) + " * " + def.right)
                << ",\"buy\":" << q("Buy " + def.left + " / sell " + def.right)
                << ",\"sell\":" << q("Sell " + def.left + " / buy " + def.right)
                << ",\"nuance\":" << q(spread_risk_read(def.symbol))
                << "}";
        }
        opportunity << "],\"tradePlanRows\":["
            << "{\"title\":\"Entry\",\"body\":\"Enter at +/-1.5 ATR only when macro regime, GOOSE direction, and live spread signal agree.\"},"
            << "{\"title\":\"Layering\",\"body\":\"Use staged clips only after the signal is confirmed by the current spread snapshot.\"},"
            << "{\"title\":\"Exit\",\"body\":\"Take risk down near the rolling mean and keep final scale-out disciplined.\"}"
            << "],\"riskChecklistRows\":["
            << "{\"risk\":\"Tail beta mismatch\",\"control\":\"Measure dollar delta by leg and rebalance when index levels move materially.\"},"
            << "{\"risk\":\"Hidden ES exposure\",\"control\":\"When combining spreads, net all ES legs before sizing.\"},"
            << "{\"risk\":\"Volatility regime shift\",\"control\":\"Use ATR percentile to reduce size above the 80th percentile.\"},"
            << "{\"risk\":\"Macro invalidation\",\"control\":\"Stop buying small-cap weakness if rates and credit both deteriorate.\"},"
            << "{\"risk\":\"Execution slippage\",\"control\":\"Use legging settings conservatively around data releases and cash open.\"}"
            << "]}";
        snapshot.opportunity_map = opportunity.str();

        std::ostringstream spread_array;
        spread_array << "[";
        for (std::size_t i = 0; i < stats.size(); ++i) {
            if (i) spread_array << ",";
            spread_array << advisory_spread_json(stats[i], true);
        }
        spread_array << "]";

        std::ostringstream signal_array;
        signal_array << "[";
        for (std::size_t i = 0; i < stats.size(); ++i) {
            if (i) signal_array << ",";
            signal_array << advisory_spread_json(stats[i], false);
        }
        signal_array << "]";

        std::ostringstream intelligence;
        intelligence << "{\"meters\":{\"riskOnRanking\":" << regime_strength
            << ",\"riskPolarity\":" << q(risk_polarity)
            << ",\"orderFlowStatus\":" << advisory_order_flow_status
            << ",\"orderFlowSpread\":" << q(strongest.label)
            << ",\"updatedAt\":" << q(fetched_at)
            << "}"
            << ",\"goose\":{\"strategy\":\"Mean Reversion\""
            << ",\"direction\":" << q(goose_direction)
            << ",\"risk\":" << q(goose_risk)
            << ",\"confidence\":" << q(goose_confidence)
            << ",\"read\":" << q("GOOSE confirms " + strongest.label + " as the current highest-scoring spread. Use relative-value mean reversion only when macro regime, spread location, and live signal agree.")
            << ",\"evidence\":["
            << "[\"Cadence\",\"GOOSE remains a low-frequency advisor, not a tick-by-tick trigger.\"],"
            << "[\"Primary Gate\",\"Macro/news context plus spread z-score confirmation.\"],"
            << "[\"Risk Gate\",\"ATR expansion cuts size and widens bands.\"],"
            << "[\"Execution\",\"Layer passively first; add only on extension plus stabilization.\"]"
            << "],\"updateCadence\":\"Completed 30m advisory review with significant-change threshold; live market data publishes separately\""
            << ",\"updatedAt\":" << q(fetched_at)
            << ",\"nextReviewSeconds\":" << (cadence_ms / 1000ULL) << "}"
            << ",\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"completed 30m advisory refresh\",\"cadenceMs\":" << cadence_ms << ",\"sources\":[\"/api/bars\",\"/api/studies/regression\",\"live top-of-book/last-trade overlay\"],\"uiPolicy\":\"render endpoint payload only\"}"
            << ",\"macroRegime\":" << snapshot.macro_regime
            << ",\"spreadConfigs\":" << advisory_spread_configs_json()
            << ",\"spreadPack\":{\"updatedAt\":" << q(fetched_at)
            << ",\"cadence\":\"Daily baseline plus completed 30m study bars; live LTP overlays update from market data\""
            << ",\"strongest\":" << (stats.empty() ? "{}" : advisory_spread_json(strongest, true))
            << ",\"spreads\":" << spread_array.str()
            << "}"
            << ",\"liveSpreadSignals\":" << signal_array.str()
            << "}";
        snapshot.intelligence = intelligence.str();
        return snapshot;
    }

    std::string with_intelligence_meters(std::string payload) const {
        if (payload.find("\"meters\"") != std::string::npos) return payload;
        const auto macro = get_json_member(payload, "macroRegime").value_or("{}");
        const auto spread_pack = get_json_member(payload, "spreadPack").value_or("{}");
        auto strongest = get_json_member(spread_pack, "strongest").value_or("{}");
        if (strongest == "{}") {
            const auto spreads = get_json_member(spread_pack, "spreads").value_or("[]");
            const auto items = json_object_array_items(spreads);
            if (!items.empty()) strongest = items.front();
        }

        const auto risk_on_ranking = std::clamp(static_cast<int>(get_u64_number(macro, "strength", 50)), 0, 100);
        const auto macro_label = get_string(macro, "label", "Mixed");
        const auto risk_polarity = macro_label == "Risk-Off" ? "risk-off" : "risk-on";
        const auto order_flow_status = std::clamp(static_cast<int>(get_u64_number(strongest, "orderFlowScore", 0)), 0, 100);
        auto order_flow_spread = get_string(strongest, "label", "Waiting");
        if (order_flow_spread.empty()) order_flow_spread = "Waiting";
        std::ostringstream meters;
        meters << "\"meters\":{\"riskOnRanking\":" << risk_on_ranking
            << ",\"riskPolarity\":" << q(risk_polarity)
            << ",\"orderFlowStatus\":" << order_flow_status
            << ",\"orderFlowSpread\":" << q(order_flow_spread)
            << ",\"updatedAt\":\"\"},";
        const auto open = payload.find('{');
        if (open == std::string::npos) return payload;
        payload.insert(open + 1, meters.str());
        return payload;
    }

    CeriousAdvisorySnapshot static_cerious_advisory_snapshot() const {
        CeriousAdvisorySnapshot snapshot;
        snapshot.fetched_at_ms = 0;
        snapshot.next_due_ms = now_ms() + advisory_refresh_ms();
        snapshot.ready = true;
        const auto root = data / "window-payloads" / "cerious";
        snapshot.intelligence = with_intelligence_meters(read_text(root / "intelligence.json").value_or(
            "{\"meters\":{\"riskOnRanking\":50,\"riskPolarity\":\"risk-on\",\"orderFlowStatus\":0,\"orderFlowSpread\":\"Waiting\",\"updatedAt\":\"\"},\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"completed 30m advisory refresh\",\"cadenceMs\":1800000,\"sources\":[\"/api/bars\",\"/api/studies/regression\",\"live top-of-book/last-trade overlay\"],\"uiPolicy\":\"render endpoint payload only\"},\"goose\":{\"strategy\":\"Mean Reversion\",\"direction\":\"Waiting\",\"risk\":\"Medium\",\"confidence\":\"Low\",\"read\":\"Waiting for advisory refresh.\",\"evidence\":[],\"updateCadence\":\"Completed 30m advisory review\"},\"spreadPack\":{\"spreads\":[]},\"liveSpreadSignals\":[],\"macroRegime\":{\"label\":\"Waiting\",\"strength\":50,\"algo\":\"Waiting\",\"factorRows\":[]}}"
        ));
        snapshot.daily_summary = read_text(root / "daily-summary.json").value_or(
            "{\"service\":\"cerious.daily.summary\",\"summaryRead\":\"Waiting for native advisory refresh.\",\"top\":[],\"classification\":[],\"sourcePills\":[],\"eligibleSpreads\":[],\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"completed 30m advisory refresh\",\"cadenceMs\":1800000}}"
        );
        snapshot.macro_regime = read_text(root / "macro-regime.json").value_or(
            "{\"service\":\"macro.regime\",\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"completed 30m advisory refresh\",\"cadenceMs\":1800000},\"label\":\"Waiting\",\"strength\":50,\"algo\":\"Waiting\",\"factorRows\":[],\"read\":\"Waiting for advisory refresh.\"}"
        );
        snapshot.opportunity_map = read_text(root / "opportunity-map.json").value_or(
            "{\"service\":\"signal.cross-spread\",\"subscriptionModel\":{\"owner\":\"cerious-gateway-cpp\",\"cadence\":\"completed 30m advisory refresh\",\"cadenceMs\":1800000},\"rows\":[],\"playbookRows\":[],\"productRows\":[],\"riskChecklistRows\":[]}"
        );
        return snapshot;
    }

    void start_cerious_advisory_refresh() const {
        {
            std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
            if (cerious_advisory_refreshing) return;
            cerious_advisory_refreshing = true;
        }
        std::thread([this]() {
            try {
                auto next = build_cerious_advisory_snapshot(true);
                std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
                if (next.ready) {
                    persist_cerious_advisory_snapshot(next);
                    cerious_advisory_cache = std::move(next);
                }
                cerious_advisory_refreshing = false;
            } catch (const std::exception& ex) {
                std::cerr << "cerious advisory refresh failed: " << ex.what() << std::endl;
                std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
                cerious_advisory_refreshing = false;
            } catch (...) {
                std::cerr << "cerious advisory refresh failed with unknown error" << std::endl;
                std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
                cerious_advisory_refreshing = false;
            }
        }).detach();
    }

    void start_cerious_advisory_scheduler() const {
        {
            std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
            if (cerious_advisory_scheduler_started) return;
            cerious_advisory_scheduler_started = true;
            if (!cerious_advisory_cache) {
                if (auto persisted = load_persisted_cerious_advisory_snapshot()) {
                    cerious_advisory_cache = std::move(persisted);
                }
            }
        }

        std::thread([this]() {
            while (!this->shutdown_requested.load()) {
                try {
                    auto next = this->build_cerious_advisory_snapshot(true);
                    if (next.ready) {
                        this->persist_cerious_advisory_snapshot(next);
                        std::lock_guard<std::mutex> lock(this->cerious_advisory_mutex);
                        this->cerious_advisory_cache = std::move(next);
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "cerious advisory scheduler failed: " << ex.what() << std::endl;
                } catch (...) {
                    std::cerr << "cerious advisory scheduler failed with unknown error" << std::endl;
                }

                const auto cadence_ms = this->advisory_refresh_ms();
                std::uint64_t slept = 0;
                while (!this->shutdown_requested.load() && slept < cadence_ms) {
                    const auto step = std::min<std::uint64_t>(1000ULL, cadence_ms - slept);
                    std::this_thread::sleep_for(std::chrono::milliseconds(step));
                    slept += step;
                }
            }
        }).detach();
    }

    CeriousAdvisorySnapshot cerious_advisory_snapshot(bool blocking_refresh = false) const {
        const auto current = now_ms();
        const auto cadence_ms = advisory_refresh_ms();
        {
            std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
            if (cerious_advisory_cache && current >= cerious_advisory_cache->fetched_at_ms
                && cerious_advisory_cache->ready
                && current - cerious_advisory_cache->fetched_at_ms < cadence_ms) {
                return *cerious_advisory_cache;
            }
            if (cerious_advisory_cache && !blocking_refresh) {
                const auto stale = *cerious_advisory_cache;
                if (!cerious_advisory_refreshing) {
                    // Refresh outside this lock; callers keep the last known good payload.
                } else {
                    return stale;
                }
            }
        }

        if (!blocking_refresh) {
            std::optional<CeriousAdvisorySnapshot> stale;
            {
                std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
                if (cerious_advisory_cache) stale = *cerious_advisory_cache;
            }
            if (!stale) {
                auto next = static_cerious_advisory_snapshot();
                {
                    std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
                    cerious_advisory_cache = next;
                    cerious_advisory_refreshing = false;
                }
                start_cerious_advisory_refresh();
                return next;
            }
            start_cerious_advisory_refresh();
            return *stale;
        }

        auto next = build_cerious_advisory_snapshot();
        {
            std::lock_guard<std::mutex> lock(cerious_advisory_mutex);
            persist_cerious_advisory_snapshot(next);
            cerious_advisory_cache = next;
            cerious_advisory_refreshing = false;
        }
        return next;
    }

    std::string cerious_intelligence_json() const {
        return cerious_advisory_snapshot(false).intelligence;
    }

    std::string cerious_daily_summary_json() const {
        return cerious_advisory_snapshot(false).daily_summary;
    }

    std::string cerious_macro_regime_json() const {
        return cerious_advisory_snapshot(false).macro_regime;
    }

    std::string cerious_opportunity_map_json() const {
        return cerious_advisory_snapshot(false).opportunity_map;
    }

    std::string cerious_subscription_model_json() const {
        const auto cadence_ms = advisory_refresh_ms();
        return "{\"ok\":true,\"runtime\":\"cpp\",\"owner\":\"cerious-gateway-cpp\",\"model\":\"server-owned advisory subscriptions\",\"rules\":["
            "{\"widget\":\"Daily Summary\",\"endpoint\":\"/api/cerious/daily-summary\",\"cadenceMs\":" + std::to_string(cadence_ms) + ",\"sources\":[\"daily bars\",\"weekly context\",\"completed 30m advisory snapshot\",\"live overlay\"]},"
            "{\"widget\":\"GOOSE\",\"endpoint\":\"/api/cerious/intelligence\",\"cadenceMs\":" + std::to_string(cadence_ms) + ",\"sources\":[\"relative value spread pack\",\"macro regime\",\"daily/weekly context\",\"live overlay\"]},"
            "{\"widget\":\"Live Spread Signals\",\"endpoint\":\"/api/cerious/intelligence\",\"payload\":\"liveSpreadSignals\",\"cadenceMs\":" + std::to_string(cadence_ms) + ",\"sources\":[\"completed 30m study bars\",\"server regression study\",\"live last-trade overlay\"]},"
            "{\"widget\":\"Relative Spread Visuals\",\"endpoint\":\"/api/cerious/intelligence\",\"payload\":\"spreadPack.spreads\",\"cadenceMs\":" + std::to_string(cadence_ms) + ",\"sources\":[\"daily baseline\",\"weekly context\",\"completed 30m study bars\",\"live overlay\"]},"
            "{\"widget\":\"Relative Spread Charts\",\"endpoint\":\"/api/cerious/intelligence\",\"payload\":\"spreadPack.spreads[].bars\",\"cadenceMs\":" + std::to_string(cadence_ms) + ",\"sources\":[\"server historical bars\",\"advisory chart bars\",\"live overlay marker\"]},"
            "{\"widget\":\"Macro Regime Summary\",\"endpoint\":\"/api/cerious/macro-regime\",\"cadenceMs\":" + std::to_string(cadence_ms) + ",\"sources\":[\"relative value scores\",\"macro factor model\",\"weekly context\"]},"
            "{\"widget\":\"Cross-Spread Opportunity Map\",\"endpoint\":\"/api/cerious/opportunity-map\",\"cadenceMs\":" + std::to_string(cadence_ms) + ",\"sources\":[\"spread rankings\",\"macro regime\",\"liquidity/live state\"]}"
            "]}";
    }

    httplib::Result execution_get(const std::string& path) const {
        httplib::Client client(args.execution_host, args.execution_port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(4, 0);
        return client.Get(path);
    }

    httplib::Result execution_post(const std::string& path, const std::string& body) const {
        httplib::Client client(args.execution_host, args.execution_port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(5, 0);
        return client.Post(path, body, "application/json");
    }

    std::string execution_state_json() const {
        auto state = execution_get("/state");
        if (state && state->status >= 200 && state->status < 300) return state->body;
        return "{\"service\":\"cerious.exchange\",\"simOrders\":[],\"simPositions\":[],\"fills\":{},\"simMessages\":[\"CERIOUS EXCHANGE STATE UNAVAILABLE\"]}";
    }

    std::string order_state_read_model_json() const {
        const auto state = execution_state_json();
        const auto sim_orders = get_json_member(state, "simOrders").value_or("[]");
        const auto sim_positions = get_json_member(state, "simPositions").value_or("[]");
        const auto fills = get_json_member(state, "fills").value_or("{}");
        const auto sim_messages = get_json_member(state, "simMessages").value_or("[]");
        const auto session_metrics = get_json_member(state, "sessionMetrics").value_or("{}");
        const auto exchange_service = get_string(state, "service", "cerious.exchange");
        const auto exchange_fetched_at = get_string(state, "fetchedAt", "");
        const bool unavailable = state.find("EXCHANGE STATE UNAVAILABLE") != std::string::npos;
        const auto fetched_at = utc_iso(std::chrono::system_clock::now()) + "Z";
        const auto working_count = json_array_count(sim_orders);
        const auto position_count = json_array_count(sim_positions);
        const auto fill_count = json_key_occurrences(fills, "timestamp");
        double open_pnl = 0.0;
        double closed_pnl = 0.0;
        double total_pnl = 0.0;
        for (const auto& position : json_object_array(sim_positions)) {
            const auto row_open = get_number(position, "openPnl").value_or(0.0);
            const auto row_closed = get_number(position, "realizedPnl").value_or(get_number(position, "closedPnl").value_or(0.0));
            const auto row_total = get_number(position, "totalPnl").value_or(row_open + row_closed);
            open_pnl += row_open;
            closed_pnl += row_closed;
            total_pnl += row_total;
        }

        std::ostringstream out;
        out << "{\"ok\":" << (unavailable ? "false" : "true")
            << ",\"service\":\"cerious.order-state\""
            << ",\"runtime\":\"cpp\""
            << ",\"owner\":\"gateway-read-model\""
            << ",\"source\":" << q(exchange_service)
            << ",\"revision\":" << now_ms()
            << ",\"fetchedAt\":" << q(fetched_at)
            << ",\"exchangeFetchedAt\":" << q(exchange_fetched_at)
            << ",\"sync\":{\"snapshot\":\"http\",\"delta\":\"exchange-events\",\"uiPolicy\":\"render backend state only\"}"
            << ",\"orders\":" << sim_orders
            << ",\"positions\":" << sim_positions
            << ",\"fills\":" << fills
            << ",\"simOrders\":" << sim_orders
            << ",\"simPositions\":" << sim_positions
            << ",\"simMessages\":" << sim_messages
            << ",\"summary\":{"
            << "\"positionCount\":" << position_count
            << ",\"workingOrderCount\":" << working_count
            << ",\"fillCount\":" << fill_count
            << ",\"openPnl\":" << json_number(open_pnl, 2)
            << ",\"closedPnl\":" << json_number(closed_pnl, 2)
            << ",\"totalPnl\":" << json_number(total_pnl, 2)
            << ",\"currentPnl\":" << json_number(get_number(session_metrics, "currentPnl").value_or(total_pnl), 2)
            << ",\"sessionPeakPnl\":" << json_number(get_number(session_metrics, "sessionPeakPnl").value_or(std::max(0.0, total_pnl)), 2)
            << ",\"sessionLowPnl\":" << json_number(get_number(session_metrics, "sessionLowPnl").value_or(std::min(0.0, total_pnl)), 2)
            << ",\"drawdown\":" << json_number(get_number(session_metrics, "drawdown").value_or(std::max(0.0, -total_pnl)), 2)
            << ",\"maxDrawdown\":" << json_number(get_number(session_metrics, "maxDrawdown").value_or(std::max(0.0, -total_pnl)), 2)
            << ",\"sessionMetrics\":" << session_metrics
            << "}"
            << ",\"state\":" << state
            << "}";
        return out.str();
    }

    std::string live_trade_analytics_json() const {
        const auto state = execution_state_json();
        const auto sim_orders = get_json_member(state, "simOrders").value_or("[]");
        const auto sim_positions = get_json_member(state, "simPositions").value_or("[]");
        const auto fills = get_json_member(state, "fills").value_or("{}");
        const auto session_metrics = get_json_member(state, "sessionMetrics").value_or("{}");
        const auto fetched_at = utc_iso(std::chrono::system_clock::now()) + "Z";
        const auto fill_count = json_key_occurrences(fills, "timestamp");
        const auto working_count = json_array_count(sim_orders);

        double open_pnl = 0.0;
        double closed_pnl = 0.0;
        double total_pnl = 0.0;
        for (const auto& position : json_object_array(sim_positions)) {
            const auto row_open = get_number(position, "openPnl").value_or(0.0);
            const auto row_closed = get_number(position, "realizedPnl").value_or(get_number(position, "closedPnl").value_or(0.0));
            const auto row_total = get_number(position, "totalPnl").value_or(row_open + row_closed);
            open_pnl += row_open;
            closed_pnl += row_closed;
            total_pnl += row_total;
        }

        constexpr double account_size = 500000.0;
        const auto current_pnl = get_number(session_metrics, "currentPnl").value_or(total_pnl);
        const auto peak_pnl = get_number(session_metrics, "sessionPeakPnl").value_or(std::max(0.0, current_pnl));
        const auto low_pnl = get_number(session_metrics, "sessionLowPnl").value_or(std::min(0.0, current_pnl));
        const auto max_drawdown = get_number(session_metrics, "maxDrawdown").value_or(std::max(0.0, -low_pnl));
        const auto current_drawdown = get_number(session_metrics, "drawdown").value_or(std::max(0.0, -current_pnl));
        const auto return_pct = total_pnl / account_size;
        const auto drawdown_pct = max_drawdown / account_size;
        const auto expectancy = fill_count > 0 ? total_pnl / static_cast<double>(fill_count) : 0.0;
        const auto calmar = drawdown_pct > 0.0 ? return_pct / drawdown_pct : 0.0;

        std::ostringstream out;
        out << "{\"service\":\"analytics.trade.live\""
            << ",\"source\":\"live\""
            << ",\"runtime\":\"cpp\""
            << ",\"owner\":\"cerious-gateway-cpp\""
            << ",\"fetchedAt\":" << q(fetched_at)
            << ",\"status\":\"Live fill journal from C++ exchange read model\""
            << ",\"riskLevel\":" << q(max_drawdown > 0.0 ? "Controlled" : "Waiting")
            << ",\"metrics\":{"
            << "\"rows\":" << fill_count
            << ",\"accountSize\":" << json_number(account_size, 2)
            << ",\"total\":" << json_number(total_pnl, 2)
            << ",\"openPnl\":" << json_number(open_pnl, 2)
            << ",\"closedPnl\":" << json_number(closed_pnl, 2)
            << ",\"currentPnl\":" << json_number(current_pnl, 2)
            << ",\"returnPct\":" << json_number(return_pct, 8)
            << ",\"winRate\":0"
            << ",\"sharpe\":0"
            << ",\"sortino\":0"
            << ",\"calmar\":" << json_number(calmar, 8)
            << ",\"profitFactor\":0"
            << ",\"expectancy\":" << json_number(expectancy, 2)
            << ",\"drawdown\":" << json_number(max_drawdown, 2)
            << ",\"currentDrawdown\":" << json_number(current_drawdown, 2)
            << ",\"drawdownPct\":" << json_number(drawdown_pct, 8)
            << ",\"sessionPeakPnl\":" << json_number(peak_pnl, 2)
            << ",\"sessionLowPnl\":" << json_number(low_pnl, 2)
            << ",\"studyCoverage\":1"
            << ",\"largestLossPct\":0"
            << ",\"knownInstrumentRows\":" << fill_count
            << ",\"syntheticUnits\":" << fill_count
            << ",\"totalContracts\":" << fill_count
            << ",\"workingOrderCount\":" << working_count
            << ",\"peakEquity\":" << json_number(account_size + peak_pnl, 2)
            << ",\"troughEquity\":" << json_number(account_size + low_pnl, 2)
            << ",\"endEquity\":" << json_number(account_size + current_pnl, 2)
            << ",\"productSummary\":\"Server live journal\""
            << "}"
            << ",\"curve\":["
            << "{\"index\":0,\"equity\":" << json_number(account_size, 2) << ",\"pnl\":0,\"drawdown\":0,\"maxDrawdown\":0},"
            << "{\"index\":1,\"equity\":" << json_number(account_size + peak_pnl, 2) << ",\"pnl\":" << json_number(peak_pnl, 2) << ",\"drawdown\":0,\"maxDrawdown\":" << json_number(max_drawdown, 2) << "},"
            << "{\"index\":2,\"equity\":" << json_number(account_size + low_pnl, 2) << ",\"pnl\":" << json_number(low_pnl, 2) << ",\"drawdown\":" << json_number(max_drawdown, 2) << ",\"maxDrawdown\":" << json_number(max_drawdown, 2) << "},"
            << "{\"index\":3,\"equity\":" << json_number(account_size + current_pnl, 2) << ",\"pnl\":" << json_number(current_pnl, 2) << ",\"drawdown\":" << json_number(current_drawdown, 2) << ",\"maxDrawdown\":" << json_number(max_drawdown, 2) << "}"
            << "]"
            << ",\"records\":[]"
            << ",\"productTotals\":[]"
            << ",\"studies\":["
            << "{\"study\":\"Live P&L authority\",\"passed\":true,\"result\":\"C++ exchange\",\"read\":\"Open, realized, total P&L, and max drawdown are published by the backend read model.\"},"
            << "{\"study\":\"Max drawdown\",\"passed\":true,\"result\":" << q("$" + json_number(max_drawdown, 2)) << ",\"read\":\"Worst negative session P&L versus zero, sampled from realized plus open P&L.\"}"
            << "]"
            << ",\"report\":["
            << "{\"label\":\"Total P&L\",\"value\":" << q("$" + json_number(total_pnl, 2)) << ",\"read\":\"Server total P&L.\"},"
            << "{\"label\":\"Max Drawdown\",\"value\":" << q("$" + json_number(max_drawdown, 2)) << ",\"read\":\"Server max drawdown.\"}"
            << "]"
            << "}";
        return out.str();
    }

    std::string imported_trade_analytics_json(const std::string& body, int& status, const std::string& filename_override = "") const {
        const auto filename = filename_override.empty() ? get_string(body, "filename", "fills.csv") : filename_override;
        auto content = get_string(body, "content", "");
        if (content.empty() && body.find(',') != std::string::npos && body.find('\n') != std::string::npos) {
            content = body;
        }
        constexpr double account_size = 500000.0;
        const auto table = parse_csv_table(content);
        if (table.size() < 2) {
            status = 400;
            return "{\"ok\":false,\"detail\":\"CSV needs a header row and at least one fill row\"}";
        }

        const auto& headers = table.front();
        const int row_type_idx = csv_header_index(headers, {"row_type", "rowType", "type"});
        const int pnl_idx = csv_header_index(headers, {"pnl", "realizedPnl", "closedPnl", "netPnl", "tradePnl"});
        const int synthetic_idx = csv_header_index(headers, {"synthetic_units", "syntheticUnits", "spreadUnits", "spread_units"});
        const int contracts_idx = csv_header_index(headers, {"contract_count", "contractCount", "contracts", "totalContracts"});
        const int account_max_dd_idx = csv_header_index(headers, {"accountMaxDrawdown", "maxDrawdown"});
        const int account_dd_idx = csv_header_index(headers, {"accountDrawdown", "drawdown"});
        const int account_equity_idx = csv_header_index(headers, {"accountEquity", "equity"});
        const int account_pnl_idx = csv_header_index(headers, {"accountPnl", "sessionPnl", "currentPnl", "dayPnl"});
        const int cumulative_idx = csv_header_index(headers, {"cumulativePnl", "cumulative_pnl"});
        const int timestamp_idx = csv_header_index(headers, {"timestamp", "time", "filledAt", "fillTime", "createdAt"});
        const int product_idx = csv_header_index(headers, {"product", "symbol", "instrument", "market_key", "marketKey"});
        const int side_idx = csv_header_index(headers, {"side", "buySell", "buy/sell"});
        const int price_idx = csv_header_index(headers, {"price", "fillPrice", "fill_price"});
        const int qty_idx = csv_header_index(headers, {"size", "qty", "quantity"});

        double total = 0.0;
        double cumulative = 0.0;
        double last_curve_pnl = 0.0;
        double peak = 0.0;
        double session_low = 0.0;
        double max_drawdown = 0.0;
        double synthetic_units = 0.0;
        double contracts = 0.0;
        int fill_rows = 0;
        int wins = 0;
        int losses = 0;
        double gross_profit = 0.0;
        double gross_loss = 0.0;
        std::unordered_map<std::string, double> product_pnl;
        std::unordered_map<std::string, double> product_units;
        std::unordered_map<std::string, double> product_contracts;
        std::vector<double> realized_events;
        struct CurvePoint {
            double equity = account_size;
            double pnl = 0.0;
            double drawdown = 0.0;
            double max_drawdown = 0.0;
        };
        std::vector<CurvePoint> curve;
        curve.push_back({account_size, 0.0, 0.0, 0.0});
        struct ImportLot {
            std::string side;
            double price = 0.0;
            double qty = 0.0;
        };
        std::unordered_map<std::string, std::deque<ImportLot>> open_lots;
        int derived_pnl_rows = 0;
        bool used_account_curve_samples = false;

        struct ImportCsvRow {
            std::vector<std::string> cells;
            std::string timestamp;
            std::uint64_t timestamp_ms = 0;
            std::size_t ordinal = 0;
        };
        std::vector<ImportCsvRow> import_rows;
        import_rows.reserve(table.size() - 1);
        for (std::size_t r = 1; r < table.size(); ++r) {
            const auto& row = table[r];
            if (row.empty()) continue;
            if (row_type_idx >= 0 && row_type_idx < static_cast<int>(row.size())) {
                const auto row_type = lower_ascii(trim_copy(row[static_cast<std::size_t>(row_type_idx)]));
                if (!row_type.empty() && row_type != "fill") continue;
            }
            const auto timestamp = timestamp_idx >= 0 && timestamp_idx < static_cast<int>(row.size())
                ? trim_copy(row[static_cast<std::size_t>(timestamp_idx)])
                : std::string{};
            import_rows.push_back({row, timestamp, parse_iso_utc_ms(timestamp), r});
        }
        std::stable_sort(import_rows.begin(), import_rows.end(), [](const ImportCsvRow& a, const ImportCsvRow& b) {
            if (a.timestamp_ms && b.timestamp_ms && a.timestamp_ms != b.timestamp_ms) {
                return a.timestamp_ms < b.timestamp_ms;
            }
            if (!a.timestamp.empty() && !b.timestamp.empty() && a.timestamp != b.timestamp) {
                return a.timestamp < b.timestamp;
            }
            if (a.timestamp.empty() != b.timestamp.empty()) return !a.timestamp.empty();
            return a.ordinal > b.ordinal;
        });

        std::unordered_map<std::string, std::vector<MarketBar>> historical_marks;
        std::unordered_set<std::string> historical_mark_attempted;
        auto mark_for = [&](const std::string& symbol, std::uint64_t timestamp_ms) -> std::optional<double> {
            if (!timestamp_ms) return std::nullopt;
            const auto key = canonical_market_symbol(symbol);
            if (!historical_mark_attempted.count(key)) {
                historical_mark_attempted.insert(key);
                historical_marks[key] = history_bars(key, "1m", 2000);
                std::sort(historical_marks[key].begin(), historical_marks[key].end(), [](const MarketBar& a, const MarketBar& b) {
                    return a.timestamp < b.timestamp;
                });
            }
            auto found = historical_marks.find(key);
            if (found == historical_marks.end() || found->second.empty()) return std::nullopt;
            const auto& bars = found->second;
            auto it = std::upper_bound(bars.begin(), bars.end(), timestamp_ms, [](std::uint64_t ts, const MarketBar& bar) {
                return ts < bar.timestamp;
            });
            if (it == bars.begin()) return std::nullopt;
            --it;
            return finite(it->close) ? std::optional<double>{it->close} : std::nullopt;
        };
        bool used_historical_market_marks = false;
        auto open_pnl_at = [&](std::uint64_t timestamp_ms) {
            double open_pnl = 0.0;
            bool marked = false;
            for (const auto& [symbol, lots] : open_lots) {
                const auto mark = mark_for(symbol, timestamp_ms);
                if (!mark) continue;
                const auto def = product_def_for(symbol);
                if (def.tick_size <= 0.0 || def.tick_value <= 0.0) continue;
                for (const auto& lot : lots) {
                    if (lot.qty <= 1e-9) continue;
                    const auto price_delta = lot.side == "buy" ? *mark - lot.price : lot.price - *mark;
                    open_pnl += (price_delta / def.tick_size) * def.tick_value * lot.qty;
                    marked = true;
                }
            }
            return std::pair<double, bool>{std::round(open_pnl * 100.0) / 100.0, marked};
        };

        for (const auto& import_row : import_rows) {
            const auto& row = import_row.cells;
            const auto value_at = [&](int idx) -> std::string {
                return idx >= 0 && idx < static_cast<int>(row.size()) ? row[static_cast<std::size_t>(idx)] : std::string{};
            };
            const auto product = product_idx >= 0 ? trim_copy(value_at(product_idx)) : std::string("UNKNOWN");
            const auto product_key = canonical_market_symbol(product.empty() ? "UNKNOWN" : product);
            auto pnl = pnl_idx >= 0 ? csv_number(value_at(pnl_idx), 0.0) : 0.0;
            const auto side = lower_ascii(trim_copy(value_at(side_idx)));
            const auto price = csv_number(value_at(price_idx), std::numeric_limits<double>::quiet_NaN());
            auto qty = std::abs(csv_number(value_at(qty_idx), 0.0));
            if (std::abs(pnl) <= 1e-9 && (side == "buy" || side == "sell") && std::isfinite(price) && qty > 0.0) {
                const auto def = product_def_for(product_key);
                if (def.tick_size > 0.0 && def.tick_value > 0.0) {
                    auto& lots = open_lots[product_key];
                    double derived = 0.0;
                    while (qty > 1e-9 && !lots.empty() && lots.front().side != side) {
                        auto& open = lots.front();
                        const auto matched = std::min(qty, open.qty);
                        const auto price_delta = side == "sell" ? price - open.price : open.price - price;
                        derived += (price_delta / def.tick_size) * def.tick_value * matched;
                        qty -= matched;
                        open.qty -= matched;
                        if (open.qty <= 1e-9) lots.pop_front();
                    }
                    if (qty > 1e-9) lots.push_back({side, price, qty});
                    if (std::abs(derived) > 1e-9) {
                        pnl = std::round(derived * 100.0) / 100.0;
                        ++derived_pnl_rows;
                    }
                }
            }
            total += pnl;
            const auto account_pnl_sample = account_pnl_idx >= 0
                ? csv_number(value_at(account_pnl_idx), std::numeric_limits<double>::quiet_NaN())
                : std::numeric_limits<double>::quiet_NaN();
            const auto account_equity_sample = account_equity_idx >= 0
                ? csv_number(value_at(account_equity_idx), std::numeric_limits<double>::quiet_NaN())
                : std::numeric_limits<double>::quiet_NaN();
            double curve_pnl = cumulative;
            if (std::isfinite(account_pnl_sample)) {
                curve_pnl = account_pnl_sample;
                used_account_curve_samples = true;
            } else if (std::isfinite(account_equity_sample)) {
                curve_pnl = account_equity_sample - account_size;
                used_account_curve_samples = true;
            } else {
                cumulative = cumulative_idx >= 0 ? csv_number(value_at(cumulative_idx), cumulative + pnl) : cumulative + pnl;
                curve_pnl = cumulative;
                const auto [marked_open_pnl, marked] = open_pnl_at(import_row.timestamp_ms);
                if (marked) {
                    curve_pnl = cumulative + marked_open_pnl;
                    used_historical_market_marks = true;
                }
            }
            peak = std::max(peak, curve_pnl);
            session_low = std::min(session_low, curve_pnl);
            const auto current_drawdown = std::max(0.0, -curve_pnl);
            max_drawdown = std::max(max_drawdown, std::max(0.0, -session_low));
            if (account_max_dd_idx >= 0) max_drawdown = std::max(max_drawdown, std::abs(csv_number(value_at(account_max_dd_idx), 0.0)));
            if (account_dd_idx >= 0) max_drawdown = std::max(max_drawdown, std::abs(csv_number(value_at(account_dd_idx), 0.0)));
            const auto row_units = std::abs(csv_number(value_at(synthetic_idx), 0.0));
            const auto row_contracts = std::abs(csv_number(value_at(contracts_idx), 0.0));
            synthetic_units += row_units;
            contracts += row_contracts;
            if (std::abs(pnl) > 1e-9) {
                realized_events.push_back(pnl);
                if (pnl > 0.0) {
                    ++wins;
                    gross_profit += pnl;
                } else {
                    ++losses;
                    gross_loss += std::abs(pnl);
                }
            }
            product_pnl[product_key.empty() ? "UNKNOWN" : product_key] += pnl;
            product_units[product_key.empty() ? "UNKNOWN" : product_key] += row_units;
            product_contracts[product_key.empty() ? "UNKNOWN" : product_key] += row_contracts;
            last_curve_pnl = curve_pnl;
            curve.push_back({
                account_size + curve_pnl,
                curve_pnl,
                current_drawdown,
                max_drawdown,
            });
            ++fill_rows;
        }

        if (fill_rows <= 0) {
            status = 400;
            return "{\"ok\":false,\"detail\":\"No fill rows found in CSV\"}";
        }

        const auto return_pct = total / account_size;
        const auto drawdown_pct = max_drawdown / account_size;
        const auto closed_trade_count = static_cast<int>(realized_events.size());
        const auto win_rate = closed_trade_count > 0 ? static_cast<double>(wins) / static_cast<double>(closed_trade_count) : 0.0;
        const auto profit_factor = gross_loss > 0.0 ? gross_profit / gross_loss : gross_profit;
        const auto expectancy = closed_trade_count > 0 ? total / static_cast<double>(closed_trade_count) : 0.0;
        const auto calmar = drawdown_pct > 0.0 ? return_pct / drawdown_pct : 0.0;
        double mean_return = 0.0;
        double stdev_return = 0.0;
        double downside_deviation = 0.0;
        if (closed_trade_count > 0) {
            std::vector<double> returns;
            returns.reserve(realized_events.size());
            for (const auto event_pnl : realized_events) returns.push_back(event_pnl / account_size);
            mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / static_cast<double>(returns.size());
            if (returns.size() > 1) {
                double variance = 0.0;
                for (const auto value : returns) variance += (value - mean_return) * (value - mean_return);
                stdev_return = std::sqrt(variance / static_cast<double>(returns.size() - 1));
            }
            double downside_sum = 0.0;
            for (const auto value : returns) {
                const auto downside = std::min(0.0, value);
                downside_sum += downside * downside;
            }
            downside_deviation = std::sqrt(downside_sum / static_cast<double>(returns.size()));
        }
        const auto sharpe = stdev_return > 0.0 ? (mean_return / stdev_return) * std::sqrt(static_cast<double>(std::max(1, closed_trade_count))) : 0.0;
        const auto sortino = downside_deviation > 0.0 ? (mean_return / downside_deviation) * std::sqrt(static_cast<double>(std::max(1, closed_trade_count))) : 0.0;
        const auto largest_loss_pct = realized_events.empty()
            ? 0.0
            : std::abs(std::min(0.0, *std::min_element(realized_events.begin(), realized_events.end()))) / account_size;

        std::ostringstream product_totals;
        product_totals << "[";
        bool first_product = true;
        for (const auto& [product, pnl] : product_pnl) {
            if (!first_product) product_totals << ",";
            first_product = false;
            product_totals << "{\"instrument\":" << q(product)
                << ",\"pnl\":" << json_number(pnl, 2)
                << ",\"syntheticUnits\":" << json_number(product_units[product], 2)
                << ",\"contracts\":" << json_number(product_contracts[product], 2) << "}";
        }
        product_totals << "]";

        std::ostringstream curve_json;
        curve_json << "[";
        for (std::size_t i = 0; i < curve.size(); ++i) {
            if (i) curve_json << ",";
            curve_json << "{\"index\":" << i
                << ",\"equity\":" << json_number(curve[i].equity, 2)
                << ",\"pnl\":" << json_number(curve[i].pnl, 2)
                << ",\"drawdown\":" << json_number(curve[i].drawdown, 2)
                << ",\"maxDrawdown\":" << json_number(curve[i].max_drawdown, 2)
                << "}";
        }
        curve_json << "]";

        const auto curve_basis = used_account_curve_samples
            ? std::string("account equity samples")
            : used_historical_market_marks
                ? std::string("server historical market marks")
                : std::string("realized fills only");
        const auto fetched_at = utc_iso(std::chrono::system_clock::now()) + "Z";
        std::ostringstream out;
        out << "{\"ok\":true"
            << ",\"service\":\"analytics.trade.imported\""
            << ",\"runtime\":\"cpp\""
            << ",\"owner\":\"cerious-gateway-cpp\""
            << ",\"source\":\"imported\""
            << ",\"filename\":" << q(filename)
            << ",\"fetchedAt\":" << q(fetched_at)
            << ",\"status\":" << q("Imported " + std::to_string(fill_rows) + " fill row(s); parsed by C++ gateway"
                + (derived_pnl_rows > 0 ? "; derived realized P&L for " + std::to_string(derived_pnl_rows) + " closing row(s)." : "."))
            << ",\"riskLevel\":" << q(max_drawdown > account_size * 0.02 ? "High" : max_drawdown > account_size * 0.01 ? "Elevated" : "Controlled")
            << ",\"metrics\":{"
            << "\"rows\":" << fill_rows
            << ",\"closedTrades\":" << closed_trade_count
            << ",\"accountSize\":" << json_number(account_size, 2)
            << ",\"total\":" << json_number(total, 2)
            << ",\"returnPct\":" << json_number(return_pct, 8)
            << ",\"winRate\":" << json_number(win_rate, 8)
            << ",\"sharpe\":" << json_number(sharpe, 8)
            << ",\"sortino\":" << json_number(sortino, 8)
            << ",\"calmar\":" << json_number(calmar, 8)
            << ",\"profitFactor\":" << json_number(profit_factor, 8)
            << ",\"expectancy\":" << json_number(expectancy, 2)
            << ",\"drawdown\":" << json_number(max_drawdown, 2)
            << ",\"drawdownPct\":" << json_number(drawdown_pct, 8)
            << ",\"studyCoverage\":1"
            << ",\"largestLossPct\":" << json_number(largest_loss_pct, 8)
            << ",\"knownInstrumentRows\":" << fill_rows
            << ",\"syntheticUnits\":" << json_number(synthetic_units, 2)
            << ",\"totalContracts\":" << json_number(contracts, 2)
            << ",\"peakEquity\":" << json_number(account_size + peak, 2)
            << ",\"troughEquity\":" << json_number(account_size + session_low, 2)
            << ",\"endEquity\":" << json_number(account_size + last_curve_pnl, 2)
            << ",\"productSummary\":\"Imported CSV parsed by C++ gateway; factors use realized/closed events\""
            << ",\"curveBasis\":" << q(curve_basis)
            << "}"
            << ",\"curve\":" << curve_json.str()
            << ",\"records\":[]"
            << ",\"productTotals\":" << product_totals.str()
            << ",\"studies\":["
            << "{\"study\":\"CSV parser\",\"passed\":true,\"result\":\"C++ gateway\",\"read\":\"Trade Analytics import math and file handling ran on the backend.\"},"
            << "{\"study\":\"Metric basis\",\"passed\":true,\"result\":\"Closed trades\",\"read\":\"Win rate, expectancy, Sharpe, Sortino, profit factor, and Calmar are calculated from realized P&L events, not raw entry rows.\"}"
            << "]"
            << ",\"report\":["
            << "{\"label\":\"Import\",\"value\":" << q(filename) << ",\"read\":\"Parsed by backend.\"},"
            << "{\"label\":\"Metric Basis\",\"value\":" << q(std::to_string(closed_trade_count) + " closed events") << ",\"read\":\"A raw fill row is not counted as a win or loss unless it contributes realized P&L.\"},"
            << "{\"label\":\"Max Drawdown\",\"value\":" << q("$" + json_number(max_drawdown, 2)) << ",\"read\":" << q(used_account_curve_samples
                ? "Worst negative account P&L versus zero from backend equity/P&L samples in this imported file."
                : used_historical_market_marks
                    ? "Worst negative account P&L versus zero from realized P&L plus backend historical market marks for open inventory."
                    : "Worst negative realized P&L versus zero from this fills-only file. Open-position drawdown requires backend market marks or exported equity/P&L samples.") << "}"
            << "]"
            << "}";
        return out.str();
    }

    std::string order_id_from_payload(const std::string& body) const {
        auto order_id = trim_copy(get_string(body, "orderId", ""));
        if (order_id.empty()) order_id = trim_copy(get_string(body, "id", ""));
        if (order_id.empty()) order_id = trim_copy(get_string(body, "clientOrderId", ""));
        if (order_id.empty()) order_id = trim_copy(get_string(body, "order_id", ""));
        return order_id;
    }

    std::string payload_with_order_id(const std::string& body, const std::string& order_id) const {
        if (!trim_copy(get_string(body, "orderId", "")).empty()) return body;
        const auto open = body.find('{');
        if (open == std::string::npos) {
            return "{\"orderId\":" + q(order_id) + "}";
        }
        return body.substr(0, open + 1) + "\"orderId\":" + q(order_id) + "," + body.substr(open + 1);
    }

    std::string wrap_execution_event(const std::string& event_json) const {
        return "{\"ok\":true,\"runtime\":\"cpp\",\"event\":" + event_json
            + ",\"state\":" + execution_state_json() + "}";
    }

    std::string wrap_state_payload(const std::string& state) const {
        return "{\"ok\":true,\"service\":\"cerious.exchange\",\"state\":" + state
            + ",\"simOrders\":[],\"simPositions\":[],\"fills\":{},\"simMessages\":[]}";
    }

    static double round_to_tick(double price, double tick_size) {
        if (!finite(price) || !finite(tick_size) || tick_size <= 0.0) return price;
        return std::round(price / tick_size) * tick_size;
    }

    static std::vector<std::string> json_objects_with_key(const std::string& json, const std::string& key) {
        std::vector<std::string> objects;
        const auto needle = "\"" + key + "\"";
        std::size_t pos = 0;
        while ((pos = json.find(needle, pos)) != std::string::npos) {
            const auto start = json.rfind('{', pos);
            if (start == std::string::npos) {
                pos += needle.size();
                continue;
            }
            int depth = 0;
            bool in_string = false;
            bool escaped = false;
            for (std::size_t i = start; i < json.size(); ++i) {
                const char ch = json[i];
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (ch == '\\' && in_string) {
                    escaped = true;
                    continue;
                }
                if (ch == '"') {
                    in_string = !in_string;
                    continue;
                }
                if (in_string) continue;
                if (ch == '{') ++depth;
                else if (ch == '}') {
                    --depth;
                    if (depth == 0) {
                        objects.push_back(json.substr(start, i - start + 1));
                        pos = i + 1;
                        break;
                    }
                }
            }
            if (pos <= start) pos = start + 1;
        }
        return objects;
    }

    void remember_algo_cover_policy(const std::string& order_json) const {
        const auto order_id = get_string(order_json, "orderId", "");
        if (order_id.empty()) return;
        if (get_string(order_json, "source", "") != "algo") return;
        if (get_string(order_json, "algoRole", "") != "entry") return;
        const auto cover_ticks = get_number(order_json, "coverTicksFromFill").value_or(0.0);
        const auto tick_size = get_number(order_json, "coverTickSize").value_or(0.0);
        if (!finite(cover_ticks) || cover_ticks <= 0.0 || !finite(tick_size) || tick_size <= 0.0) return;

        AlgoCoverPolicy policy;
        policy.symbol = canonical_market_symbol(get_string(order_json, "marketKey", get_string(order_json, "symbol", "")));
        policy.strategy = get_string(order_json, "strategy", "");
        policy.algo_id = get_string(order_json, "algoId", "");
        policy.algo_name = get_string(order_json, "algoName", "");
        policy.layer = static_cast<int>(std::llround(get_number(order_json, "layer").value_or(0.0)));
        policy.quantity = std::max(1, static_cast<int>(std::llround(
            get_number(order_json, "size").value_or(get_number(order_json, "quantity").value_or(1.0)))));
        policy.cover_ticks = cover_ticks;
        policy.tick_size = tick_size;

        std::lock_guard<std::mutex> lock(algo_cover_mutex);
        algo_cover_policies[order_id] = std::move(policy);
    }

    void process_exchange_fill_events(const std::string& state_or_response_json) const {
        std::vector<std::string> cover_orders;
        for (const auto& fill : json_objects_with_key(state_or_response_json, "orderId")) {
            const auto order_id = get_string(fill, "orderId", "");
            if (order_id.empty()) continue;
            const auto fill_status = lower_ascii(get_string(fill, "type", get_string(fill, "status", "")));
            const auto raw_qty = get_number(fill, "size").value_or(get_number(fill, "qty").value_or(get_number(fill, "fillQuantity").value_or(0.0)));
            const auto is_fill_status = fill_status == "filled" || fill_status == "partial" || fill_status == "partial_fill" || fill_status == "fill";
            if (!is_fill_status || raw_qty <= 0.0) continue;
            const auto event_ts = get_string(fill, "timestamp",
                get_string(fill, "timestampMs", get_string(fill, "ts", "0")));
            const auto event_sequence = get_string(fill, "sequence", "0");
            const auto event_price = get_string(fill, "price", get_string(fill, "executionPrice", "0"));
            const auto event_qty = get_string(fill, "size", get_string(fill, "qty", get_string(fill, "fillQuantity", "0")));
            const auto event_key = order_id + "|" + event_ts + "|" + event_sequence + "|" + event_price + "|" + event_qty;

            AlgoCoverPolicy policy;
            {
                std::lock_guard<std::mutex> lock(algo_cover_mutex);
                if (!processed_sim_fill_events.insert(event_key).second) continue;
                const auto policy_it = algo_cover_policies.find(order_id);
                if (policy_it != algo_cover_policies.end()) {
                    policy = policy_it->second;
                }
            }

            const auto role = get_string(fill, "algoRole", "");
            const auto source = get_string(fill, "source", "");
            if (source != "algo" || role != "entry") continue;
            const auto symbol = canonical_market_symbol(get_string(fill, "marketKey", get_string(fill, "symbol", policy.symbol)));
            const auto fill_price = get_number(fill, "price").value_or(get_number(fill, "executionPrice").value_or(std::nan("")));
            const auto fill_qty = std::max(1, static_cast<int>(std::llround(raw_qty)));
            if (!finite(fill_price) || fill_qty <= 0) continue;
            if (policy.cover_ticks <= 0.0 || policy.tick_size <= 0.0) {
                policy.symbol = symbol;
                policy.strategy = get_string(fill, "strategy", "");
                policy.algo_id = get_string(fill, "algoId", "");
                policy.algo_name = get_string(fill, "algoName", "");
                policy.layer = static_cast<int>(std::llround(get_number(fill, "layer").value_or(0.0)));
                policy.quantity = std::max(1, static_cast<int>(std::llround(
                    get_number(fill, "originalQty").value_or(get_number(fill, "size").value_or(static_cast<double>(fill_qty))))));
                policy.cover_ticks = get_number(fill, "coverTicksFromFill").value_or(0.0);
                policy.tick_size = get_number(fill, "coverTickSize").value_or(0.0);
            }
            if (policy.cover_ticks <= 0.0 || policy.tick_size <= 0.0) continue;

            int cover_qty = fill_qty;
            {
                std::lock_guard<std::mutex> lock(algo_cover_mutex);
                auto& already_covered = covered_algo_entry_qty[order_id];
                const auto max_cover_qty = std::max(policy.quantity, fill_qty);
                const auto remaining_cover_qty = max_cover_qty - already_covered;
                if (remaining_cover_qty <= 0) {
                    append_algo_audit("COVER SKIP duplicate fill already covered for " + order_id);
                    continue;
                }
                cover_qty = std::min(fill_qty, remaining_cover_qty);
                already_covered += cover_qty;
            }

            const auto display_side = upper_ascii(get_string(fill, "displaySide", ""));
            const auto raw_side = lower_ascii(get_string(fill, "side", ""));
            const bool entry_buy = display_side == "BUY" || raw_side == "yes" || raw_side == "bid" || raw_side == "buy";
            const auto cover_side = entry_buy ? std::string("offer") : std::string("bid");
            const auto raw_cover_price = entry_buy
                ? fill_price + (policy.cover_ticks * policy.tick_size)
                : fill_price - (policy.cover_ticks * policy.tick_size);
            const auto cover_price = round_to_tick(raw_cover_price, policy.tick_size);
            const auto cover_id = order_id + "-COVER-" + get_string(fill, "timestamp", std::to_string(now_ms()));

            std::ostringstream out;
            out << "{\"orderId\":" << q(cover_id)
                << ",\"clientOrderId\":" << q(cover_id)
                << ",\"provider\":\"cme\""
                << ",\"marketKey\":" << q(symbol)
                << ",\"symbol\":" << q(symbol)
                << ",\"side\":" << q(cover_side)
                << ",\"orderType\":\"limit\""
                << ",\"price\":" << json_number(cover_price)
                << ",\"size\":" << cover_qty
                << ",\"source\":\"algo\""
                << ",\"strategy\":" << q(policy.strategy.empty() ? "algo-cover" : policy.strategy)
                << ",\"algoId\":" << q(policy.algo_id)
                << ",\"algoName\":" << q(policy.algo_name)
                << ",\"algoRole\":\"cover\""
                << ",\"orderTag\":\"ALGO COVER\""
                << ",\"parentOrderId\":" << q(order_id)
                << ",\"layer\":" << policy.layer
                << ",\"trigger\":" << q("cover from fill " + order_id)
                << "}";
            cover_orders.push_back(out.str());
        }

        for (const auto& cover_order : cover_orders) {
            auto result = execution_post("/send", cover_order);
            if (!result || result->status < 200 || result->status >= 300) {
                append_algo_audit("COVER ERROR failed to send cover order: " + (result ? result->body : std::string("cerious exchange unavailable")));
            }
        }
    }

    static std::string json_number(double value, int precision = 9) {
        if (!finite(value)) return "null";
        std::ostringstream out;
        out << std::fixed << std::setprecision(precision) << value;
        auto text = out.str();
        while (text.size() > 1 && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
        return text.empty() ? "0" : text;
    }

    void append_algo_audit(const std::string& message) const {
        const auto dir = data / "logs";
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::ofstream out(dir / "algo-audit.log", std::ios::app | std::ios::binary);
        if (!out) return;
        out << utc_iso(std::chrono::system_clock::now()) << "Z " << message << "\n";
    }

    std::optional<std::string> algo_definition_for_id(const std::string& id) const {
        const auto clean_id = trim_copy(id);
        if (clean_id.empty()) return std::nullopt;
        const auto dir = data / "algo-definitions";
        std::error_code ec;
        if (!fs::exists(dir, ec)) return std::nullopt;

        std::optional<std::string> best;
        std::uint64_t best_updated_at = 0;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const auto path = entry.path();
            if (path.extension() != ".json") continue;
            if (path.filename().string().starts_with("_")) continue;
            auto content = read_text(path);
            if (!content || is_deleted_definition(*content)) continue;
            const auto payload_id = get_string(*content, "id", "");
            const auto file_id = path.stem().string();
            if (payload_id != clean_id && file_id != clean_id) continue;
            const auto updated_at = get_u64_number(*content, "updatedAt", 0);
            if (!best || updated_at >= best_updated_at) {
                best = *content;
                best_updated_at = updated_at;
            }
        }
        return best;
    }

    std::string build_algo_order_json(
        const std::string& algo_id,
        const std::string& algo_name,
        const std::string& symbol,
        const std::string& side,
        double price,
        int size,
        int layer,
        int lookback,
        const std::string& band_label,
        double cover_ticks,
        double tick_size) const {
        const auto order_id = "ALGO-" + algo_id + "-" + upper_ascii(side) + "-"
            + std::to_string(layer) + "-" + std::to_string(now_ms());
        std::ostringstream out;
        out << "{\"orderId\":" << q(order_id)
            << ",\"clientOrderId\":" << q(order_id)
            << ",\"provider\":\"cme\""
            << ",\"marketKey\":" << q(symbol)
            << ",\"symbol\":" << q(symbol)
            << ",\"side\":" << q(side)
            << ",\"orderType\":\"limit\""
            << ",\"price\":" << json_number(price)
            << ",\"size\":" << std::max(1, size)
            << ",\"source\":\"algo\""
            << ",\"strategy\":" << q(algo_name)
            << ",\"algoId\":" << q(algo_id)
            << ",\"algoName\":" << q(algo_name)
            << ",\"algoRole\":\"entry\""
            << ",\"orderTag\":\"ALGO ENTRY\""
            << ",\"layer\":" << layer
            << ",\"trigger\":" << q("Linear Regression lookback " + std::to_string(lookback) + " " + band_label)
            << ",\"coverTicksFromFill\":" << json_number(cover_ticks)
            << ",\"coverTickSize\":" << json_number(tick_size)
            << "}";
        return out.str();
    }

    std::string deploy_algo_definitions_json(const std::string& body, int& status) const {
        status = 200;
        const auto algo_ids = get_string_array(body, "algoIds");
        const bool dry_run = get_bool(body, "dryRun", false);
        const bool include_state = get_bool(body, "includeState", true);
        const bool cache_only = get_bool(body, "cacheOnly", false);
        std::vector<std::string> errors;
        std::vector<std::string> notes;
        std::vector<std::string> orders;
        std::vector<std::string> previews;

        if (algo_ids.empty()) {
            status = 400;
            return "{\"ok\":false,\"detail\":\"deploy request contains no algo ids\",\"errors\":[\"No algo definitions selected\"]"
                + (include_state ? ",\"state\":" + execution_state_json() : "") + "}";
        }

        for (const auto& algo_id : algo_ids) {
            const auto definition = algo_definition_for_id(algo_id);
            if (!definition) {
                errors.push_back("algo definition not found: " + algo_id);
                continue;
            }

            const auto& def = *definition;
            const auto name = get_string(def, "name", algo_id);
            auto symbol = canonical_market_symbol(get_string(def, "marketKey", get_string(def, "symbol", "")));
            if (symbol.empty()) {
                const auto instruments = get_string_array(def, "instruments");
                if (!instruments.empty()) symbol = canonical_market_symbol(instruments.front());
            }
            if (symbol.empty()) {
                errors.push_back(name + ": product missing");
                continue;
            }

            const auto entry_peg = get_object(def, "entryPeg").value_or("{}");
            const auto layer_plan = get_object(def, "layerPlan").value_or("{}");
            const auto exit_policy = get_object(def, "exitPolicy").value_or("{}");
            const auto order_policy = get_object(def, "orderPolicy").value_or("{}");
            const auto product = product_def_for(symbol);
            if (!finite(product.tick_size) || product.tick_size <= 0.0) {
                errors.push_back(name + ": product tick size missing for " + symbol);
                continue;
            }

            auto raw_lookback = get_number(entry_peg, "lookback");
            if (!raw_lookback || !finite(*raw_lookback) || *raw_lookback < 2.0) {
                const auto detail = name + ": regression lookback is not defined";
                errors.push_back(detail);
                append_algo_audit("DEPLOY ERROR " + detail);
                continue;
            }
            const auto lookback = std::clamp(static_cast<int>(std::llround(*raw_lookback)), 2, 2000);
            const auto standard_deviations = std::clamp(get_number(entry_peg, "standardDeviations").value_or(2.0), 0.0, 20.0);
            const auto interval = get_string(entry_peg, "interval", get_string(entry_peg, "timeframe", "30m"));
            const auto cached_study = cached_regression_study(symbol, interval, lookback, standard_deviations);
            RegressionStudy study = cached_study ? *cached_study : RegressionStudy{};
            if (!cached_study || !cached_study->ok) {
                if (cache_only) {
                    request_regression_study_warmup(symbol, interval, lookback, standard_deviations);
                    study.symbol = symbol;
                    study.interval = interval;
                    study.lookback = lookback;
                    study.standard_deviations = standard_deviations;
                    study.error = "server study has not published this subscription yet";
                } else {
                    study = calculate_regression_study(symbol, interval, lookback, standard_deviations);
                }
            }
            if (!study.ok) {
                const auto detail = name + ": send price not published for linear-regression lookback " + std::to_string(lookback)
                    + " (" + symbol + " " + interval + ", bars " + std::to_string(study.bars)
                    + (study.error.empty() ? "" : ": " + study.error) + ")";
                errors.push_back(detail);
                append_algo_audit("DEPLOY ERROR " + detail);
                continue;
            }

            const auto side = lower_ascii(get_string(def, "side", "both"));
            const bool side_allows_bid = side == "both" || side == "bid" || side == "buy";
            const bool side_allows_offer = side == "both" || side == "offer" || side == "ask" || side == "sell";
            const bool work_bid = side_allows_bid && get_bool(layer_plan, "workBuySide", true);
            const bool work_offer = side_allows_offer && get_bool(layer_plan, "workSellSide", true);
            if (!work_bid && !work_offer) {
                errors.push_back(name + ": layer plan has no active side");
                continue;
            }

            const auto layers = std::clamp(static_cast<int>(std::llround(get_number(layer_plan, "layerCount").value_or(1.0))), 1, 100);
            const auto spacing_ticks = std::max(0.0, get_number(layer_plan, "layerSpacingTicks").value_or(0.0));
            const auto size = std::max(1, static_cast<int>(std::llround(get_number(def, "clipSize").value_or(1.0))));
            const auto cover_ticks = std::max(0.0, get_number(exit_policy, "coverTicksFromFill").value_or(0.0));
            const auto price_reference = lower_ascii(get_string(order_policy, "priceReference",
                get_string(entry_peg, "priceReference", "linear-regression")));
            const bool do_not_cross_inside = get_bool(order_policy, "doNotCrossInside", false);
            const auto bid_base = study.lower;
            const auto offer_base = study.upper;
            const auto bid_band_label = std::string("-2");
            const auto offer_band_label = std::string("+2");
            if ((work_bid && !finite(bid_base)) || (work_offer && !finite(offer_base))) {
                const auto detail = name + ": send price basis unavailable for " + price_reference
                    + " lookback " + std::to_string(lookback);
                errors.push_back(detail);
                append_algo_audit("DEPLOY ERROR " + detail);
                continue;
            }

            const auto inside_book = do_not_cross_inside ? current_book(symbol) : std::optional<MarketBook>{};
            if (do_not_cross_inside && (!inside_book || !finite(inside_book->bid) || !finite(inside_book->ask))) {
                const auto detail = name + ": do-not-cross requires a live inside market for " + symbol;
                errors.push_back(detail);
                append_algo_audit("DEPLOY BLOCK " + detail);
                continue;
            }

            std::optional<double> first_bid_price;
            std::optional<double> first_offer_price;
            std::optional<std::string> do_not_cross_error;
            std::vector<std::string> algo_orders;
            for (int layer = 0; layer < layers; ++layer) {
                const auto offset = static_cast<double>(layer) * spacing_ticks * product.tick_size;
                if (work_bid) {
                    const auto price = round_to_tick(bid_base - offset, product.tick_size);
                    if (layer == 0) first_bid_price = price;
                    if (do_not_cross_inside && price >= inside_book->ask) {
                        do_not_cross_error = name + ": do-not-cross blocked BUY " + json_number(price, 4)
                            + " because current ask is " + json_number(inside_book->ask, 4)
                            + "; market is currently through your send price";
                        break;
                    }
                    algo_orders.push_back(build_algo_order_json(algo_id, name, symbol, "buy", price, size, layer + 1, lookback, bid_band_label, cover_ticks, product.tick_size));
                }
                if (work_offer) {
                    const auto price = round_to_tick(offer_base + offset, product.tick_size);
                    if (layer == 0) first_offer_price = price;
                    if (do_not_cross_inside && price <= inside_book->bid) {
                        do_not_cross_error = name + ": do-not-cross blocked SELL " + json_number(price, 4)
                            + " because current bid is " + json_number(inside_book->bid, 4)
                            + "; market is currently through your send price";
                        break;
                    }
                    algo_orders.push_back(build_algo_order_json(algo_id, name, symbol, "sell", price, size, layer + 1, lookback, offer_band_label, cover_ticks, product.tick_size));
                }
            }
            std::ostringstream preview;
            preview << "{\"algoId\":" << q(algo_id)
                << ",\"algoName\":" << q(name)
                << ",\"symbol\":" << q(symbol)
                << ",\"interval\":" << q(interval)
                << ",\"lookback\":" << lookback
                << ",\"standardDeviations\":" << json_number(standard_deviations)
                << ",\"studyUpdatedAt\":" << study.updated_at
                << ",\"studyMean\":" << json_number(study.mean)
                << ",\"studyBid\":" << json_number(study.lower)
                << ",\"studyAsk\":" << json_number(study.upper)
                << ",\"firstBid\":" << (first_bid_price ? json_number(*first_bid_price) : std::string("null"))
                << ",\"firstAsk\":" << (first_offer_price ? json_number(*first_offer_price) : std::string("null"))
                << ",\"layers\":" << layers
                << ",\"spacingTicks\":" << json_number(spacing_ticks)
                << ",\"tickSize\":" << json_number(product.tick_size)
                << ",\"clipSize\":" << size
                << ",\"workBid\":" << (work_bid ? "true" : "false")
                << ",\"workAsk\":" << (work_offer ? "true" : "false")
                << ",\"doNotCrossInside\":" << (do_not_cross_inside ? "true" : "false")
                << ",\"subscription\":\"study-driven\""
                << ",\"source\":\"cerious-algo-service\""
                << "}";
            previews.push_back(preview.str());
            if (do_not_cross_error) {
                errors.push_back(*do_not_cross_error);
                append_algo_audit("DEPLOY BLOCK " + *do_not_cross_error);
                continue;
            }
            orders.insert(orders.end(), algo_orders.begin(), algo_orders.end());
            notes.push_back(name + " resolved linear-regression lookback " + std::to_string(lookback) + " " + interval
                + " reference linear-regression"
                + " mean " + json_number(study.mean, 4)
                + " minus " + json_number(study.lower, 4)
                + " plus " + json_number(study.upper, 4));
        }

        if (dry_run) {
            std::ostringstream out;
            out << "{\"ok\":" << (errors.empty() ? "true" : "false")
                << ",\"dryRun\":true,\"acceptedCount\":" << orders.size();
            if (!errors.empty()) out << ",\"detail\":" << q(errors.front());
            out << ",\"errors\":[";
            for (std::size_t i = 0; i < errors.size(); ++i) {
                if (i) out << ",";
                out << q(errors[i]);
            }
            out << "],\"previews\":[";
            for (std::size_t i = 0; i < previews.size(); ++i) {
                if (i) out << ",";
                out << previews[i];
            }
            out << "]"
                << ",\"orders\":[";
            for (std::size_t i = 0; i < orders.size(); ++i) {
                if (i) out << ",";
                out << orders[i];
            }
            out << "],\"notes\":[";
            for (std::size_t i = 0; i < notes.size(); ++i) {
                if (i) out << ",";
                out << q(notes[i]);
            }
            out << "]";
            if (include_state) out << ",\"state\":" << execution_state_json();
            out << "}";
            return out.str();
        }

        if (!errors.empty() && orders.empty()) {
            status = 400;
            std::ostringstream out;
            out << "{\"ok\":false,\"acceptedCount\":0,\"detail\":" << q(errors.front())
                << ",\"errors\":[";
            for (std::size_t i = 0; i < errors.size(); ++i) {
                if (i) out << ",";
                out << q(errors[i]);
            }
            out << "],\"notes\":[";
            for (std::size_t i = 0; i < notes.size(); ++i) {
                if (i) out << ",";
                out << q(notes[i]);
            }
            out << "]";
            if (include_state) out << ",\"state\":" << execution_state_json();
            out << "}";
            return out.str();
        }

        if (!errors.empty()) {
            status = 400;
            return "{\"ok\":false,\"acceptedCount\":0,\"detail\":\"one or more algo definitions are invalid; no orders sent\""
                + (include_state ? ",\"state\":" + execution_state_json() : "") + "}";
        }

        std::string latest_state = execution_state_json();
        for (const auto& order : orders) {
            remember_algo_cover_policy(order);
            auto result = execution_post("/send", order);
            if (!result || result->status < 200 || result->status >= 300) {
                status = 503;
                const auto detail = result ? result->body : std::string("cerious exchange unavailable");
                append_algo_audit("DEPLOY ERROR exchange send failed: " + detail);
                return "{\"ok\":false,\"acceptedCount\":0,\"detail\":\"cerious exchange unavailable while sending algo order\",\"state\":"
                    + latest_state + "}";
            }
            process_exchange_fill_events(result->body);
            latest_state = execution_state_json();
        }

        std::ostringstream out;
        out << "{\"ok\":true,\"runtime\":\"cpp\",\"acceptedCount\":" << orders.size()
            << ",\"previews\":[";
        for (std::size_t i = 0; i < previews.size(); ++i) {
            if (i) out << ",";
            out << previews[i];
        }
        out << "],\"notes\":[";
        for (std::size_t i = 0; i < notes.size(); ++i) {
            if (i) out << ",";
            out << q(notes[i]);
        }
        out << "],\"state\":" << latest_state << "}";
        return out.str();
    }

    std::string algo_definitions_json() const {
        const auto dir = data / "algo-definitions";
        std::vector<std::string> defs;
        std::error_code ec;
        if (!fs::exists(dir, ec)) return "[]";
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const auto path = entry.path();
            if (path.extension() != ".json") continue;
            if (path.filename().string().starts_with("_")) continue;
            auto content = read_text(path);
            if (content && !is_deleted_definition(*content)) {
                defs.push_back(*content);
            }
        }
        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < defs.size(); ++i) {
            if (i) out << ",";
            out << defs[i];
        }
        out << "]";
        return out.str();
    }

    std::string algo_definition_status_counts_json() const {
        const auto dir = data / "algo-definitions";
        int held = 0;
        int quoting = 0;
        int paused = 0;
        int draft = 0;
        std::error_code ec;
        if (fs::exists(dir, ec)) {
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                const auto path = entry.path();
                if (path.extension() != ".json") continue;
                if (path.filename().string().starts_with("_")) continue;
                auto content = read_text(path);
                if (!content || is_deleted_definition(*content)) continue;
                const auto status = lower_ascii(trim_copy(get_string(*content, "status", "held")));
                if (status == "quoting") {
                    ++quoting;
                } else if (status == "paused") {
                    ++paused;
                } else if (status == "draft") {
                    ++draft;
                } else {
                    ++held;
                }
            }
        }

        std::ostringstream out;
        out << "{\"held\":" << held
            << ",\"quoting\":" << quoting
            << ",\"paused\":" << paused
            << ",\"draft\":" << draft << "}";
        return out.str();
    }

    std::uint64_t news_refresh_ms() const {
        const auto configured = read_text(data / "window-payloads" / "cerious" / "news-subscriptions.json");
        const auto from_file = configured ? get_u64_number(*configured, "refreshMs", 60000) : 60000;
        try {
            return std::clamp<std::uint64_t>(
                static_cast<std::uint64_t>(std::stoull(env_or("CERIOUS_NEWS_REFRESH_MS", std::to_string(from_file)))),
                15000,
                600000);
        } catch (...) {
            return std::clamp<std::uint64_t>(from_file, 15000, 600000);
        }
    }

    std::uint64_t economic_calendar_refresh_ms() const {
        try {
            return std::clamp<std::uint64_t>(
                static_cast<std::uint64_t>(std::stoull(env_or("CERIOUS_ECON_CALENDAR_REFRESH_MS", "300000"))),
                60000,
                1800000);
        } catch (...) {
            return 300000;
        }
    }

    std::vector<NewsSource> default_news_sources() const {
        return {
            {"cnbc-top", "CNBC", "https://www.cnbc.com/id/100003114/device/rss/rss.html", "markets"},
            {"marketwatch-top", "MarketWatch", "https://feeds.content.dowjones.io/public/rss/mw_topstories", "markets"},
            {"investing-markets", "Investing.com", "https://www.investing.com/rss/news_25.rss", "markets"},
            {"bbc-business", "BBC Business", "https://feeds.bbci.co.uk/news/business/rss.xml", "global"},
            {"npr-business", "NPR Business", "https://feeds.npr.org/1006/rss.xml", "business"}
        };
    }

    std::vector<NewsSource> news_sources() const {
        const auto fallback = default_news_sources();
        const auto configured = read_text(data / "window-payloads" / "cerious" / "news-subscriptions.json");
        if (!configured) return fallback;
        const auto sources_raw = get_json_member(*configured, "sources");
        if (!sources_raw) return fallback;

        std::vector<NewsSource> parsed;
        for (const auto& item : json_object_array_items(*sources_raw)) {
            NewsSource source{
                trim_copy(get_string(item, "id")),
                trim_copy(get_string(item, "name")),
                trim_copy(get_string(item, "url")),
                trim_copy(get_string(item, "category", "global"))
            };
            if (source.id.empty() || source.name.empty() || !safe_news_url(source.url)) continue;
            parsed.push_back(std::move(source));
        }
        return parsed.empty() ? fallback : parsed;
    }

    std::optional<std::string> fetch_public_http_body(const std::string& url,
                                                      const std::string& user_agent,
                                                      std::size_t max_bytes,
                                                      const std::string& label,
                                                      std::string& warning) const {
        if (!safe_news_url(url)) {
            warning = label + ": rejected unsafe URL";
            return std::nullopt;
        }
#ifdef _WIN32
        const auto default_curl = "curl.exe";
        const auto stderr_null = " 2>NUL";
#else
        const auto default_curl = "curl";
        const auto stderr_null = " 2>/dev/null";
#endif
        const auto curl = env_or("CERIOUS_NEWS_CURL_PATH", default_curl);
        const auto command = pipe_command(
            shell_quote_arg(curl)
            + " -L -sS --max-time 8 --connect-timeout 3 -A "
            + shell_quote_arg(user_agent)
            + " "
            + shell_quote_arg(url)
            + stderr_null);

        FILE* pipe = open_process_pipe(command);
        if (!pipe) {
            warning = label + ": curl process unavailable";
            return std::nullopt;
        }

        std::string body;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe) && body.size() < max_bytes) {
            body += buffer;
        }
        const auto exit_code = close_process_pipe(pipe);
        if (exit_code != 0 && body.empty()) {
            warning = label + ": fetch failed";
            return std::nullopt;
        }
        return body;
    }

    std::optional<std::string> fetch_news_xml(const NewsSource& source, std::string& warning) const {
        const auto body = fetch_public_http_body(source.url, "CeriousSystemsNews/1.0", 512 * 1024, source.name, warning);
        if (!body) return std::nullopt;
        const auto lower = lower_ascii(*body);
        if (lower.find("<item") == std::string::npos && lower.find("<entry") == std::string::npos) {
            warning = source.name + ": feed returned no headline entries";
            return std::nullopt;
        }
        return body;
    }

    std::pair<std::string, std::string> classify_news_headline(const std::string& title, const std::string& description) const {
        const auto text = lower_ascii(title + " " + description);
        auto contains_any = [&](const std::vector<std::string>& terms) {
            return std::any_of(terms.begin(), terms.end(), [&](const std::string& term) {
                return text.find(term) != std::string::npos;
            });
        };

        const bool high = contains_any({
            "breaking", "fed", "fomc", "powell", "cpi", "inflation", "payroll",
            "jobs report", "treasury yields", "rate decision", "tariff", "sanction",
            "war", "oil", "credit", "default", "recession", "shutdown"
        });
        const bool risk_off = contains_any({
            "selloff", "plunge", "slump", "falls", "drop", "weak", "recession",
            "inflation", "hawkish", "yields rise", "credit", "default", "war",
            "sanction", "tariff", "risk-off", "volatility"
        });
        const bool risk_on = contains_any({
            "rally", "rebound", "gains", "rise", "surge", "dovish", "rate cut",
            "soft landing", "strong earnings", "growth", "deal", "risk-on"
        });

        std::string bias = "mixed";
        if (risk_off && !risk_on) bias = "risk-off";
        else if (risk_on && !risk_off) bias = "risk-on";
        return {high ? "high" : "normal", bias};
    }

    std::vector<NewsHeadline> parse_news_feed(const NewsSource& source, const std::string& xml) const {
        auto blocks = xml_blocks(xml, "item");
        if (blocks.empty()) blocks = xml_blocks(xml, "entry");

        std::vector<NewsHeadline> headlines;
        for (const auto& block : blocks) {
            if (headlines.size() >= 12) break;
            NewsHeadline item;
            item.source = source.name;
            item.title = xml_tag_value(block, "title");
            item.link = xml_tag_value(block, "link");
            item.pub_date = xml_tag_value(block, "pubDate");
            if (item.pub_date.empty()) item.pub_date = xml_tag_value(block, "published");
            if (item.pub_date.empty()) item.pub_date = xml_tag_value(block, "updated");
            item.description = xml_tag_value(block, "description");
            if (item.description.empty()) item.description = xml_tag_value(block, "summary");
            if (item.title.empty()) continue;
            auto [urgency, bias] = classify_news_headline(item.title, item.description);
            item.urgency = std::move(urgency);
            item.bias = std::move(bias);
            const auto hash = std::hash<std::string>{}(source.id + "|" + lower_ascii(item.title) + "|" + item.pub_date);
            item.id = source.id + "-" + std::to_string(static_cast<unsigned long long>(hash));
            headlines.push_back(std::move(item));
        }
        return headlines;
    }

    std::string news_headline_json(const NewsHeadline& item) const {
        std::ostringstream out;
        out << "{\"id\":" << q(item.id)
            << ",\"source\":" << q(item.source)
            << ",\"title\":" << q(item.title);
        if (!item.link.empty()) out << ",\"link\":" << q(item.link);
        if (!item.pub_date.empty()) out << ",\"pubDate\":" << q(item.pub_date);
        if (!item.description.empty()) out << ",\"description\":" << q(item.description);
        out << ",\"urgency\":" << q(item.urgency)
            << ",\"bias\":" << q(item.bias)
            << "}";
        return out.str();
    }

    std::string build_news_snapshot_json(const std::vector<NewsSource>& sources,
                                         const std::vector<NewsHeadline>& headlines,
                                         const std::vector<std::string>& warnings,
                                         int live_sources) const {
        const auto fetched_at = utc_iso(std::chrono::system_clock::now()) + "Z";
        std::ostringstream out;
        out << "{\"service\":\"news.stream\""
            << ",\"provider\":\"cerious.news-gateway\""
            << ",\"status\":" << q(live_sources > 0 ? "ok" : "degraded")
            << ",\"fetchedAt\":" << q(fetched_at)
            << ",\"refreshMs\":" << news_refresh_ms()
            << ",\"publicSourcesExpected\":" << sources.size()
            << ",\"publicSourcesLive\":" << live_sources
            << ",\"subscription\":{\"owner\":\"gateway\",\"mode\":\"server-side-rss\",\"sourceCount\":" << sources.size() << "}"
            << ",\"warnings\":[";
        for (std::size_t i = 0; i < warnings.size(); ++i) {
            if (i) out << ",";
            out << q(warnings[i]);
        }
        out << "],\"items\":[";
        for (std::size_t i = 0; i < headlines.size(); ++i) {
            if (i) out << ",";
            out << news_headline_json(headlines[i]);
        }
        out << "]}";
        return out.str();
    }

    std::string cerious_news_json() const {
        const auto now = now_ms();
        const auto refresh_ms = news_refresh_ms();
        {
            std::lock_guard<std::mutex> lock(news_cache_mutex);
            if (!news_cache_json.empty() && now >= news_cache_ms && now - news_cache_ms < refresh_ms) {
                return news_cache_json;
            }
        }

        const auto sources = news_sources();
        std::vector<NewsHeadline> headlines;
        std::vector<std::string> warnings;
        std::unordered_set<std::string> seen_titles;
        int live_sources = 0;

        for (const auto& source : sources) {
            std::string warning;
            const auto body = fetch_news_xml(source, warning);
            if (!body) {
                if (!warning.empty()) warnings.push_back(warning);
                continue;
            }
            const auto parsed = parse_news_feed(source, *body);
            if (parsed.empty()) {
                warnings.push_back(source.name + ": feed parsed with no usable titles");
                continue;
            }
            ++live_sources;
            for (const auto& item : parsed) {
                const auto key = lower_ascii(item.title);
                if (headlines.size() < 40 && seen_titles.insert(key).second) headlines.push_back(item);
            }
        }

        if (headlines.empty()) {
            std::lock_guard<std::mutex> lock(news_cache_mutex);
            if (!news_cache_json.empty()) return news_cache_json;
        }

        const auto snapshot = build_news_snapshot_json(sources, headlines, warnings, live_sources);
        {
            std::lock_guard<std::mutex> lock(news_cache_mutex);
            news_cache_json = snapshot;
            news_cache_ms = now;
        }
        return snapshot;
    }

    std::string json_string_value(const std::string& object, const std::string& key) const {
        auto value = trim_copy(get_string(object, key, ""));
        if (value == "null" || value == "undefined") return "";
        return value;
    }

    std::optional<std::string> extract_route_init_data(const std::string& html) const {
        const auto lower = lower_ascii(html);
        const auto marker = "id=\"route-init-data\"";
        auto marker_pos = lower.find(marker);
        if (marker_pos == std::string::npos) return std::nullopt;
        auto open_end = lower.find('>', marker_pos);
        if (open_end == std::string::npos) return std::nullopt;
        auto close = lower.find("</script>", open_end + 1);
        if (close == std::string::npos) return std::nullopt;
        return html.substr(open_end + 1, close - open_end - 1);
    }

    std::pair<std::string, std::string> calendar_date_time_labels(const std::string& date_time) const {
        if (date_time.size() >= 16) {
            return {date_time.substr(0, 10), date_time.substr(11, 5) + " ET"};
        }
        return {date_time, ""};
    }

    std::string calendar_importance_label(std::uint64_t importance) const {
        if (importance >= 3) return "high";
        if (importance == 2) return "medium";
        return "low";
    }

    std::vector<EconomicCalendarEvent> parse_finviz_calendar(const std::string& html,
                                                             std::string& week_start,
                                                             std::vector<std::string>& warnings) const {
        const auto payload = extract_route_init_data(html);
        if (!payload) {
            warnings.push_back("FINVIZ: route-init-data payload missing");
            return {};
        }
        week_start = json_string_value(*payload, "initialDateFrom");
        const auto data_member = get_json_member(*payload, "data");
        const auto entries_member = data_member ? get_json_member(*data_member, "entries") : get_json_member(*payload, "entries");
        if (!entries_member) {
            warnings.push_back("FINVIZ: economic calendar entries missing");
            return {};
        }

        std::vector<EconomicCalendarEvent> events;
        for (const auto& object : json_object_array_items(*entries_member)) {
            if (events.size() >= 120) break;
            EconomicCalendarEvent event;
            const auto calendar_id = json_string_value(object, "calendarId");
            event.id = "finviz-" + (calendar_id.empty() ? std::to_string(events.size() + 1) : calendar_id);
            event.ticker = json_string_value(object, "ticker");
            event.event = json_string_value(object, "event");
            event.category = json_string_value(object, "category");
            event.date_time = json_string_value(object, "date");
            auto [date_label, time_label] = calendar_date_time_labels(event.date_time);
            event.date_label = std::move(date_label);
            event.time_label = std::move(time_label);
            event.actual = json_string_value(object, "actual");
            event.forecast = json_string_value(object, "forecast");
            event.previous = json_string_value(object, "previous");
            event.reference = json_string_value(object, "reference");
            event.importance = calendar_importance_label(get_u64_number(object, "importance", 1));
            if (!event.event.empty()) events.push_back(std::move(event));
        }
        if (events.empty()) warnings.push_back("FINVIZ: economic calendar parsed with no usable events");
        return events;
    }

    std::string economic_calendar_event_json(const EconomicCalendarEvent& event) const {
        std::ostringstream out;
        out << "{\"id\":" << q(event.id)
            << ",\"source\":\"FINVIZ\""
            << ",\"event\":" << q(event.event)
            << ",\"ticker\":" << q(event.ticker)
            << ",\"category\":" << q(event.category)
            << ",\"dateTime\":" << q(event.date_time)
            << ",\"date\":" << q(event.date_label)
            << ",\"time\":" << q(event.time_label)
            << ",\"actual\":" << q(event.actual)
            << ",\"forecast\":" << q(event.forecast)
            << ",\"previous\":" << q(event.previous)
            << ",\"reference\":" << q(event.reference)
            << ",\"importance\":" << q(event.importance)
            << ",\"link\":\"https://finviz.com/calendar/economic\""
            << "}";
        return out.str();
    }

    std::string build_economic_calendar_json(const std::vector<EconomicCalendarEvent>& events,
                                             const std::vector<std::string>& warnings,
                                             const std::string& week_start) const {
        const auto fetched_at = utc_iso(std::chrono::system_clock::now()) + "Z";
        std::ostringstream out;
        out << "{\"service\":\"economic.calendar\""
            << ",\"provider\":\"finviz.economic-calendar\""
            << ",\"status\":" << q(events.empty() ? "degraded" : "ok")
            << ",\"fetchedAt\":" << q(fetched_at)
            << ",\"refreshMs\":" << economic_calendar_refresh_ms()
            << ",\"calendarUrl\":\"https://finviz.com/calendar/economic\""
            << ",\"weekStart\":" << q(week_start)
            << ",\"subscription\":{\"owner\":\"gateway\",\"mode\":\"server-side-calendar\",\"source\":\"finviz\"}"
            << ",\"warnings\":[";
        for (std::size_t i = 0; i < warnings.size(); ++i) {
            if (i) out << ",";
            out << q(warnings[i]);
        }
        out << "],\"items\":[";
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (i) out << ",";
            out << economic_calendar_event_json(events[i]);
        }
        out << "]}";
        return out.str();
    }

    std::string cerious_economic_calendar_json() const {
        const auto now = now_ms();
        const auto refresh_ms = economic_calendar_refresh_ms();
        {
            std::lock_guard<std::mutex> lock(economic_calendar_cache_mutex);
            if (!economic_calendar_cache_json.empty() && now >= economic_calendar_cache_ms && now - economic_calendar_cache_ms < refresh_ms) {
                return economic_calendar_cache_json;
            }
        }

        std::vector<std::string> warnings;
        std::string week_start;
        std::vector<EconomicCalendarEvent> events;
        std::string warning;
        const auto body = fetch_public_http_body(
            "https://finviz.com/calendar/economic",
            "Mozilla/5.0 CeriousSystemsCalendar/1.0",
            2 * 1024 * 1024,
            "FINVIZ",
            warning);
        if (!body) {
            if (!warning.empty()) warnings.push_back(warning);
        } else {
            events = parse_finviz_calendar(*body, week_start, warnings);
        }

        if (events.empty()) {
            std::lock_guard<std::mutex> lock(economic_calendar_cache_mutex);
            if (!economic_calendar_cache_json.empty()) return economic_calendar_cache_json;
        }

        const auto snapshot = build_economic_calendar_json(events, warnings, week_start);
        {
            std::lock_guard<std::mutex> lock(economic_calendar_cache_mutex);
            economic_calendar_cache_json = snapshot;
            economic_calendar_cache_ms = now;
        }
        return snapshot;
    }

    void send_json(httplib::Response& res, const std::string& body, int status = 200) const {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "application/json");
    }

    void send_json_file(httplib::Response& res, const fs::path& path, const std::string& service_name) const {
        const auto body = read_text(path);
        if (!body) {
            send_json(res, "{\"ok\":false,\"service\":" + q(service_name) + ",\"detail\":\"payload file missing\"}", 404);
            return;
        }
        send_json(res, *body);
    }

    bool safe_cerious_content_kind(const std::string& kind) const {
        static const std::vector<std::string> allowed = {
            "atrZScoreEngine",
            "executionRules",
            "orderLayeringTechniques",
            "moneyManagement",
            "riskChecklist",
            "sourceNotes",
            "modelResearchGovernance",
            "liveApiArchitecture"
        };
        return std::find(allowed.begin(), allowed.end(), kind) != allowed.end();
    }

    bool is_allowed_cors_origin(const std::string& origin) const {
        if (origin.empty()) return false;
        return origin == "http://127.0.0.1:8000"
            || origin == "http://localhost:8000";
    }

    void apply_cors(const httplib::Request& req, httplib::Response& res) const {
        const auto origin_it = req.headers.find("Origin");
        const auto origin = origin_it == req.headers.end() ? std::string{} : origin_it->second;
        if (is_allowed_cors_origin(origin)) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Vary", "Origin");
        }
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Cerious-Session");
        res.set_header("Access-Control-Max-Age", "600");
    }

    void register_routes(httplib::Server& server) {
        server.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
            apply_cors(req, res);
            if (req.method == "OPTIONS") {
                res.status = 204;
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

        server.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
            const auto exchange = execution_get("/health");
            const bool exchange_ok = exchange && exchange->status >= 200 && exchange->status < 300;
            send_json(res,
                "{\"ok\":true,\"app\":\"cerious-systems\",\"runtime\":\"cpp\","
                "\"gateway\":\"cerious_gateway\",\"backend\":\"native-cpp\","
                "\"exchange\":" + std::string(exchange_ok ? "true" : "false")
                + ",\"marketData\":" + market_data_status_json()
                + ",\"execution\":" + execution_status_json() + "}");
        });

        server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true,\"app\":\"cerious-systems\",\"runtime\":\"cpp\"}");
        });

        server.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
            const auto username = trim_copy(get_string(req.body, "username", ""));
            const auto password = trim_copy(get_string(req.body, "password", ""));
            if (!valid_login(username, password)) {
                send_json(res, "{\"ok\":false,\"detail\":\"Invalid username or password\"}", 401);
                return;
            }
            send_json(res, auth_success_json(username));
        });

        server.Post("/api/auth/auto", [&](const httplib::Request&, httplib::Response& res) {
            auto username = portal_username();
            if (trim_copy(portal_password()).empty()) {
                username = admin_username();
            }
            if (trim_copy(username).empty()) {
                send_json(res, "{\"ok\":false,\"detail\":\"Local auth is not configured\"}", 503);
                return;
            }
            send_json(res, auth_success_json(username));
        });

        server.Get("/api/auth/session", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true,\"username\":" + q(portal_username()) + ",\"runtime\":\"cpp\"}");
        });

        server.Post("/api/auth/logout", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true}");
        });

        server.Get("/api/system/ready", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true,\"runtime\":\"cpp\",\"services\":[\"gateway\",\"cerious-exchange\"]}");
        });

        server.Get("/api/market-data/status", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, market_data_status_json());
        });

        server.Get("/api/execution/status", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, execution_status_json());
        });

        server.Get("/api/system/contract", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res,
                "{\"ok\":true,\"runtime\":\"cpp\",\"orderPath\":\"/api/order\","
                "\"executionDestination\":\"cerious-exchange\",\"stateOwner\":\"cerious.exchange\"}");
        });

        server.Post("/api/system/warmup", [&](const httplib::Request&, httplib::Response& res) {
            const auto started = now_ms();
            (void)cerious_advisory_snapshot(true);
            const auto elapsed = now_ms() >= started ? now_ms() - started : 0;
            send_json(res, "{\"ok\":true,\"status\":\"ready\",\"runtime\":\"cpp\",\"warmupMs\":" + std::to_string(elapsed) + ",\"advisory\":\"ready\"}");
        });

        server.Post("/api/system/shutdown", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true,\"runtime\":\"cpp\",\"shutdown\":\"requested\"}");
            execution_post("/shutdown", "{}");
            shutdown_requested.store(true);
        });

        server.Get("/api/workspaces/saved", [&](const httplib::Request&, httplib::Response& res) {
            const auto latest = read_text(data / "workspace-store" / "tsturiale" / "latest.json");
            if (latest) send_json(res, "{\"ok\":true,\"workspaces\":[" + *latest + "]}");
            else send_json(res, "{\"ok\":true,\"workspaces\":[]}");
        });

        server.Get("/api/workspaces/recovered", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true,\"workspaces\":[]}");
        });

        server.Post("/api/workspaces/save", [&](const httplib::Request& req, httplib::Response& res) {
            const auto workspace_dir = data / "workspace-store" / "tsturiale";
            const auto audit_path = workspace_dir / "native-last-save.json";
            const auto latest_path = workspace_dir / "latest.json";
            const auto workspace = get_json_member(req.body, "workspace").value_or(req.body);
            const bool ok = write_text_atomic(audit_path, req.body) && write_text_atomic(latest_path, workspace);
            send_json(res, ok ? "{\"ok\":true,\"runtime\":\"cpp\"}" : "{\"ok\":false,\"detail\":\"workspace save failed\"}", ok ? 200 : 500);
        });

        server.Get("/api/algo-manager/state", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true,\"runtime\":\"cpp\",\"definitions\":" + algo_definitions_json()
                + ",\"state\":" + algo_definition_status_counts_json() + "}");
        });

        server.Post("/api/algo-manager/deploy", [&](const httplib::Request& req, httplib::Response& res) {
            int status = 200;
            send_json(res, deploy_algo_definitions_json(req.body, status), status);
        });

        server.Post("/api/algo-manager/send-preview", [&](const httplib::Request& req, httplib::Response& res) {
            const auto algo_ids = get_string_array(req.body, "algoIds");
            std::ostringstream preview_body;
            preview_body << "{\"dryRun\":true,\"includeState\":false,\"cacheOnly\":true,\"algoIds\":[";
            for (std::size_t i = 0; i < algo_ids.size(); ++i) {
                if (i) preview_body << ",";
                preview_body << q(algo_ids[i]);
            }
            preview_body << "]}";
            int status = 200;
            send_json(res, deploy_algo_definitions_json(preview_body.str(), status), status);
        });

        server.Post("/api/algo-definitions/save", [&](const httplib::Request& req, httplib::Response& res) {
            const auto definition = get_object(req.body, "definition").value_or(req.body);
            const auto id = get_string(definition, "id", "algo-" + std::to_string(now_ms()));
            const auto dir = data / "algo-definitions";
            std::error_code ec;
            fs::create_directories(dir, ec);
            const auto path = dir / (id + ".json");
            const bool ok = write_text(path, definition);
            send_json(res, ok ? "{\"ok\":true,\"runtime\":\"cpp\"}" : "{\"ok\":false,\"detail\":\"algo definition save failed\"}", ok ? 200 : 500);
        });

        server.Post("/api/order", [&](const httplib::Request& req, httplib::Response& res) {
            const auto order_id = order_id_from_payload(req.body);
            if (order_id.empty()) {
                send_json(res,
                    "{\"ok\":false,\"detail\":\"orderId required\",\"service\":\"cerious.gateway\",\"state\":"
                    + execution_state_json() + "}",
                    400);
                return;
            }
            const auto order_body = payload_with_order_id(req.body, order_id);
            remember_algo_cover_policy(order_body);
            auto result = execution_post("/send", order_body);
            if (!result) {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange unavailable\",\"state\":" + execution_state_json() + "}", 503);
                return;
            }
            if (result->status >= 200 && result->status < 300) {
                process_exchange_fill_events(result->body);
            }
            if (result->status >= 200 && result->status < 300) {
                send_json(res, wrap_execution_event(result->body), result->status);
            } else {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange rejected order\",\"event\":"
                    + result->body + ",\"state\":" + execution_state_json() + "}", result->status);
            }
        });

        server.Post(R"(/api/cerious/orders/([^/]+)/cancel)", [&](const httplib::Request& req, httplib::Response& res) {
            const std::string order_id = req.matches[1];
            auto result = execution_post("/cancel", "{\"orderId\":" + q(order_id) + "}");
            if (!result) {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange unavailable\",\"state\":" + execution_state_json() + "}", 503);
                return;
            }
            if (result->status >= 200 && result->status < 300) {
                send_json(res, wrap_execution_event(result->body), result->status);
            } else {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange rejected cancel\",\"event\":"
                    + result->body + ",\"state\":" + execution_state_json() + "}", result->status);
            }
        });

        server.Post("/api/cerious/orders/cancel-all", [&](const httplib::Request&, httplib::Response& res) {
            auto result = execution_post("/reset", "{\"clearFills\":false,\"reason\":\"cancel all working orders\"}");
            if (!result) {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange unavailable\",\"state\":" + execution_state_json() + "}", 503);
                return;
            }
            if (result->status >= 200 && result->status < 300) {
                send_json(res, wrap_execution_event(result->body), result->status);
            } else {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange rejected cancel-all\",\"event\":"
                    + result->body + ",\"state\":" + execution_state_json() + "}", result->status);
            }
        });

        server.Post("/api/cerious/session/reset", [&](const httplib::Request& req, httplib::Response& res) {
            {
                std::lock_guard<std::mutex> lock(algo_cover_mutex);
                algo_cover_policies.clear();
                processed_sim_fill_events.clear();
                covered_algo_entry_qty.clear();
            }
            auto result = execution_post("/reset", req.body.empty() ? "{\"clearFills\":true}" : req.body);
            if (!result) {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange unavailable\",\"state\":" + execution_state_json() + "}", 503);
                return;
            }
            if (result->status >= 200 && result->status < 300) {
                send_json(res, wrap_execution_event(result->body), result->status);
            } else {
                send_json(res, "{\"ok\":false,\"detail\":\"cerious exchange rejected reset\",\"event\":"
                    + result->body + ",\"state\":" + execution_state_json() + "}", result->status);
            }
        });

        server.Get("/api/cerious/order-state", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, order_state_read_model_json());
        });

        server.Get("/api/cerious/positions-orders", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, order_state_read_model_json());
        });

        const auto cerious_window_payloads = data / "window-payloads" / "cerious";

        server.Get("/api/cerious/intelligence", [&, cerious_window_payloads](const httplib::Request& req, httplib::Response& res) {
            (void)cerious_window_payloads;
            const auto refresh = req.has_param("refresh")
                && req.get_param_value("refresh") != "0"
                && lower_ascii(req.get_param_value("refresh")) != "false";
            if (refresh) {
                send_json(res, cerious_advisory_snapshot(true).intelligence);
                return;
            }
            send_json(res, cerious_intelligence_json());
        });

        server.Get("/api/cerious/daily-summary", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            (void)cerious_window_payloads;
            send_json(res, cerious_daily_summary_json());
        });

        server.Get("/api/cerious/subscriptions", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, cerious_subscription_model_json());
        });

        server.Get("/api/cerious/macro-regime", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            (void)cerious_window_payloads;
            send_json(res, cerious_macro_regime_json());
        });

        server.Get("/api/cerious/opportunity-map", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            (void)cerious_window_payloads;
            send_json(res, cerious_opportunity_map_json());
        });

        server.Get("/api/cerious/trade-analytics", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            (void)cerious_window_payloads;
            send_json(res, live_trade_analytics_json());
        });

        server.Post("/api/cerious/trade-analytics/import", [&](const httplib::Request& req, httplib::Response& res) {
            int status = 200;
            const auto filename = req.has_param("filename") ? req.get_param_value("filename") : std::string{};
            send_json(res, imported_trade_analytics_json(req.body, status, filename), status);
        });

        server.Get("/api/cerious/notional", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            send_json_file(res, cerious_window_payloads / "notional.json", "notional");
        });

        server.Get("/api/cerious/audit", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            send_json_file(res, cerious_window_payloads / "audit.json", "audit");
        });

        server.Get("/api/cerious/news", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            (void)cerious_window_payloads;
            send_json(res, cerious_news_json());
        });

        server.Get("/api/cerious/economic-calendar", [&, cerious_window_payloads](const httplib::Request&, httplib::Response& res) {
            (void)cerious_window_payloads;
            send_json(res, cerious_economic_calendar_json());
        });

        server.Get(R"(/api/cerious/content/([^/]+))", [&, cerious_window_payloads](const httplib::Request& req, httplib::Response& res) {
            const auto kind = req.matches[1].str();
            if (!safe_cerious_content_kind(kind)) {
                send_json(res, "{\"ok\":false,\"detail\":\"Invalid content key\"}", 400);
                return;
            }
            send_json_file(res, cerious_window_payloads / "content" / (kind + ".json"), kind);
        });

        server.Get(R"(/api/cme/book/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
            const auto symbol = canonical_market_symbol(req.matches[1].str());
            const auto book = current_book(symbol);
            if (!book || !finite(book->bid) || !finite(book->ask)) {
                send_json(res, market_session_scaffold_json(symbol));
                return;
            }
            send_json(res, market_book_json(*book));
        });

        server.Get(R"(/api/cme/trades/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
            send_json(res, market_trades_json(req.matches[1].str()));
        });

        server.Get("/api/markets", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, market_catalog_json());
        });

        server.Get("/api/product-definitions", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, product_definitions_json());
        });

        server.Get("/api/cerious/product-definitions", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, product_definitions_json());
        });

        server.Get(R"(/api/bars/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
            const auto symbol = canonical_market_symbol(req.matches[1].str());
            const auto interval = req.has_param("interval") ? req.get_param_value("interval") : "1m";
            int limit = 300;
            if (req.has_param("limit")) {
                try {
                    limit = std::clamp(std::stoi(req.get_param_value("limit")), 1, 1200);
                } catch (...) {
                    limit = 300;
                }
            }
            send_json(res, bars_json(symbol, interval, limit));
        });

        server.Get(R"(/api/studies/regression/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
            const auto symbol = canonical_market_symbol(req.matches[1].str());
            const auto interval = req.has_param("interval") ? req.get_param_value("interval") : "30m";
            std::optional<int> lookback;
            double standard_deviations = 2.0;
            if (req.has_param("lookback")) {
                try {
                    const auto parsed = std::stoi(req.get_param_value("lookback"));
                    if (parsed >= 2) lookback = std::clamp(parsed, 2, 2000);
                } catch (...) {
                    lookback.reset();
                }
            }
            if (!lookback) {
                send_json(res, "{\"ok\":false,\"runtime\":\"cpp\",\"source\":\"cerious-study-service\",\"study\":\"linear-regression\",\"symbol\":"
                    + q(symbol) + ",\"interval\":" + q(interval)
                    + ",\"error\":\"regression lookback is required\"}", 400);
                return;
            }
            if (req.has_param("stdDev")) {
                try {
                    standard_deviations = std::clamp(std::stod(req.get_param_value("stdDev")), 0.0, 20.0);
                } catch (...) {
                    standard_deviations = 2.0;
                }
            }
            send_json(res, regression_study_json(symbol, interval, *lookback, standard_deviations));
        });

        server.Get("/api/metrics", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":true,\"runtime\":\"cpp\",\"metrics\":{}}");
        });

        server.Get("/api/alerts/sms/status", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, alert_smtp_status_json());
        });

        server.Post("/api/alerts/sms", [&](const httplib::Request& req, httplib::Response& res) {
            int status = 200;
            const auto response = send_smtp_text_alert(req.body, status);
            send_json(res, response.value_or("{\"ok\":false,\"error\":\"SMS alert transport unavailable\"}"), status);
        });

        server.Get(R"(/api/.*)", [&](const httplib::Request&, httplib::Response& res) {
            send_json(res, "{\"ok\":false,\"runtime\":\"cpp\",\"error\":\"not_found\"}", 404);
        });

        server.Get(R"(/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
            auto relative = req.path == "/" ? std::string("index.html") : req.path.substr(1);
            if (relative.find("..") != std::string::npos) {
                res.status = 400;
                res.set_content("bad path", "text/plain");
                return;
            }
            auto target = dist / fs::path(relative);
            if (!fs::exists(target) || fs::is_directory(target)) {
                target = dist / "index.html";
            }
            auto content = read_text(target);
            if (!content) {
                res.status = 404;
                res.set_content("Cerious terminal bundle not found. Build apps/terminal first.", "text/plain");
                return;
            }
            res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
            res.set_header("Pragma", "no-cache");
            res.set_header("Expires", "0");
            res.set_content(*content, content_type_for(target));
        });
    }
};

} // namespace

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);
    load_dotenv_file(fs::current_path() / ".env");
    load_dotenv_file(args.root / ".env");
    if (argc > 0 && argv[0] && std::string(argv[0]).find_first_of("\\/") != std::string::npos) {
        std::error_code ec;
        const auto exe_dir = fs::absolute(fs::path(argv[0]), ec).parent_path();
        if (!ec) {
            load_dotenv_file(exe_dir / ".env");
            load_dotenv_file(exe_dir.parent_path() / ".env");
            load_dotenv_file(exe_dir.parent_path().parent_path() / ".env");
            load_dotenv_file(exe_dir.parent_path().parent_path().parent_path() / ".env");
            load_dotenv_file(exe_dir.parent_path().parent_path().parent_path().parent_path() / ".env");
        }
    }
    set_env_if_missing("CERIOUS_PRODUCT_DEFINITIONS_PATH", (args.root / "data" / "product-definitions" / "product-definitions.json").string());
    Gateway gateway(args);

    httplib::Server server;
    server.new_task_queue = [] {
        return new httplib::ThreadPool(32, 512);
    };
    server.set_read_timeout(10, 0);
    server.set_write_timeout(10, 0);
    server.set_idle_interval(1, 0);

    gateway.start_market_data();
    gateway.start_cerious_advisory_scheduler();
    gateway.register_routes(server);

    std::cerr << "cerious_gateway: native C++ gateway listening on "
              << args.host << ":" << args.port << " root=" << args.root << std::endl;

    std::thread shutdown_thread([&]() {
        while (!gateway.shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server.stop();
    });

    const bool ok = server.listen(args.host, args.port);
    gateway.shutdown_requested.store(true);
    shutdown_thread.join();
    if (!ok) {
        std::cerr << "cerious_gateway: failed to listen on " << args.host << ":" << args.port << std::endl;
        return 1;
    }
    return 0;
}
