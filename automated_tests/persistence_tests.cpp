// 持久化模块测试（Zhang Yikai）
//
// 覆盖：保存/载入往返、Active/Dropped、账号启停、round_opened、备份恢复安全
// （恢复不覆盖 .bak、失败可重试）、主文件被占用时的恢复、业务约束校验
// （跨角色重复账号、超容量、重复 active、时间冲突、学分超限、课程无教学班、
//   教学班无上课安排）、整数范围、UTF-16 代理对、自动开放后保存仍锁学分、
// 首次初始化失败保留调用方状态、保存/读取/损坏/schema/无备份等失败路径。
//
// 关键检查统一用 CHECK 宏（不依赖 assert），发布构建也不会被跳过。

#include "persistence_service.hpp"
#include "student_query_and_timetable_services.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace course_registration;

namespace {

int g_failures = 0;

void check(bool ok, const char* expr, int line) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL line " << line << ": " << expr << "\n";
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool replace_once(std::string& text, const std::string& from,
                  const std::string& to) {
    const size_t pos = text.find(from);
    if (pos == std::string::npos) {
        return false;
    }
    text.replace(pos, from.size(), to);
    return true;
}

StorageOptions options_at(const std::string& path) {
    StorageOptions options;
    options.path = path;
    return options;
}

// 删掉所有 ", \"password_hash\": \"...\"" 片段，用来构造 v1 旧快照。
// PHC 字符串里没有需要转义的双引号，找到下一个引号即可截断。
std::string strip_password_hash(std::string text) {
    const std::string key = ", \"password_hash\": \"";
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
        const size_t value_start = pos + key.size();
        const size_t close = text.find('"', value_start);
        if (close == std::string::npos) {
            break;
        }
        text.erase(pos, close + 1 - pos);
    }
    return text;
}

// 逐字段比较两份状态（不只比数量），用于验证失败后原状态未被改动。
void expect_same_state(const SystemContext& left, const SystemContext& right,
                       int line) {
    const std::vector<Course> courses_left = left.courses();
    const std::vector<Course> courses_right = right.courses();
    check(courses_left.size() == courses_right.size(), "course count", line);
    if (courses_left.size() == courses_right.size()) {
        for (size_t i = 0; i < courses_left.size(); ++i) {
            const Course& a = courses_left[i];
            const Course& b = courses_right[i];
            check(a.course_id == b.course_id, "course id", line);
            check(a.course_code == b.course_code, "course code", line);
            check(a.course_name == b.course_name, "course name", line);
            check(a.credits == b.credits, "course credits", line);
        }
    }
    const std::vector<TeachingClass> classes_left = left.teaching_classes();
    const std::vector<TeachingClass> classes_right = right.teaching_classes();
    check(classes_left.size() == classes_right.size(), "class count", line);
    if (classes_left.size() == classes_right.size()) {
        for (size_t i = 0; i < classes_left.size(); ++i) {
            const TeachingClass& a = classes_left[i];
            const TeachingClass& b = classes_right[i];
            check(a.teaching_class_id == b.teaching_class_id, "class id", line);
            check(a.course_id == b.course_id, "class course", line);
            check(a.teacher_id == b.teacher_id, "class teacher", line);
            check(a.capacity == b.capacity, "class capacity", line);
            check(a.meetings.size() == b.meetings.size(), "class meetings",
                  line);
            if (a.meetings.size() == b.meetings.size()) {
                for (size_t m = 0; m < a.meetings.size(); ++m) {
                    check(a.meetings[m].day_of_week ==
                              b.meetings[m].day_of_week,
                          "meeting day", line);
                    check(a.meetings[m].start_minute ==
                              b.meetings[m].start_minute,
                          "meeting start", line);
                    check(a.meetings[m].end_minute ==
                              b.meetings[m].end_minute,
                          "meeting end", line);
                    check(a.meetings[m].room == b.meetings[m].room,
                          "meeting room", line);
                }
            }
        }
    }
    check(left.students().size() == right.students().size(), "student count",
          line);
    if (left.students().size() == right.students().size()) {
        for (size_t i = 0; i < left.students().size(); ++i) {
            const Student& a = left.students()[i];
            const Student& b = right.students()[i];
            check(a.student_id == b.student_id, "student id", line);
            check(a.student_number == b.student_number, "student number", line);
            check(a.name == b.name, "student name", line);
            check(a.major == b.major, "student major", line);
            check(a.account_active == b.account_active, "student active", line);
        }
    }
    check(left.teachers().size() == right.teachers().size(), "teacher count",
          line);
    if (left.teachers().size() == right.teachers().size()) {
        for (size_t i = 0; i < left.teachers().size(); ++i) {
            const Teacher& a = left.teachers()[i];
            const Teacher& b = right.teachers()[i];
            check(a.teacher_id == b.teacher_id, "teacher id", line);
            check(a.teacher_number == b.teacher_number, "teacher number", line);
            check(a.name == b.name, "teacher name", line);
            check(a.department == b.department, "teacher department", line);
            check(a.account_active == b.account_active, "teacher active", line);
        }
    }
    check(left.administrators().size() == right.administrators().size(),
          "admin count", line);
    if (left.administrators().size() == right.administrators().size()) {
        for (size_t i = 0; i < left.administrators().size(); ++i) {
            const Administrator& a = left.administrators()[i];
            const Administrator& b = right.administrators()[i];
            check(a.administrator_id == b.administrator_id, "admin id", line);
            check(a.account_number == b.account_number, "admin number", line);
            check(a.name == b.name, "admin name", line);
            check(a.account_active == b.account_active, "admin active", line);
        }
    }
    const std::vector<Enrollment> enrollments_left = left.all_enrollments();
    const std::vector<Enrollment> enrollments_right = right.all_enrollments();
    check(enrollments_left.size() == enrollments_right.size(),
          "enrollment count", line);
    if (enrollments_left.size() == enrollments_right.size()) {
        for (size_t i = 0; i < enrollments_left.size(); ++i) {
            const Enrollment& a = enrollments_left[i];
            const Enrollment& b = enrollments_right[i];
            check(a.student_id == b.student_id, "enrollment student", line);
            check(a.teaching_class_id == b.teaching_class_id,
                  "enrollment class", line);
            check(a.status == b.status, "enrollment status", line);
        }
    }
    const RegistrationRule ra = left.rule();
    const RegistrationRule rb = right.rule();
    check(ra.registration_open == rb.registration_open, "rule open", line);
    check(ra.maximum_credits == rb.maximum_credits, "rule credits", line);
    check(ra.opens_at == rb.opens_at, "rule opens_at", line);
    check(ra.closes_at == rb.closes_at, "rule closes_at", line);
    check(left.round_opened() == right.round_opened(), "round opened", line);
}

#define EXPECT_SAME_STATE(a, b) expect_same_state((a), (b), __LINE__)

}  // namespace

int main() {
    namespace fs = std::filesystem;

    std::error_code ec;
    const std::string dir = "build/persistence_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // ------------------------------------------------------------------
    // 0. 种子数据本身必须通过校验。
    // ------------------------------------------------------------------
    SystemContext seed = make_seed_context();
    std::vector<std::string> problems;
    CHECK(validate_state(seed, problems).code == ResultCode::Success);
    CHECK(problems.empty());

    // ------------------------------------------------------------------
    // 1. 保存 / 载入往返：active、dropped、账号启停、round_opened。
    // ------------------------------------------------------------------
    const std::string state_path = dir + "/state.json";
    const StorageOptions state_options = options_at(state_path);

    SystemContext context = make_seed_context();
    CHECK(context.registration().enroll(1001, 10).code == ResultCode::Success);
    CHECK(context.registration().enroll(1002, 10).code == ResultCode::Success);
    CHECK(context.registration().drop(1002, 10).code == ResultCode::Success);
    Student alice = context.students()[0];
    alice.account_active = false;
    context.apply_student_change(alice);
    context.restore_round_opened(true);

    CHECK(save_state(context, state_options).code == ResultCode::Success);
    CHECK(fs::exists(state_path));
    CHECK(!fs::exists(state_path + ".tmp"));

    SystemContext loaded = make_seed_context();
    CHECK(load_state(state_options, loaded).code == ResultCode::Success);
    CHECK(loaded.courses().size() == 3);
    CHECK(loaded.teaching_classes().size() == 4);
    CHECK(loaded.students().size() == 2);
    CHECK(loaded.teachers().size() == 3);
    CHECK(loaded.administrators().size() == 1);
    CHECK(loaded.all_enrollments().size() == 2);   // 1 active + 1 dropped
    CHECK(loaded.remaining_seats(10) == 1);        // capacity 2, active 1
    CHECK(loaded.rule().maximum_credits == 18);
    CHECK(loaded.registration_window_open() == true);
    CHECK(loaded.students()[0].account_active == false);
    CHECK(loaded.round_opened() == true);

    // ------------------------------------------------------------------
    // 2. 备份安全：主文件损坏时，恢复只用 .bak，且不覆盖 .bak；可再次恢复。
    // ------------------------------------------------------------------
    SystemContext second = loaded;
    CHECK(second.registration().enroll(1001, 30).code == ResultCode::Success);
    CHECK(save_state(second, state_options).code == ResultCode::Success);
    CHECK(fs::exists(state_path + ".bak"));

    write_text(state_path, "{ this is corrupted ");  // 模拟主文件损坏

    SystemContext restored = make_seed_context();
    CHECK(restore_state(state_options, restored).code == ResultCode::Success);
    CHECK(restored.all_enrollments().size() == 2);  // 回到第二版之前
    CHECK(restored.remaining_seats(30) == 2);
    CHECK(fs::exists(state_path + ".bak"));         // .bak 仍保留

    SystemContext again = make_seed_context();
    CHECK(restore_state(state_options, again).code == ResultCode::Success);  // 可重复恢复
    CHECK(again.all_enrollments().size() == 2);

    // ------------------------------------------------------------------
    // 3. 主文件被占用时恢复失败，但 .bak 仍是有效的，释放后可再恢复。
    // ------------------------------------------------------------------
#ifdef _WIN32
    {
        HANDLE handle = CreateFileA(state_path.c_str(), GENERIC_READ, 0,
                                    nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE);
        SystemContext blocked = make_seed_context();
        CHECK(blocked.registration().enroll(1001, 10).code ==
              ResultCode::Success);
        CHECK(restore_state(state_options, blocked).code ==
              ResultCode::StorageIoError);
        CHECK(blocked.all_enrollments().size() == 1);  // 失败后原状态不变
        CHECK(fs::exists(state_path + ".bak"));
        CloseHandle(handle);

        SystemContext retry = make_seed_context();
        CHECK(restore_state(state_options, retry).code == ResultCode::Success);
    }
#endif

    // ------------------------------------------------------------------
    // 4. 业务约束校验：每种坏状态都必须被拒绝。
    // ------------------------------------------------------------------
    {
        // 4.1 跨角色重复账号（教师工号与学生学号相同，大小写不同也算）
        SystemContext bad = make_seed_context();
        Teacher teacher = bad.teachers()[0];
        teacher.teacher_number = "s2023001";
        bad.apply_teacher_change(teacher);
        CHECK(validate_state(bad, problems).code != ResultCode::Success);
        CHECK(!problems.empty());
        const std::string bad_path = dir + "/bad_account.json";
        CHECK(save_state(bad, options_at(bad_path)).code ==
              ResultCode::StorageIntegrityError);
        CHECK(!fs::exists(bad_path));  // 无效状态不落盘
    }
    {
        // 4.2 超容量
        SystemContext over = make_seed_context();
        TeachingClass item = over.teaching_classes()[0];  // class 10
        item.capacity = 1;
        over.apply_teaching_class_change(item);
        over.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1002, 10, EnrollmentStatus::Active}});
        CHECK(validate_state(over, problems).code != ResultCode::Success);
    }
    {
        // 4.3 同一学生同一教学班重复 active
        SystemContext duplicate = make_seed_context();
        duplicate.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1001, 10, EnrollmentStatus::Active}});
        CHECK(validate_state(duplicate, problems).code != ResultCode::Success);
    }
    {
        // 4.4 学生时间冲突（class 10 与 class 20 都在周一且重叠）
        SystemContext conflict = make_seed_context();
        conflict.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1001, 20, EnrollmentStatus::Active}});
        CHECK(validate_state(conflict, problems).code != ResultCode::Success);
    }
    {
        // 4.5 学分超限
        SystemContext credits = make_seed_context();
        RegistrationRule rule = credits.rule();
        rule.maximum_credits = 2;
        credits.apply_rule_change(rule);
        credits.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active}});  // 3 学分 > 2
        CHECK(validate_state(credits, problems).code != ResultCode::Success);
    }
    {
        // 4.6 课程没有教学班
        SystemContext no_class = make_seed_context();
        no_class.remove_teaching_class_record(30);  // CS101 失去唯一教学班
        CHECK(validate_state(no_class, problems).code != ResultCode::Success);
    }
    {
        // 4.7 教学班没有上课安排
        SystemContext no_meeting = make_seed_context();
        TeachingClass item = no_meeting.teaching_classes()[2];  // class 20
        item.meetings.clear();
        no_meeting.apply_teaching_class_change(item);
        CHECK(validate_state(no_meeting, problems).code != ResultCode::Success);
    }

    // ------------------------------------------------------------------
    // 5. 载入时拒绝违反约束的 JSON（超容量），且不替换调用方状态。
    // ------------------------------------------------------------------
    {
        SystemContext valid = make_seed_context();
        CHECK(valid.registration().enroll(1001, 10).code == ResultCode::Success);
        const std::string valid_path = dir + "/valid.json";
        CHECK(save_state(valid, options_at(valid_path)).code ==
              ResultCode::Success);

        std::string text = read_text(valid_path);
        CHECK(replace_once(
            text,
            "\"teaching_class_id\": 10, \"course_id\": 1, \"teacher_id\": 100, \"capacity\": 2,",
            "\"teaching_class_id\": 10, \"course_id\": 1, \"teacher_id\": 100, \"capacity\": 0,"));
        const std::string over_path = dir + "/over.json";
        write_text(over_path, text);

        SystemContext keep = make_seed_context();
        CHECK(load_state(options_at(over_path), keep).code ==
              ResultCode::StorageIntegrityError);
        CHECK(keep.courses().size() == 3);  // 原状态未被破坏

        // 5b. 角色内重复 student_id（只在 JSON 层能构造）
        std::string duplicate_id = read_text(valid_path);
        CHECK(replace_once(duplicate_id, "\"student_id\": 1002",
                           "\"student_id\": 1001"));
        const std::string duplicate_id_path = dir + "/duplicate_id.json";
        write_text(duplicate_id_path, duplicate_id);
        SystemContext keep_id = make_seed_context();
        CHECK(load_state(options_at(duplicate_id_path), keep_id).code ==
              ResultCode::StorageIntegrityError);
    }

    // ------------------------------------------------------------------
    // 6. 整数超出 int 范围：拒绝而不是静默截断。
    // ------------------------------------------------------------------
    {
        std::string text = read_text(dir + "/valid.json");
        CHECK(replace_once(text,
                           "\"course_id\": 1, \"course_code\": \"SE101\"",
                           "\"course_id\": 999999999999, \"course_code\": \"SE101\""));
        const std::string range_path = dir + "/range.json";
        write_text(range_path, text);
        SystemContext out = make_seed_context();
        CHECK(load_state(options_at(range_path), out).code !=
              ResultCode::Success);
        CHECK(out.courses().size() == 3);
    }

    // ------------------------------------------------------------------
    // 7. Unicode：代理对 + 中文 + 引号 + 反斜杠，往返保存保持一致。
    // ------------------------------------------------------------------
    {
        std::string text = read_text(dir + "/valid.json");
        // JSON 文本： "name": "\u5f20\u4e09 \"A\" \\ \ud83d\ude00"
        CHECK(replace_once(
            text, "\"name\": \"Alice Chen\"",
            "\"name\": \"\\u5f20\\u4e09 \\\"A\\\" \\\\ \\ud83d\\ude00\""));
        const std::string unicode_path = dir + "/unicode.json";
        write_text(unicode_path, text);

        // 张 三 "A" \ 😀
        const std::string expected =
            "\xE5\xBC\xA0\xE4\xB8\x89 \"A\" \\ \xF0\x9F\x98\x80";
        SystemContext unicode = make_seed_context();
        CHECK(load_state(options_at(unicode_path), unicode).code ==
              ResultCode::Success);
        CHECK(unicode.students()[0].name == expected);

        const std::string unicode_saved = dir + "/unicode_saved.json";
        CHECK(save_state(unicode, options_at(unicode_saved)).code ==
              ResultCode::Success);
        SystemContext unicode2 = make_seed_context();
        CHECK(load_state(options_at(unicode_saved), unicode2).code ==
              ResultCode::Success);
        CHECK(unicode2.students()[0].name == expected);
    }

    // ------------------------------------------------------------------
    // 8. 自动开放后保存：round_opened 被写入，重启后学分上限仍锁定。
    // ------------------------------------------------------------------
    {
        SystemContext auto_open = make_seed_context();
        RegistrationRule rule = auto_open.rule();
        rule.registration_open = false;
        rule.opens_at = "2020-01-01T00:00";
        rule.closes_at = "2020-01-02T00:00";
        rule.maximum_credits = 18;
        auto_open.apply_rule_change(rule);
        auto_open.set_now_for_tests("2021-01-01T00:00");  // 已过开放窗口
        CHECK(auto_open.round_opened() == false);         // 还没观察

        const std::string auto_path = dir + "/auto.json";
        CHECK(save_state(auto_open, options_at(auto_path)).code ==
              ResultCode::Success);  // 保存入口负责 observe

        SystemContext reloaded = make_seed_context();
        CHECK(load_state(options_at(auto_path), reloaded).code ==
              ResultCode::Success);
        CHECK(reloaded.round_opened() == true);

        AdministratorService admin(reloaded);
        UserIdentity admin_user{900, "A2020001", Role::Administrator};
        CHECK(admin.set_maximum_credits(admin_user, 30).code !=
              ResultCode::Success);  // 重启后仍不能再改上限
    }

    // ------------------------------------------------------------------
    // 8b. 自动开放后"关闭"或"改期到未来"，保存并重启后学分上限仍锁定。
    // ------------------------------------------------------------------
    {
        SystemContext flow = make_seed_context();
        RegistrationRule rule = flow.rule();
        rule.registration_open = false;
        rule.opens_at = "2020-01-01T00:00";
        rule.closes_at = "2020-01-02T00:00";
        flow.apply_rule_change(rule);
        flow.set_now_for_tests("2021-01-01T00:00");
        CHECK(flow.round_opened() == false);
        CHECK(save_state(flow, options_at(dir + "/flow_open.json")).code ==
              ResultCode::Success);
        CHECK(flow.round_opened() == true);  // save 观察后置 true

        // 关闭：清空时段
        RegistrationRule closed = flow.rule();
        closed.opens_at.clear();
        closed.closes_at.clear();
        closed.registration_open = false;
        flow.apply_rule_change(closed);
        CHECK(save_state(flow, options_at(dir + "/flow_closed.json")).code ==
              ResultCode::Success);

        SystemContext reloaded_closed = make_seed_context();
        CHECK(load_state(options_at(dir + "/flow_closed.json"),
                         reloaded_closed)
                  .code == ResultCode::Success);
        CHECK(reloaded_closed.round_opened() == true);
        AdministratorService admin_closed(reloaded_closed);
        UserIdentity admin_closed_user{900, "A2020001", Role::Administrator};
        CHECK(admin_closed.set_maximum_credits(admin_closed_user, 30).code !=
              ResultCode::Success);

        // 改期到未来
        RegistrationRule future = flow.rule();
        future.opens_at = "2030-01-01T00:00";
        future.closes_at = "2030-01-02T00:00";
        flow.apply_rule_change(future);
        CHECK(save_state(flow, options_at(dir + "/flow_future.json")).code ==
              ResultCode::Success);

        SystemContext reloaded_future = make_seed_context();
        CHECK(load_state(options_at(dir + "/flow_future.json"),
                         reloaded_future)
                  .code == ResultCode::Success);
        CHECK(reloaded_future.round_opened() == true);
        AdministratorService admin_future(reloaded_future);
        UserIdentity admin_future_user{900, "A2020001", Role::Administrator};
        CHECK(admin_future.set_maximum_credits(admin_future_user, 30).code !=
              ResultCode::Success);
    }

    // ------------------------------------------------------------------
    // 9. 保存失败（父路径是文件）返回 StorageIoError，且不留 .tmp。
    // ------------------------------------------------------------------
    {
        const std::string blocked_parent = dir + "/blocked";
        write_text(blocked_parent, "not a directory");
        const std::string blocked_path = blocked_parent + "/state.json";
        SystemContext seed_for_save = make_seed_context();
        CHECK(save_state(seed_for_save, options_at(blocked_path)).code ==
              ResultCode::StorageIoError);
        CHECK(!fs::exists(blocked_path + ".tmp"));
    }

    // ------------------------------------------------------------------
    // 10. 读取失败 / 损坏 / schema / 无备份。
    // ------------------------------------------------------------------
    {
        SystemContext out = make_seed_context();
        CHECK(load_state(options_at(dir + "/missing.json"), out).code ==
              ResultCode::StorageIoError);

        const std::string corrupt = dir + "/corrupt.json";
        write_text(corrupt, "{ not json");
        CHECK(load_state(options_at(corrupt), out).code ==
              ResultCode::StorageParseError);

        const std::string schema = dir + "/schema.json";
        write_text(schema, "{\"schema_version\": 99}");
        CHECK(load_state(options_at(schema), out).code ==
              ResultCode::StorageSchemaMismatch);

        SystemContext single = make_seed_context();
        const std::string single_path = dir + "/single.json";
        CHECK(save_state(single, options_at(single_path)).code ==
              ResultCode::Success);
        CHECK(restore_state(options_at(single_path), out).code ==
              ResultCode::StorageBackupMissing);
    }

    // ------------------------------------------------------------------
    // 11. 首次运行初始化；初始化失败时保留调用方原状态。
    // ------------------------------------------------------------------
    {
        const StorageOptions init_options = options_at(dir + "/init/state.json");
        SystemContext initialized = make_seed_context();
        CHECK(initialize_storage(init_options, initialized).code ==
              ResultCode::Success);
        CHECK(fs::exists(init_options.path));
        CHECK(initialize_storage(init_options, initialized).code ==
              ResultCode::Success);

        // 父路径是文件：写盘失败，调用方状态不能被 seed 替换。
        SystemContext keep = make_seed_context();
        CHECK(keep.registration().enroll(1001, 10).code == ResultCode::Success);
        const size_t before = keep.all_enrollments().size();
        CHECK(initialize_storage(options_at(dir + "/blocked/init.json"), keep)
                  .code == ResultCode::StorageIoError);
        CHECK(keep.all_enrollments().size() == before);
    }

    // ==================================================================
    // 12. 同一课程的不同教学班：无时间冲突、学分也不超，仍必须拒绝。
    // ==================================================================
    {
        std::vector<std::string> problems;
        SystemContext same_course = make_seed_context();
        same_course.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1001, 11, EnrollmentStatus::Active}});  // 课程 1 的班 10/11
        CHECK(validate_state(same_course, problems).code !=
              ResultCode::Success);
        CHECK(!problems.empty());

        // 正常对照：不同课程、时间不冲突 → 允许
        SystemContext normal = make_seed_context();
        normal.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1001, 30, EnrollmentStatus::Active}});
        CHECK(validate_state(normal, problems).code == ResultCode::Success);
    }

    // ==================================================================
    // 13. 账号比较：trim + 小写；跨角色、管理员、停用账号都要查。
    // ==================================================================
    {
        std::vector<std::string> problems;

        SystemContext spaced = make_seed_context();
        Teacher teacher = spaced.teachers()[0];
        teacher.teacher_number = "  s2023001  ";
        spaced.apply_teacher_change(teacher);
        CHECK(validate_state(spaced, problems).code != ResultCode::Success);

        SystemContext admin_dup = make_seed_context();
        Administrator admin = admin_dup.administrators()[0];
        admin.account_number = "S2023001";
        admin_dup.apply_administrator_change(admin);
        CHECK(validate_state(admin_dup, problems).code != ResultCode::Success);

        SystemContext disabled = make_seed_context();
        Student student = disabled.students()[0];
        student.account_active = false;
        disabled.apply_student_change(student);
        Teacher other = disabled.teachers()[0];
        other.teacher_number = "s2023001";
        disabled.apply_teacher_change(other);
        CHECK(validate_state(disabled, problems).code != ResultCode::Success);

        // 不同角色相同数字 ID → 允许（只按各自集合唯一）
        SystemContext same_numeric = make_seed_context();
        Teacher extra = same_numeric.teachers()[0];
        extra.teacher_id = 1001;  // 与 student_id 1001 相同
        extra.teacher_number = "T9000001";
        same_numeric.apply_teacher_change(extra);
        CHECK(validate_state(same_numeric, problems).code ==
              ResultCode::Success);

        // 修改时排除自身：管理员更新同一条学生记录应成功
        AdministratorService admin_service(same_numeric);
        UserIdentity admin_user{900, "A2020001", Role::Administrator};
        Student unchanged = same_numeric.students()[0];
        CHECK(admin_service.update_student(admin_user, unchanged).code ==
              ResultCode::Success);

        // 共享查找也使用 trim+lower：带首尾空白的查询能命中。
        CHECK(same_numeric.find_student_by_number("  S2023001  ") != nullptr);
        CHECK(same_numeric.find_teacher_by_number("  t9000001  ") != nullptr);
    }

    // ==================================================================
    // 14. 基础字段、取值与日期。
    // ==================================================================
    {
        std::vector<std::string> problems;

        SystemContext cap = make_seed_context();
        TeachingClass item = cap.teaching_classes()[0];
        item.capacity = 0;
        cap.apply_teaching_class_change(item);
        CHECK(validate_state(cap, problems).code != ResultCode::Success);

        SystemContext negative = make_seed_context();
        Course course = negative.courses()[0];
        course.credits = -1;
        negative.apply_course_change(course);
        CHECK(validate_state(negative, problems).code != ResultCode::Success);

        SystemContext blank_account = make_seed_context();
        Student student = blank_account.students()[0];
        student.student_number = "   ";
        blank_account.apply_student_change(student);
        CHECK(validate_state(blank_account, problems).code !=
              ResultCode::Success);

        SystemContext blank_course = make_seed_context();
        Course course2 = blank_course.courses()[0];
        course2.course_code = " ";
        blank_course.apply_course_change(course2);
        CHECK(validate_state(blank_course, problems).code !=
              ResultCode::Success);

        SystemContext zero_credits = make_seed_context();
        RegistrationRule rule = zero_credits.rule();
        rule.maximum_credits = 0;
        zero_credits.apply_rule_change(rule);
        CHECK(validate_state(zero_credits, problems).code !=
              ResultCode::Success);

        SystemContext bad_date = make_seed_context();
        RegistrationRule rule2 = bad_date.rule();
        rule2.opens_at = "2026-02-30T09:00";
        rule2.closes_at = "2026-03-01T09:00";
        bad_date.apply_rule_change(rule2);
        CHECK(validate_state(bad_date, problems).code != ResultCode::Success);

        SystemContext leap = make_seed_context();
        RegistrationRule rule3 = leap.rule();
        rule3.opens_at = "2024-02-29T09:00";  // 闰日合法
        rule3.closes_at = "2024-03-01T09:00";
        leap.apply_rule_change(rule3);
        CHECK(validate_state(leap, problems).code == ResultCode::Success);

        SystemContext order = make_seed_context();
        RegistrationRule rule4 = order.rule();
        rule4.opens_at = "2026-03-02T09:00";
        rule4.closes_at = "2026-03-01T09:00";
        order.apply_rule_change(rule4);
        CHECK(validate_state(order, problems).code != ResultCode::Success);

        SystemContext half = make_seed_context();
        RegistrationRule rule5 = half.rule();
        rule5.opens_at = "2026-03-01T09:00";
        rule5.closes_at.clear();
        half.apply_rule_change(rule5);
        CHECK(validate_state(half, problems).code != ResultCode::Success);

        // 两个日期都为空 → 手动开关，允许
        SystemContext manual = make_seed_context();
        CHECK(validate_state(manual, problems).code == ResultCode::Success);

        // 专业/部门为空 → 允许（不新增必填）
        SystemContext optional = make_seed_context();
        Student student2 = optional.students()[0];
        student2.major.clear();
        optional.apply_student_change(student2);
        Teacher teacher2 = optional.teachers()[0];
        teacher2.department.clear();
        optional.apply_teacher_change(teacher2);
        CHECK(validate_state(optional, problems).code == ResultCode::Success);
    }

    // ==================================================================
    // 15. 关联规则：课程代码重复、教师排课冲突、Active/Dropped 引用。
    // ==================================================================
    {
        std::vector<std::string> problems;

        SystemContext duplicate_code = make_seed_context();
        Course course = duplicate_code.courses()[1];
        course.course_code = "se101";
        duplicate_code.apply_course_change(course);
        CHECK(validate_state(duplicate_code, problems).code !=
              ResultCode::Success);

        SystemContext teacher_conflict = make_seed_context();
        TeachingClass moved = teacher_conflict.teaching_classes()[1];
        moved.teacher_id = 100;                  // 与 class 10 同一教师
        moved.meetings = {{1, 560, 620, "A102"}};  // 周一与 class 10 重叠
        teacher_conflict.apply_teaching_class_change(moved);
        CHECK(validate_state(teacher_conflict, problems).code !=
              ResultCode::Success);

        SystemContext bad_dropped = make_seed_context();
        bad_dropped.registration().restore_enrollments(
            {{1001, 999, EnrollmentStatus::Dropped}});
        CHECK(validate_state(bad_dropped, problems).code !=
              ResultCode::Success);

        SystemContext bad_active = make_seed_context();
        bad_active.registration().restore_enrollments(
            {{9999, 10, EnrollmentStatus::Active}});
        CHECK(validate_state(bad_active, problems).code !=
              ResultCode::Success);
    }

    // ==================================================================
    // 16. 允许行为对照：Dropped 历史、同班内部重叠、停用账号历史关联。
    // ==================================================================
    {
        std::vector<std::string> problems;

        SystemContext history = make_seed_context();
        history.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Dropped},
             {1001, 11, EnrollmentStatus::Active}});
        CHECK(validate_state(history, problems).code == ResultCode::Success);

        SystemContext internal = make_seed_context();
        TeachingClass meeting = internal.teaching_classes()[2];
        meeting.meetings.push_back({1, 580, 640, "B202"});
        internal.apply_teaching_class_change(meeting);
        CHECK(validate_state(internal, problems).code == ResultCode::Success);

        SystemContext inactive = make_seed_context();
        Student student = inactive.students()[0];
        student.account_active = false;
        inactive.apply_student_change(student);
        Teacher teacher = inactive.teachers()[0];
        teacher.account_active = false;
        inactive.apply_teacher_change(teacher);
        inactive.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active}});
        CHECK(validate_state(inactive, problems).code == ResultCode::Success);
    }

    // ==================================================================
    // 17. 保存/加载失败：返回失败、原状态与原文件不变。
    // ==================================================================
    {
        std::vector<std::string> problems;
        SystemContext bad = make_seed_context();
        TeachingClass item = bad.teaching_classes()[0];
        item.capacity = 0;
        bad.apply_teaching_class_change(item);
        CHECK(validate_state(bad, problems).code != ResultCode::Success);

        const std::string reject_path = dir + "/reject_save.json";
        CHECK(save_state(bad, options_at(reject_path)).code ==
              ResultCode::StorageIntegrityError);
        CHECK(!fs::exists(reject_path));

        // 已存在的合法文件不会被失败的保存改写
        const std::string stable_path = dir + "/stable.json";
        SystemContext good = make_seed_context();
        CHECK(save_state(good, options_at(stable_path)).code ==
              ResultCode::Success);
        const std::string before = read_text(stable_path);
        CHECK(save_state(bad, options_at(stable_path)).code ==
              ResultCode::StorageIntegrityError);
        CHECK(read_text(stable_path) == before);

        // 加载失败不替换原状态
        SystemContext keep = make_seed_context();
        CHECK(keep.registration().enroll(1001, 10).code == ResultCode::Success);
        SystemContext snapshot = keep;
        std::string broken = read_text(stable_path);
        CHECK(replace_once(broken, "\"capacity\": 2", "\"capacity\": 0"));
        write_text(dir + "/bad_capacity.json", broken);
        CHECK(load_state(options_at(dir + "/bad_capacity.json"), keep).code ==
              ResultCode::StorageIntegrityError);
        EXPECT_SAME_STATE(keep, snapshot);
    }

    // ==================================================================
    // 18. 加载路径拒绝非法 JSON（空白账号 / 跨角色重名 / 负学分 / 非法日期）。
    // ==================================================================
    {
        const std::string base_text = read_text(dir + "/valid.json");
        auto expect_load_rejected = [&](const std::string& name,
                                        const std::string& from,
                                        const std::string& to) {
            std::string text = base_text;
            CHECK(replace_once(text, from, to));
            const std::string path = dir + "/" + name + ".json";
            write_text(path, text);
            SystemContext keep = make_seed_context();
            CHECK(keep.registration().enroll(1001, 10).code ==
                  ResultCode::Success);
            SystemContext snapshot = keep;
            CHECK(load_state(options_at(path), keep).code ==
                  ResultCode::StorageIntegrityError);
            EXPECT_SAME_STATE(keep, snapshot);
        };

        expect_load_rejected("bad_account", "\"student_number\": \"S2023001\"",
                             "\"student_number\": \"   \"");
        expect_load_rejected("cross_role",
                             "\"teacher_number\": \"T2015007\"",
                             "\"teacher_number\": \"S2023001\"");
        expect_load_rejected("bad_credits", "\"credits\": 3",
                             "\"credits\": -1");
        expect_load_rejected("bad_date", "\"opens_at\": \"\"",
                             "\"opens_at\": \"2026-02-30T09:00\"");
    }

    // ==================================================================
    // 19. 正常对照：合法状态保存后加载，逐字段一致。
    // ==================================================================
    {
        SystemContext normal = make_seed_context();
        CHECK(normal.registration().enroll(1001, 10).code ==
              ResultCode::Success);
        CHECK(normal.registration().enroll(1002, 20).code ==
              ResultCode::Success);
        Student student = normal.students()[0];
        student.account_active = false;
        normal.apply_student_change(student);
        const std::string path = dir + "/normal.json";
        CHECK(save_state(normal, options_at(path)).code == ResultCode::Success);
        SystemContext loaded = make_seed_context();
        CHECK(load_state(options_at(path), loaded).code == ResultCode::Success);
        EXPECT_SAME_STATE(normal, loaded);
    }

    // ==================================================================
    // 20. 累计学分溢出与边界（int 相加会溢出，必须拒绝）。
    // ==================================================================
    {
        std::vector<std::string> problems;

        // 20.1 2147483647 + 5：int 相加会溢出，long long 累加后必须超限
        SystemContext overflow = make_seed_context();
        Course huge = overflow.courses()[0];
        huge.credits = 2147483647;
        overflow.apply_course_change(huge);
        overflow.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1001, 30, EnrollmentStatus::Active}});
        CHECK(validate_state(overflow, problems).code != ResultCode::Success);
        const std::string overflow_path = dir + "/overflow.json";
        CHECK(save_state(overflow, options_at(overflow_path)).code ==
              ResultCode::StorageIntegrityError);
        CHECK(!fs::exists(overflow_path));

        // 20.2 单门课已经超过上限
        SystemContext single = make_seed_context();
        Course huge2 = single.courses()[0];
        huge2.credits = 2147483647;
        single.apply_course_change(huge2);
        single.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active}});
        CHECK(validate_state(single, problems).code != ResultCode::Success);

        // 20.3 恰好等于上限（3 + 5 = 8）→ 允许
        SystemContext exact = make_seed_context();
        RegistrationRule rule = exact.rule();
        rule.maximum_credits = 8;
        exact.apply_rule_change(rule);
        exact.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1001, 30, EnrollmentStatus::Active}});
        CHECK(validate_state(exact, problems).code == ResultCode::Success);
        const std::string exact_path = dir + "/exact.json";
        CHECK(save_state(exact, options_at(exact_path)).code ==
              ResultCode::Success);
        SystemContext exact_loaded = make_seed_context();
        CHECK(load_state(options_at(exact_path), exact_loaded).code ==
              ResultCode::Success);
        EXPECT_SAME_STATE(exact, exact_loaded);

        // 20.4 超过上限（3 + 5 = 8 > 7）→ 拒绝
        SystemContext over = make_seed_context();
        RegistrationRule rule2 = over.rule();
        rule2.maximum_credits = 7;
        over.apply_rule_change(rule2);
        over.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1001, 30, EnrollmentStatus::Active}});
        CHECK(validate_state(over, problems).code != ResultCode::Success);

        // 20.5 加载路径共用同一校验：巨大值 + 两门课 → 拒绝且原状态不变
        std::string text = read_text(dir + "/valid.json");
        CHECK(replace_once(text, "\"credits\": 3",
                           "\"credits\": 2147483647"));
        CHECK(replace_once(
            text,
            "{\"student_id\": 1001, \"teaching_class_id\": 10, \"status\": "
            "\"active\"}",
            "{\"student_id\": 1001, \"teaching_class_id\": 10, \"status\": "
            "\"active\"}, "
            "{\"student_id\": 1001, \"teaching_class_id\": 30, \"status\": "
            "\"active\"}"));
        const std::string overflow_load = dir + "/overflow_load.json";
        write_text(overflow_load, text);
        SystemContext keep = make_seed_context();
        CHECK(keep.registration().enroll(1001, 10).code ==
              ResultCode::Success);
        SystemContext snapshot = keep;
        CHECK(load_state(options_at(overflow_load), keep).code ==
              ResultCode::StorageIntegrityError);
        EXPECT_SAME_STATE(keep, snapshot);
    }

    // ==================================================================
    // 21. 第 2 周任务：课表 + 时间冲突 + 基础数据保存（持久化后仍一致）。
    // ==================================================================
    {
        SystemContext source = make_seed_context();
        CHECK(source.registration().enroll(1001, 10).code ==
              ResultCode::Success);  // 周一 540-600
        CHECK(source.registration().enroll(1001, 30).code ==
              ResultCode::Success);  // 周三 840-900
        // 与周一 570-630 的班 20 冲突 → 拒绝
        CHECK(source.registration().enroll(1001, 20).code ==
              ResultCode::TimeConflict);

        const std::string path = dir + "/week2.json";
        CHECK(save_state(source, options_at(path)).code == ResultCode::Success);

        SystemContext loaded = make_seed_context();
        CHECK(load_state(options_at(path), loaded).code == ResultCode::Success);
        EXPECT_SAME_STATE(source, loaded);

        // 课表在持久化前后一致
        const UserIdentity student{1001, "S2023001", Role::Student};
        StudentQueryService source_query(source);
        StudentQueryService loaded_query(loaded);
        std::vector<TimetableEntry> before;
        std::vector<TimetableEntry> after;
        CHECK(source_query.view_timetable(student, before).code ==
              ResultCode::Success);
        CHECK(loaded_query.view_timetable(student, after).code ==
              ResultCode::Success);
        CHECK(before.size() == 2);
        CHECK(after.size() == before.size());
        for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
            CHECK(before[i].teaching_class_id == after[i].teaching_class_id);
            CHECK(before[i].course_code == after[i].course_code);
            CHECK(before[i].meeting.day_of_week ==
                  after[i].meeting.day_of_week);
            CHECK(before[i].meeting.start_minute ==
                  after[i].meeting.start_minute);
            CHECK(before[i].meeting.end_minute ==
                  after[i].meeting.end_minute);
            CHECK(before[i].meeting.room == after[i].meeting.room);
        }

        // 重启后时间冲突仍被拒绝
        CHECK(loaded.registration().enroll(1001, 20).code ==
              ResultCode::TimeConflict);

        // 重启后退课，课表同步减少
        CHECK(loaded.registration().drop(1001, 30).code ==
              ResultCode::Success);
        std::vector<TimetableEntry> after_drop;
        CHECK(loaded_query.view_timetable(student, after_drop).code ==
              ResultCode::Success);
        CHECK(after_drop.size() == 1);
        CHECK(after_drop[0].teaching_class_id == 10);
    }

    // ==================================================================
    // 25. password_hash：三角色往返、空哈希保留、类型错误拒绝。
    // ==================================================================
    {
        SystemContext state = make_seed_context();
        const std::string student_hash =
            "$argon2id$v=19$m=65536,t=3,p=4$c3R1ZGVudA$YWJjMTIz";
        const std::string teacher_hash =
            "$argon2id$v=19$m=65536,t=3,p=4$dGVhY2hlcg$ZGVmNDU2";
        const std::string admin_hash =
            "$argon2id$v=19$m=65536,t=3,p=4$YWRtaW4$Z2hpNzg5";

        Student alice = state.students()[0];
        alice.password_hash = student_hash;
        state.apply_student_change(alice);
        Teacher wang = state.teachers()[0];
        wang.password_hash = teacher_hash;
        state.apply_teacher_change(wang);
        Administrator admin = state.administrators()[0];
        admin.password_hash = admin_hash;
        state.apply_administrator_change(admin);
        // 学生 1002 保持空哈希，作为“尚未设置密码”的对照

        const std::string path = dir + "/password.json";
        CHECK(save_state(state, options_at(path)).code == ResultCode::Success);
        const std::string text = read_text(path);
        CHECK(text.find("\"schema_version\": 2") != std::string::npos);
        CHECK(text.find(student_hash) != std::string::npos);
        CHECK(text.find(teacher_hash) != std::string::npos);
        CHECK(text.find(admin_hash) != std::string::npos);

        SystemContext loaded = make_seed_context();
        CHECK(load_state(options_at(path), loaded).code == ResultCode::Success);
        CHECK(loaded.students()[0].password_hash == student_hash);
        CHECK(loaded.teachers()[0].password_hash == teacher_hash);
        CHECK(loaded.administrators()[0].password_hash == admin_hash);
        CHECK(loaded.students()[1].password_hash.empty());

        // 类型错误：加载失败且当前内存状态不变
        std::string bad = read_text(path);
        CHECK(replace_once(bad, "\"password_hash\": \"\"",
                           "\"password_hash\": 123"));
        const std::string bad_path = dir + "/password_bad.json";
        write_text(bad_path, bad);
        SystemContext keep = make_seed_context();
        SystemContext snapshot = keep;
        CHECK(load_state(options_at(bad_path), keep).code ==
              ResultCode::StorageIntegrityError);
        EXPECT_SAME_STATE(keep, snapshot);
    }

    // ==================================================================
    // 26. v1 升级：全部人员无 password_hash，加载→再保存→v1 .bak 恢复。
    // ==================================================================
    {
        // 先造一份带选课记录和任课关系的 v2 文件
        SystemContext source = make_seed_context();
        source.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active},
             {1002, 30, EnrollmentStatus::Dropped}});
        const std::string v2_path = dir + "/upgrade_source.json";
        CHECK(save_state(source, options_at(v2_path)).code ==
              ResultCode::Success);

        // 转成 v1：版本改 1，删掉所有 password_hash 字段
        std::string v1_text = read_text(v2_path);
        CHECK(replace_once(v1_text, "\"schema_version\": 2",
                           "\"schema_version\": 1"));
        v1_text = strip_password_hash(v1_text);
        CHECK(v1_text.find("\"password_hash\"") == std::string::npos);
        const std::string v1_path = dir + "/upgrade_v1.json";
        write_text(v1_path, v1_text);

        // 加载 v1：三类账号哈希都为空，选课与任课关系保留
        SystemContext upgraded = make_seed_context();
        CHECK(load_state(options_at(v1_path), upgraded).code ==
              ResultCode::Success);
        CHECK(upgraded.students()[0].password_hash.empty());
        CHECK(upgraded.teachers()[0].password_hash.empty());
        CHECK(upgraded.administrators()[0].password_hash.empty());
        CHECK(upgraded.all_enrollments().size() == 2);
        TeachingClass class10{};
        CHECK(upgraded.find_teaching_class(10, class10));
        CHECK(class10.teacher_id == 100);
        TeachingClass class30{};
        CHECK(upgraded.find_teaching_class(30, class30));
        CHECK(class30.teacher_id == 300);

        // 再保存：自动升级为 v2，password_hash 字段齐备
        const std::string upgraded_path = dir + "/upgrade_v2.json";
        CHECK(save_state(upgraded, options_at(upgraded_path)).code ==
              ResultCode::Success);
        const std::string upgraded_text = read_text(upgraded_path);
        CHECK(upgraded_text.find("\"schema_version\": 2") !=
              std::string::npos);
        CHECK(upgraded_text.find("\"password_hash\"") != std::string::npos);

        // 从 v1 的 .bak 恢复：主文件写成 v2，.bak 原样保留
        const std::string restore_path = dir + "/upgrade_restore.json";
        const std::string backup_path = restore_path + ".bak";
        write_text(backup_path, v1_text);
        SystemContext restored = make_seed_context();
        CHECK(restore_state(options_at(restore_path), restored).code ==
              ResultCode::Success);
        CHECK(restored.students()[0].password_hash.empty());
        CHECK(restored.teachers()[0].password_hash.empty());
        CHECK(restored.administrators()[0].password_hash.empty());
        CHECK(restored.all_enrollments().size() == 2);
        CHECK(read_text(restore_path).find("\"schema_version\": 2") !=
              std::string::npos);
        CHECK(read_text(backup_path) == v1_text);  // 恢复不覆盖 .bak
    }

    if (g_failures != 0) {
        std::cerr << "FAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "All persistence tests passed\n";
    return 0;
}
