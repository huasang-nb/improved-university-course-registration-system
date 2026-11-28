// ============================================================================
// 教师 / 管理员接口 + 公共会话骨架的端到端测试（Zhu Ziliang）
//
// 目的：用真实的本机 HTTP 连接验证复核意见里的场景，而不是只调服务函数：
//   - 无 Cookie / 停用账号的旧会话 -> 401；
//   - 教师读别人班、教师调管理员接口 -> 403；
//   - 写操作缺少或使用别人的 CSRF Token -> 403；
//   - 合法管理员操作成功保存，重新加载后一致；
//   - 业务拒绝（409）不改变数据；
//   - 实际保存失败 -> 500 且保留 Storage* 码，页面看不到状态文件路径，
//     内存数据不被失败操作替换，解除占用后可以重试；
//   - 重复 JSON 字段 -> 400，且不执行业务；
//   - 改密：当前密码错误 400、新密码不合规则 400、成功 200 并清 Cookie、
//     旧会话被撤销、新密码可以登录。
//
// 只使用临时目录里的独立数据文件和虚构账号，不接触真实凭据。
// ============================================================================

#include "session.hpp"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "http_teacher_admin_session_routes.hpp"
#include "password_service.hpp"

using namespace course_registration;
using namespace course_registration::http;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* expression, int line, const std::string& detail = {}) {
    ++g_checks;
    if (ok) {
        return;
    }
    ++g_failures;
    std::cerr << "FAIL line " << line << ": " << expression;
    if (!detail.empty()) {
        std::cerr << " -> " << detail;
    }
    std::cerr << "\n";
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_CODE(actual, expected) check((actual) == (expected), #actual " == " #expected, __LINE__)

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read test file " + path);
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// 一个浏览器会话：Cookie 与 CSRF Token 一起保存，避免测试里混用。
struct Browser {
    std::string cookie;
    std::string csrf;

    httplib::Headers headers(bool write = false) const {
        httplib::Headers result{{"Cookie", cookie}};
        if (write) {
            result.emplace("X-CSRF-Token", csrf);
        }
        return result;
    }
};

struct Fixture {
    explicit Fixture(const fs::path& directory)
        : directory_(directory), context_(make_seed_context()),
          storage_{(directory / "state.json").string(), true} {
        fs::remove_all(directory_);
        fs::create_directories(directory_);
        for (const char* account : {"S2023001", "S2023002", "T2015007", "A2020001"}) {
            const OperationResult prepared =
                set_account_password(context_, storage_, account, "Initial1!");
            if (prepared.code != ResultCode::Success) {
                throw std::runtime_error("cannot prepare password: " + prepared.message);
            }
        }

        const int port = server_.bind_to_any_port("127.0.0.1");
        if (port <= 0) {
            throw std::runtime_error("cannot bind a test port");
        }
        origin_ = "http://127.0.0.1:" + std::to_string(port);

        SessionOptions options;
        options.allowed_origin = origin_;
        runtime_ = std::make_unique<SessionRuntime>(context_, storage_, options);
        bind_session_runtime(*runtime_);
        runtime_->register_auth_routes(server_);
        register_teacher_admin_routes_with_session(server_);

        worker_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
        client_ = std::make_unique<httplib::Client>("127.0.0.1", port);
        client_->set_read_timeout(10, 0);
    }

    ~Fixture() {
        server_.stop();
        if (worker_.joinable()) {
            worker_.join();
        }
        client_.reset();
        runtime_.reset();
        fs::remove_all(directory_);
    }

    httplib::Result call(const char* method, const std::string& path,
                         const httplib::Headers& headers, const std::string& body) {
        if (std::string(method) == "GET") {
            return client_->Get(path, headers);
        }
        if (std::string(method) == "POST") {
            return client_->Post(path, headers, body, "application/json");
        }
        if (std::string(method) == "PUT") {
            return client_->Put(path, headers, body, "application/json");
        }
        if (std::string(method) == "PATCH") {
            return client_->Patch(path, headers, body, "application/json");
        }
        return client_->Delete(path, headers, body, "application/json");
    }

    Browser login(const char* account, const std::string& password = "Initial1!") {
        httplib::Headers headers;
        headers.emplace("Origin", origin_);
        const auto result = client_->Post(
            "/api/auth/login", headers,
            json{{"account_number", account}, {"password", password}}.dump(),
            "application/json");
        if (!result || result->status != 200) {
            throw std::runtime_error("login failed: " +
                                     (result ? result->body : "no response"));
        }
        const auto set_cookie = result->get_header_value("Set-Cookie");
        Browser browser;
        browser.cookie = set_cookie.substr(0, set_cookie.find(';'));
        browser.csrf = json::parse(result->body).at("data").at("csrf_token");
        return browser;
    }

    fs::path state_path() const { return directory_ / "state.json"; }

    fs::path directory_;
    SystemContext context_;
    StorageOptions storage_;
    std::string origin_;
    std::unique_ptr<SessionRuntime> runtime_;
    std::unique_ptr<httplib::Client> client_;

private:
    httplib::Server server_;
    std::thread worker_;
};

void expect_status(const httplib::Result& result, int status, const char* code, int line) {
    check(bool(result), "response received", line,
          result ? "" : httplib::to_string(result.error()));
    if (!result) {
        return;
    }
    check(result->status == status, "status matches", line,
          "expected " + std::to_string(status) + ", got " +
              std::to_string(result->status) + ": " + result->body);
    if (result->body.empty()) {
        return;
    }
    json body;
    try {
        body = json::parse(result->body);
    } catch (const json::exception&) {
        check(false, "response is JSON", line, result->body.substr(0, 200));
        return;
    }
    check(body.value("code", std::string()) == code, "code matches", line,
          body.value("code", std::string()) + " != " + code);
    if (status != 200) {
        check(body.contains("data") && body["data"].is_null(), "failure data is null", line);
    }
}

void test_result_source_mapping() {
    CHECK_CODE(result_source_for(ResultCode::AuthenticationRequired), ResultSource::Session);
    CHECK_CODE(result_source_for(ResultCode::PermissionDenied), ResultSource::Permission);
    CHECK_CODE(result_source_for(ResultCode::StorageIoError), ResultSource::Business);
    CHECK_CODE(result_source_for(ResultCode::StorageParseError), ResultSource::Business);
    CHECK_CODE(result_source_for(ResultCode::StorageSchemaMismatch), ResultSource::Business);
    CHECK_CODE(result_source_for(ResultCode::StorageIntegrityError), ResultSource::Business);
    CHECK_CODE(result_source_for(ResultCode::StorageBackupMissing), ResultSource::Business);
    CHECK_CODE(result_source_for(ResultCode::InvalidInput), ResultSource::Internal);
    CHECK_CODE(result_source_for(ResultCode::CatalogInUse), ResultSource::Internal);

    // 内部异常必须是 500 InternalError，不能被显示成“请重新登录”。
    httplib::Response response;
    write_result(response, {ResultCode::InvalidInput, "boom"}, ResultSource::Internal);
    CHECK(response.status == 500);
    const json body = json::parse(response.body);
    CHECK(body.at("code") == "InternalError");
    CHECK(response.body.find("login") == std::string::npos);
}

void test_unauthenticated(Fixture& fixture) {
    expect_status(fixture.client_->Get("/api/teacher/classes"), 401, "AuthenticationRequired",
                  __LINE__);
    expect_status(fixture.client_->Get("/api/admin/courses"), 401, "AuthenticationRequired",
                  __LINE__);
    expect_status(fixture.client_->Post("/api/admin/courses", std::string("{}"),
                                        "application/json"),
                  401, "AuthenticationRequired", __LINE__);
}

void test_teacher_scope(Fixture& fixture) {
    const Browser teacher = fixture.login("T2015007");

    expect_status(fixture.client_->Get("/api/teacher/classes", teacher.headers()), 200,
                  "Success", __LINE__);
    expect_status(fixture.client_->Get("/api/teacher/classes/10/roster", teacher.headers()), 200,
                  "Success", __LINE__);
    // 班 20 属于另一位教师：不能读汇总，也不能读名单。
    expect_status(fixture.client_->Get("/api/teacher/classes/20/summary", teacher.headers()), 403,
                  "PermissionDenied", __LINE__);
    expect_status(fixture.client_->Get("/api/teacher/classes/20/roster", teacher.headers()), 403,
                  "PermissionDenied", __LINE__);
    // 教师不能借用管理员接口。
    expect_status(fixture.client_->Post("/api/admin/courses", teacher.headers(true),
                                        std::string(R"({"course_id":9})"), "application/json"),
                  403, "PermissionDenied", __LINE__);
    // 教师也不能绕过会话直接调管理员写操作。
    expect_status(fixture.client_->Patch("/api/admin/classes/10/capacity", teacher.headers(false),
                                         std::string(R"({"capacity":5})"), "application/json"),
                  403, "CsrfRejected", __LINE__);
}

void test_admin_writes_and_csrf(Fixture& fixture) {
    const Browser admin = fixture.login("A2020001");

    expect_status(fixture.client_->Get("/api/admin/courses", admin.headers()), 200, "Success",
                  __LINE__);
    // 缺 CSRF Token：403，且不能落盘。
    expect_status(fixture.client_->Post("/api/admin/courses", admin.headers(false),
                                        std::string(R"({"course_id":9})"), "application/json"),
                  403, "CsrfRejected", __LINE__);
    // 用别人的 Token 同样拒绝。
    httplib::Headers wrong = admin.headers(true);
    wrong.find("X-CSRF-Token")->second = "not-my-token";
    expect_status(fixture.client_->Post("/api/admin/courses", wrong, std::string(R"({"course_id":9})"),
                             "application/json"), 403,
                  "CsrfRejected", __LINE__);

    // 建课程请求必须同时带课程与首班，用显式 array 构造，避免初始化列表的括号层级出错。
    json meeting{{"day_of_week", 3}, {"start_minute", 600}, {"end_minute", 660},
                 {"room", "<b>TestRoom</b>"}};
    json course_payload{{"course_id", 8}, {"course_code", "QA800"},
                          {"course_name", "Quality Assurance"}, {"credits", 3}};
    json first_class{{"teaching_class_id", 80}, {"course_id", 8}, {"teacher_id", 200},
                     {"capacity", 4}};
    first_class["meetings"] = json::array();
    first_class["meetings"].push_back(meeting);
    const std::string body = json{{"course", course_payload}, {"first_teaching_class", first_class}}.dump();
    expect_status(fixture.client_->Post("/api/admin/courses", admin.headers(true), body, "application/json"), 200,
                  "Success", __LINE__);

    // 保存后重新读文件，确认真的落盘（不只改了内存）。
    const json stored = json::parse(read_file(fixture.state_path().string()));
    CHECK(stored.at("schema_version").get<int>() == 2);
    bool found_course = false;
    for (const auto& course : stored.at("courses")) {
        if (course.at("course_id").get<int>() == 8) {
            found_course = true;
            CHECK(course.at("course_code").get<std::string>() == "QA800");
        }
    }
    CHECK(found_course);

    // 重新加载后仍能看到该课程。
    SystemContext reloaded = make_seed_context();
    const OperationResult loaded = load_state(fixture.storage_, reloaded);
    CHECK_CODE(loaded.code, ResultCode::Success);
    Course course{};
    CHECK(reloaded.find_course(8, course));
}

void test_duplicate_json_field(Fixture& fixture) {
    const Browser admin = fixture.login("A2020001");
    // 解析阶段必须发现重复字段：不能让最后一个值静默生效。
    const std::string duplicated =
        R"({"capacity":0,"capacity":2})";
    expect_status(fixture.client_->Patch("/api/admin/classes/10/capacity", admin.headers(true), duplicated,
                                         "application/json"),
                  400, "InvalidInput", __LINE__);
    // 数据未被修改。
    const auto listing = fixture.client_->Get("/api/admin/classes", admin.headers());
    expect_status(listing, 200, "Success", __LINE__);
    const json data = json::parse(listing->body).at("data");
    int capacity = -1;
    for (const auto& item : data) {
        if (item.at("teaching_class_id").get<int>() == 10) {
            capacity = item.at("capacity").get<int>();
        }
    }
    CHECK(capacity == 2);
}

void test_business_rejection_keeps_data(Fixture& fixture) {
    const Browser admin = fixture.login("A2020001");
    // 班 30 是课程 3 唯一的教学班，不能删除。
    expect_status(fixture.client_->Delete("/api/admin/classes/30", admin.headers(true), std::string(),
                                  "application/json"), 409,
                  "CatalogInUse", __LINE__);
    const json stored = json::parse(read_file(fixture.state_path().string()));
    bool class_present = false;
    for (const auto& item : stored.at("teaching_classes")) {
        if (item.at("teaching_class_id").get<int>() == 30) {
            class_present = true;
        }
    }
    CHECK(class_present);
}

void test_save_failure_is_sanitized(Fixture& fixture) {
    const Browser admin = fixture.login("A2020001");

    // 用独占句柄占住状态文件，让保存真的失败（而不是靠注入假错误）。
    const std::wstring wide = fs::path(fixture.state_path()).wstring();
    HANDLE lock = CreateFileW(wide.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) {
        check(false, "lock state file", __LINE__, "cannot open state file exclusively");
        return;
    }

    const auto failed = fixture.client_->Patch("/api/admin/classes/10/capacity", admin.headers(true),
                                               std::string(R"({"capacity":5})"), "application/json");
    expect_status(failed, 500, "StorageIoError", __LINE__);
    if (failed) {
        // 页面不能看到服务器状态文件路径。
        CHECK(failed->body.find("state.json") == std::string::npos);
        CHECK(failed->body.find(":\\\\") == std::string::npos);
        CHECK(failed->body.find("C:") == std::string::npos);
    }

    // 内存数据没有被失败操作替换：容量仍是 2。
    const auto listing = fixture.client_->Get("/api/admin/classes", admin.headers());
    expect_status(listing, 200, "Success", __LINE__);
    int capacity = -1;
    const json listed = json::parse(listing->body);
    for (const auto& item : listed.at("data")) {
        if (item.at("teaching_class_id").get<int>() == 10) {
            capacity = item.at("capacity").get<int>();
        }
    }
    CHECK(capacity == 2);

    CloseHandle(lock);

    // 解除占用后可以重试成功。
    expect_status(fixture.client_->Patch("/api/admin/classes/10/capacity", admin.headers(true),
                                         std::string(R"({"capacity":5})"), "application/json"),
                  200, "Success", __LINE__);
    const json stored = json::parse(read_file(fixture.state_path().string()));
    for (const auto& item : stored.at("teaching_classes")) {
        if (item.at("teaching_class_id").get<int>() == 10) {
            CHECK(item.at("capacity").get<int>() == 5);
        }
    }
}

void test_deactivated_session(Fixture& fixture) {
    const Browser teacher = fixture.login("T2015007");
    const Browser admin = fixture.login("A2020001");
    // 先确认会话有效。
    expect_status(fixture.client_->Get("/api/teacher/classes", teacher.headers()), 200, "Success",
                  __LINE__);

    const std::string body =
        json{{"teacher_id", 100},
             {"teacher_number", "T2015007"},
             {"name", "Wang"},
             {"department", "Software"},
             {"account_active", false}}
            .dump();
    expect_status(fixture.client_->Put("/api/admin/teachers/100", admin.headers(true), body, "application/json"), 200,
                  "Success", __LINE__);

    // 停用后旧会话立即被拒绝，不需要等空闲超时。
    expect_status(fixture.client_->Get("/api/teacher/classes", teacher.headers()), 401,
                  "AuthenticationRequired", __LINE__);
}

void test_password_change(Fixture& fixture) {
    Browser teacher = fixture.login("T2015007");

    expect_status(fixture.client_->Put(
                      "/api/auth/password", teacher.headers(true),
                      json{{"current_password", "wrong-pass"},
                           {"new_password", "Another1!"}}.dump(), "application/json"),
                  400, "InvalidInput", __LINE__);
    expect_status(fixture.client_->Put(
                      "/api/auth/password", teacher.headers(true),
                      json{{"current_password", "Initial1!"},
                           {"new_password", "short"}}.dump(), "application/json"),
                  400, "InvalidInput", __LINE__);
    expect_status(fixture.client_->Put(
                      "/api/auth/password", teacher.headers(true),
                      json{{"current_password", "Initial1!"},
                           {"new_password", "NoDigits!!"}}.dump(), "application/json"),
                  400, "InvalidInput", __LINE__);

    const auto changed = fixture.client_->Put(
        "/api/auth/password", teacher.headers(true),
        json{{"current_password", "Initial1!"}, {"new_password", "Changed2!"}}.dump(),
        "application/json");
    expect_status(changed, 200, "Success", __LINE__);
    // 成功响应同时清 Cookie（本次请求仍 200）。
    CHECK(changed->get_header_value("Set-Cookie").find("team4_session=") != std::string::npos);

    // 旧会话被撤销。
    expect_status(fixture.client_->Get("/api/teacher/classes", teacher.headers()), 401,
                  "AuthenticationRequired", __LINE__);
    // 新密码可以登录。
    const Browser renewed = fixture.login("T2015007", "Changed2!");
    expect_status(fixture.client_->Get("/api/teacher/classes", renewed.headers()), 200, "Success",
                  __LINE__);
}

// 公共层已定死 GET /api/auth/me 的字段（2026-11-28）：页面只依赖下面的形状，
// 所以这里把它锁进测试——字段名、层级或大小写一变，这里先失败，而不是等到
// 三个页面在浏览器里显示成 undefined。
void test_me_shape(Fixture& fixture) {
    struct Expected {
        const char* account;
        const char* role;
        int user_id;
    };
    const Expected accounts[] = {
        {"A2020001", "Administrator", 900},
        {"T2015007", "Teacher", 100},
        {"S2023001", "Student", 1001},
    };

    for (const Expected& expected : accounts) {
        const Browser browser = fixture.login(expected.account);
        const auto result = fixture.client_->Get("/api/auth/me", browser.headers());
        expect_status(result, 200, "Success", __LINE__);
        const json body = json::parse(result->body);

        // user 在 data 内，不是响应顶层；失败时才有顶层别的形状。
        CHECK(!body.contains("user"));
        const json& data = body.at("data");
        const json& user = data.at("user");
        CHECK(user.at("role").get<std::string>() == expected.role);
        CHECK(user.at("user_id").get<int>() == expected.user_id);
        CHECK(user.at("account_number").get<std::string>() == expected.account);
        // 页面不再读 name，接口也就不必提供；有的话也不该被依赖。
        CHECK(!user.contains("name"));
        CHECK(data.at("csrf_token").is_string());
        CHECK(!data.at("csrf_token").get<std::string>().empty());

        // 页面用 data.csrf_token 填 X-CSRF-Token；这里确认这个 Token 真的能过
        // CSRF 校验：带 Token 的管理员写操作成功，不带时报 CsrfRejected。
        if (std::string(expected.role) == "Administrator") {
            const std::string payload =
                json{{"student_id", 1500}, {"student_number", "S2099001"},
                     {"name", "Shape Probe"}, {"major", "Probe"},
                     {"account_active", true}}
                    .dump();
            expect_status(fixture.client_->Post("/api/admin/students", browser.headers(),
                                                payload, "application/json"),
                          403, "CsrfRejected", __LINE__);
            expect_status(fixture.client_->Post("/api/admin/students", browser.headers(true),
                                                payload, "application/json"),
                          200, "Success", __LINE__);
            expect_status(fixture.client_->Delete("/api/admin/students/1500",
                                                  browser.headers(true), "", "application/json"),
                          200, "Success", __LINE__);
        }
    }
}

}  // namespace

int main() {
    try {
        const fs::path base = fs::path("http-test-runtime") / "team4_http_session_integration";
        test_result_source_mapping();

        {
            // 登录接口返回的字段形状先单独锁一次，后面的用例再依赖它。
            Fixture fixture(base / "me");
            test_me_shape(fixture);
        }
        {
            Fixture fixture(base / "unauthenticated");
            test_unauthenticated(fixture);
        }
        {
            // 每个场景用独立数据文件，避免互相影响。
            Fixture fixture(base / "teacher");
            test_teacher_scope(fixture);
        }
        {
            Fixture fixture(base / "admin");
            test_admin_writes_and_csrf(fixture);
        }
        {
            Fixture fixture(base / "duplicate");
            test_duplicate_json_field(fixture);
        }
        {
            Fixture fixture(base / "reject");
            test_business_rejection_keeps_data(fixture);
        }
        {
            Fixture fixture(base / "savefail");
            test_save_failure_is_sanitized(fixture);
        }
        {
            Fixture fixture(base / "deactivated");
            test_deactivated_session(fixture);
        }
        {
            Fixture fixture(base / "password");
            test_password_change(fixture);
        }
    } catch (const std::exception& error) {
        std::cerr << "EXCEPTION: " << error.what() << "\n";
        ++g_failures;
    }
    std::cout << "PASS: " << g_checks << " checks, " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
