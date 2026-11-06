// 教师与管理员模块的命令行演示（Zhu Ziliang）
//
// 目的：
//   1. 直观展示 UC09 / UC13 / UC14 / UC15 / UC16 的成功与失败结果；
//   2. 复跑团队说明第 5 节要求每周共同执行的场景：
//      管理员把容量设为 1 -> 学生 A 选课成功 -> 余量 0、A 课表出现课程、
//      教师人数 1 -> 学生 B 满员被拒 -> A 退课后余量恢复、课表与人数同步。
//
// 命令行程序只用于阶段验证；最终系统仍需接入 Web 页面或 HTTP 接口。

#include <iostream>
#include <string>
#include <vector>

#include "course_registration_business_rules.hpp"
#include "teacher_and_administration_services.hpp"

using namespace course_registration;

namespace {

void section(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

void show(const std::string& action, const OperationResult& result) {
    std::cout << "  " << action << "\n    -> ["
              << static_cast<int>(result.code) << "] " << result.message
              << '\n';
}

TeachingClass make_class(int id, int course_id, int teacher_id, int capacity,
                         int day, int start, int end, const std::string& room) {
    return TeachingClass{id, course_id, teacher_id, capacity,
                         {{day, start, end, room}}};
}

SystemContext make_demo_context() {
    return SystemContext(
        {{1001, "S2023001", "Alice Chen", "Software Engineering", true},
         {1002, "S2023002", "Bob Liu", "Software Engineering", true}},
        {{100, "T2015007", "Dr. Wang", "Software Engineering", true},
         {300, "T2018003", "Dr. Zhao", "Computer Science", true}},
        {{900, "A2020001", "Admin Sun", true}},
        {{1, "SE101", "Software Engineering", 3},
         {2, "MA101", "Mathematics", 4}},
        {make_class(10, 1, 100, 2, 1, 540, 600, "A101"),
         make_class(11, 1, 300, 2, 2, 540, 600, "A102"),
         make_class(20, 2, 300, 2, 3, 840, 900, "C301")},
        RegistrationRule{true, 18, "", ""});
}

}  // namespace

int main() {
    SystemContext context = make_demo_context();
    const UserIdentity admin{900, "A2020001", Role::Administrator};
    const UserIdentity alice{1001, "S2023001", Role::Student};
    const UserIdentity dr_wang{100, "T2015007", Role::Teacher};
    const UserIdentity dr_zhao{300, "T2018003", Role::Teacher};

    AdministratorService administration(context);
    TeacherService teacher_service(context);

    section("UC09 View Assigned Teaching Classes");
    std::vector<TeachingClassSummary> assigned;
    show("Dr. Wang lists own teaching classes",
         teacher_service.assigned_teaching_classes(dr_wang, assigned));
    for (const TeachingClassSummary& summary : assigned) {
        std::cout << "     class " << summary.teaching_class_id << "  "
                  << summary.course_code << " " << summary.course_name
                  << "  teacher " << summary.teacher_name << "  capacity "
                  << summary.capacity << "  active "
                  << summary.active_enrollments << "  remaining "
                  << summary.remaining_seats << '\n';
    }
    TeachingClassSummary others_class{};
    show("Dr. Wang tries to read Dr. Zhao's class 11",
         teacher_service.class_summary(dr_wang, 11, others_class));

    section("UC14 Set Teaching Class Capacity (weekly scenario, step 1)");
    show("Admin sets capacity of class 10 to 1",
         administration.set_teaching_class_capacity(admin, 10, 1));
    std::cout << "     remaining seats of class 10: "
              << context.remaining_seats(10) << '\n';

    section("Weekly scenario, step 2: student A enrolls");
    show("Alice (1001) enrolls in class 10",
         context.registration().enroll(1001, 10));
    std::cout << "     remaining seats of class 10: "
              << context.remaining_seats(10) << '\n';

    section("Weekly scenario, step 3: A's timetable and teacher's count");
    std::cout << "     Alice active enrollments: "
              << context.active_enrollments(1001).size() << '\n';
    ClassRoster roster{};
    show("Dr. Wang reads the roster of class 10",
         teacher_service.class_roster(dr_wang, 10, roster));
    std::cout << "     active " << roster.active_enrollments << "  capacity "
              << roster.capacity << "  remaining " << roster.remaining_seats
              << '\n';
    for (const RosterEntry& entry : roster.students) {
        std::cout << "     - " << entry.student_number << " " << entry.name
                  << '\n';
    }

    section("Weekly scenario, step 4: student B is rejected");
    show("Bob (1002) enrolls in the full class 10",
         context.registration().enroll(1002, 10));
    std::cout << "     active count after rejection: "
              << context.enrollment_count(10) << '\n';

    section("Weekly scenario, step 5: A drops the class");
    show("Alice (1001) drops class 10", context.registration().drop(1001, 10));
    std::cout << "     remaining seats of class 10: "
              << context.remaining_seats(10) << '\n';
    show("Dr. Wang reads the roster again",
         teacher_service.class_roster(dr_wang, 10, roster));
    std::cout << "     active " << roster.active_enrollments << "  remaining "
              << roster.remaining_seats << '\n';

    section("UC13 Manage Courses and Teaching Classes");
    show("Admin creates course PH101",
         administration.create_course(admin, {4, "PH101", "Physics", 3}));
    show("Admin creates duplicated course code PH101",
         administration.create_course(admin, {5, "PH101", "Physics II", 3}));
    show("Admin creates teaching class 40 of PH101",
         administration.create_teaching_class(
             admin, make_class(40, 4, 100, 2, 4, 600, 660, "A201")));
    show("Admin removes course PH101 while class 40 still exists",
         administration.remove_course(admin, 4));
    show("Admin removes teaching class 40",
         administration.remove_teaching_class(admin, 40));
    show("Admin removes course PH101", administration.remove_course(admin, 4));

    section("UC15 Set Registration Period");
    context.set_now_for_tests("2026-11-03T10:00");
    show("Admin sets the period to 2026-11-02T08:00 -> 2026-11-13T18:00",
         administration.set_registration_period(admin, "2026-11-02T08:00",
                                                "2026-11-13T18:00"));
    std::cout << "     registration_open = "
              << (context.rule().registration_open ? "true" : "false") << '\n';

    std::cout << "\n     defect-001: the window is checked on every request, no refresh call:\n";
    context.set_now_for_tests("2026-11-13T18:30");
    std::cout << "     now = 2026-11-13T18:30, stored flag is still "
              << (context.rule().registration_open ? "true" : "false")
              << ", refresh_registration_window() not called\n";
    show("Alice tries to enroll after the window closed",
         context.registration().enroll(1001, 10));

    context.set_now_for_tests("2026-11-04T09:00");
    show("Alice tries again inside the window",
         context.registration().enroll(1001, 10));

    show("Admin sets an inverted period",
         administration.set_registration_period(admin, "2026-11-13T18:00",
                                                "2026-11-02T08:00"));
    show("Admin closes registration",
         administration.close_registration(admin));
    show("Alice tries to enroll while registration is closed",
         context.registration().enroll(1001, 10));

    section("UC16 Manage Student / Teacher Information");
    show("Admin creates student S2023003",
         administration.create_student(
             admin, {1003, "S2023003", "Cara Wu", "Mathematics", true}));
    show("Admin creates a duplicated student number",
         administration.create_student(
             admin, {1004, "S2023003", "Cara Duplicate", "Mathematics", true}));
    show("Admin tries to remove Dr. Zhao who still teaches classes",
         administration.remove_teacher(admin, 300));

    section("Data integrity check");
    std::vector<std::string> problems;
    show("Admin runs the integrity check",
         administration.check_data_integrity(admin, problems));
    for (const std::string& problem : problems) {
        std::cout << "     ! " << problem << '\n';
    }

    return 0;
}
