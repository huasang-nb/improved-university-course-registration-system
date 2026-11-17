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

    if (g_failures != 0) {
        std::cerr << "FAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "All persistence tests passed\n";
    return 0;
}
