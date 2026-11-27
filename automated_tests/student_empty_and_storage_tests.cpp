// 第 4 周：空结果、空课表与存储一致性测试（Zhang Yikai）
//
// 覆盖：
//   - 搜索无匹配 → Success + 空；课程详情/余量找不到 → 失败且清空输出；
//   - 新学生没有选课 → 空课表；退课后课表变空；
//   - 空状态保存/加载一致；
//   - 选课/退课后重启，余量、课表、Dropped 历史一致；
//   - 同一状态两次落盘内容一致（确定性）；
//   - 损坏文件被拒绝且原状态不变；无备份时恢复返回 StorageBackupMissing。
// 关键检查用 CHECK（不依赖 assert），NDEBUG 下同样执行。

#include "student_interface.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

StorageOptions options_at(const std::string& path) {
    StorageOptions options;
    options.path = path;
    return options;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;

    std::error_code ec;
    const std::string dir = "build/student_empty_storage_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const StorageOptions options = options_at(dir + "/state.json");
    SystemContext context = make_seed_context();
    CHECK(initialize_storage(options, context).code == ResultCode::Success);

    StudentInterface ui(context, options);
    CHECK(ui.login("S2023001").code == ResultCode::Success);

    // ---- 1. 空结果 ----
    std::vector<CourseView> found;
    CHECK(ui.search("no-such-course-xyz", found).code == ResultCode::Success);
    CHECK(found.empty());

    // 正常对照：有匹配时不能为空
    CHECK(ui.search("SE", found).code == ResultCode::Success);
    CHECK(!found.empty());

    CourseView detail{};
    CHECK(ui.course_detail(9999, detail).code == ResultCode::RecordNotFound);
    CHECK(detail.course.course_id == 0);
    CHECK(detail.teaching_classes.empty());

    SeatsView seats{};
    CHECK(ui.available_seats(9999, seats).code == ResultCode::RecordNotFound);
    CHECK(seats.teaching_class_id == 0);

    // ---- 2. 空课表：新学生没有选课 ----
    StudentInterface bob(context, options);
    CHECK(bob.login("S2023002").code == ResultCode::Success);
    std::vector<TimetableEntry> table;
    CHECK(bob.timetable(table).code == ResultCode::Success);
    CHECK(table.empty());

    // ---- 3. 空状态保存/加载一致 ----
    const StorageOptions empty_options = options_at(dir + "/empty.json");
    SystemContext empty_state = make_seed_context();
    CHECK(save_state(empty_state, empty_options).code == ResultCode::Success);
    SystemContext empty_loaded = make_seed_context();
    CHECK(load_state(empty_options, empty_loaded).code == ResultCode::Success);
    CHECK(empty_loaded.all_enrollments().empty());
    CHECK(empty_loaded.remaining_seats(10) == 2);
    CHECK(empty_loaded.remaining_seats(20) == 2);
    CHECK(empty_loaded.remaining_seats(30) == 2);

    // ---- 4. 选课 → 重启：余量、课表一致 ----
    CHECK(ui.enroll(10).code == ResultCode::Success);
    SystemContext restarted = make_seed_context();
    CHECK(load_state(options, restarted).code == ResultCode::Success);
    CHECK(restarted.remaining_seats(10) == 1);
    StudentInterface restarted_ui(restarted, options);
    CHECK(restarted_ui.login("S2023001").code == ResultCode::Success);
    CHECK(restarted_ui.timetable(table).code == ResultCode::Success);
    CHECK(table.size() == 1);
    CHECK(table[0].teaching_class_id == 10);
    CHECK(table[0].meeting.day_of_week == 1);

    // ---- 5. 退课 → 重启：课表空、余量恢复、Dropped 历史保留 ----
    CHECK(ui.drop(10).code == ResultCode::Success);
    SystemContext after_drop = make_seed_context();
    CHECK(load_state(options, after_drop).code == ResultCode::Success);
    CHECK(after_drop.remaining_seats(10) == 2);
    StudentInterface drop_ui(after_drop, options);
    CHECK(drop_ui.login("S2023001").code == ResultCode::Success);
    CHECK(drop_ui.timetable(table).code == ResultCode::Success);
    CHECK(table.empty());
    int active = 0;
    int dropped = 0;
    CHECK(drop_ui.my_status(active, dropped).code == ResultCode::Success);
    CHECK(active == 0);
    CHECK(dropped == 1);

    // ---- 6. 确定性：同一状态两次落盘内容一致 ----
    const StorageOptions first = options_at(dir + "/same1.json");
    const StorageOptions second = options_at(dir + "/same2.json");
    SystemContext stable = make_seed_context();
    CHECK(save_state(stable, first).code == ResultCode::Success);
    SystemContext reloaded = make_seed_context();
    CHECK(load_state(first, reloaded).code == ResultCode::Success);
    CHECK(save_state(reloaded, second).code == ResultCode::Success);
    CHECK(!read_text(first.path).empty());
    CHECK(read_text(first.path) == read_text(second.path));

    // ---- 7. 损坏文件：拒绝且原状态不变 ----
    const std::string corrupt = dir + "/corrupt.json";
    write_text(corrupt, "{ not json");
    SystemContext keep = make_seed_context();
    CHECK(keep.registration().enroll(1001, 10).code == ResultCode::Success);
    const size_t before = keep.all_enrollments().size();
    CHECK(load_state(options_at(corrupt), keep).code ==
          ResultCode::StorageParseError);
    CHECK(keep.all_enrollments().size() == before);

    // ---- 8. 无备份：恢复返回 StorageBackupMissing ----
    const StorageOptions fresh = options_at(dir + "/fresh.json");
    SystemContext fresh_state = make_seed_context();
    CHECK(save_state(fresh_state, fresh).code == ResultCode::Success);
    SystemContext restore_target = make_seed_context();
    CHECK(restore_state(fresh, restore_target).code ==
          ResultCode::StorageBackupMissing);

    if (g_failures != 0) {
        std::cerr << "FAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "All empty result and storage consistency tests passed\n";
    return 0;
}
