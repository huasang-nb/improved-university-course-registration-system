// 学生端会话接口测试（Zhang Yikai，第 3 周）
//
// 覆盖：未登录拒绝、未知账号、停用账号、去空白+小写的登录、搜索/详情/余量/
// 课表、选课写盘、时间冲突、重启后课表仍在、退课保留历史、注销后拒绝。
// 关键检查用 CHECK（不依赖 assert），NDEBUG 下同样执行。

#include "student_interface.hpp"

#include <filesystem>
#include <iostream>
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

}  // namespace

int main() {
    namespace fs = std::filesystem;

    std::error_code ec;
    const std::string dir = "build/student_interface_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    StorageOptions options;
    options.path = dir + "/state.json";

    SystemContext context = make_seed_context();
    CHECK(initialize_storage(options, context).code == ResultCode::Success);

    StudentInterface ui(context, options);
    CHECK(ui.logged_in() == false);

    std::vector<CourseView> courses;
    CHECK(ui.search("SE", courses).code == ResultCode::PermissionDenied);
    CHECK(courses.empty());

    CHECK(ui.login("S9999999").code == ResultCode::RecordNotFound);
    CHECK(ui.logged_in() == false);

    // 停用账号不能登录
    Student bob = context.students()[1];
    bob.account_active = false;
    context.apply_student_change(bob);
    CHECK(ui.login("S2023002").code == ResultCode::PermissionDenied);
    bob.account_active = true;
    context.apply_student_change(bob);

    // 去首尾空白 + 大小写
    CHECK(ui.login("  s2023001  ").code == ResultCode::Success);
    CHECK(ui.logged_in() == true);
    CHECK(ui.identity().user_id == 1001);
    CHECK(ui.identity().role == Role::Student);

    CHECK(ui.search("SE", courses).code == ResultCode::Success);
    CHECK(!courses.empty());

    CourseView detail{};
    CHECK(ui.course_detail(1, detail).code == ResultCode::Success);
    CHECK(detail.course.course_id == 1);

    std::vector<TimetableEntry> table;
    CHECK(ui.timetable(table).code == ResultCode::Success);
    CHECK(table.empty());

    SeatsView seats{};
    CHECK(ui.available_seats(10, seats).code == ResultCode::Success);
    CHECK(seats.remaining_seats == 2);

    CHECK(ui.enroll(10).code == ResultCode::Success);
    CHECK(ui.available_seats(10, seats).code == ResultCode::Success);
    CHECK(seats.remaining_seats == 1);

    CHECK(ui.timetable(table).code == ResultCode::Success);
    CHECK(table.size() == 1);
    CHECK(table[0].teaching_class_id == 10);

    int active = 0;
    int dropped = 0;
    CHECK(ui.my_status(active, dropped).code == ResultCode::Success);
    CHECK(active == 1);
    CHECK(dropped == 0);

    // 时间冲突：班 20 与班 10 周一重叠
    CHECK(ui.enroll(20).code == ResultCode::TimeConflict);
    CHECK(ui.my_status(active, dropped).code == ResultCode::Success);
    CHECK(active == 1);

    // 重启：新进程/新上下文从同一文件加载，课表仍在
    SystemContext restarted = make_seed_context();
    CHECK(load_state(options, restarted).code == ResultCode::Success);
    StudentInterface restarted_ui(restarted, options);
    CHECK(restarted_ui.login("S2023001").code == ResultCode::Success);
    CHECK(restarted_ui.timetable(table).code == ResultCode::Success);
    CHECK(table.size() == 1);
    CHECK(table[0].teaching_class_id == 10);

    // 退课：课表减少，历史保留
    CHECK(ui.drop(10).code == ResultCode::Success);
    CHECK(ui.timetable(table).code == ResultCode::Success);
    CHECK(table.empty());
    CHECK(ui.my_status(active, dropped).code == ResultCode::Success);
    CHECK(active == 0);
    CHECK(dropped == 1);

    // 注销后不能选课
    ui.logout();
    CHECK(ui.logged_in() == false);
    CHECK(ui.enroll(10).code == ResultCode::PermissionDenied);

    if (g_failures != 0) {
        std::cerr << "FAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "All student interface tests passed\n";
    return 0;
}
