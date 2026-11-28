#include "session.hpp"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace course_registration;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
int checks = 0;

void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}

void expect(const httplib::Result& result, int status, const char* code) {
    require(bool(result), "HTTP request failed before receiving a response: " + httplib::to_string(result.error()));
    require(result->status == status, "unexpected HTTP status: " + result->body);
    require(json::parse(result->body).at("code") == code, "unexpected response code");
    if (status != 200) {
        require(json::parse(result->body).at("data").is_null(), "failure leaked response data");
    }
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot read test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Browser {
    std::string cookie;
    std::string csrf;
    httplib::Headers headers(bool write = false) const {
        httplib::Headers result{{"Cookie", cookie}};
        if (write) result.emplace("X-CSRF-Token", csrf);
        return result;
    }
};

// 使用真实的本机 HTTP 连接，测试 Cookie、响应头和路由，而不只调用服务函数。
class Fixture {
public:
    explicit Fixture(const fs::path& directory)
        : context(make_seed_context()), storage{(directory / "state.json").string(), false} {
        fs::create_directories(directory);
        for (const auto* account : {"S2023001", "S2023002", "T2015007", "A2020001"}) {
            require(set_account_password(context, storage, account, "Initial1!").code == ResultCode::Success,
                    "cannot prepare initial password");
        }
        const int port = server.bind_to_any_port("127.0.0.1");
        require(port > 0, "cannot bind a test port");
        SessionOptions options;
        origin = "http://127.0.0.1:" + std::to_string(port);
        options.allowed_origin = origin;
        options.now = [this] {
            return std::chrono::steady_clock::time_point(std::chrono::seconds(seconds.load()));
        };
        runtime = std::make_unique<SessionRuntime>(context, storage, options);
        bind_session_runtime(*runtime);
        runtime->register_auth_routes(server);
        server.Get("/test/exception", [](const auto&, auto&) {
            with_business_lock({900, "A2020001", Role::Administrator}, Role::Administrator,
                [](LockedServices&) { throw std::runtime_error("private file path"); });
        });
        server.Get("/test/static", [](const auto&, auto& response) { response.set_content("page", "text/plain"); });
        worker = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
        client = std::make_unique<httplib::Client>("127.0.0.1", port);
        client->set_read_timeout(5, 0);
    }
    ~Fixture() {
        server.stop();
        if (worker.joinable()) worker.join();
    }
    Browser login(const char* account, const std::string& password = "Initial1!") {
        const auto result = client->Post("/api/auth/login", {{"Origin", origin}},
            json{{"account_number", account}, {"password", password}}.dump(), "application/json");
        expect(result, 200, "Success");
        const auto set_cookie = result->get_header_value("Set-Cookie");
        require(set_cookie.find("HttpOnly") != std::string::npos &&
                set_cookie.find("SameSite=Lax") != std::string::npos,
                "session cookie is missing required attributes");
        require(result->body.find("password_hash") == std::string::npos &&
                result->body.find("team4_session") == std::string::npos,
                "login response leaked credential data");
        return {set_cookie.substr(0, set_cookie.find(';')),
                json::parse(result->body).at("data").at("csrf_token")};
    }
    httplib::Result change(const Browser& browser, const std::string& current,
                           const std::string& next) {
        return client->Put("/api/auth/password", browser.headers(true),
            json{{"current_password", current}, {"new_password", next}}.dump(), "application/json");
    }

    SystemContext context;
    StorageOptions storage;
    std::atomic<long long> seconds{0};
    std::string origin;
    std::unique_ptr<SessionRuntime> runtime;
    std::unique_ptr<httplib::Client> client;
private:
    httplib::Server server;
    std::thread worker;
};

void test_authentication(Fixture& fixture) {
    auto& client = *fixture.client;
    expect(client.Get("/api/auth/me"), 401, "AuthenticationRequired");
    const std::string body = R"({"account_number":"S2023001","password":"Initial1!"})";
    expect(client.Post("/api/auth/login", body, "application/json"), 403, "CsrfRejected");
    for (const auto& source : {std::string("null"), fixture.origin + ".evil", fixture.origin + "/evil"}) {
        expect(client.Post("/api/auth/login", {{"Origin", source}}, body, "application/json"), 403, "CsrfRejected");
    }
    expect(client.Post("/api/auth/login", {{"Origin", fixture.origin}},
                       R"({"account_number":"S2023001","password":"wrong"})", "application/json"),
           401, "AuthenticationRequired");
    expect(client.Post("/api/auth/login", {{"Referer", fixture.origin + "/login.html"}}, body, "application/json"),
           200, "Success");
    expect(client.Post("/api/auth/login", {{"Origin", fixture.origin}},
                       R"({"account_number":1,"password":"Initial1!"})", "application/json"), 400, "InvalidInput");
    expect(client.Post("/api/auth/login", {{"Origin", fixture.origin}},
                       R"({"account_number":"S2023001","password":"wrong","password":"Initial1!"})",
                       "application/json"), 400, "InvalidInput");
    expect(client.Post("/api/auth/login", {{"Origin", fixture.origin}}, std::string(65537, 'x'),
                       "application/json"), 400, "InvalidInput");
    const auto old_browser = fixture.login("S2023001");
    auto relogin_headers = old_browser.headers();
    relogin_headers.emplace("Origin", fixture.origin);
    const auto relogin = client.Post("/api/auth/login", relogin_headers, body, "application/json");
    expect(relogin, 200, "Success");
    require(relogin->get_header_value("Set-Cookie").find(old_browser.cookie) == std::string::npos,
            "login reused the old session identifier");
    expect(client.Get("/api/auth/me", old_browser.headers()), 401, "AuthenticationRequired");
    for (const auto* account : {"S2023001", "T2015007", "A2020001"}) {
        const auto browser = fixture.login(account);
        expect(client.Get("/api/auth/me", browser.headers()), 200, "Success");
        expect(client.Post("/api/auth/logout", browser.headers(), "", "application/json"), 403, "CsrfRejected");
        expect(client.Get("/api/auth/me", browser.headers()), 200, "Success");
        const auto result = client.Post("/api/auth/logout", browser.headers(true), "", "application/json");
        expect(result, 200, "Success");
        require(result->get_header_value("Set-Cookie").find("Max-Age=0") != std::string::npos,
                "logout did not clear the cookie");
        expect(client.Get("/api/auth/me", browser.headers()), 401, "AuthenticationRequired");
    }
    const auto first = fixture.login("S2023001");
    const auto second = fixture.login("T2015007");
    Browser forged = first;
    forged.csrf = second.csrf;
    expect(fixture.change(forged, "Initial1!", "Changed1!"), 403, "CsrfRejected");
    auto duplicate = first.headers();
    duplicate.erase("Cookie");
    duplicate.emplace("Cookie", "team4_session=; " + first.cookie);
    expect(client.Get("/api/auth/me", duplicate), 401, "AuthenticationRequired");
    expect(client.Get("/test/exception"), 500, "InternalError");
    const auto exception = client.Get("/test/exception");
    require(exception->body.find("private file path") == std::string::npos, "internal exception leaked");
    httplib::Response mapped;
    write_result(mapped, {ResultCode::StorageIoError, "C:/private/state.json"}, ResultSource::Session);
    require(mapped.status == 500 && mapped.body.find("C:/private") == std::string::npos,
            "storage failure became 401 or leaked a server path");
}

void test_parallel_writes(Fixture& fixture) {
    const UserIdentity admin{900, "A2020001", Role::Administrator};
    const UserIdentity first{1001, "S2023001", Role::Student};
    const UserIdentity second{1002, "S2023002", Role::Student};
    with_business_lock(admin, Role::Administrator, [&](LockedServices& services) {
        require(services.admin_service().set_teaching_class_capacity(admin, 10, 1).code == ResultCode::Success,
                "cannot prepare capacity for concurrent requests");
    });
    OperationResult outcomes[2]{{ResultCode::InvalidInput, "not run"}, {ResultCode::InvalidInput, "not run"}};
    OperationResult identities[2]{{ResultCode::InvalidInput, "not run"}, {ResultCode::InvalidInput, "not run"}};
    std::atomic<int> ready{0};
    const auto enroll = [&](int index, const UserIdentity& actor) {
        ++ready;
        while (ready.load() != 2) std::this_thread::yield();
        identities[index] = with_business_lock(actor, Role::Student, [&](LockedServices& services) {
            outcomes[index] = services.registration_service().enroll(actor, 10);
        });
    };
    std::thread one(enroll, 0, first);
    std::thread two(enroll, 1, second);
    one.join();
    two.join();
    require(identities[0].code == ResultCode::Success && identities[1].code == ResultCode::Success,
            "concurrent enrollment failed identity checks");
    const bool first_won = outcomes[0].code == ResultCode::Success;
    require((first_won && outcomes[1].code == ResultCode::TeachingClassFull) ||
            (!first_won && outcomes[0].code == ResultCode::TeachingClassFull &&
             outcomes[1].code == ResultCode::Success),
            "two concurrent requests used the same remaining seat");
    auto reloaded = make_seed_context();
    require(load_state(fixture.storage, reloaded).code == ResultCode::Success &&
            reloaded.enrollment_count(10) == 1, "concurrent enrollment did not persist exactly once");
    with_business_lock(admin, Role::Administrator, [&](LockedServices& services) {
        require(services.admin_service().set_teaching_class_capacity(admin, 10, 2).code == ResultCode::Success,
                "administrator update could not use the common lock after enrollment");
    });
}

void test_password_change(Fixture& fixture) {
    auto first = fixture.login("S2023001");
    auto other_session = fixture.login("S2023001");
    const auto teacher = fixture.login("T2015007");
    const auto before = read_file(fixture.storage.path);
    const auto backup = read_file(fixture.storage.path + ".bak");
    expect(fixture.change(first, "wrong", "Changed1!"), 400, "InvalidInput");
    for (const auto* value : {"Abc12!", "lowercase1", "UPPERCASE1", "NoDigits!", "Has space1", "Has\tTab1", u8"Password1中文"}) {
        expect(fixture.change(first, "Initial1!", value), 400, "InvalidInput");
    }
    require(read_file(fixture.storage.path) == before &&
            read_file(fixture.storage.path + ".bak") == backup,
            "rejected password change modified files");

    // 占用真实主文件，验证保存失败后，密码、磁盘和两个会话都保持可用。
    HANDLE locked = CreateFileW(fs::path(fixture.storage.path).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(locked != INVALID_HANDLE_VALUE, "cannot occupy the test state file");
    const auto failed = fixture.change(first, "Initial1!", "Changed1!");
    CloseHandle(locked);
    expect(failed, 500, "StorageIoError");
    require(read_file(fixture.storage.path) == before, "failed save replaced the main file");
    expect(fixture.client->Get("/api/auth/me", first.headers()), 200, "Success");
    expect(fixture.client->Get("/api/auth/me", other_session.headers()), 200, "Success");

    const auto changed = fixture.change(first, "Initial1!", "Changed1!");
    expect(changed, 200, "Success");
    require(changed->get_header_value("Set-Cookie").find("Max-Age=0") != std::string::npos,
            "successful password change did not clear the cookie");
    expect(fixture.client->Get("/api/auth/me", first.headers()), 401, "AuthenticationRequired");
    expect(fixture.client->Get("/api/auth/me", other_session.headers()), 401, "AuthenticationRequired");
    expect(fixture.client->Get("/api/auth/me", teacher.headers()), 200, "Success");
    fixture.login("S2023001", "Changed1!");
    auto reloaded = make_seed_context();
    require(load_state(fixture.storage, reloaded).code == ResultCode::Success, "cannot reload password state");
    LoginService login(reloaded, make_stored_password_verifier(reloaded));
    require(login.login("S2023001", "Changed1!").identity.has_value() &&
            !login.login("S2023001", "Initial1!").identity, "restart did not preserve the changed password");

    // 教师和管理员使用同一个接口，不能只完成学生改密。
    for (const auto* account : {"T2015007", "A2020001"}) {
        const auto browser = fixture.login(account);
        expect(fixture.change(browser, "Initial1!", "Changed1!"), 200, "Success");
        fixture.login(account, "Changed1!");
    }
}

void test_expiry_and_business_lock(Fixture& fixture) {
    auto student = fixture.login("S2023001", "Changed1!");
    fixture.seconds = 1799;
    expect(fixture.client->Get("/api/auth/me", student.headers()), 200, "Success");
    fixture.seconds = 3598;
    expect(fixture.client->Get("/api/auth/me", student.headers()), 200, "Success");
    fixture.seconds = 5398;
    expect(fixture.client->Get("/api/auth/me", student.headers()), 401, "AuthenticationRequired");

    fixture.seconds = 6000;
    student = fixture.login("S2023001", "Changed1!");
    fixture.seconds = 7799;
    require(fixture.client->Get("/test/static")->status == 200, "static request failed");
    fixture.seconds = 7800;
    expect(fixture.client->Get("/api/auth/me", student.headers()), 401, "AuthenticationRequired");

    fixture.seconds = 10000;
    student = fixture.login("S2023001", "Changed1!");
    for (long long elapsed = 1700; elapsed < 28800; elapsed += 1700) {
        fixture.seconds = 10000 + elapsed;
        expect(fixture.client->Get("/api/auth/me", student.headers()), 200, "Success");
    }
    fixture.seconds = 38800;
    expect(fixture.client->Get("/api/auth/me", student.headers()), 401, "AuthenticationRequired");

    const UserIdentity admin{900, "A2020001", Role::Administrator};
    bool called = false;
    auto result = with_business_lock(admin, Role::Teacher, [&](LockedServices&) { called = true; });
    require(result.code == ResultCode::PermissionDenied && !called, "wrong role reached the callback");
    const auto snapshot_result = with_business_lock(admin, Role::Administrator, [&](LockedServices& services) {
        require(services.context_snapshot().students().size() == services.context().students().size(),
                "locked services did not share the current context");
    });
    require(snapshot_result.code == ResultCode::Success, "valid administrator was rejected");
    bool exception_received = false;
    try {
        with_business_lock(admin, Role::Administrator, [](LockedServices&) { throw std::runtime_error("test"); });
    } catch (const std::runtime_error&) {
        exception_received = true;
    }
    require(exception_received, "callback exception was swallowed");
    result = with_business_lock(admin, Role::Administrator, [](LockedServices&) {});
    require(result.code == ResultCode::Success, "exception did not release the lock");

    student = fixture.login("S2023001", "Changed1!");
    with_business_lock(admin, Role::Administrator, [](LockedServices& services) {
        auto record = services.context().students()[0];
        record.account_active = false;
        services.context().apply_student_change(record);
    });
    expect(fixture.client->Get("/api/auth/me", student.headers()), 401, "AuthenticationRequired");
    result = with_business_lock({1001, "S2023001", Role::Student}, Role::Student,
                                [&](LockedServices&) { called = true; });
    require(result.code == ResultCode::AuthenticationRequired && !called, "disabled account reached business code");
    httplib::Request read;
    read.method = "GET";
    require(require_csrf(read).code == ResultCode::Success, "GET unexpectedly required CSRF");

    const auto teacher_browser = fixture.login("T2015007", "Changed1!");
    with_business_lock(admin, Role::Administrator, [&](LockedServices& services) {
        auto teacher = services.context().teachers()[0];
        teacher.teacher_number = "T2015008";
        require(services.admin_service().update_teacher(admin, teacher).code == ResultCode::Success,
                "cannot update teacher account number");
    });
    expect(fixture.client->Get("/api/auth/me", teacher_browser.headers()), 401, "AuthenticationRequired");
    fixture.login("T2015008", "Changed1!");
}
}  // namespace

int main() {
    const auto root = fs::temp_directory_path() /
        ("team4_session_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::string cookie_before_restart;
        {
            Fixture fixture(root);
            test_authentication(fixture);
            test_password_change(fixture);
            test_parallel_writes(fixture);
            test_expiry_and_business_lock(fixture);
            cookie_before_restart = fixture.login("A2020001", "Changed1!").cookie;
        }
        {
            auto loaded = make_seed_context();
            const StorageOptions storage{(root / "state.json").string(), false};
            require(load_state(storage, loaded).code == ResultCode::Success, "cannot reload after server stop");
            SessionRuntime restarted(loaded, storage);
            bind_session_runtime(restarted);
            httplib::Request request;
            request.headers.emplace("Cookie", cookie_before_restart);
            require(!require_session(request).identity, "old session survived a server restart");
        }
        bool unbound = false;
        try { active_session_runtime(); }
        catch (const std::logic_error&) { unbound = true; }
        require(unbound, "destroyed runtime stayed bound");
        fs::remove_all(root);
        std::cout << "Session HTTP: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Session HTTP FAILED: " << error.what() << "\nTest data kept at " << root << '\n';
        return 1;
    }
}
