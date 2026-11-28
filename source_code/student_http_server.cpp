// 学生端 HTTP 服务（Zhang Yikai，HTTP 第 1 阶段）
//
// 按团队总手册的公共约定实现一条完整的学生流程：
//   登录/退出（Cookie 会话 + CSRF）→ 课程搜索/详情/名额 → 课表/我的选课
//   → 选课/退课 → 静态学生页面。
//
// 说明：
//   * 公共骨架（会话、CSRF、JSON 外壳、静态页面）先放在本文件，等 Yuhua 的
//     公共 http_application 就位后替换 server 部分，学生路由与页面保持不变；
//   * 真实密码校验走 libsodium（定义 COURSE_REGISTRATION_USE_SODIUM 时）；
//     未定义时使用演示密码，便于本机联调；
//   * 业务规则和文件保存继续由现有 C++ 服务负责，本层只做参数转换。

#include "httplib.h"
#include "login_service.hpp"
#include "persistent_registration_service.hpp"
#include "student_query_and_timetable_services.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef COURSE_REGISTRATION_USE_SODIUM
#include <sodium.h>
#endif

using namespace course_registration;

namespace {

// ---------------------------------------------------------------------------
// JSON 输出
// ---------------------------------------------------------------------------

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8] = {0};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string quote(const std::string& text) {
    return "\"" + json_escape(text) + "\"";
}

std::string envelope(const std::string& code, const std::string& message,
                     const std::string& data) {
    return "{\"code\":" + quote(code) + ",\"message\":" + quote(message) +
           ",\"data\":" + data + "}";
}

// ---------------------------------------------------------------------------
// JSON 请求（扁平对象，够本阶段接口使用）
// ---------------------------------------------------------------------------

struct JsonValue {
    enum class Type { Null, String, Integer, Boolean } type = Type::Null;
    std::string text;
    long long integer = 0;
    bool boolean = false;
};

void append_utf8(std::string& out, unsigned code) {
    if (code <= 0x7F) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    bool parse_object(std::map<std::string, JsonValue>& out) {
        skip_ws();
        if (!consume('{')) {
            return false;
        }
        skip_ws();
        if (peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            std::string key;
            if (!read_string(key)) {
                return false;
            }
            skip_ws();
            if (!consume(':')) {
                return false;
            }
            JsonValue value;
            if (!read_value(value)) {
                return false;
            }
            out[key] = value;
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                skip_ws();
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool consume(char c) {
        if (peek() != c) {
            return false;
        }
        ++pos_;
        return true;
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool read_string(std::string& out) {
        if (!consume('"')) {
            return false;
        }
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) {
                    return false;
                }
                const char esc = text_[pos_];
                if (esc == 'u') {
                    ++pos_;
                    if (!read_unicode(out)) {
                        return false;
                    }
                    continue;
                }
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: return false;
                }
                ++pos_;
            } else {
                out.push_back(c);
                ++pos_;
            }
        }
        return false;
    }

    bool read_hex4(unsigned& code) {
        if (pos_ + 4 > text_.size()) {
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_ + static_cast<size_t>(i)];
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            if (digit < 0) {
                return false;
            }
            code = code * 16u + static_cast<unsigned>(digit);
        }
        pos_ += 4;
        return true;
    }

    bool read_unicode(std::string& out) {
        unsigned code = 0;
        if (!read_hex4(code)) {
            return false;
        }
        if (code >= 0xD800u && code <= 0xDBFFu) {
            if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' ||
                text_[pos_ + 1] != 'u') {
                return false;
            }
            pos_ += 2;
            unsigned low = 0;
            if (!read_hex4(low) || low < 0xDC00u || low > 0xDFFFu) {
                return false;
            }
            code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
        }
        append_utf8(out, code);
        return true;
    }

    bool read_integer(JsonValue& out) {
        const size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ == start || (pos_ == start + 1 && text_[start] == '-')) {
            return false;
        }
        try {
            out.type = JsonValue::Type::Integer;
            out.integer = std::stoll(text_.substr(start, pos_ - start));
        } catch (...) {
            return false;
        }
        return true;
    }

    bool read_value(JsonValue& out) {
        skip_ws();
        const char c = peek();
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return read_string(out.text);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return read_integer(out);
        }
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out.type = JsonValue::Type::Boolean;
            out.boolean = true;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out.type = JsonValue::Type::Boolean;
            out.boolean = false;
            return true;
        }
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out.type = JsonValue::Type::Null;
            return true;
        }
        return false;
    }
};

bool get_string(const std::map<std::string, JsonValue>& object,
                const std::string& key, std::string& out) {
    const auto found = object.find(key);
    if (found == object.end() || found->second.type != JsonValue::Type::String) {
        return false;
    }
    out = found->second.text;
    return true;
}

bool get_int(const std::map<std::string, JsonValue>& object,
             const std::string& key, int& out) {
    const auto found = object.find(key);
    if (found == object.end() ||
        found->second.type != JsonValue::Type::Integer) {
        return false;
    }
    if (found->second.integer < -2147483648LL ||
        found->second.integer > 2147483647LL) {
        return false;
    }
    out = static_cast<int>(found->second.integer);
    return true;
}

// ---------------------------------------------------------------------------
// 结果码 / HTTP 状态
// ---------------------------------------------------------------------------

const char* code_name(ResultCode code) {
    switch (code) {
        case ResultCode::Success: return "Success";
        case ResultCode::RegistrationClosed: return "RegistrationClosed";
        case ResultCode::AlreadyEnrolled: return "AlreadyEnrolled";
        case ResultCode::TimeConflict: return "TimeConflict";
        case ResultCode::CreditLimitExceeded: return "CreditLimitExceeded";
        case ResultCode::TeachingClassFull: return "TeachingClassFull";
        case ResultCode::TeachingClassNotFound: return "TeachingClassNotFound";
        case ResultCode::ActiveEnrollmentNotFound: return "ActiveEnrollmentNotFound";
        case ResultCode::PermissionDenied: return "PermissionDenied";
        case ResultCode::InvalidInput: return "InvalidInput";
        case ResultCode::RecordNotFound: return "RecordNotFound";
        case ResultCode::DuplicateIdentifier: return "DuplicateIdentifier";
        case ResultCode::CapacityBelowEnrollment: return "CapacityBelowEnrollment";
        case ResultCode::CatalogInUse: return "CatalogInUse";
        case ResultCode::StorageIoError: return "StorageIoError";
        case ResultCode::StorageParseError: return "StorageParseError";
        case ResultCode::StorageSchemaMismatch: return "StorageSchemaMismatch";
        case ResultCode::StorageIntegrityError: return "StorageIntegrityError";
        case ResultCode::StorageBackupMissing: return "StorageBackupMissing";
    }
    return "InvalidInput";
}

int http_status(ResultCode code) {
    switch (code) {
        case ResultCode::Success: return 200;
        case ResultCode::InvalidInput: return 400;
        case ResultCode::PermissionDenied: return 403;
        case ResultCode::RecordNotFound:
        case ResultCode::TeachingClassNotFound:
        case ResultCode::ActiveEnrollmentNotFound: return 404;
        case ResultCode::AlreadyEnrolled:
        case ResultCode::TimeConflict:
        case ResultCode::CreditLimitExceeded:
        case ResultCode::TeachingClassFull:
        case ResultCode::RegistrationClosed:
        case ResultCode::DuplicateIdentifier:
        case ResultCode::CapacityBelowEnrollment:
        case ResultCode::CatalogInUse: return 409;
        default: return 500;
    }
}

const char* role_name(Role role) {
    switch (role) {
        case Role::Student: return "Student";
        case Role::Teacher: return "Teacher";
        case Role::Administrator: return "Administrator";
    }
    return "Student";
}

// ---------------------------------------------------------------------------
// 视图 JSON
// ---------------------------------------------------------------------------

std::string meeting_json(const ClassMeeting& meeting) {
    return "{\"day_of_week\":" + std::to_string(meeting.day_of_week) +
           ",\"start_minute\":" + std::to_string(meeting.start_minute) +
           ",\"end_minute\":" + std::to_string(meeting.end_minute) +
           ",\"room\":" + quote(meeting.room) + "}";
}

std::string course_view_json(const CourseView& view) {
    std::string classes;
    for (size_t i = 0; i < view.teaching_classes.size(); ++i) {
        const TeachingClassView& item = view.teaching_classes[i];
        std::string meetings;
        for (size_t m = 0; m < item.meetings.size(); ++m) {
            if (m > 0) meetings += ",";
            meetings += meeting_json(item.meetings[m]);
        }
        if (i > 0) classes += ",";
        classes += "{\"teaching_class_id\":" +
                   std::to_string(item.teaching_class_id) +
                   ",\"teacher_id\":" + std::to_string(item.teacher_id) +
                   ",\"teacher_name\":" + quote(item.teacher_name) +
                   ",\"capacity\":" + std::to_string(item.capacity) +
                   ",\"active_enrollments\":" +
                   std::to_string(item.active_enrollments) +
                   ",\"remaining_seats\":" +
                   std::to_string(item.remaining_seats) +
                   ",\"meetings\":[" + meetings + "]}";
    }
    return "{\"course\":{\"course_id\":" +
           std::to_string(view.course.course_id) +
           ",\"course_code\":" + quote(view.course.course_code) +
           ",\"course_name\":" + quote(view.course.course_name) +
           ",\"credits\":" + std::to_string(view.course.credits) +
           "},\"teaching_classes\":[" + classes + "]}";
}

std::string courses_json(const std::vector<CourseView>& views) {
    std::string out = "[";
    for (size_t i = 0; i < views.size(); ++i) {
        if (i > 0) out += ",";
        out += course_view_json(views[i]);
    }
    return out + "]";
}

std::string seats_json(const SeatsView& view) {
    return "{\"teaching_class_id\":" + std::to_string(view.teaching_class_id) +
           ",\"course_id\":" + std::to_string(view.course_id) +
           ",\"course_code\":" + quote(view.course_code) +
           ",\"course_name\":" + quote(view.course_name) +
           ",\"capacity\":" + std::to_string(view.capacity) +
           ",\"active_enrollments\":" +
           std::to_string(view.active_enrollments) +
           ",\"remaining_seats\":" + std::to_string(view.remaining_seats) + "}";
}

std::string timetable_json(const std::vector<TimetableEntry>& entries) {
    std::string out = "[";
    for (size_t i = 0; i < entries.size(); ++i) {
        const TimetableEntry& entry = entries[i];
        if (i > 0) out += ",";
        out += "{\"teaching_class_id\":" +
               std::to_string(entry.teaching_class_id) +
               ",\"course_code\":" + quote(entry.course_code) +
               ",\"course_name\":" + quote(entry.course_name) +
               ",\"teacher_name\":" + quote(entry.teacher_name) +
               ",\"meeting\":" + meeting_json(entry.meeting) + "}";
    }
    return out + "]";
}

std::string enrollments_json(const std::vector<Enrollment>& records, int active,
                             int dropped) {
    std::string list = "[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) list += ",";
        list += "{\"teaching_class_id\":" +
                std::to_string(records[i].teaching_class_id) +
                ",\"status\":" +
                quote(records[i].status == EnrollmentStatus::Active
                          ? "active"
                          : "dropped") +
                "}";
    }
    list += "]";
    return "{\"active\":" + std::to_string(active) +
           ",\"dropped\":" + std::to_string(dropped) +
           ",\"records\":" + list + "}";
}

// ---------------------------------------------------------------------------
// 会话
// ---------------------------------------------------------------------------

struct Session {
    UserIdentity identity;
    std::string csrf;
    std::chrono::steady_clock::time_point last_seen;
};

std::string cookie_value(const std::string& header, const std::string& name) {
    size_t pos = 0;
    while (pos < header.size()) {
        const size_t end = header.find(';', pos);
        std::string item = header.substr(
            pos, end == std::string::npos ? std::string::npos : end - pos);
        while (!item.empty() && item.front() == ' ') {
            item.erase(item.begin());
        }
        const size_t eq = item.find('=');
        if (eq != std::string::npos && item.substr(0, eq) == name) {
            return item.substr(eq + 1);
        }
        if (end == std::string::npos) {
            break;
        }
        pos = end + 1;
    }
    return {};
}

std::string random_hex(size_t bytes) {
    std::random_device device;
    std::mt19937_64 generator(device());
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        const unsigned value = static_cast<unsigned>(generator() & 0xFFu);
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0x0F]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// HTTP 服务
// ---------------------------------------------------------------------------

class StudentHttpService {
public:
    StudentHttpService(SystemContext& context, StorageOptions options,
                       std::string web_root, std::string demo_password)
        : context_(context),
          options_(std::move(options)),
          web_root_(std::move(web_root)),
          demo_password_(std::move(demo_password)) {}

    bool listen(const std::string& host, int port) {
        register_routes();
        // 静态页面统一按 UTF-8 返回，浏览器和 Invoke-WebRequest 才能正确解码中文。
        server_.set_file_extension_and_mimetype_mapping(
            "html", "text/html; charset=utf-8");
        server_.set_file_extension_and_mimetype_mapping(
            "css", "text/css; charset=utf-8");
        server_.set_file_extension_and_mimetype_mapping(
            "js", "application/javascript; charset=utf-8");
        if (!web_root_.empty()) {
            server_.set_mount_point("/", web_root_);
        }
        return server_.listen(host, port);
    }

private:
    // ---- 会话与身份 ----
    bool find_session(const httplib::Request& request, Session& out) {
        const std::string token =
            cookie_value(request.get_header_value("Cookie"), "team4_session");
        if (token.empty()) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        const auto found = sessions_.find(token);
        if (found == sessions_.end()) {
            return false;
        }
        if (now - found->second.last_seen > std::chrono::hours(8)) {
            sessions_.erase(found);
            return false;
        }
        found->second.last_seen = now;
        out = found->second;
        return true;
    }

    bool require_student(const httplib::Request& request,
                         httplib::Response& response, Session& session,
                         bool csrf) {
        if (!find_session(request, session)) {
            send_error(response, 401, "AuthenticationRequired",
                       "sign-in required");
            return false;
        }
        if (session.identity.role != Role::Student) {
            send_error(response, 403, "PermissionDenied",
                       "student account required");
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(context_mutex_);
            const Student* student =
                context_.find_student(session.identity.user_id);
            if (student == nullptr || !student->account_active ||
                normalize_account_key(session.identity.account_number) !=
                    normalize_account_key(student->student_number)) {
                send_error(response, 401, "AuthenticationRequired",
                           "session is no longer valid");
                return false;
            }
        }
        if (csrf) {
            const std::string token =
                request.get_header_value("X-CSRF-Token");
            if (token.empty() || token != session.csrf) {
                send_error(response, 403, "CsrfRejected",
                           "invalid csrf token");
                return false;
            }
        }
        return true;
    }

    // ---- 响应 ----
    static void send_error(httplib::Response& response, int status,
                           const std::string& code,
                           const std::string& message) {
        response.status = status;
        response.set_content(envelope(code, message, "null"),
                             "application/json; charset=utf-8");
    }

    static void send_result_error(httplib::Response& response,
                                  const OperationResult& result) {
        send_error(response, http_status(result.code), code_name(result.code),
                   result.message);
    }

    static void send_ok(httplib::Response& response,
                        const std::string& message,
                        const std::string& data) {
        response.status = 200;
        response.set_content(envelope("Success", message, data),
                             "application/json; charset=utf-8");
    }

    // ---- 密码校验 ----
    bool verify_password(const UserIdentity& identity, std::string_view password) {
        if (password.empty()) {
            return false;
        }
#ifndef COURSE_REGISTRATION_USE_SODIUM
        (void)identity;
#endif
#ifdef COURSE_REGISTRATION_USE_SODIUM
        std::string hash;
        if (identity.role == Role::Student) {
            const Student* student = context_.find_student(identity.user_id);
            if (student != nullptr) hash = student->password_hash;
        } else if (identity.role == Role::Teacher) {
            const Teacher* teacher = context_.find_teacher(identity.user_id);
            if (teacher != nullptr) hash = teacher->password_hash;
        } else {
            for (const Administrator& admin : context_.administrators()) {
                if (admin.administrator_id == identity.user_id) {
                    hash = admin.password_hash;
                    break;
                }
            }
        }
        if (hash.empty()) {
            return false;
        }
        return crypto_pwhash_str_verify(hash.c_str(), password.data(),
                                        password.size()) == 0;
#else
        return password == demo_password_;
#endif
    }

    // ---- 路由 ----
    void register_routes() {
        server_.Post("/api/auth/login",
                     [this](const httplib::Request& request,
                            httplib::Response& response) {
                         std::map<std::string, JsonValue> body;
                         JsonReader reader(request.body);
                         std::string account;
                         std::string password;
                         if (!reader.parse_object(body) ||
                             !get_string(body, "account_number", account) ||
                             !get_string(body, "password", password) ||
                             account.empty() || password.empty()) {
                             send_error(response, 400, "InvalidInput",
                                        "account_number and password are required");
                             return;
                         }
                         LoginService login(
                             context_,
                             [this](const UserIdentity& identity,
                                    std::string_view value) {
                                 return verify_password(identity, value);
                             });
                         LoginResult result;
                         {
                             std::lock_guard<std::mutex> lock(context_mutex_);
                             result = login.login(account, password);
                         }
                         if (result.result.code != ResultCode::Success ||
                             !result.identity) {
                             send_error(response, 401,
                                        "AuthenticationRequired",
                                        "sign-in failed");
                             return;
                         }
                         const std::string token = random_hex(32);
                         const std::string csrf = random_hex(32);
                         {
                             std::lock_guard<std::mutex> lock(sessions_mutex_);
                             sessions_[token] = Session{
                                 *result.identity, csrf,
                                 std::chrono::steady_clock::now()};
                         }
                         response.set_header(
                             "Set-Cookie",
                             "team4_session=" + token +
                                 "; Path=/; HttpOnly; SameSite=Lax");
                         const UserIdentity& identity = *result.identity;
                         send_ok(
                             response, "sign-in succeeded",
                             "{\"user\":{\"role\":" +
                                 quote(role_name(identity.role)) +
                                 ",\"user_id\":" +
                                 std::to_string(identity.user_id) +
                                 ",\"account_number\":" +
                                 quote(identity.account_number) +
                                 "},\"csrf_token\":" + quote(csrf) + "}");
                     });

        server_.Get("/api/auth/me",
                    [this](const httplib::Request& request,
                           httplib::Response& response) {
                        Session session;
                        if (!require_student(request, response, session, false)) {
                            return;
                        }
                        send_ok(response, "ok",
                                "{\"user\":{\"role\":\"Student\",\"user_id\":" +
                                    std::to_string(session.identity.user_id) +
                                    ",\"account_number\":" +
                                    quote(session.identity.account_number) +
                                    "},\"csrf_token\":" + quote(session.csrf) +
                                    "}");
                    });

        server_.Post("/api/auth/logout",
                     [this](const httplib::Request& request,
                            httplib::Response& response) {
                         const std::string token = cookie_value(
                             request.get_header_value("Cookie"), "team4_session");
                         {
                             std::lock_guard<std::mutex> lock(sessions_mutex_);
                             const auto found = sessions_.find(token);
                             if (found == sessions_.end()) {
                                 send_error(response, 401,
                                            "AuthenticationRequired",
                                            "sign-in required");
                                 return;
                             }
                             if (request.get_header_value("X-CSRF-Token") !=
                                 found->second.csrf) {
                                 send_error(response, 403, "CsrfRejected",
                                            "invalid csrf token");
                                 return;
                             }
                             sessions_.erase(found);
                         }
                         response.set_header(
                             "Set-Cookie",
                             "team4_session=; Path=/; Max-Age=0; HttpOnly; "
                             "SameSite=Lax");
                         send_ok(response, "signed out", "null");
                     });

        server_.Get("/api/courses",
                    [this](const httplib::Request& request,
                           httplib::Response& response) {
                        Session session;
                        if (!require_student(request, response, session, false)) {
                            return;
                        }
                        CourseSearchFilter filter;
                        if (request.has_param("course_code")) {
                            filter.course_code =
                                request.get_param_value("course_code");
                        }
                        if (request.has_param("credits")) {
                            try {
                                filter.credits =
                                    std::stoi(request.get_param_value("credits"));
                            } catch (...) {
                                send_error(response, 400, "InvalidInput",
                                           "credits must be an integer");
                                return;
                            }
                        }
                        if (request.has_param("teacher_id")) {
                            try {
                                filter.teacher_id =
                                    std::stoi(request.get_param_value("teacher_id"));
                            } catch (...) {
                                send_error(response, 400, "InvalidInput",
                                           "teacher_id must be an integer");
                                return;
                            }
                        }
                        const std::string keyword =
                            request.has_param("keyword")
                                ? request.get_param_value("keyword")
                                : std::string();
                        std::vector<CourseView> courses;
                        OperationResult result;
                        {
                            std::lock_guard<std::mutex> lock(context_mutex_);
                            StudentQueryService query(context_);
                            result = query.search_courses(keyword, filter, courses);
                        }
                        if (result.code != ResultCode::Success) {
                            send_result_error(response, result);
                            return;
                        }
                        send_ok(response, result.message, courses_json(courses));
                    });

        server_.Get(R"(/api/courses/(\d+))",
                    [this](const httplib::Request& request,
                           httplib::Response& response) {
                        Session session;
                        if (!require_student(request, response, session, false)) {
                            return;
                        }
                        const int course_id =
                            std::stoi(request.matches[1].str());
                        CourseView view;
                        OperationResult result;
                        {
                            std::lock_guard<std::mutex> lock(context_mutex_);
                            StudentQueryService query(context_);
                            result = query.course_detail(course_id, view);
                        }
                        if (result.code != ResultCode::Success) {
                            send_result_error(response, result);
                            return;
                        }
                        send_ok(response, result.message, course_view_json(view));
                    });

        server_.Get(R"(/api/classes/(\d+)/seats)",
                    [this](const httplib::Request& request,
                           httplib::Response& response) {
                        Session session;
                        if (!require_student(request, response, session, false)) {
                            return;
                        }
                        const int class_id = std::stoi(request.matches[1].str());
                        SeatsView view;
                        OperationResult result;
                        {
                            std::lock_guard<std::mutex> lock(context_mutex_);
                            StudentQueryService query(context_);
                            result = query.view_available_seats(class_id, view);
                        }
                        if (result.code != ResultCode::Success) {
                            send_result_error(response, result);
                            return;
                        }
                        send_ok(response, result.message, seats_json(view));
                    });

        server_.Get("/api/me/timetable",
                    [this](const httplib::Request& request,
                           httplib::Response& response) {
                        Session session;
                        if (!require_student(request, response, session, false)) {
                            return;
                        }
                        std::vector<TimetableEntry> entries;
                        OperationResult result;
                        {
                            std::lock_guard<std::mutex> lock(context_mutex_);
                            StudentQueryService query(context_);
                            result = query.view_timetable(session.identity, entries);
                        }
                        if (result.code != ResultCode::Success) {
                            send_result_error(response, result);
                            return;
                        }
                        send_ok(response, result.message, timetable_json(entries));
                    });

        server_.Get("/api/me/enrollments",
                    [this](const httplib::Request& request,
                           httplib::Response& response) {
                        Session session;
                        if (!require_student(request, response, session, false)) {
                            return;
                        }
                        int active = 0;
                        int dropped = 0;
                        std::vector<Enrollment> records;
                        {
                            std::lock_guard<std::mutex> lock(context_mutex_);
                            for (const Enrollment& record :
                                 context_.all_enrollments()) {
                                if (record.student_id !=
                                    session.identity.user_id) {
                                    continue;
                                }
                                records.push_back(record);
                                if (record.status == EnrollmentStatus::Active) {
                                    ++active;
                                } else {
                                    ++dropped;
                                }
                            }
                        }
                        send_ok(response, "ok",
                                enrollments_json(records, active, dropped));
                    });

        server_.Post("/api/me/enrollments",
                     [this](const httplib::Request& request,
                            httplib::Response& response) {
                         Session session;
                         if (!require_student(request, response, session, true)) {
                             return;
                         }
                         std::map<std::string, JsonValue> body;
                         int class_id = 0;
                         if (!JsonReader(request.body).parse_object(body) ||
                             !get_int(body, "teaching_class_id", class_id) ||
                             class_id <= 0) {
                             send_error(response, 400, "InvalidInput",
                                        "teaching_class_id must be a positive integer");
                             return;
                         }
                         OperationResult result;
                         {
                             std::lock_guard<std::mutex> lock(context_mutex_);
                             PersistentRegistrationService service(context_,
                                                                    options_);
                             result = service.enroll(session.identity, class_id);
                         }
                         if (result.code != ResultCode::Success) {
                             send_result_error(response, result);
                             return;
                         }
                         send_ok(response, result.message, "null");
                     });

        server_.Delete(R"(/api/me/enrollments/(\d+))",
                       [this](const httplib::Request& request,
                              httplib::Response& response) {
                           Session session;
                           if (!require_student(request, response, session, true)) {
                               return;
                           }
                           const int class_id =
                               std::stoi(request.matches[1].str());
                           OperationResult result;
                           {
                               std::lock_guard<std::mutex> lock(context_mutex_);
                               PersistentRegistrationService service(context_,
                                                                      options_);
                               result = service.drop(session.identity, class_id);
                           }
                           if (result.code != ResultCode::Success) {
                               send_result_error(response, result);
                               return;
                           }
                           send_ok(response, result.message, "null");
                       });
    }

    SystemContext& context_;
    StorageOptions options_;
    std::string web_root_;
    std::string demo_password_;
    std::mutex context_mutex_;
    std::mutex sessions_mutex_;
    std::map<std::string, Session> sessions_;
    httplib::Server server_;
};

void usage() {
    std::cerr << "用法：course_registration_http_server --state PATH "
                 "[--host 127.0.0.1] [--port 8080] [--web-root ./web] "
                 "[--demo-password PASSWORD]\n"
                 "  --state 必须显式指定，服务器不会写入默认 data/state.json。\n"
                 "  未启用 libsodium 时，必须显式传 --demo-password 才允许本地演示。\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string state_path;
    std::string web_root = "web";
    std::string demo_password;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--state" && i + 1 < argc) {
            state_path = argv[++i];
        } else if (argument == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (argument == "--port" && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                usage();
                return 2;
            }
        } else if (argument == "--web-root" && i + 1 < argc) {
            web_root = argv[++i];
        } else if (argument == "--demo-password" && i + 1 < argc) {
            demo_password = argv[++i];
        } else {
            usage();
            return 2;
        }
    }
    if (state_path.empty() || port <= 0 || port > 65535) {
        usage();
        return 2;
    }

#ifndef COURSE_REGISTRATION_USE_SODIUM
    // 正式登录不再使用演示密码：没启用 libsodium 时必须显式要求本地演示。
    if (demo_password.empty()) {
        std::cerr << "未启用 libsodium：正式登录需要真实密码校验。\n"
                     "本地演示请显式传 --demo-password，或按文档启用 "
                     "COURSE_REGISTRATION_USE_SODIUM。\n";
        return 2;
    }
#endif

#ifdef COURSE_REGISTRATION_USE_SODIUM
    if (sodium_init() < 0) {
        std::cerr << "libsodium 初始化失败\n";
        return 1;
    }
#endif

    StorageOptions storage;
    storage.path = state_path;
    SystemContext context = make_seed_context();
    const OperationResult initialized = initialize_storage(storage, context);
    if (initialized.code != ResultCode::Success) {
        std::cerr << "初始化失败：" << initialized.message << "\n";
        return 1;
    }

    StudentHttpService service(context, storage, web_root, demo_password);
    std::cout << "HTTP 服务监听 http://" << host << ":" << port << "\n";
    std::cout << "数据文件：" << state_path << "\n";
    std::cout << "静态页面：" << web_root << "\n";
#ifndef COURSE_REGISTRATION_USE_SODIUM
    std::cout << "密码校验：演示模式（--demo-password）；启用 libsodium 后改用 "
                 "password_hash\n";
#endif
    if (!service.listen(host, port)) {
        std::cerr << "监听失败：" << host << ":" << port << "\n";
        return 1;
    }
    return 0;
}
