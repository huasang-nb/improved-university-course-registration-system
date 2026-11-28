// ============================================================================
// 教师 / 管理员 HTTP 接口的自测（Zhu Ziliang，第 5 周）
//
// 直接调用 http::dispatch()，不启动真实服务器：请求/响应与框架无关，
// 所以这里能精确验证"未登录 401、越权 403、CSRF 缺失 403、非法入参 400、
// 业务冲突 409、成功 200 且落盘"这些分支。
//
// 编译需要 C++17 与 <filesystem>（持久化实现），因此用 MSVC 2022：
//   cmake --build build/cmake-http --config Debug --target http_teacher_admin_tests
// ============================================================================

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "http_teacher_admin_handlers.hpp"
#include "persistence_service.hpp"
#include "persistent_administration_service.hpp"
#include "teacher_and_administration_services.hpp"

namespace course_registration {
namespace http {
namespace {

using json = nlohmann::json;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* expression, int line, const std::string& detail = {}) {
    ++g_checks;
    if (ok) {
        return;
    }
    ++g_failures;
    std::cout << "FAIL line " << line << ": " << expression;
    if (!detail.empty()) {
        std::cout << " -> " << detail;
    }
    std::cout << "\n";
}

void check_contains(const std::string& text, const std::string& needle, const char* expression,
                    int line) {
    check(text.find(needle) != std::string::npos, expression, line, "missing \"" + needle + "\"");
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_TEXT(text, needle) check_contains((text), (needle), #text " contains " #needle, __LINE__)

std::atomic<int> g_fixture_counter{0};

struct Reply {
    int status = 0;
    std::string raw;
    json body;
};

struct Fixture {
    std::string directory;
    StorageOptions options;
    SystemContext context;
    PersistentAdministratorService admin;
    TeacherService teacher;
    std::mutex business_mutex;
    Services services;

    Fixture()
        : directory(make_directory()),
          options{directory + "/state.json", true},
          context(make_seed_context()),
          admin(context, options),
          teacher(context),
          services{context, admin, teacher, business_mutex} {
        const OperationResult prepared = initialize_storage(options, context);
        if (prepared.code != ResultCode::Success) {
            std::cout << "fixture initialization failed: " << prepared.message << "\n";
        }
    }

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    static std::string make_directory() {
        // 放在当前工作目录下，避免系统临时目录的权限/沙箱差异。
        std::error_code ec;
        const std::filesystem::path dir =
            std::filesystem::path("http-test-runtime") /
            ("team4_http_tests_" + std::to_string(++g_fixture_counter));
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        return dir.string();
    }

    Reply call(const std::string& method, const std::string& path, const std::string& body = {},
               std::optional<UserIdentity> identity = std::nullopt, bool csrf_ok = false) {
        Request request;
        request.method = method;
        request.path = path;
        request.body = body;
        request.identity = identity;
        request.csrf_ok = csrf_ok;
        const Response response = dispatch(request, services);
        Reply reply;
        reply.status = response.status;
        reply.raw = response.body;
        reply.body = json::parse(response.body);
        return reply;
    }

    Reply get(const std::string& path, std::optional<UserIdentity> identity = std::nullopt) {
        return call("GET", path, {}, identity);
    }

    Reply write(const std::string& method, const std::string& path, const json& body,
                std::optional<UserIdentity> identity = std::nullopt, bool csrf_ok = true) {
        return call(method, path, body.dump(), identity, csrf_ok);
    }
};

const UserIdentity kAdmin{900, "A2020001", Role::Administrator};
const UserIdentity kWang{100, "T2015007", Role::Teacher};
const UserIdentity kLi{200, "T2016011", Role::Teacher};
const UserIdentity kZhao{300, "T2018003", Role::Teacher};
const UserIdentity kAlice{1001, "S2023001", Role::Student};

void expect_reply(const Reply& reply, int status, const std::string& code, const char* expression,
                  int line) {
    check(reply.status == status, expression, line,
          "status " + std::to_string(reply.status) + " != " + std::to_string(status));
    const std::string actual_code = reply.body.value("code", std::string{});
    check(actual_code == code, expression, line, "code " + actual_code + " != " + code);
    if (status >= 400) {
        check(reply.body.at("data").is_null(), expression, line, "failure data must be null");
        check(!reply.body.value("message", std::string{}).empty(), expression, line,
              "failure message must not be empty");
    }
}

#define EXPECT_STATUS(reply, status, code) \
    expect_reply((reply), (status), (code), #reply " -> " #status " " #code, __LINE__)

// ---------------------------------------------------------------------------
// 1. 未登录与角色越权
// ---------------------------------------------------------------------------

void test_authentication_and_roles() {
    Fixture fixture;

    EXPECT_STATUS(fixture.get("/api/teacher/classes"), 401, "AuthenticationRequired");
    EXPECT_STATUS(fixture.get("/api/admin/classes"), 401, "AuthenticationRequired");
    EXPECT_STATUS(fixture.write("POST", "/api/admin/students",
                                json{{"student_id", 1009}, {"student_number", "S2023009"},
                                     {"name", "Eve"}, {"major", "Physics"},
                                     {"account_active", true}}),
                  401, "AuthenticationRequired");

    // 角色不符：教师不能进管理员接口，管理员不能冒充教师，学生两边都不行。
    EXPECT_STATUS(fixture.get("/api/admin/classes", kWang), 403, "PermissionDenied");
    EXPECT_STATUS(fixture.get("/api/teacher/classes", kAdmin), 403, "PermissionDenied");
    EXPECT_STATUS(fixture.get("/api/teacher/classes", kAlice), 403, "PermissionDenied");
    EXPECT_STATUS(fixture.get("/api/admin/students", kAlice), 403, "PermissionDenied");
    CHECK_TEXT(fixture.get("/api/admin/classes", kWang).body.at("message").get<std::string>(),
               "requires a different role");

    // 未匹配的路由：404，而不是 405 或崩溃。
    EXPECT_STATUS(fixture.get("/api/admin/does-not-exist", kAdmin), 404, "RecordNotFound");
    EXPECT_STATUS(fixture.write("POST", "/api/teacher/classes", json::object(), kWang), 404,
                  "RecordNotFound");
    // 路径编号必须是正整数：0 由正则放过，交给 parse_path_id 拒绝。
    EXPECT_STATUS(fixture.get("/api/teacher/classes/0/summary", kWang), 400, "InvalidInput");
    EXPECT_STATUS(fixture.get("/api/teacher/classes/99999999999/summary", kWang), 400,
                  "InvalidInput");
}

// ---------------------------------------------------------------------------
// 2. 停用账号的旧会话立即失效
// ---------------------------------------------------------------------------

void test_disabled_account_loses_session() {
    Fixture fixture;

    EXPECT_STATUS(fixture.get("/api/teacher/classes", kWang), 200, "Success");

    // 管理员停用王老师（PUT 要求填全字段）。
    const json disabled{{"teacher_id", 100}, {"teacher_number", "T2015007"},
                        {"name", "Dr. Wang"}, {"department", "Software Engineering"},
                        {"account_active", false}};
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/teachers/100", disabled, kAdmin), 200, "Success");

    const Reply rejected = fixture.get("/api/teacher/classes", kWang);
    EXPECT_STATUS(rejected, 401, "AuthenticationRequired");
    CHECK_TEXT(rejected.body.at("message").get<std::string>(), "no longer valid");
}

void test_csrf() {
    Fixture fixture;

    // 写操作缺少 CSRF：403；读操作不受影响。
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/credit-limit", json{{"maximum_credits", 20}},
                                kAdmin, false),
                  403, "CsrfRejected");
    EXPECT_STATUS(fixture.get("/api/admin/registration-rule", kAdmin), 200, "Success");
    // 未登录时先报 401，不泄露 CSRF 规则。
    EXPECT_STATUS(fixture.call("POST", "/api/admin/registration-close", "", std::nullopt, false),
                  401, "AuthenticationRequired");
}

// ---------------------------------------------------------------------------
// 3. 教师只读：只能看本人教学班
// ---------------------------------------------------------------------------

void test_teacher_reads() {
    Fixture fixture;

    const Reply classes = fixture.get("/api/teacher/classes", kWang);
    EXPECT_STATUS(classes, 200, "Success");
    CHECK(classes.body.at("data").is_array());
    CHECK(classes.body.at("data").size() == 1);  // 种子：10 归王老师，11 归赵老师
    for (const json& item : classes.body.at("data")) {
        CHECK(item.at("teacher_id").get<int>() == 100);
    }

    const Reply summary = fixture.get("/api/teacher/classes/10/summary", kWang);
    EXPECT_STATUS(summary, 200, "Success");
    CHECK(summary.body.at("data").at("teaching_class_id").get<int>() == 10);
    CHECK(summary.body.at("data").at("course_code").get<std::string>() == "SE101");
    CHECK(summary.body.at("data").at("remaining_seats").get<int>() == 2);

    const Reply roster = fixture.get("/api/teacher/classes/10/roster", kWang);
    EXPECT_STATUS(roster, 200, "Success");
    CHECK(roster.body.at("data").at("students").is_array());
    CHECK(roster.body.at("data").at("students").empty());

    // 赵老师的班：王老师看不到名单，也不能看汇总。
    EXPECT_STATUS(fixture.get("/api/teacher/classes/11/roster", kWang), 403, "PermissionDenied");
    EXPECT_STATUS(fixture.get("/api/teacher/classes/11/summary", kWang), 403, "PermissionDenied");
    CHECK(!fixture.get("/api/teacher/classes/11/roster", kWang).raw.empty());
    // 越权响应里不能带出名单内容。
    CHECK_TEXT(fixture.get("/api/teacher/classes/11/roster", kWang).raw, "Permission denied");
    // 不存在的班：404。
    EXPECT_STATUS(fixture.get("/api/teacher/classes/999/summary", kWang), 404,
                  "TeachingClassNotFound");
    EXPECT_STATUS(fixture.get("/api/teacher/classes/999/roster", kWang), 404,
                  "TeachingClassNotFound");
}

// ---------------------------------------------------------------------------
// 4. 管理员读接口的字段与形状
// ---------------------------------------------------------------------------

void test_admin_reads() {
    Fixture fixture;

    const Reply courses = fixture.get("/api/admin/courses", kAdmin);
    EXPECT_STATUS(courses, 200, "Success");
    CHECK(courses.body.at("data").is_array());
    CHECK(courses.body.at("data").size() == 3);
    CHECK(courses.body.at("data").at(0).at("course").at("course_code").get<std::string>() == "SE101");
    CHECK(courses.body.at("data").at(0).at("teaching_classes").is_array());
    CHECK(courses.body.at("data").at(0).at("teaching_classes").size() == 2);  // 种子：课程 1 有班 10 与 11

    const Reply classes = fixture.get("/api/admin/classes", kAdmin);
    EXPECT_STATUS(classes, 200, "Success");
    CHECK(classes.body.at("data").size() == 4);

    const Reply students = fixture.get("/api/admin/students", kAdmin);
    EXPECT_STATUS(students, 200, "Success");
    CHECK(students.body.at("data").size() == 2);
    CHECK(students.body.at("data").at(0).contains("student_number"));
    CHECK(!students.body.at("data").at(0).contains("password_hash"));

    const Reply teachers = fixture.get("/api/admin/teachers", kAdmin);
    EXPECT_STATUS(teachers, 200, "Success");
    CHECK(teachers.body.at("data").size() == 3);
    CHECK(teachers.raw.find("password_hash") == std::string::npos);

    const Reply rule = fixture.get("/api/admin/registration-rule", kAdmin);
    EXPECT_STATUS(rule, 200, "Success");
    CHECK(rule.body.at("data").at("registration_open").get<bool>());
    CHECK(rule.body.at("data").at("maximum_credits").get<int>() == 18);
    CHECK(rule.body.at("data").at("round_opened").is_boolean());
    CHECK(rule.body.at("data").at("open_now").is_boolean());

    // 所有成功响应的 data 都不是 null。
    CHECK(!courses.body.at("data").is_null());
    CHECK(!rule.body.at("data").is_null());
}

// ---------------------------------------------------------------------------
// 5. 入参校验：未知字段、超长正文、JSON 类型、编号一致性
// ---------------------------------------------------------------------------

void test_request_validation() {
    Fixture fixture;

    // 未知字段（包括试图顺手写 password_hash）。
    const json sneaky{{"student_id", 1009}, {"student_number", "S2023009"},
                      {"name", "Eve"}, {"major", "Physics"}, {"account_active", true},
                      {"password_hash", "x"}};
    EXPECT_STATUS(fixture.write("POST", "/api/admin/students", sneaky, kAdmin), 400, "InvalidInput");
    CHECK_TEXT(fixture.write("POST", "/api/admin/students", sneaky, kAdmin).body.at("message")
                   .get<std::string>(),
               "unknown field");

    // 正文超过 64 KiB：先判大小，不解析。
    const std::string huge(64u * 1024u + 1u, ' ');
    EXPECT_STATUS(fixture.call("POST", "/api/admin/students", huge, kAdmin, true), 400,
                  "InvalidInput");
    CHECK_TEXT(fixture.call("POST", "/api/admin/students", huge, kAdmin, true).body.at("message")
                   .get<std::string>(),
               "64 KiB");

    // 不是 JSON 对象。
    EXPECT_STATUS(fixture.call("PUT", "/api/admin/credit-limit", "[1,2,3]", kAdmin, true), 400,
                  "InvalidInput");
    EXPECT_STATUS(fixture.call("PUT", "/api/admin/credit-limit", "not json", kAdmin, true), 400,
                  "InvalidInput");

    // 类型错误：credits 是字符串。
    const json wrong_type{{"course_id", 7}, {"course_code", "X101"},
                          {"course_name", "X"}, {"credits", "3"}};
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/courses/7", wrong_type, kAdmin), 400,
                  "InvalidInput");
    CHECK_TEXT(fixture.write("PUT", "/api/admin/courses/7", wrong_type, kAdmin).body.at("message")
                   .get<std::string>(),
               "must be an integer");

    // 路径编号与正文编号不一致。
    const json student{{"student_id", 1002}, {"student_number", "S2023002"},
                       {"name", "Bob Liu"}, {"major", "Computer Science"},
                       {"account_active", true}};
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/students/1001", student, kAdmin), 400,
                  "InvalidInput");
    CHECK_TEXT(fixture.write("PUT", "/api/admin/students/1001", student, kAdmin).body.at("message")
                   .get<std::string>(),
               "must match");

    // 创建课程缺 first_teaching_class。
    const json only_course{{"course", {{"course_id", 7}, {"course_code", "X101"},
                                       {"course_name", "X"}, {"credits", 3}}}};
    EXPECT_STATUS(fixture.write("POST", "/api/admin/courses", only_course, kAdmin), 400,
                  "InvalidInput");

    // 空 meetings 由业务层拒绝（400 InvalidInput）。
    const json no_meetings{
        {"teaching_class_id", 99}, {"course_id", 1}, {"teacher_id", 100},
        {"capacity", 10}, {"meetings", json::array()}};
    EXPECT_STATUS(fixture.write("POST", "/api/admin/classes", no_meetings, kAdmin), 400,
                  "InvalidInput");

    // 所有失败都不改数据：课程数仍是 3。
    CHECK(fixture.get("/api/admin/courses", kAdmin).body.at("data").size() == 3);
}

// ---------------------------------------------------------------------------
// 6. 管理员写操作：成功、落盘、失败原因
// ---------------------------------------------------------------------------

void test_admin_writes() {
    Fixture fixture;

    // 创建"课程 + 首班"：一次完整操作。
    const json meeting{{"day_of_week", 5}, {"start_minute", 600},
                       {"end_minute", 690}, {"room", "D401"}};
    const json created{
        {"course", {{"course_id", 4}, {"course_code", "DB201"}, {"course_name", "Database"},
                    {"credits", 4}}},
        {"first_teaching_class", {{"teaching_class_id", 40}, {"course_id", 4}, {"teacher_id", 200},
                                  {"capacity", 2},
                                  {"meetings", json::array({meeting})}}}};
    const Reply created_reply = fixture.write("POST", "/api/admin/courses", created, kAdmin);
    EXPECT_STATUS(created_reply, 200, "Success");
    CHECK(created_reply.body.at("data").at("course_id").get<int>() == 4);
    CHECK(created_reply.body.at("data").at("teaching_class_id").get<int>() == 40);

    // 同一份数据再提交一次：课程编号冲突 -> 409。
    EXPECT_STATUS(fixture.write("POST", "/api/admin/courses", created, kAdmin), 409,
                  "DuplicateIdentifier");
    // 课程代码按去空白 + 小写查重。
    json duplicate_code = created;
    duplicate_code["course"]["course_id"] = 5;
    duplicate_code["course"]["course_code"] = " db201 ";
    duplicate_code["first_teaching_class"]["teaching_class_id"] = 41;
    duplicate_code["first_teaching_class"]["course_id"] = 5;
    duplicate_code["first_teaching_class"]["teacher_id"] = 100;
    EXPECT_STATUS(fixture.write("POST", "/api/admin/courses", duplicate_code, kAdmin), 409,
                  "DuplicateIdentifier");

    // 写入确实落盘：重新从文件载入能看到新课程。
    SystemContext reloaded = make_seed_context();
    const OperationResult loaded = load_state(fixture.options, reloaded);
    CHECK(loaded.code == ResultCode::Success);
    Course stored{};
    CHECK(reloaded.find_course(4, stored));
    CHECK(stored.course_code == "DB201");
    TeachingClass stored_class{};
    CHECK(reloaded.find_teaching_class(40, stored_class));
    CHECK(stored_class.capacity == 2);

    // 改名课程（PUT 全字段）。
    const json renamed{{"course_id", 4}, {"course_code", "DB201"},
                       {"course_name", "Database Systems"}, {"credits", 4}};
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/courses/4", renamed, kAdmin), 200, "Success");
    CHECK(fixture.get("/api/admin/courses", kAdmin).body.at("data").at(3).at("course").at(
              "course_name").get<std::string>() == "Database Systems");

    // 改容量：合法值成功，非法值 400，低于已选人数 409。
    EXPECT_STATUS(fixture.write("PATCH", "/api/admin/classes/40/capacity",
                                json{{"capacity", 5}}, kAdmin),
                  200, "Success");
    EXPECT_STATUS(fixture.write("PATCH", "/api/admin/classes/40/capacity",
                                json{{"capacity", 0}}, kAdmin),
                  400, "InvalidInput");
    CHECK(fixture.write("PATCH", "/api/admin/classes/40/capacity", json{{"capacity", 0}}, kAdmin)
              .body.at("message").get<std::string>().find("at least 1") != std::string::npos);

    // 选满后再降容量：409，且容量保持 5。
    CHECK(fixture.context.registration().enroll(1001, 40).code == ResultCode::Success);
    CHECK(fixture.context.registration().enroll(1002, 40).code == ResultCode::Success);
    EXPECT_STATUS(fixture.write("PATCH", "/api/admin/classes/40/capacity",
                                json{{"capacity", 1}}, kAdmin),
                  409, "CapacityBelowEnrollment");
    CHECK(fixture.get("/api/admin/classes", kAdmin).body.at("data").at(4).at("capacity")
              .get<int>() == 5);
    // 教师立刻看到新容量与人数（同一份共享状态）。
    const Reply summary = fixture.get("/api/teacher/classes/40/summary", kLi);
    EXPECT_STATUS(summary, 200, "Success");
    CHECK(summary.body.at("data").at("active_enrollments").get<int>() == 2);

    // 有有效选课记录时不能删课程、不能删班、不能把学生删掉。
    EXPECT_STATUS(fixture.write("DELETE", "/api/admin/courses/4", json::object(), kAdmin), 409,
                  "CatalogInUse");
    EXPECT_STATUS(fixture.write("DELETE", "/api/admin/classes/40", json::object(), kAdmin), 409,
                  "CatalogInUse");
    EXPECT_STATUS(fixture.write("DELETE", "/api/admin/students/1001", json::object(), kAdmin), 409,
                  "CatalogInUse");
    // 课程与班级仍然存在。
    CHECK(fixture.get("/api/admin/classes", kAdmin).body.at("data").size() == 5);

    // 退课后仍然有 Dropped 历史：继续拒绝删除，并提示"保留历史"。
    CHECK(fixture.context.registration().drop(1001, 40).code == ResultCode::Success);
    CHECK(fixture.context.registration().drop(1002, 40).code == ResultCode::Success);
    const Reply still_blocked =
        fixture.write("DELETE", "/api/admin/classes/40", json::object(), kAdmin);
    EXPECT_STATUS(still_blocked, 409, "CatalogInUse");
    CHECK_TEXT(still_blocked.body.at("message").get<std::string>(), "dropped enrollment record");
}

void test_admin_person_writes() {
    Fixture fixture;

    const json new_student{{"student_id", 1003}, {"student_number", "S2023003"},
                           {"name", "Cara Wu"}, {"major", "Mathematics"},
                           {"account_active", true}};
    EXPECT_STATUS(fixture.write("POST", "/api/admin/students", new_student, kAdmin), 200, "Success");
    CHECK(fixture.get("/api/admin/students", kAdmin).body.at("data").size() == 3);

    // 跨角色占号（学号撞教师工号在种子里不存在，这里用大小写变体测同角色）。
    json duplicate_student = new_student;
    duplicate_student["student_id"] = 1004;
    duplicate_student["student_number"] = " s2023003 ";
    EXPECT_STATUS(fixture.write("POST", "/api/admin/students", duplicate_student, kAdmin), 409,
                  "DuplicateIdentifier");

    // 停用学生（只改状态，不删历史）。
    const json disabled_student{{"student_id", 1003}, {"student_number", "S2023003"},
                                {"name", "Cara Wu"}, {"major", "Mathematics"},
                                {"account_active", false}};
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/students/1003", disabled_student, kAdmin), 200,
                  "Success");
    CHECK(!fixture.get("/api/admin/students", kAdmin).body.at("data").at(2).at("account_active")
               .get<bool>());

    // 无选课记录的学生可以删除。
    EXPECT_STATUS(fixture.write("DELETE", "/api/admin/students/1003", json::object(), kAdmin), 200,
                  "Success");
    CHECK(fixture.get("/api/admin/students", kAdmin).body.at("data").size() == 2);

    // 教师仍带班时不能删。
    EXPECT_STATUS(fixture.write("DELETE", "/api/admin/teachers/100", json::object(), kAdmin), 409,
                  "CatalogInUse");
    // 新建教师 -> 可以删。
    const json new_teacher{{"teacher_id", 400}, {"teacher_number", "T2024001"},
                           {"name", "Dr. Lin"}, {"department", "Physics"},
                           {"account_active", true}};
    EXPECT_STATUS(fixture.write("POST", "/api/admin/teachers", new_teacher, kAdmin), 200, "Success");
    EXPECT_STATUS(fixture.write("DELETE", "/api/admin/teachers/400", json::object(), kAdmin), 200,
                  "Success");
}

void test_registration_rule_writes() {
    Fixture fixture;
    // 种子规则默认 registration_open=true，等价于"本轮已经开放过"，学分上限会被锁住。
    // 这里先恢复成"本轮未开放"，再验证上限修改。
    fixture.context.apply_rule_change(RegistrationRule{false, 18, "", ""});
    fixture.context.restore_round_opened(false);

    // 首次开放前才能改学分上限。
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/credit-limit", json{{"maximum_credits", 21}},
                                kAdmin),
                  200, "Success");
    CHECK(fixture.get("/api/admin/registration-rule", kAdmin).body.at("data").at("maximum_credits")
              .get<int>() == 21);

    // 时段必须成对、格式正确、起点早于终点。
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/registration-period",
                                json{{"opens_at", "2026-11-01T08:00"},
                                     {"closes_at", "2026-12-31T20:00"}},
                                kAdmin),
                  200, "Success");
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/registration-period",
                                json{{"opens_at", "2026-13-01T08:00"},
                                     {"closes_at", "2026-12-31T20:00"}},
                                kAdmin),
                  400, "InvalidInput");
    EXPECT_STATUS(fixture.write("PUT", "/api/admin/registration-period",
                                json{{"opens_at", "2026-12-31T20:00"},
                                     {"closes_at", "2026-11-01T08:00"}},
                                kAdmin),
                  400, "InvalidInput");

    // 关闭选课：不接受字段。
    EXPECT_STATUS(fixture.write("POST", "/api/admin/registration-close", json{{"x", 1}}, kAdmin),
                  400, "InvalidInput");
    EXPECT_STATUS(fixture.write("POST", "/api/admin/registration-close", json::object(), kAdmin),
                  200, "Success");
    const Reply rule = fixture.get("/api/admin/registration-rule", kAdmin);
    CHECK(!rule.body.at("data").at("registration_open").get<bool>());
    CHECK(rule.body.at("data").at("opens_at").get<std::string>().empty());
    // 关闭后学生选课被拒绝（跨模块一致性）。
    CHECK(fixture.context.registration().enroll(1001, 10).code == ResultCode::RegistrationClosed);
}

// ---------------------------------------------------------------------------
// 7. 响应外壳统一
// ---------------------------------------------------------------------------

void test_envelope() {
    Fixture fixture;

    const Reply ok = fixture.get("/api/admin/classes", kAdmin);
    CHECK(ok.body.contains("code"));
    CHECK(ok.body.contains("message"));
    CHECK(ok.body.contains("data"));
    CHECK(ok.body.at("code").get<std::string>() == "Success");
    CHECK(ok.raw.find("application/json") == std::string::npos);  // 头由适配层加，body 只有 JSON

    const Reply failed =
        fixture.write("PATCH", "/api/admin/classes/99999999999/capacity", json{{"capacity", 1}},
                      kAdmin);
    EXPECT_STATUS(failed, 400, "InvalidInput");
    CHECK(failed.body.at("data").is_null());
}

void run(const char* name, void (*test)()) {
    std::cout << "== " << name << std::endl;
    try {
        test();
    } catch (const std::exception& error) {
        ++g_failures;
        std::cout << "EXCEPTION in " << name << ": " << error.what() << std::endl;
    }
}

}  // namespace
}  // namespace http
}  // namespace course_registration

int main() {
    using namespace course_registration::http;
    run("authentication", test_authentication_and_roles);
    run("disabled-account", test_disabled_account_loses_session);
    run("csrf", test_csrf);
    run("teacher-reads", test_teacher_reads);
    run("admin-reads", test_admin_reads);
    run("request-validation", test_request_validation);
    run("admin-writes", test_admin_writes);
    run("admin-person-writes", test_admin_person_writes);
    run("registration-rule", test_registration_rule_writes);
    run("envelope", test_envelope);

    std::cout << (g_failures == 0 ? "PASS: " : "FAILED: ") << g_checks << " checks, " << g_failures
              << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
