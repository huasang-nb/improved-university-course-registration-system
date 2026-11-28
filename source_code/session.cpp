#include "session.hpp"

#include <sodium.h>

#include <atomic>
#include <cctype>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>

namespace course_registration {
namespace {
using json = nlohmann::json;
std::atomic<SessionRuntime*> active_runtime{nullptr};

OperationResult success() { return {ResultCode::Success, "success"}; }
OperationResult authentication_required() {
    return {ResultCode::AuthenticationRequired, "sign-in required"};
}

bool same_actor(const UserIdentity& first, const UserIdentity& second) {
    return first.role == second.role && first.user_id == second.user_id &&
           normalize_account_key(first.account_number) ==
               normalize_account_key(second.account_number);
}

std::string random_token() {
    unsigned char bytes[32];
    randombytes_buf(bytes, sizeof(bytes));
    char encoded[65];
    sodium_bin2hex(encoded, sizeof(encoded), bytes, sizeof(bytes));
    return encoded;
}

bool token_equal(const std::string& first, const std::string& second) {
    return first.size() == second.size() && !first.empty() &&
           sodium_memcmp(first.data(), second.data(), first.size()) == 0;
}

std::string session_token(const httplib::Request& request) {
    std::string result;
    bool seen = false;
    const std::string header = request.get_header_value("Cookie");
    std::size_t position = 0;
    while (position < header.size()) {
        const auto end = header.find(';', position);
        const auto item = header.substr(position, end - position);
        const auto begin = item.find_first_not_of(" \t");
        if (begin != std::string::npos && item.compare(begin, 14, "team4_session=") == 0) {
            // 重复的同名 Cookie 含义不明确，直接拒绝，不随意取其中一个。
            if (seen) return {};
            seen = true;
            result = item.substr(begin + 14);
        }
        if (end == std::string::npos) break;
        position = end + 1;
    }
    if (result.size() != 64 || result.find_first_not_of("0123456789abcdef") != std::string::npos) {
        return {};
    }
    return result;
}

// 来源按协议、主机和有效端口比较。Referer 可以有路径，Origin 不能带路径。
std::string normalized_origin(const std::string& value, bool referer = false) {
    static const std::regex pattern(
        R"(^(https?)://(\[[0-9A-Fa-f:]+\]|[A-Za-z0-9.-]+)(?::([0-9]+))?(/[^\r\n]*)?$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern) || (!referer && match[4].matched)) {
        return {};
    }
    std::string host = match[2].str();
    for (char& character : host) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    unsigned long port = match[1] == "https" ? 443 : 80;
    if (match[3].matched) {
        try { port = std::stoul(match[3].str()); }
        catch (const std::exception&) { return {}; }
    }
    if (port == 0 || port > 65535) return {};
    return match[1].str() + "://" + host + ":" + std::to_string(port);
}

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
    case ResultCode::AuthenticationRequired: return "AuthenticationRequired";
    }
    return "InternalError";
}

int business_status(ResultCode code) {
    switch (code) {
    case ResultCode::Success: return 200;
    case ResultCode::InvalidInput: return 400;
    case ResultCode::AuthenticationRequired: return 401;
    case ResultCode::PermissionDenied: return 403;
    case ResultCode::TeachingClassNotFound:
    case ResultCode::ActiveEnrollmentNotFound:
    case ResultCode::RecordNotFound: return 404;
    case ResultCode::RegistrationClosed:
    case ResultCode::AlreadyEnrolled:
    case ResultCode::TimeConflict:
    case ResultCode::CreditLimitExceeded:
    case ResultCode::TeachingClassFull:
    case ResultCode::DuplicateIdentifier:
    case ResultCode::CapacityBelowEnrollment:
    case ResultCode::CatalogInUse: return 409;
    default: return 500;
    }
}

json identity_json(const UserIdentity& identity) {
    const char* role = identity.role == Role::Student ? "Student" :
                       identity.role == Role::Teacher ? "Teacher" : "Administrator";
    return {{"role", role}, {"user_id", identity.user_id},
            {"account_number", identity.account_number}};
}

bool read_body(const httplib::Request& request, httplib::Response& response,
               const char* first, const char* second, json& body) {
    if (request.body.size() > 64 * 1024) {
        write_result(response, {ResultCode::InvalidInput, "request body exceeds 64 KiB"},
                     ResultSource::Business);
        return false;
    }
    bool duplicate_key = false;
    std::set<std::string> keys;
    body = json::parse(request.body, [&](int, json::parse_event_t event, json& value) {
        if (event == json::parse_event_t::key && !keys.insert(value.get<std::string>()).second) {
            duplicate_key = true;
        }
        return true;
    }, false);
    if (duplicate_key || !body.is_object() || body.size() != 2 ||
        !body.contains(first) || !body[first].is_string() ||
        !body.contains(second) || !body[second].is_string()) {
        write_result(response, {ResultCode::InvalidInput, "request must contain exactly the two required string fields"},
                     ResultSource::Business);
        return false;
    }
    return true;
}
}  // namespace

void write_result(httplib::Response& response, const OperationResult& result,
                  ResultSource source, const json& data) {
    int status = business_status(result.code);
    std::string code = code_name(result.code);
    std::string message = result.message;
    if (source == ResultSource::Internal) {
        status = 500;
        code = "InternalError";
        message = "internal server error";
    } else if (status >= 500) {
        // 保留 Storage* 错误码，但把文件路径等细节留在服务器端。
        // 即使调用方误传 Session/Permission 来源，也不能把内部失败变成 401/403。
        message = "server could not complete the operation";
    } else if (result.code != ResultCode::Success) {
        if (source == ResultSource::Session || result.code == ResultCode::AuthenticationRequired) {
            status = 401;
            code = "AuthenticationRequired";
        } else if (source == ResultSource::Csrf) {
            status = 403;
            code = "CsrfRejected";
        } else if (source == ResultSource::Permission) {
            status = 403;
            code = "PermissionDenied";
        }
    }
    response.status = status;
    response.set_header("Cache-Control", "no-store");
    response.set_content(json{{"code", code}, {"message", message},
                              {"data", status == 200 ? data : json(nullptr)}}.dump(),
                         "application/json; charset=utf-8");
}

SessionRuntime::SessionRuntime(SystemContext& context, StorageOptions storage,
                                 SessionOptions options)
    : context_(context), storage_(std::move(storage)), options_(std::move(options)),
      origin_(normalized_origin(options_.allowed_origin)) {
    if (origin_.empty() || !options_.now || options_.idle_timeout.count() <= 0 ||
        options_.absolute_timeout.count() <= 0) {
        throw std::invalid_argument("invalid session configuration");
    }
    if (sodium_init() < 0) throw std::runtime_error("cannot initialize libsodium");
    secure_cookie_ = origin_.rfind("https://", 0) == 0;
}

SessionRuntime::~SessionRuntime() {
    SessionRuntime* expected = this;
    active_runtime.compare_exchange_strong(expected, nullptr);
}

OperationResult SessionRuntime::check_actor_locked(const UserIdentity& actor) const {
    PasswordAccount account;
    const auto found = find_password_account(context_, actor.account_number, account);
    if (found.code != ResultCode::Success || !same_actor(actor, account.identity) ||
        stored_password_hash(context_, actor).empty()) {
        return authentication_required();
    }
    return success();
}

SessionRuntime::Session* SessionRuntime::find_session_locked(
    const httplib::Request& request, bool refresh) {
    const auto found = sessions_.find(session_token(request));
    if (found == sessions_.end()) return nullptr;
    const auto now = options_.now();
    Session& session = found->second;
    if (now - session.last_seen >= options_.idle_timeout ||
        now - session.created_at >= options_.absolute_timeout ||
        check_actor_locked(session.identity).code != ResultCode::Success ||
        stored_password_hash(context_, session.identity) != session.password_hash) {
        sessions_.erase(found);
        return nullptr;
    }
    if (refresh) session.last_seen = now;
    return &session;
}

SessionCheck SessionRuntime::require_session(const httplib::Request& request) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto* session = find_session_locked(request, true);
    if (!session) return {authentication_required(), std::nullopt};
    return {success(), session->identity};
}

OperationResult SessionRuntime::require_csrf(const httplib::Request& request) {
    if (request.method == "GET" || request.method == "HEAD" || request.method == "OPTIONS") {
        return success();
    }
    std::lock_guard<std::mutex> guard(mutex_);
    const auto* session = find_session_locked(request, false);
    if (!session) return authentication_required();
    if (!token_equal(request.get_header_value("X-CSRF-Token"), session->csrf_token)) {
        return {ResultCode::PermissionDenied, "CSRF token is missing or invalid"};
    }
    return success();
}

OperationResult SessionRuntime::run_business_locked(
    const UserIdentity& actor, Role required_role,
    const std::function<void(LockedServices&)>& operation) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto checked = check_actor_locked(actor);
    if (checked.code != ResultCode::Success) return checked;
    if (actor.role != required_role) {
        return {ResultCode::PermissionDenied, "role is not permitted"};
    }
    LockedServices services(context_, storage_);
    operation(services);
    return success();
}

bool SessionRuntime::same_origin(const httplib::Request& request) const {
    if (request.has_header("Origin")) {
        return request.get_header_value_count("Origin") == 1 &&
               normalized_origin(request.get_header_value("Origin")) == origin_;
    }
    return request.get_header_value_count("Referer") == 1 &&
           normalized_origin(request.get_header_value("Referer"), true) == origin_;
}

std::string SessionRuntime::cookie(const std::string& token, bool clear) const {
    std::string value = "team4_session=" + token + "; Path=/; HttpOnly; SameSite=Lax";
    if (secure_cookie_) value += "; Secure";
    if (clear) value += "; Max-Age=0";
    return value;
}

void SessionRuntime::login(const httplib::Request& request, httplib::Response& response) {
    if (!same_origin(request)) {
        write_result(response, {ResultCode::PermissionDenied, "same-origin login is required"}, ResultSource::Csrf);
        return;
    }
    json body;
    if (!read_body(request, response, "account_number", "password", body)) return;
    const auto account = body["account_number"].get<std::string>();
    const auto password = body["password"].get<std::string>();
    // 在锁内复制当前资料，在锁外运行较慢的 Argon2id 校验。
    const SystemContext snapshot = [&] {
        std::lock_guard<std::mutex> guard(mutex_);
        return context_;
    }();
    LoginService service(snapshot, make_stored_password_verifier(snapshot));
    const auto result = service.login(account, password);
    if (!result.identity) {
        if (result.result.code == ResultCode::InvalidInput) {
            write_result(response, result.result, ResultSource::Business);
            return;
        }
        write_result(response, authentication_required(), ResultSource::Session);
        return;
    }
    const auto& actor = *result.identity;
    const auto hash = stored_password_hash(snapshot, actor);
    const auto token = random_token();
    const auto csrf = random_token();
    {
        std::lock_guard<std::mutex> guard(mutex_);
        // 密码计算期间可能发生停用、改号或改密，发放会话前必须重新检查。
        if (check_actor_locked(actor).code != ResultCode::Success ||
            stored_password_hash(context_, actor) != hash) {
            write_result(response, authentication_required(), ResultSource::Session);
            return;
        }
        const auto now = options_.now();
        for (auto entry = sessions_.begin(); entry != sessions_.end();) {
            if (now - entry->second.last_seen >= options_.idle_timeout ||
                now - entry->second.created_at >= options_.absolute_timeout) {
                entry = sessions_.erase(entry);
            } else {
                ++entry;
            }
        }
        // 同一浏览器重新登录时销毁旧编号；其他浏览器的会话保留到退出或改密。
        sessions_.erase(session_token(request));
        sessions_.emplace(token, Session{actor, hash, csrf, now, now});
    }
    response.set_header("Set-Cookie", cookie(token));
    write_result(response, success(), ResultSource::Business,
                 {{"user", identity_json(actor)}, {"csrf_token", csrf}});
}

void SessionRuntime::me(const httplib::Request& request, httplib::Response& response) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto* session = find_session_locked(request, true);
    if (!session) {
        write_result(response, authentication_required(), ResultSource::Session);
        return;
    }
    write_result(response, success(), ResultSource::Business,
                 {{"user", identity_json(session->identity)}, {"csrf_token", session->csrf_token}});
}

void SessionRuntime::revoke_account_locked(const UserIdentity& actor) {
    for (auto entry = sessions_.begin(); entry != sessions_.end();) {
        if (same_actor(entry->second.identity, actor)) entry = sessions_.erase(entry);
        else ++entry;
    }
}

void SessionRuntime::logout(const httplib::Request& request, httplib::Response& response) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto* session = find_session_locked(request, false);
    if (!session) {
        write_result(response, authentication_required(), ResultSource::Session);
        return;
    }
    if (!token_equal(request.get_header_value("X-CSRF-Token"), session->csrf_token)) {
        write_result(response, {ResultCode::PermissionDenied, "CSRF token is missing or invalid"}, ResultSource::Csrf);
        return;
    }
    sessions_.erase(session_token(request));
    response.set_header("Set-Cookie", cookie("", true));
    write_result(response, success(), ResultSource::Business);
}

void SessionRuntime::password(const httplib::Request& request, httplib::Response& response) {
    std::optional<UserIdentity> actor;
    // 获取身份、检查 CSRF 和复制资料在同一次加锁中完成。
    const SystemContext snapshot = [&] {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto* session = find_session_locked(request, true);
        if (!session) {
            write_result(response, authentication_required(), ResultSource::Session);
        } else if (!token_equal(request.get_header_value("X-CSRF-Token"), session->csrf_token)) {
            write_result(response, {ResultCode::PermissionDenied, "CSRF token is missing or invalid"}, ResultSource::Csrf);
        } else {
            actor = session->identity;
        }
        return context_;
    }();
    if (!actor) return;
    json body;
    if (!read_body(request, response, "current_password", "new_password", body)) return;
    PreparedPasswordChange prepared;
    auto result = prepare_password_change(snapshot, *actor,
        body["current_password"].get<std::string>(), body["new_password"].get<std::string>(), prepared);
    if (result.code != ResultCode::Success) {
        write_result(response, result, ResultSource::Business);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        // 在计算期间可能已退出、过期或改密，保存前再查当前会话和最新凭据。
        if (!find_session_locked(request, false)) {
            write_result(response, authentication_required(), ResultSource::Session);
            return;
        }
        result = apply_password_change(context_, storage_, prepared);
        if (result.code == ResultCode::Success) revoke_account_locked(*actor);
    }
    if (result.code == ResultCode::Success) response.set_header("Set-Cookie", cookie("", true));
    // 会话已撤销，但本次改密的成功响应仍是 200，不再重新执行身份检查。
    write_result(response, result, ResultSource::Business);
}

void SessionRuntime::register_auth_routes(httplib::Server& server) {
    server.set_exception_handler([](const httplib::Request&, httplib::Response& response,
                                    std::exception_ptr) {
        std::cerr << "HTTP request failed with an internal exception\n";
        write_result(response, {ResultCode::InvalidInput, ""}, ResultSource::Internal);
    });
    server.Post("/api/auth/login", [this](const auto& request, auto& response) { login(request, response); });
    server.Get("/api/auth/me", [this](const auto& request, auto& response) { me(request, response); });
    server.Post("/api/auth/logout", [this](const auto& request, auto& response) { logout(request, response); });
    server.Put("/api/auth/password", [this](const auto& request, auto& response) { password(request, response); });
}

void bind_session_runtime(SessionRuntime& runtime) {
    SessionRuntime* expected = nullptr;
    if (!active_runtime.compare_exchange_strong(expected, &runtime) && expected != &runtime) {
        throw std::logic_error("another session runtime is already bound");
    }
}
SessionRuntime& active_session_runtime() {
    auto* runtime = active_runtime.load();
    if (!runtime) throw std::logic_error("session runtime has not been bound");
    return *runtime;
}
SessionCheck require_session(const httplib::Request& request) {
    return active_session_runtime().require_session(request);
}
OperationResult require_csrf(const httplib::Request& request) {
    return active_session_runtime().require_csrf(request);
}
}  // namespace course_registration
