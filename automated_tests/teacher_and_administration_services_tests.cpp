// 教师与管理员模块测试（Zhu Ziliang）
//
// 覆盖 UC09、UC13、UC14、UC15、UC16 与每周共同场景，并检查：
//   1. 失败操作返回明确原因且不改动数据；
//   2. 教师只能读本人教学班；
//   3. 名单/人数/余量三者与选课记录一致。
//
// 编译与运行见 README.md；测试失败时进程返回 1。

#include "teacher_and_administration_services.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace course_registration;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool passed, const std::string& expression, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::cout << "FAIL line " << line << ": " << expression << '\n';
    }
}

void check_result(const OperationResult& actual, ResultCode expected,
                  const std::string& expression, int line) {
    ++g_checks;
    if (actual.code != expected) {
        ++g_failures;
        std::cout << "FAIL line " << line << ": " << expression
                  << " -> expected ResultCode " << static_cast<int>(expected)
                  << ", got " << static_cast<int>(actual.code) << " \""
                  << actual.message << "\"\n";
    }
}

void check_contains(const OperationResult& actual, const std::string& needle,
                    const std::string& expression, int line) {
    ++g_checks;
    if (actual.message.find(needle) == std::string::npos) {
        ++g_failures;
        std::cout << "FAIL line " << line << ": " << expression
                  << " -> message \"" << actual.message
                  << "\" does not contain \"" << needle << "\"\n";
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)
#define CHECK_CODE(call, expected) \
    check_result((call), (expected), #call, __LINE__)
#define CHECK_MESSAGE(call, needle) \
    check_contains((call), (needle), #call, __LINE__)

// ---------------------------------------------------------------------------
// 共用示例数据：两名教师各带教学班，第三个教学班与第一个在同一时段。
// ---------------------------------------------------------------------------

TeachingClass make_class(int id, int course_id, int teacher_id, int capacity,
                         int day, int start, int end, const std::string& room) {
    return TeachingClass{id, course_id, teacher_id, capacity,
                         {{day, start, end, room}}};
}

std::vector<Student> make_students() {
    return {{1001, "S2023001", "Alice Chen", "Software Engineering", true},
            {1002, "S2023002", "Bob Liu", "Software Engineering", true},
            {1003, "S2023003", "Cara Wu", "Mathematics", true}};
}

std::vector<Teacher> make_teachers() {
    return {{100, "T2015007", "Dr. Wang", "Software Engineering", true},
            {200, "T2016011", "Dr. Li", "Mathematics", true},
            {300, "T2018003", "Dr. Zhao", "Computer Science", true}};
}

std::vector<Administrator> make_administrators() {
    return {{900, "A2020001", "Admin Sun", true}};
}

std::vector<Course> make_courses() {
    return {{1, "SE101", "Software Engineering", 3},
            {2, "MA101", "Mathematics", 4},
            {3, "CS101", "Programming", 5}};
}

std::vector<TeachingClass> make_teaching_classes() {
    return {make_class(10, 1, 100, 2, 1, 540, 600, "A101"),
            make_class(11, 1, 300, 2, 2, 540, 600, "A102"),
            make_class(20, 2, 200, 2, 1, 570, 630, "B201"),
            make_class(30, 3, 300, 2, 3, 840, 900, "C301")};
}

struct Fixture {
    SystemContext context;
    UserIdentity admin;
    UserIdentity alice;
    UserIdentity bob;
    UserIdentity cara;
    UserIdentity dr_wang;
    UserIdentity dr_li;
    UserIdentity dr_zhao;

    // 可以指定初始规则：缺陷-010 需要"本轮尚未开放"的起点。
    explicit Fixture(RegistrationRule rule = RegistrationRule{true, 18, "", ""})
        : context(make_students(), make_teachers(), make_administrators(),
                  make_courses(), make_teaching_classes(), rule),
          admin{900, "A2020001", Role::Administrator},
          alice{1001, "S2023001", Role::Student},
          bob{1002, "S2023002", Role::Student},
          cara{1003, "S2023003", Role::Student},
          dr_wang{100, "T2015007", Role::Teacher},
          dr_li{200, "T2016011", Role::Teacher},
          dr_zhao{300, "T2018003", Role::Teacher} {}
};

// ---------------------------------------------------------------------------
// UC09 View Assigned Teaching Classes
// ---------------------------------------------------------------------------

void test_uc09_teacher_reads_only_own_classes() {
    Fixture fixture;
    TeacherService teacher_service(fixture.context);

    std::vector<TeachingClassSummary> classes;
    CHECK_CODE(teacher_service.assigned_teaching_classes(fixture.dr_wang,
                                                         classes),
               ResultCode::Success);
    CHECK(classes.size() == 1);
    if (classes.size() == 1) {
        CHECK(classes[0].teaching_class_id == 10);
        CHECK(classes[0].course_code == "SE101");
        CHECK(classes[0].teacher_name == "Dr. Wang");
        CHECK(classes[0].capacity == 2);
        CHECK(classes[0].remaining_seats == 2);
    }

    // 另一位教师只看到自己的两个教学班。
    CHECK_CODE(teacher_service.assigned_teaching_classes(fixture.dr_zhao,
                                                         classes),
               ResultCode::Success);
    CHECK(classes.size() == 2);
    if (classes.size() == 2) {
        CHECK(classes[0].teaching_class_id == 11);
        CHECK(classes[1].teaching_class_id == 30);
    }

    // 学生不能使用教师入口。
    CHECK_CODE(teacher_service.assigned_teaching_classes(fixture.alice,
                                                         classes),
               ResultCode::PermissionDenied);

    // 管理员可以读取全部教学班，用于核对。
    CHECK_CODE(teacher_service.assigned_teaching_classes(fixture.admin,
                                                         classes),
               ResultCode::Success);
    CHECK(classes.size() == 4);
}

void test_uc09_cross_teacher_access_is_denied() {
    Fixture fixture;
    TeacherService teacher_service(fixture.context);

    TeachingClassSummary summary{};
    ClassRoster roster{};
    CHECK_CODE(teacher_service.class_summary(fixture.dr_wang, 11, summary),
               ResultCode::PermissionDenied);
    CHECK_CODE(teacher_service.class_roster(fixture.dr_wang, 11, roster),
               ResultCode::PermissionDenied);
    CHECK_CODE(teacher_service.class_summary(fixture.dr_wang, 999, summary),
               ResultCode::TeachingClassNotFound);

    // 越权失败后不得泄露教学班内容。
    CHECK(summary.teaching_class_id == 0);
}

void test_uc09_roster_follows_active_enrollments() {
    Fixture fixture;
    TeacherService teacher_service(fixture.context);

    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    CHECK_CODE(fixture.context.registration().enroll(1002, 10),
               ResultCode::Success);

    ClassRoster roster{};
    CHECK_CODE(teacher_service.class_roster(fixture.dr_wang, 10, roster),
               ResultCode::Success);
    CHECK(roster.active_enrollments == 2);
    CHECK(roster.remaining_seats == 0);
    CHECK(roster.students.size() == 2);
    if (roster.students.size() == 2) {
        // 名单按学号排序，且不含已退课学生。
        CHECK(roster.students[0].student_number == "S2023001");
        CHECK(roster.students[1].student_number == "S2023002");
    }

    CHECK_CODE(fixture.context.registration().drop(1002, 10),
               ResultCode::Success);
    CHECK_CODE(teacher_service.class_roster(fixture.dr_wang, 10, roster),
               ResultCode::Success);
    CHECK(roster.active_enrollments == 1);
    CHECK(roster.remaining_seats == 1);
    CHECK(roster.students.size() == 1);

    // 未登记学生的选课记录不进名单，但人数仍按有效记录计算，
    // 由一致性检查报出，避免教师看到不存在的人。
    CHECK_CODE(fixture.context.registration().enroll(9999, 10),
               ResultCode::Success);
    CHECK_CODE(teacher_service.class_roster(fixture.dr_wang, 10, roster),
               ResultCode::Success);
    CHECK(roster.students.size() == 1);
    CHECK(roster.active_enrollments == 2);
    CHECK(roster.remaining_seats == 0);
}

// ---------------------------------------------------------------------------
// UC13 Manage Courses and Teaching Classes
// ---------------------------------------------------------------------------

void test_uc13_course_validation() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    CHECK_CODE(administration.create_course(fixture.admin,
                                            {4, "PH101", "Physics", 3}),
               ResultCode::Success);
    CHECK(fixture.context.courses().size() == 4);

    // 编号与课程代码都不能重复。
    CHECK_CODE(administration.create_course(fixture.admin,
                                            {5, "PH101", "Physics II", 3}),
               ResultCode::DuplicateIdentifier);
    CHECK_CODE(administration.create_course(fixture.admin,
                                            {4, "PH102", "Physics III", 3}),
               ResultCode::DuplicateIdentifier);
    // 空名称、非正学分、非正编号都被拒绝。
    CHECK_CODE(administration.create_course(fixture.admin,
                                            {6, "PH103", "  ", 3}),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.create_course(fixture.admin,
                                            {7, "PH104", "Physics IV", 0}),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.create_course(fixture.admin,
                                            {0, "PH105", "Physics V", 3}),
               ResultCode::InvalidInput);

    // 教师不能维护课程目录。
    CHECK_CODE(administration.create_course(fixture.dr_wang,
                                            {8, "PH106", "Physics VI", 3}),
               ResultCode::PermissionDenied);
    CHECK(fixture.context.courses().size() == 4);

    // 修改不存在的课程返回 RecordNotFound。
    CHECK_CODE(administration.update_course(fixture.admin,
                                            {99, "XX101", "Missing", 3}),
               ResultCode::RecordNotFound);
}

void test_uc13_teaching_class_validation() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(40, 1, 100, 2, 4, 600, 660, "A201")),
               ResultCode::Success);

    // 编号重复、课程不存在、教师不存在。
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(40, 1, 100, 2, 5, 600, 660, "A202")),
               ResultCode::DuplicateIdentifier);
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(41, 99, 100, 2, 5, 600, 660, "A203")),
               ResultCode::RecordNotFound);
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(42, 1, 999, 2, 5, 600, 660, "A204")),
               ResultCode::RecordNotFound);

    // 非法时段与非法容量。
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(43, 1, 100, 2, 8, 600, 660, "A205")),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(44, 1, 100, 2, 5, 660, 600, "A206")),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(45, 1, 100, 0, 5, 600, 660, "A207")),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(46, 1, 100, 2, 5, 600, 600, "A208")),
               ResultCode::InvalidInput);

    // 同一教师同一时段不能带两个教学班。
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(47, 1, 100, 2, 4, 630, 690, "A209")),
               ResultCode::TimeConflict);
    CHECK(fixture.context.teaching_classes().size() == 5);
}

void test_uc13_removal_respects_relationships() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    // 课程仍有教学班时不能删除。
    CHECK_CODE(administration.remove_course(fixture.admin, 1),
               ResultCode::CatalogInUse);
    CHECK_MESSAGE(administration.remove_course(fixture.admin, 1), "still has");

    // 教学班仍有有效选课时不能删除。
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    CHECK_CODE(administration.remove_teaching_class(fixture.admin, 10),
               ResultCode::CatalogInUse);
    CHECK_MESSAGE(administration.remove_teaching_class(fixture.admin, 10),
                  "active enrollment");

    // 退课后只有已退课历史记录，当前版本仍不允许删除（不做归档）。
    CHECK_CODE(fixture.context.registration().drop(1001, 10),
               ResultCode::Success);
    CHECK_CODE(administration.remove_teaching_class(fixture.admin, 10),
               ResultCode::CatalogInUse);

    // 无选课记录的教学班可以删除，删除后课程才可删除。
    CHECK_CODE(administration.remove_teaching_class(fixture.admin, 30),
               ResultCode::Success);
    CHECK_CODE(administration.remove_course(fixture.admin, 3),
               ResultCode::Success);
    CHECK_CODE(administration.remove_course(fixture.admin, 3),
               ResultCode::RecordNotFound);

    // 管理员以外的人不能删除。
    CHECK_CODE(administration.remove_teaching_class(fixture.dr_wang, 11),
               ResultCode::PermissionDenied);
}

void test_uc13_time_change_must_not_break_existing_timetables() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    CHECK_CODE(fixture.context.registration().enroll(1001, 30),
               ResultCode::Success);

    // 把教学班 10 改到与 30 重叠的周三 14:30-15:30，会破坏学生 1001 的课表。
    TeachingClass moved = make_class(10, 1, 100, 2, 3, 870, 930, "A101");
    CHECK_CODE(administration.update_teaching_class(fixture.admin, moved),
               ResultCode::TimeConflict);
    CHECK_MESSAGE(administration.update_teaching_class(fixture.admin, moved),
                  "S2023001");

    // 合法时段可以修改，已选课记录不受影响。
    TeachingClass moved_ok = make_class(10, 1, 100, 2, 5, 900, 960, "A101");
    CHECK_CODE(administration.update_teaching_class(fixture.admin, moved_ok),
               ResultCode::Success);
    CHECK(fixture.context.enrollment_count(10) == 1);
}

// ---------------------------------------------------------------------------
// UC14 Set Teaching Class Capacity
// ---------------------------------------------------------------------------

void test_uc14_capacity_rules() {
    Fixture fixture;
    AdministratorService administration(fixture.context);
    TeacherService teacher_service(fixture.context);

    CHECK_CODE(administration.set_teaching_class_capacity(fixture.admin, 10, 1),
               ResultCode::Success);
    CHECK(fixture.context.remaining_seats(10) == 1);

    // 容量 1 的班只能收一名学生。
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    CHECK(fixture.context.remaining_seats(10) == 0);
    CHECK_CODE(fixture.context.registration().enroll(1002, 10),
               ResultCode::TeachingClassFull);

    // 容量不能低于现有有效选课人数。
    CHECK_CODE(administration.set_teaching_class_capacity(fixture.admin, 10, 2),
               ResultCode::Success);
    CHECK_CODE(fixture.context.registration().enroll(1002, 10),
               ResultCode::Success);
    const OperationResult below =
        administration.set_teaching_class_capacity(fixture.admin, 10, 1);
    CHECK_CODE(below, ResultCode::CapacityBelowEnrollment);
    CHECK_MESSAGE(below, "below the 2 active enrollment");
    CHECK(fixture.context.enrollment_count(10) == 2);
    CHECK(fixture.context.remaining_seats(10) == 0);

    // 非正容量与不存在的教学班。
    CHECK_CODE(administration.set_teaching_class_capacity(fixture.admin, 10, 0),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.set_teaching_class_capacity(fixture.admin, 10, -3),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.set_teaching_class_capacity(fixture.admin, 999, 3),
               ResultCode::TeachingClassNotFound);

    // 教师不能改容量。
    CHECK_CODE(administration.set_teaching_class_capacity(fixture.dr_wang, 10, 5),
               ResultCode::PermissionDenied);
    CHECK(fixture.context.remaining_seats(10) == 0);

    // 扩容后教师端人数与余量同步变化。
    CHECK_CODE(administration.set_teaching_class_capacity(fixture.admin, 10, 3),
               ResultCode::Success);
    TeachingClassSummary summary{};
    CHECK_CODE(teacher_service.class_summary(fixture.dr_wang, 10, summary),
               ResultCode::Success);
    CHECK(summary.capacity == 3);
    CHECK(summary.active_enrollments == 2);
    CHECK(summary.remaining_seats == 1);
}

// ---------------------------------------------------------------------------
// UC15 Set Registration Period
// ---------------------------------------------------------------------------

void test_uc15_registration_window() {
    Fixture fixture;
    AdministratorService administration(fixture.context);
    fixture.context.set_now_for_tests("2026-11-03T10:00");

    // 非法时段：格式错误、起止颠倒。
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026/11/02 08:00", "2026-11-13T18:00"),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-13T18:00", "2026-11-02T08:00"),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-13T18:00", "2026-11-13T18:00"),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-02T08:00", "2026-13-13T18:00"),
               ResultCode::InvalidInput);

    // 当前时间落在时段内：选课开放。
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-02T08:00", "2026-11-13T18:00"),
               ResultCode::Success);
    CHECK(fixture.context.rule().registration_open);
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);

    // 管理员手动关闭后，选课和退课都被拒绝。
    CHECK_CODE(administration.close_registration(fixture.admin),
               ResultCode::Success);
    CHECK_CODE(fixture.context.registration().enroll(1002, 10),
               ResultCode::RegistrationClosed);
    CHECK_CODE(fixture.context.registration().drop(1001, 10),
               ResultCode::RegistrationClosed);

    // 时段在未来：设置为关闭状态。
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2027-02-01T08:00", "2027-02-14T18:00"),
               ResultCode::Success);
    CHECK(!fixture.context.rule().registration_open);
    CHECK_CODE(fixture.context.registration().enroll(1002, 10),
               ResultCode::RegistrationClosed);

    // 时段已结束：同样关闭。
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-09-01T08:00", "2026-09-14T18:00"),
               ResultCode::Success);
    CHECK(!fixture.context.rule().registration_open);

    // 学分上限必须是正数、教师无权修改；本轮已开放过，因此这里应当被拒绝
    // （允许修改的情形见缺陷-010 的测试）。
    CHECK_CODE(administration.set_maximum_credits(fixture.admin, 12),
               ResultCode::InvalidInput);
    CHECK(fixture.context.rule().maximum_credits == 18);
    CHECK_CODE(administration.set_maximum_credits(fixture.admin, 0),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.set_maximum_credits(fixture.dr_wang, 20),
               ResultCode::PermissionDenied);
    CHECK(fixture.context.rule().maximum_credits == 18);
}

// 时段状态按请求时刻现算：保存后时间继续走，状态不会停在旧值。
void test_uc15_window_is_evaluated_at_request_time() {
    Fixture fixture;
    AdministratorService administration(fixture.context);
    fixture.context.set_now_for_tests("2026-11-03T10:00");

    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-02T08:00", "2026-11-13T18:00"),
               ResultCode::Success);

    // 纯函数：窗口内为开放，正好到关闭时刻即为关闭。
    CHECK(fixture.context.registration_window_open());
    CHECK(fixture.context.registration_window_open_at("2026-11-02T08:00"));
    CHECK(fixture.context.registration_window_open_at("2026-11-13T17:59"));
    CHECK(!fixture.context.registration_window_open_at("2026-11-13T18:00"));
    CHECK(!fixture.context.registration_window_open_at("2026-11-02T07:59"));

    // 保存时是开放，时间走到关闭之后：请求前现算即关闭，选课被拒绝，
    // 说明状态不是保存时算一次就固定下来的。
    fixture.context.set_now_for_tests("2026-11-13T18:30");
    CHECK(!fixture.context.registration_window_open());
    fixture.context.refresh_registration_window();
    CHECK(!fixture.context.rule().registration_open);
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::RegistrationClosed);

    // 时间回到窗口内：现算后重新开放。
    fixture.context.set_now_for_tests("2026-11-04T09:00");
    fixture.context.refresh_registration_window();
    CHECK(fixture.context.rule().registration_open);
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);

    // 管理员手动关闭会清空时段，避免下一次现算又被时段打开。
    CHECK_CODE(administration.close_registration(fixture.admin),
               ResultCode::Success);
    CHECK(fixture.context.rule().opens_at.empty());
    CHECK(fixture.context.rule().closes_at.empty());
    CHECK(!fixture.context.registration_window_open());

    // 未设置时段时沿用管理员的手动开关。
    Fixture manual;
    manual.context.set_now_for_tests("2026-11-04T09:00");
    CHECK(manual.context.rule().opens_at.empty());
    CHECK(manual.context.registration_window_open());
}

// 缺陷-001 复现：时段结束后不手动刷新，也必须拒绝选课和退课。
void test_defect_001_enroll_after_window_closed() {
    Fixture fixture;
    AdministratorService administration(fixture.context);
    fixture.context.set_now_for_tests("2026-11-03T10:00");

    // 1. 固定 10:00，管理员设置当天 09:00～11:00 开放。
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
               ResultCode::Success);
    CHECK(fixture.context.rule().registration_open);

    // 2. 时间推进到 12:00，不调用 refresh_registration_window()。
    fixture.context.set_now_for_tests("2026-11-03T12:00");

    // 3. 直接选课：必须拒绝，且不产生记录、不改变余量。
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::RegistrationClosed);
    CHECK(fixture.context.active_enrollments(1001).empty());
    CHECK(fixture.context.enrollment_count(10) == 0);
    CHECK(fixture.context.remaining_seats(10) == 2);

    // 4. 退课走同一个判断入口，同样被拒绝。
    fixture.context.set_now_for_tests("2026-11-03T10:30");
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    fixture.context.set_now_for_tests("2026-11-03T12:00");
    CHECK_CODE(fixture.context.registration().drop(1001, 10),
               ResultCode::RegistrationClosed);
    CHECK(fixture.context.enrollment_count(10) == 1);

    // 5. 回到开放区间后重新可以选课，说明不是把开关写死。
    fixture.context.set_now_for_tests("2026-11-03T10:30");
    CHECK_CODE(fixture.context.registration().enroll(1002, 10),
               ResultCode::Success);
}

// 缺陷-002 复现：教学班有有效选课时改课程，会绕过重复选课与学分限制。
void test_defect_002_course_change_blocked_while_enrolled() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    // 同一学生选中课程 1 的教学班 10 与课程 3 的教学班 30（时间不冲突）。
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    CHECK_CODE(fixture.context.registration().enroll(1001, 30),
               ResultCode::Success);

    // 把教学班 30 改成课程 1：会让学生同时选同一门课的两个教学班。
    const TeachingClass to_course_one =
        make_class(30, 1, 300, 2, 3, 840, 900, "C301");
    const OperationResult blocked =
        administration.update_teaching_class(fixture.admin, to_course_one);
    CHECK_CODE(blocked, ResultCode::InvalidInput);
    CHECK_MESSAGE(blocked, "cannot be changed");
    CHECK_MESSAGE(blocked, "active enrollment");

    // 原数据保持不变，也没有出现重复选课。
    TeachingClass stored{};
    CHECK(fixture.context.find_teaching_class(30, stored));
    CHECK(stored.course_id == 3);
    std::vector<std::string> problems;
    CHECK_CODE(administration.check_data_integrity(fixture.admin, problems),
               ResultCode::Success);
    CHECK(problems.empty());

    // 只改时间、不改课程仍然允许。
    const TeachingClass later_slot =
        make_class(30, 3, 300, 2, 3, 900, 960, "C301");
    CHECK_CODE(administration.update_teaching_class(fixture.admin, later_slot),
               ResultCode::Success);
    CHECK(fixture.context.find_teaching_class(30, stored));
    CHECK(stored.course_id == 3);
    CHECK(stored.meetings.size() == 1);
    CHECK(stored.meetings[0].start_minute == 900);

    // 学生退课之后可以改课程，说明限制只在有有效选课时生效。
    CHECK_CODE(fixture.context.registration().drop(1001, 30),
               ResultCode::Success);
    CHECK_CODE(
        administration.update_teaching_class(fixture.admin, to_course_one),
        ResultCode::Success);
    CHECK(fixture.context.find_teaching_class(30, stored));
    CHECK(stored.course_id == 1);
}

// 缺陷-003 复现：管理员手动关闭后，普通时段刷新不能把它改回开放。
void test_defect_003_manual_close_survives_refresh() {
    Fixture fixture;
    AdministratorService administration(fixture.context);
    fixture.context.set_now_for_tests("2026-11-03T10:00");
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
               ResultCode::Success);
    CHECK(fixture.context.rule().registration_open);

    // 手动关闭，然后做一次普通刷新：仍然是关闭。
    CHECK_CODE(administration.close_registration(fixture.admin),
               ResultCode::Success);
    fixture.context.refresh_registration_window();
    CHECK(!fixture.context.rule().registration_open);
    CHECK(!fixture.context.registration_window_open());
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::RegistrationClosed);

    // 时间仍在原来的开放区间内，也不能被自动打开。
    fixture.context.set_now_for_tests("2026-11-03T10:30");
    fixture.context.refresh_registration_window();
    CHECK(!fixture.context.rule().registration_open);
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::RegistrationClosed);

    // 管理员明确重新设置时段后才恢复。
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
               ResultCode::Success);
    CHECK(fixture.context.rule().registration_open);
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
}

// 缺陷-006 复现：删除学生/教师后，提示必须用被删除那条记录的编号。
void test_defect_006_delete_message_uses_deleted_record() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    // 学生：首条、中间、末条（删到最后一条即"唯一记录"）
    const OperationResult first =
        administration.remove_student(fixture.admin, 1001);
    CHECK_CODE(first, ResultCode::Success);
    CHECK_MESSAGE(first, "S2023001");
    CHECK(first.message.find("S2023002") == std::string::npos);

    const OperationResult middle =
        administration.remove_student(fixture.admin, 1002);
    CHECK_CODE(middle, ResultCode::Success);
    CHECK_MESSAGE(middle, "S2023002");
    CHECK(middle.message.find("S2023003") == std::string::npos);

    const OperationResult last =
        administration.remove_student(fixture.admin, 1003);
    CHECK_CODE(last, ResultCode::Success);
    CHECK_MESSAGE(last, "S2023003");
    CHECK(fixture.context.students().empty());

    // 已删除的编号再删一次：RecordNotFound，提示里只有这个编号
    const OperationResult gone =
        administration.remove_student(fixture.admin, 1003);
    CHECK_CODE(gone, ResultCode::RecordNotFound);
    CHECK_MESSAGE(gone, "1003");

    // 教师：先删掉没有选课记录的教学班，再按首条、中间、末条删除
    for (int class_id : {10, 11, 20, 30}) {
        CHECK_CODE(
            administration.remove_teaching_class(fixture.admin, class_id),
            ResultCode::Success);
    }
    const OperationResult teacher_first =
        administration.remove_teacher(fixture.admin, 100);
    CHECK_CODE(teacher_first, ResultCode::Success);
    CHECK_MESSAGE(teacher_first, "T2015007");
    CHECK(teacher_first.message.find("T2016011") == std::string::npos);

    const OperationResult teacher_middle =
        administration.remove_teacher(fixture.admin, 200);
    CHECK_CODE(teacher_middle, ResultCode::Success);
    CHECK_MESSAGE(teacher_middle, "T2016011");

    const OperationResult teacher_last =
        administration.remove_teacher(fixture.admin, 300);
    CHECK_CODE(teacher_last, ResultCode::Success);
    CHECK_MESSAGE(teacher_last, "T2018003");
    CHECK(fixture.context.teachers().empty());
}

// 缺陷-007 复现：查询失败后，输出里不能残留上一次成功的班级。
void test_defect_007_failed_query_clears_output() {
    Fixture fixture;
    TeacherService teacher_service(fixture.context);

    TeachingClassSummary summary{};
    CHECK_CODE(teacher_service.class_summary(fixture.dr_wang, 10, summary),
               ResultCode::Success);
    CHECK(summary.teaching_class_id == 10);
    CHECK(summary.course_code == "SE101");

    // 成功之后越权读取：PermissionDenied，且输出被清空
    const OperationResult denied_result =
        teacher_service.class_summary(fixture.dr_wang, 11, summary);
    CHECK_CODE(denied_result, ResultCode::PermissionDenied);
    CHECK(summary.teaching_class_id == 0);
    CHECK(summary.course_code.empty());
    CHECK(summary.capacity == 0);

    // 成功之后查询不存在的编号：同样清空
    CHECK_CODE(teacher_service.class_summary(fixture.dr_wang, 10, summary),
               ResultCode::Success);
    const OperationResult missing =
        teacher_service.class_summary(fixture.dr_wang, 999, summary);
    CHECK_CODE(missing, ResultCode::TeachingClassNotFound);
    CHECK(summary.teaching_class_id == 0);
    CHECK(summary.course_code.empty());
}

// 缺陷-009 复现：不存在的日期（2 月 30 日、非闰年 2 月 29 日、4 月 31 日）必须被拒绝。
void test_defect_009_rejects_impossible_dates() {
    Fixture fixture;
    AdministratorService administration(fixture.context);
    fixture.context.set_now_for_tests("2026-11-03T10:00");

    // 合法闰日 2024-02-29 可以保存
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2024-02-29T08:00", "2024-03-01T18:00"),
               ResultCode::Success);
    CHECK(fixture.context.rule().opens_at == "2024-02-29T08:00");

    // 先把规则设成一个合法时段，作为"原规则"
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-02T08:00", "2026-11-13T18:00"),
               ResultCode::Success);
    const RegistrationRule kept = fixture.context.rule();

    // 逐个非法日期：返回 InvalidInput，且原规则完全不变
    const char* const bad_openings[] = {
        "2026-02-29T08:00",  // 非闰年没有 2 月 29 日
        "2026-02-30T08:00",  // 2 月没有 30 日
        "2026-04-31T08:00",  // 4 月只有 30 天
        "2026-13-01T08:00",  // 月份越界
        "2026-00-10T08:00",  // 月份为 0
    };
    for (const char* bad : bad_openings) {
        const OperationResult rejected =
            administration.set_registration_period(fixture.admin, bad,
                                                   "2026-12-31T18:00");
        CHECK_CODE(rejected, ResultCode::InvalidInput);
        CHECK(fixture.context.rule().opens_at == kept.opens_at);
        CHECK(fixture.context.rule().closes_at == kept.closes_at);
    }

    // 4 月 30 日是合法的
    const OperationResult april_thirtieth =
        administration.set_registration_period(fixture.admin,
                                               "2026-04-30T08:00",
                                               "2026-05-10T18:00");
    CHECK_CODE(april_thirtieth, ResultCode::Success);
    CHECK(fixture.context.rule().opens_at == "2026-04-30T08:00");
}

// 缺陷-010 复现：学分上限必须在本轮开放前确定，开放过就不能再改。
void test_defect_010_credit_limit_locked_after_open() {
    // 起点：本轮尚未开放
    Fixture fixture(RegistrationRule{false, 18, "", ""});
    AdministratorService administration(fixture.context);
    fixture.context.set_now_for_tests("2026-11-03T10:00");

    // 1. 尚未开放：允许设置与修改
    CHECK_CODE(administration.set_maximum_credits(fixture.admin, 20),
               ResultCode::Success);
    CHECK(fixture.context.rule().maximum_credits == 20);
    CHECK_CODE(administration.set_maximum_credits(fixture.admin, 18),
               ResultCode::Success);
    CHECK(fixture.context.rule().maximum_credits == 18);

    // 2. 开放本轮
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
               ResultCode::Success);
    CHECK(fixture.context.rule().registration_open);

    // 3. 开放之后不允许修改，原上限不变
    const OperationResult after_open =
        administration.set_maximum_credits(fixture.admin, 12);
    CHECK_CODE(after_open, ResultCode::InvalidInput);
    CHECK_MESSAGE(after_open, "still 18");
    CHECK(fixture.context.rule().maximum_credits == 18);

    // 4. 暂停之后仍然不允许（不能用"先关闭、改上限、再开放"绕过）
    CHECK_CODE(administration.close_registration(fixture.admin),
               ResultCode::Success);
    CHECK_CODE(administration.set_maximum_credits(fixture.admin, 12),
               ResultCode::InvalidInput);
    CHECK(fixture.context.rule().maximum_credits == 18);

    // 5. 本轮结束之后仍然不允许
    CHECK_CODE(administration.set_registration_period(
                   fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
               ResultCode::Success);
    fixture.context.set_now_for_tests("2026-11-03T12:00");
    // 现算结果是"已关闭"；保存的 registration_open 只是显示用，可能还是旧值。
    CHECK(!fixture.context.registration_window_open());
    CHECK_CODE(administration.set_maximum_credits(fixture.admin, 12),
               ResultCode::InvalidInput);
    CHECK(fixture.context.rule().maximum_credits == 18);

    // 6. 原有的权限与正数校验仍然生效
    CHECK_CODE(administration.set_maximum_credits(fixture.dr_wang, 20),
               ResultCode::PermissionDenied);
    CHECK_CODE(administration.set_maximum_credits(fixture.admin, 0),
               ResultCode::InvalidInput);
}

// 缺陷-010 复现（第二轮）：提前预约、按时间自动开放后，把时段改到未来也不能解锁学分上限。
void test_defect_010_reserved_window_cannot_unlock_credits() {
    // 场景 1：无人选课。预约明天开放 → 到点自动开放 → 把时段改到后天 → 再改上限。
    {
        Fixture fixture(RegistrationRule{false, 18, "", ""});
        AdministratorService administration(fixture.context);
        fixture.context.set_now_for_tests("2026-11-02T08:00");

        // 预约未开始前：允许修改上限
        CHECK_CODE(administration.set_registration_period(
                       fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
                   ResultCode::Success);
        CHECK(!fixture.context.registration_window_open());
        CHECK_CODE(administration.set_maximum_credits(fixture.admin, 20),
                   ResultCode::Success);
        CHECK(fixture.context.rule().maximum_credits == 20);
        CHECK_CODE(administration.set_maximum_credits(fixture.admin, 18),
                   ResultCode::Success);

        // 时间到点，系统自动开放；没有任何人调用过接口，也没有学生选课
        fixture.context.set_now_for_tests("2026-11-03T10:00");
        CHECK(fixture.context.registration_window_open());

        // 直接把时段改到明天，不能因此"重新开始一轮"
        CHECK_CODE(administration.set_registration_period(
                       fixture.admin, "2026-11-04T09:00", "2026-11-04T11:00"),
                   ResultCode::Success);
        const OperationResult blocked =
            administration.set_maximum_credits(fixture.admin, 12);
        CHECK_CODE(blocked, ResultCode::InvalidInput);
        CHECK_MESSAGE(blocked, "still 18");
        CHECK(fixture.context.rule().maximum_credits == 18);
    }

    // 场景 2：已有学生选课成功。结论必须与场景 1 一致。
    {
        Fixture fixture(RegistrationRule{false, 18, "", ""});
        AdministratorService administration(fixture.context);
        fixture.context.set_now_for_tests("2026-11-02T08:00");
        CHECK_CODE(administration.set_registration_period(
                       fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
                   ResultCode::Success);
        fixture.context.set_now_for_tests("2026-11-03T10:00");
        CHECK_CODE(fixture.context.registration().enroll(1001, 10),
                   ResultCode::Success);
        CHECK_CODE(administration.set_registration_period(
                       fixture.admin, "2026-11-04T09:00", "2026-11-04T11:00"),
                   ResultCode::Success);
        CHECK_CODE(administration.set_maximum_credits(fixture.admin, 12),
                   ResultCode::InvalidInput);
        CHECK(fixture.context.rule().maximum_credits == 18);
        // 已经选上的记录不受影响
        CHECK(fixture.context.enrollment_count(10) == 1);
    }

    // 场景 3：原时段自然结束后，把时段改到未来，同样不能解锁。
    {
        Fixture fixture(RegistrationRule{false, 18, "", ""});
        AdministratorService administration(fixture.context);
        fixture.context.set_now_for_tests("2026-11-02T08:00");
        CHECK_CODE(administration.set_registration_period(
                       fixture.admin, "2026-11-03T09:00", "2026-11-03T11:00"),
                   ResultCode::Success);
        fixture.context.set_now_for_tests("2026-11-03T12:00");
        CHECK(!fixture.context.registration_window_open());
        CHECK_CODE(administration.set_registration_period(
                       fixture.admin, "2026-11-05T09:00", "2026-11-05T11:00"),
                   ResultCode::Success);
        CHECK_CODE(administration.set_maximum_credits(fixture.admin, 12),
                   ResultCode::InvalidInput);
        CHECK(fixture.context.rule().maximum_credits == 18);
    }
}

// ---------------------------------------------------------------------------
// UC16 Manage Student / Teacher Information
// ---------------------------------------------------------------------------

void test_uc16_student_records() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    CHECK_CODE(administration.create_student(
                   fixture.admin, {1004, "S2023004", "Dan Zhou", "Mathematics", true}),
               ResultCode::Success);
    CHECK(fixture.context.students().size() == 4);

    // 学生编号与学号都不能重复。
    CHECK_CODE(administration.create_student(
                   fixture.admin, {1004, "S2023099", "Other", "Mathematics", true}),
               ResultCode::DuplicateIdentifier);
    CHECK_CODE(administration.create_student(
                   fixture.admin, {1005, "S2023004", "Other Two", "Mathematics", true}),
               ResultCode::DuplicateIdentifier);
    // 学号重复检查不区分大小写。
    CHECK_CODE(administration.create_student(
                   fixture.admin, {1006, "s2023004", "Other Three", "Mathematics", true}),
               ResultCode::DuplicateIdentifier);
    // 空学号、空姓名、非正编号。
    CHECK_CODE(administration.create_student(
                   fixture.admin, {1007, "", "No Number", "Mathematics", true}),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.create_student(
                   fixture.admin, {1008, "S2023008", "", "Mathematics", true}),
               ResultCode::InvalidInput);
    CHECK_CODE(administration.create_student(
                   fixture.admin, {-1, "S2023009", "Negative", "Mathematics", true}),
               ResultCode::InvalidInput);

    CHECK_CODE(administration.update_student(
                   fixture.admin, {1004, "S2023004", "Dan Zhou (renamed)", "Mathematics", true}),
               ResultCode::Success);
    CHECK(fixture.context.find_student(1004)->name == "Dan Zhou (renamed)");
    CHECK_CODE(administration.update_student(
                   fixture.admin, {9999, "S2023999", "Ghost", "Mathematics", true}),
               ResultCode::RecordNotFound);

    // 有有效选课的学生不能删除。
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    CHECK_CODE(administration.remove_student(fixture.admin, 1001),
               ResultCode::CatalogInUse);
    CHECK_MESSAGE(administration.remove_student(fixture.admin, 1001),
                  "active enrollment");
    CHECK_CODE(fixture.context.registration().drop(1001, 10),
               ResultCode::Success);
    CHECK_CODE(administration.remove_student(fixture.admin, 1001),
               ResultCode::Success);
    CHECK(fixture.context.find_student(1001) == nullptr);

    // 学生不能维护其他学生资料。
    CHECK_CODE(administration.create_student(
                   fixture.alice, {1010, "S2023010", "Hacker", "Mathematics", true}),
               ResultCode::PermissionDenied);
}

void test_uc16_teacher_records() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    CHECK_CODE(administration.create_teacher(
                   fixture.admin, {400, "T2020001", "Dr. Qin", "Physics", true}),
               ResultCode::Success);
    CHECK_CODE(administration.create_teacher(
                   fixture.admin, {400, "T2020099", "Dr. Duplicate", "Physics", true}),
               ResultCode::DuplicateIdentifier);
    CHECK_CODE(administration.create_teacher(
                   fixture.admin, {401, "T2020001", "Dr. Duplicate Two", "Physics", true}),
               ResultCode::DuplicateIdentifier);
    CHECK_CODE(administration.create_teacher(
                   fixture.admin, {402, "T2020002", "", "Physics", true}),
               ResultCode::InvalidInput);

    // 停用账号不能被安排教学班。
    CHECK_CODE(administration.create_teacher(
                   fixture.admin, {403, "T2020003", "Dr. Retired", "Physics", false}),
               ResultCode::Success);
    CHECK_CODE(administration.create_teaching_class(
                   fixture.admin, make_class(50, 1, 403, 2, 5, 600, 660, "D101")),
               ResultCode::InvalidInput);

    // 仍负责教学班的教师不能删除。
    CHECK_CODE(administration.remove_teacher(fixture.admin, 100),
               ResultCode::CatalogInUse);
    CHECK_MESSAGE(administration.remove_teacher(fixture.admin, 100), "teaches");
    CHECK_CODE(administration.remove_teacher(fixture.admin, 400),
               ResultCode::Success);
    CHECK_CODE(administration.remove_teacher(fixture.admin, 400),
               ResultCode::RecordNotFound);

    // 匿名之外的越权：教师不能创建教师账号。
    CHECK_CODE(administration.create_teacher(
                   fixture.dr_wang, {405, "T2020005", "Dr. Fake", "Physics", true}),
               ResultCode::PermissionDenied);
}

// ---------------------------------------------------------------------------
// 一致性检查
// ---------------------------------------------------------------------------

void test_data_integrity_check() {
    Fixture fixture;
    AdministratorService administration(fixture.context);

    std::vector<std::string> problems;
    CHECK_CODE(administration.check_data_integrity(fixture.admin, problems),
               ResultCode::Success);
    CHECK(problems.empty());

    // 模拟导入数据绕过校验：教学班引用不存在的课程、容量为 0、没有时段。
    std::vector<TeachingClass> classes = fixture.context.teaching_classes();
    classes.push_back(TeachingClass{99, 777, 100, 0, {}});
    fixture.context.registration().apply_catalog_change(
        fixture.context.courses(), classes);

    CHECK_CODE(administration.check_data_integrity(fixture.admin, problems),
               ResultCode::InvalidInput);
    CHECK(problems.size() >= 3);
    if (problems.size() >= 3) {
        CHECK(problems[0].find("missing course_id 777") != std::string::npos);
    }

    // 教师不能执行管理员的一致性检查。
    CHECK_CODE(administration.check_data_integrity(fixture.dr_wang, problems),
               ResultCode::PermissionDenied);
}

// ---------------------------------------------------------------------------
// 每周共同场景（团队说明第 5 节，必须每周复跑）
// ---------------------------------------------------------------------------

void test_weekly_joint_scenario() {
    Fixture fixture;
    AdministratorService administration(fixture.context);
    TeacherService teacher_service(fixture.context);

    // 1. 管理员把教学班 10 的容量设为 1。
    CHECK_CODE(administration.set_teaching_class_capacity(fixture.admin, 10, 1),
               ResultCode::Success);
    CHECK(fixture.context.remaining_seats(10) == 1);

    // 2. 学生 A 选课成功。
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);

    // 3. 余量为 0，A 的课表出现该教学班，教师人数为 1。
    CHECK(fixture.context.remaining_seats(10) == 0);
    const std::vector<Enrollment> alice_active =
        fixture.context.active_enrollments(1001);
    CHECK(alice_active.size() == 1);
    if (alice_active.size() == 1) {
        CHECK(alice_active[0].teaching_class_id == 10);
    }
    TeachingClassSummary summary{};
    CHECK_CODE(teacher_service.class_summary(fixture.dr_wang, 10, summary),
               ResultCode::Success);
    CHECK(summary.active_enrollments == 1);
    CHECK(summary.remaining_seats == 0);

    ClassRoster roster{};
    CHECK_CODE(teacher_service.class_roster(fixture.dr_wang, 10, roster),
               ResultCode::Success);
    CHECK(roster.students.size() == 1);
    if (roster.students.size() == 1) {
        CHECK(roster.students[0].student_number == "S2023001");
    }

    // 三方数字必须一致：教师人数 == 选课人数 == 容量 - 余量。
    CHECK(summary.active_enrollments ==
          fixture.context.enrollment_count(10));
    CHECK(summary.active_enrollments ==
          summary.capacity - summary.remaining_seats);
    CHECK(static_cast<int>(roster.students.size()) == summary.active_enrollments);

    // 4. 学生 B 因满员被拒绝，且没有产生记录。
    CHECK_CODE(fixture.context.registration().enroll(1002, 10),
               ResultCode::TeachingClassFull);
    CHECK(fixture.context.enrollment_count(10) == 1);
    CHECK(fixture.context.active_enrollments(1002).empty());

    // 5. A 退课后余量恢复，课表与教师人数同步更新。
    CHECK_CODE(fixture.context.registration().drop(1001, 10),
               ResultCode::Success);
    CHECK(fixture.context.remaining_seats(10) == 1);
    CHECK(fixture.context.active_enrollments(1001).empty());
    CHECK_CODE(teacher_service.class_summary(fixture.dr_wang, 10, summary),
               ResultCode::Success);
    CHECK(summary.active_enrollments == 0);
    CHECK(summary.remaining_seats == 1);
    CHECK_CODE(teacher_service.class_roster(fixture.dr_wang, 10, roster),
               ResultCode::Success);
    CHECK(roster.students.empty());

    // 6. 场景结束后数据仍然自洽。
    std::vector<std::string> problems;
    CHECK_CODE(administration.check_data_integrity(fixture.admin, problems),
               ResultCode::Success);
    CHECK(problems.empty());
}

}  // namespace

int main() {
    test_uc09_teacher_reads_only_own_classes();
    test_uc09_cross_teacher_access_is_denied();
    test_uc09_roster_follows_active_enrollments();
    test_uc13_course_validation();
    test_uc13_teaching_class_validation();
    test_uc13_removal_respects_relationships();
    test_uc13_time_change_must_not_break_existing_timetables();
    test_uc14_capacity_rules();
    test_uc15_registration_window();
    test_uc15_window_is_evaluated_at_request_time();
    test_defect_001_enroll_after_window_closed();
    test_defect_002_course_change_blocked_while_enrolled();
    test_defect_003_manual_close_survives_refresh();
    test_defect_006_delete_message_uses_deleted_record();
    test_defect_007_failed_query_clears_output();
    test_defect_009_rejects_impossible_dates();
    test_defect_010_credit_limit_locked_after_open();
    test_defect_010_reserved_window_cannot_unlock_credits();
    test_uc16_student_records();
    test_uc16_teacher_records();
    test_data_integrity_check();
    test_weekly_joint_scenario();

    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << ": " << g_checks
              << " checks, " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
