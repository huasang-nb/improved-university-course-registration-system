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

    // ==================================================================
    // 22. 可选列表隐藏停用教师的教学班；已有课表与详情不受影响。
    // ==================================================================
    {
        SystemContext state = make_seed_context();
        Teacher wang = state.teachers()[0];  // Dr. Wang 教 class 10（课程 1）
        wang.account_active = false;
        state.apply_teacher_change(wang);
        state.registration().restore_enrollments(
            {{1001, 10, EnrollmentStatus::Active}});

        StudentInterface scoped(state, options);
        CHECK(scoped.login("S2023001").code == ResultCode::Success);

        // 课程 1 还有 class 11（teacher 300 启用），所以仍出现，但不能带 class 10
        std::vector<CourseView> found;
        CHECK(scoped.search("SE", found).code == ResultCode::Success);
        bool has_course1 = false;
        bool has_class10 = false;
        for (const CourseView& view : found) {
            if (view.course.course_id != 1) {
                continue;
            }
            has_course1 = true;
            for (const TeachingClassView& item : view.teaching_classes) {
                if (item.teaching_class_id == 10) {
                    has_class10 = true;
                }
            }
        }
        CHECK(has_course1);
        CHECK(!has_class10);

        // 教师姓名只在实际显示的班里匹配：停用 Wang 后搜 "Wang" 应为空，
        // 不能靠已隐藏的班命中、再把 Zhao 的班带出来。
        CHECK(scoped.search("Wang", found).code == ResultCode::Success);
        CHECK(found.empty());

        // 课程 2 只有 class 20（teacher 200）→ 停用后整门从可选列表消失
        Teacher li = state.teachers()[1];
        li.account_active = false;
        state.apply_teacher_change(li);
        CHECK(scoped.search("MA", found).code == ResultCode::Success);
        CHECK(found.empty());

        // 已有课表仍显示 class 10（教师停用不影响已有记录）
        std::vector<TimetableEntry> table;
        CHECK(scoped.timetable(table).code == ResultCode::Success);
        CHECK(table.size() == 1);
        CHECK(table[0].teaching_class_id == 10);

        // 详情保持完整（不隐藏停用教师的班）
        CourseView detail{};
        CHECK(scoped.course_detail(1, detail).code == ResultCode::Success);
        bool detail_has_class10 = false;
        for (const TeachingClassView& item : detail.teaching_classes) {
            if (item.teaching_class_id == 10) {
                detail_has_class10 = true;
            }
        }
        CHECK(detail_has_class10);
    }

    // ==================================================================
    // 23. 模拟登录：切换失败回到未登录；登录后被停用则读取也拒绝。
    // ==================================================================
    {
        SystemContext state = make_seed_context();
        StudentInterface ui3(state, options);
        CHECK(ui3.login("S2023001").code == ResultCode::Success);
        CHECK(ui3.logged_in() == true);

        // 切换到不存在的账号：失败并回到未登录
        CHECK(ui3.login("S9999999").code == ResultCode::RecordNotFound);
        CHECK(ui3.logged_in() == false);
        std::vector<TimetableEntry> table;
        CHECK(ui3.timetable(table).code == ResultCode::PermissionDenied);
        CHECK(ui3.enroll(10).code == ResultCode::PermissionDenied);

        // 登录后被停用：课表、个人状态、搜索、选课都拒绝
        CHECK(ui3.login("S2023001").code == ResultCode::Success);
        Student student = state.students()[0];
        student.account_active = false;
        state.apply_student_change(student);
        CHECK(ui3.timetable(table).code == ResultCode::PermissionDenied);
        int active = 0;
        int dropped = 0;
        CHECK(ui3.my_status(active, dropped).code ==
              ResultCode::PermissionDenied);
        std::vector<CourseView> found;
        CHECK(ui3.search("SE", found).code == ResultCode::PermissionDenied);
        CHECK(ui3.enroll(10).code == ResultCode::PermissionDenied);
    }

    // ==================================================================
    // 24. 管理员改学号后，旧会话的读取与选退课都要拒绝。
    // ==================================================================
    {
        SystemContext state = make_seed_context();
        StudentInterface ui4(state, options);
        CHECK(ui4.login("S2023001").code == ResultCode::Success);

        AdministratorService admin(state);
        const UserIdentity admin_user{900, "A2020001", Role::Administrator};
        Student student = state.students()[0];
        student.student_number = "S2023099";
        CHECK(admin.update_student(admin_user, student).code ==
              ResultCode::Success);

        // 旧身份：课表、个人状态、选课都拒绝
        std::vector<TimetableEntry> table;
        CHECK(ui4.timetable(table).code == ResultCode::PermissionDenied);
        int active = 0;
        int dropped = 0;
        CHECK(ui4.my_status(active, dropped).code ==
              ResultCode::PermissionDenied);
        CHECK(ui4.enroll(10).code == ResultCode::PermissionDenied);

        // 旧学号不能再登录；新学号可以
        CHECK(ui4.login("S2023001").code == ResultCode::RecordNotFound);
        CHECK(ui4.login("S2023099").code == ResultCode::Success);
        CHECK(ui4.timetable(table).code == ResultCode::Success);
    }

    if (g_failures != 0) {
        std::cerr << "FAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "All student interface tests passed\n";
    return 0;
}
