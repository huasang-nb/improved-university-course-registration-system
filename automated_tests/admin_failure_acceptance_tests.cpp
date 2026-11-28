// ============================================================================
// 第 4 周（2026-11-23 至 2026-11-29）失败场景验收测试
// Zhu Ziliang
//
// 手册本周要求：测试容量调整、已有选课记录、重复账号、无效时段及越权访问；
// 修复本人模块问题；验收标准是"每个失败操作都有清楚结果且不破坏原数据"。
//
// 本文件按这五类各写一组用例，并且每个被拒绝的操作都用 capture_state() 做
// 前后快照比较，证明拒绝之后课程、教学班、选课记录、账号与选课规则都没有
// 被改动（不是只看返回码）。快照包含：
//   * 选课规则四个字段与"本轮是否已开放"这一历史事实；
//   * 全部课程、教学班（含每个上课时段）、学生/教师/管理员（含 password_hash）；
//   * 全部选课记录（含 Active / Dropped 状态）。
//
// 这里的断言只依赖 teacher_and_administration_services.hpp 的公开接口，
// 与教师端、管理员端走同一条路径，不直接调用 apply_* 之类的内部写入。
// ============================================================================

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "teacher_and_administration_services.hpp"

namespace {

using course_registration::Administrator;
using course_registration::AdministratorService;
using course_registration::ClassMeeting;
using course_registration::Course;
using course_registration::Enrollment;
using course_registration::EnrollmentStatus;
using course_registration::OperationResult;
using course_registration::RegistrationRule;
using course_registration::ResultCode;
using course_registration::Role;
using course_registration::Student;
using course_registration::SystemContext;
using course_registration::Teacher;
using course_registration::TeacherService;
using course_registration::TeachingClass;
using course_registration::TeachingClassSummary;
using course_registration::UserIdentity;

int g_checks = 0;
int g_failures = 0;

// 返回码转成可读名字，失败时打印出来，避免只看到 "0 != 3"。
const char* code_name(ResultCode code) {
    switch (code) {
        case ResultCode::Success: return "Success";
        case ResultCode::RegistrationClosed: return "RegistrationClosed";
        case ResultCode::AlreadyEnrolled: return "AlreadyEnrolled";
        case ResultCode::TimeConflict: return "TimeConflict";
        case ResultCode::CreditLimitExceeded: return "CreditLimitExceeded";
        case ResultCode::TeachingClassFull: return "TeachingClassFull";
        case ResultCode::TeachingClassNotFound: return "TeachingClassNotFound";
        case ResultCode::ActiveEnrollmentNotFound:
            return "ActiveEnrollmentNotFound";
        case ResultCode::PermissionDenied: return "PermissionDenied";
        case ResultCode::InvalidInput: return "InvalidInput";
        case ResultCode::RecordNotFound: return "RecordNotFound";
        case ResultCode::DuplicateIdentifier: return "DuplicateIdentifier";
        case ResultCode::CapacityBelowEnrollment:
            return "CapacityBelowEnrollment";
        case ResultCode::CatalogInUse: return "CatalogInUse";
        case ResultCode::StorageIoError: return "StorageIoError";
        case ResultCode::StorageParseError: return "StorageParseError";
        case ResultCode::StorageSchemaMismatch:
            return "StorageSchemaMismatch";
        case ResultCode::StorageIntegrityError: return "StorageIntegrityError";
        case ResultCode::StorageBackupMissing:
            return "StorageBackupMissing";
    }
    return "UnknownCode";
}

void check(bool condition, const char* expression, int line,
           const std::string& detail = std::string()) {
    ++g_checks;
    if (condition) {
        return;
    }
    ++g_failures;
    std::cout << "FAIL line " << line << ": " << expression;
    if (!detail.empty()) {
        std::cout << " [" << detail << "]";
    }
    std::cout << '\n';
}

void check_contains(const std::string& text, const std::string& needle,
                    const char* expression, int line) {
    check(text.find(needle) != std::string::npos, expression, line,
          "\"" + needle + "\" not found in \"" + text + "\"");
}

// 断言操作成功，并且结果消息不为空（每个操作都要有可读结果）。
void check_success(const OperationResult& result, const char* expression,
                   int line) {
    check(result.code == ResultCode::Success, expression, line,
          std::string("code=") + code_name(result.code) + " message=" +
              result.message);
    check(!result.message.empty(), expression, line,
          "a successful operation must still report a message");
}

// 断言操作以指定原因失败，并且给了非空说明。
OperationResult expect_failure(const OperationResult& result,
                               ResultCode expected, const char* expression,
                               int line) {
    check(result.code == expected, expression, line,
          std::string("expected ") + code_name(expected) + " but got " +
              code_name(result.code) + " (" + result.message + ")");
    check(!result.message.empty(), expression, line,
          "a rejected operation must explain itself");
    return result;
}

// 把整个 SystemContext 的可观察状态拼成字符串，用于"失败不破坏原数据"比较。
std::string capture_state(const SystemContext& context) {
    std::ostringstream out;
    const RegistrationRule rule = context.rule();
    out << "rule " << (rule.registration_open ? 1 : 0) << '|'
        << rule.maximum_credits << '|' << rule.opens_at << '|' << rule.closes_at
        << '\n';
    out << "round " << (context.round_opened() ? 1 : 0) << '\n';
    for (const Course& course : context.courses()) {
        out << "course " << course.course_id << '|' << course.course_code << '|'
            << course.course_name << '|' << course.credits << '\n';
    }
    for (const TeachingClass& teaching_class : context.teaching_classes()) {
        out << "class " << teaching_class.teaching_class_id << '|'
            << teaching_class.course_id << '|' << teaching_class.teacher_id
            << '|' << teaching_class.capacity << '|';
        for (const ClassMeeting& meeting : teaching_class.meetings) {
            out << meeting.day_of_week << ',' << meeting.start_minute << ','
                << meeting.end_minute << ',' << meeting.room << ';';
        }
        out << '\n';
    }
    for (const Student& student : context.students()) {
        out << "student " << student.student_id << '|' << student.student_number
            << '|' << student.name << '|' << student.major << '|'
            << (student.account_active ? 1 : 0) << '|' << student.password_hash
            << '\n';
    }
    for (const Teacher& teacher : context.teachers()) {
        out << "teacher " << teacher.teacher_id << '|' << teacher.teacher_number
            << '|' << teacher.name << '|' << teacher.department << '|'
            << (teacher.account_active ? 1 : 0) << '|' << teacher.password_hash
            << '\n';
    }
    for (const Administrator& administrator : context.administrators()) {
        out << "administrator " << administrator.administrator_id << '|'
            << administrator.account_number << '|' << administrator.name << '|'
            << (administrator.account_active ? 1 : 0) << '|'
            << administrator.password_hash << '\n';
    }
    for (const Enrollment& enrollment : context.all_enrollments()) {
        out << "enrollment " << enrollment.student_id << '|'
            << enrollment.teaching_class_id << '|'
            << (enrollment.status == EnrollmentStatus::Active ? "active"
                                                             : "dropped")
            << '\n';
    }
    return out.str();
}

void check_unchanged(const SystemContext& context, const std::string& before,
                     const char* expression, int line) {
    const std::string after = capture_state(context);
    check(after == before, expression, line,
          after == before ? std::string()
                          : std::string("state changed:\n--- before ---\n") +
                                before + "--- after ---\n" + after);
}

// ---------------------------------------------------------------------------
// 示例数据。与 teacher_and_administration_services_tests.cpp 的编号保持一致，
// 方便对照；本文件单独定义，避免改那边时互相牵连。
// ---------------------------------------------------------------------------

ClassMeeting make_meeting(int day, int start, int end, const std::string& room) {
    ClassMeeting meeting;
    meeting.day_of_week = day;
    meeting.start_minute = start;
    meeting.end_minute = end;
    meeting.room = room;
    return meeting;
}

TeachingClass make_class(int id, int course_id, int teacher_id, int capacity,
                         int day, int start, int end,
                         const std::string& room) {
    TeachingClass teaching_class;
    teaching_class.teaching_class_id = id;
    teaching_class.course_id = course_id;
    teaching_class.teacher_id = teacher_id;
    teaching_class.capacity = capacity;
    teaching_class.meetings.push_back(make_meeting(day, start, end, room));
    return teaching_class;
}

std::vector<Student> make_students() {
    return {
        Student{1001, "S2023001", "Alice", "Software Engineering", true, {}},
        Student{1002, "S2023002", "Bob", "Software Engineering", true, {}},
        Student{1003, "S2023003", "Cara", "Mathematics", true, {}},
    };
}

std::vector<Teacher> make_teachers() {
    return {
        Teacher{100, "T2015007", "Wang", "Software Engineering", true, {}},
        Teacher{200, "T2016011", "Li", "Mathematics", true, {}},
        Teacher{300, "T2018003", "Zhao", "Computer Science", true, {}},
    };
}

std::vector<Administrator> make_administrators() {
    return {
        Administrator{900, "A2020001", "Sun", true, {}},
    };
}

std::vector<Course> make_courses() {
    return {
        Course{1, "SE101", "Software Engineering Basics", 3},
        Course{2, "MA101", "Mathematics Basics", 4},
        Course{3, "CS101", "Computer Science Basics", 5},
    };
}

std::vector<TeachingClass> make_teaching_classes() {
    return {
        make_class(10, 1, 100, 2, 1, 540, 660, "J1-101"),
        make_class(11, 1, 100, 30, 3, 540, 660, "J1-102"),
        make_class(20, 2, 200, 10, 2, 540, 660, "J2-201"),
        make_class(30, 3, 300, 40, 4, 540, 660, "J3-301"),
    };
}

struct Fixture {
    SystemContext context;
    UserIdentity admin{900, "A2020001", Role::Administrator};
    UserIdentity dr_wang{100, "T2015007", Role::Teacher};
    UserIdentity dr_li{200, "T2016011", Role::Teacher};
    UserIdentity dr_zhao{300, "T2018003", Role::Teacher};
    UserIdentity alice{1001, "S2023001", Role::Student};
    AdministratorService admin_service;
    TeacherService teacher_service;

    explicit Fixture(bool registration_open = true)
        : context(make_students(), make_teachers(), make_administrators(),
                  make_courses(), make_teaching_classes(),
                  RegistrationRule{registration_open, 18, "", ""}),
          admin_service(context),
          teacher_service(context) {}
};

// 便捷断言：教学班当前容量；不存在时返回 -1 并记录一次失败。
int class_capacity(SystemContext& context, int class_id, const char* line_expr,
                   int line) {
    TeachingClass teaching_class;
    const bool found = context.find_teaching_class(class_id, teaching_class);
    check(found, line_expr, line,
          "teaching class " + std::to_string(class_id) + " not found");
    return found ? teaching_class.capacity : -1;
}

// ---------------------------------------------------------------------------
// 一、容量调整：非法容量、低于已选人数、等于/高于已选人数
// ---------------------------------------------------------------------------

void test_capacity_adjustment() {
    std::cout << "-- capacity adjustment\n";
    Fixture f;
    SystemContext& context = f.context;
    AdministratorService& service = f.admin_service;

    // 提高容量：从 2 到 5，余量同步增加。
    std::string before = capture_state(context);
    OperationResult result =
        service.set_teaching_class_capacity(f.admin, 10, 5);
    check_success(result, "set capacity up", __LINE__);
    check_contains(result.message, "changed from 2 to 5", "capacity message",
                   __LINE__);
    check(class_capacity(context, 10, "capacity stored", __LINE__) == 5,
          "capacity stored", __LINE__);
    check(context.remaining_seats(10) == 5, "remaining seats", __LINE__);
    check(capture_state(context) != before, "successful change is visible",
          __LINE__);

    // 0 与负数容量：拒绝并保持原容量 5。
    before = capture_state(context);
    result = expect_failure(service.set_teaching_class_capacity(f.admin, 10, 0),
                            ResultCode::InvalidInput, "capacity 0", __LINE__);
    check_contains(result.message, "at least 1", "capacity 0 message",
                   __LINE__);
    check_unchanged(context, before, "capacity 0 keeps state", __LINE__);

    result = expect_failure(
        service.set_teaching_class_capacity(f.admin, 10, -3),
        ResultCode::InvalidInput, "negative capacity", __LINE__);
    check_contains(result.message, "at least 1", "negative capacity message",
                   __LINE__);
    check_unchanged(context, before, "negative capacity keeps state", __LINE__);

    // 两个学生选课之后，容量不能低于 2。
    check_success(context.registration().enroll(1001, 10), "enroll alice",
                  __LINE__);
    check_success(context.registration().enroll(1002, 10), "enroll bob",
                  __LINE__);
    check(context.remaining_seats(10) == 3, "remaining after two enrollments",
          __LINE__);

    before = capture_state(context);
    result = expect_failure(service.set_teaching_class_capacity(f.admin, 10, 1),
                            ResultCode::CapacityBelowEnrollment,
                            "capacity below enrollment", __LINE__);
    check_contains(result.message, "no seat can be taken away",
                   "capacity below message", __LINE__);
    check_contains(result.message, "1", "message names the new capacity",
                   __LINE__);
    check_contains(result.message, "2 active", "message names the count",
                   __LINE__);
    check(class_capacity(context, 10, "capacity kept", __LINE__) == 5,
          "capacity kept", __LINE__);
    check(context.remaining_seats(10) == 3, "remaining kept", __LINE__);
    check_unchanged(context, before, "rejected capacity keeps state", __LINE__);

    // 等于已选人数：允许，余量为 0。
    result = service.set_teaching_class_capacity(f.admin, 10, 2);
    check_success(result, "capacity equal to enrollment", __LINE__);
    check_contains(result.message, "remaining seats 0", "no seat left",
                   __LINE__);
    check(context.remaining_seats(10) == 0, "remaining is zero", __LINE__);

    // 再加回 4：余量 2。
    result = service.set_teaching_class_capacity(f.admin, 10, 4);
    check_success(result, "capacity up again", __LINE__);
    check(context.remaining_seats(10) == 2, "remaining is two", __LINE__);

    // 不存在的教学班。
    before = capture_state(context);
    expect_failure(service.set_teaching_class_capacity(f.admin, 999, 10),
                   ResultCode::TeachingClassNotFound, "unknown class",
                   __LINE__);
    check_unchanged(context, before, "unknown class keeps state", __LINE__);

    // 教师与学生都不能改容量，而且失败后状态不变。
    before = capture_state(context);
    result = expect_failure(service.set_teaching_class_capacity(f.dr_wang, 10, 9),
                            ResultCode::PermissionDenied, "teacher capacity",
                            __LINE__);
    check_contains(result.message, "requires a different role",
                   "permission message", __LINE__);
    check_unchanged(context, before, "teacher capacity keeps state", __LINE__);

    expect_failure(service.set_teaching_class_capacity(f.alice, 10, 9),
                   ResultCode::PermissionDenied, "student capacity", __LINE__);
    check_unchanged(context, before, "student capacity keeps state", __LINE__);

    // 通过整条教学班更新入口改容量，也要遵守同一条下界规则。
    TeachingClass teaching_class;
    check(context.find_teaching_class(10, teaching_class), "load class 10",
          __LINE__);
    teaching_class.capacity = 1;
    before = capture_state(context);
    result = expect_failure(service.update_teaching_class(f.admin, teaching_class),
                            ResultCode::CapacityBelowEnrollment,
                            "update class capacity below enrollment", __LINE__);
    check_contains(result.message, "below the",
                   "update capacity message", __LINE__);
    check_unchanged(context, before, "update capacity keeps state", __LINE__);

    // 拒绝之后数据仍然一致。
    std::vector<std::string> problems;
    check_success(service.check_data_integrity(f.admin, problems),
                  "integrity after capacity tests", __LINE__);
    check(problems.empty(), "no integrity problems", __LINE__,
          problems.empty() ? std::string() : problems.front());
}

// ---------------------------------------------------------------------------
// 二、已有选课记录：课程 / 教学班 / 学生 / 教师都不能被删掉或改归属
// ---------------------------------------------------------------------------

void test_existing_enrollments() {
    std::cout << "-- existing enrollment records\n";
    Fixture f;
    SystemContext& context = f.context;
    AdministratorService& service = f.admin_service;

    check_success(context.registration().enroll(1001, 10), "enroll alice",
                  __LINE__);
    check_success(context.registration().enroll(1002, 10), "enroll bob",
                  __LINE__);

    // 有有效选课记录时，课程不能删。
    std::string before = capture_state(context);
    OperationResult result =
        expect_failure(service.remove_course(f.admin, 1),
                       ResultCode::CatalogInUse, "remove course in use",
                       __LINE__);
    check_contains(result.message, "dropping the records does not make the "
                                   "course deletable",
                   "course in use explains history", __LINE__);
    check_contains(result.message, "2 active", "course in use counts",
                   __LINE__);
    check_unchanged(context, before, "remove course keeps state", __LINE__);

    // 有有效选课记录时，教学班不能删。
    result = expect_failure(service.remove_teaching_class(f.admin, 10),
                            ResultCode::CatalogInUse, "remove class in use",
                            __LINE__);
    check_contains(result.message, "active enrollment", "class in use message",
                   __LINE__);
    check_unchanged(context, before, "remove class keeps state", __LINE__);

    // 退课之后记录变成 Dropped，仍然作为历史保留：教学班与课程都不能删。
    check_success(context.registration().drop(1001, 10), "drop alice",
                  __LINE__);
    check_success(context.registration().drop(1002, 10), "drop bob", __LINE__);

    before = capture_state(context);
    result = expect_failure(service.remove_teaching_class(f.admin, 10),
                            ResultCode::CatalogInUse, "remove class with history",
                            __LINE__);
    check_contains(result.message, "dropped enrollment record",
                   "dropped history message", __LINE__);
    check_unchanged(context, before, "dropped class keeps state", __LINE__);

    result = expect_failure(service.remove_course(f.admin, 1),
                            ResultCode::CatalogInUse, "remove course with history",
                            __LINE__);
    check_contains(result.message, "2 dropped", "course history counts",
                   __LINE__);
    check_unchanged(context, before, "dropped course keeps state", __LINE__);

    // 有历史记录的学生不能删，只能停用账号。
    result = expect_failure(service.remove_student(f.admin, 1001),
                            ResultCode::CatalogInUse, "remove student with history",
                            __LINE__);
    check_contains(result.message, "deactivate the account instead of removing it",
                   "student history message", __LINE__);
    check_unchanged(context, before, "remove student keeps state", __LINE__);

    // 只有 Dropped 记录时也不能改教学班所属课程，否则历史归属被改写。
    TeachingClass teaching_class;
    check(context.find_teaching_class(10, teaching_class), "load class 10",
          __LINE__);
    teaching_class.course_id = 2;
    before = capture_state(context);
    result = expect_failure(service.update_teaching_class(f.admin, teaching_class),
                            ResultCode::InvalidInput,
                            "move class with dropped history", __LINE__);
    check_contains(result.message, "dropped", "move class message names history",
                   __LINE__);
    check_unchanged(context, before, "move class keeps state", __LINE__);

    // 重新选上之后，改归属同样被拒绝（有效的记录）。
    check_success(context.registration().enroll(1001, 10), "re-enroll alice",
                  __LINE__);
    check(context.find_teaching_class(10, teaching_class), "reload class 10",
          __LINE__);
    teaching_class.course_id = 2;
    before = capture_state(context);
    result = expect_failure(service.update_teaching_class(f.admin, teaching_class),
                            ResultCode::InvalidInput, "move class with active record",
                            __LINE__);
    check_contains(result.message, "cannot be changed",
                   "move class active message", __LINE__);
    check_unchanged(context, before, "move active class keeps state", __LINE__);
    check_success(context.registration().drop(1001, 10), "drop alice again",
                  __LINE__);

    // 没有记录的教学班可以删，且课程 1 还留着教学班 10。
    check_success(service.remove_teaching_class(f.admin, 11),
                  "remove class without records", __LINE__);

    // 课程 3 的最后一个教学班不能单独删。
    before = capture_state(context);
    result = expect_failure(service.remove_teaching_class(f.admin, 30),
                            ResultCode::CatalogInUse, "remove last class",
                            __LINE__);
    check_contains(result.message, "last", "last class message", __LINE__);
    check_unchanged(context, before, "last class keeps state", __LINE__);

    // 整门课程连同唯一的教学班一起删是允许的。
    result = service.remove_course(f.admin, 3);
    check_success(result, "remove empty course", __LINE__);
    check_contains(result.message, "removed together with 1 teaching class",
                   "cascade message", __LINE__);
    TeachingClass gone;
    check(!context.find_teaching_class(30, gone), "class 30 removed", __LINE__);
    Course course;
    check(!context.find_course(3, course), "course 3 removed", __LINE__);

    // 教师：没课了可以删；仍带班时不能删。
    check_success(service.remove_teacher(f.admin, 300), "remove free teacher",
                  __LINE__);
    before = capture_state(context);
    result = expect_failure(service.remove_teacher(f.admin, 100),
                            ResultCode::CatalogInUse, "remove teacher with classes",
                            __LINE__);
    check_contains(result.message, "still teaches", "teacher in use message",
                   __LINE__);
    check_unchanged(context, before, "remove teacher keeps state", __LINE__);

    std::vector<std::string> problems;
    check_success(service.check_data_integrity(f.admin, problems),
                  "integrity after enrollment tests", __LINE__);
    check(problems.empty(), "no integrity problems", __LINE__,
          problems.empty() ? std::string() : problems.front());
}

// ---------------------------------------------------------------------------
// 三、重复账号：三种角色之间、同角色内部、以及带空格/大小写变体
// ---------------------------------------------------------------------------

void test_duplicate_accounts() {
    std::cout << "-- duplicate accounts\n";
    Fixture f;
    SystemContext& context = f.context;
    AdministratorService& service = f.admin_service;

    std::string before = capture_state(context);

    // 新学生用教师工号。
    OperationResult result = expect_failure(
        service.create_student(
            f.admin,
            Student{1004, "T2015007", "Eve", "Physics", true, {}}),
        ResultCode::DuplicateIdentifier, "student uses teacher number",
        __LINE__);
    check_contains(result.message, "already used by teacher",
                   "cross role message", __LINE__);
    check_unchanged(context, before, "student teacher clash keeps state",
                    __LINE__);

    // 新学生用管理员账号。
    result = expect_failure(
        service.create_student(
            f.admin, Student{1004, "A2020001", "Eve", "Physics", true, {}}),
        ResultCode::DuplicateIdentifier, "student uses admin number", __LINE__);
    check_contains(result.message, "already used by administrator",
                   "admin clash message", __LINE__);
    check_unchanged(context, before, "student admin clash keeps state",
                    __LINE__);

    // 大小写与首尾空格不能绕过唯一性（统一用 normalize_account_key 比较）。
    result = expect_failure(
        service.create_student(
            f.admin, Student{1004, " s2023001 ", "Eve", "Physics", true, {}}),
        ResultCode::DuplicateIdentifier, "student number with spaces", __LINE__);
    check_contains(result.message, "already exists",
                   "normalized clash message", __LINE__);
    check_unchanged(context, before, "normalized clash keeps state", __LINE__);

    // 新教师用学号。
    result = expect_failure(
        service.create_teacher(
            f.admin, Teacher{400, "S2023001", "Qin", "Physics", true, {}}),
        ResultCode::DuplicateIdentifier, "teacher uses student number", __LINE__);
    check_contains(result.message, "already used by student",
                   "teacher clash message", __LINE__);
    check_unchanged(context, before, "teacher clash keeps state", __LINE__);

    // 同角色内部重复：id 与编号。
    expect_failure(
        service.create_student(
            f.admin, Student{1001, "S2023999", "Eve", "Physics", true, {}}),
        ResultCode::DuplicateIdentifier, "duplicate student id", __LINE__);
    expect_failure(
        service.create_student(
            f.admin, Student{1004, "S2023001", "Eve", "Physics", true, {}}),
        ResultCode::DuplicateIdentifier, "duplicate student number", __LINE__);
    expect_failure(
        service.create_teacher(
            f.admin, Teacher{100, "T2099999", "Qin", "Physics", true, {}}),
        ResultCode::DuplicateIdentifier, "duplicate teacher id", __LINE__);
    check_unchanged(context, before, "same role clashes keep state", __LINE__);

    // 空编号 / 空姓名：拒绝。
    expect_failure(service.create_student(
                       f.admin, Student{1004, "   ", "Eve", "Physics", true, {}}),
                   ResultCode::InvalidInput, "blank student number", __LINE__);
    expect_failure(service.create_student(
                       f.admin, Student{1004, "S2023100", "  ", "Physics", true, {}}),
                   ResultCode::InvalidInput, "blank student name", __LINE__);
    expect_failure(service.create_teacher(
                       f.admin, Teacher{400, "", "Qin", "Physics", true, {}}),
                   ResultCode::InvalidInput, "blank teacher number", __LINE__);
    check_unchanged(context, before, "blank fields keep state", __LINE__);

    // 修改别人成已占用的账号。
    Student alice = *context.find_student(1001);
    alice.student_number = "T2015007";
    result = expect_failure(service.update_student(f.admin, alice),
                            ResultCode::DuplicateIdentifier,
                            "update student to teacher number", __LINE__);
    check_contains(result.message, "used by teacher", "update clash message",
                   __LINE__);
    check_unchanged(context, before, "update clash keeps state", __LINE__);

    alice = *context.find_student(1001);
    alice.student_number = "S2023002";
    result = expect_failure(service.update_student(f.admin, alice),
                            ResultCode::DuplicateIdentifier,
                            "update student to another student", __LINE__);
    check_contains(result.message, "1002", "update clash names the owner",
                   __LINE__);
    check_unchanged(context, before, "update same role clash keeps state",
                    __LINE__);

    Teacher wang = *context.find_teacher(100);
    wang.teacher_number = "S2023001";
    expect_failure(service.update_teacher(f.admin, wang),
                   ResultCode::DuplicateIdentifier,
                   "update teacher to student number", __LINE__);
    check_unchanged(context, before, "update teacher clash keeps state",
                    __LINE__);

    // 改成自己账号的大小写变体是允许的（排除自身）。
    alice = *context.find_student(1001);
    alice.student_number = "s2023001";
    check_success(service.update_student(f.admin, alice),
                  "update to own number in lower case", __LINE__);
    check(context.find_student_by_number("S2023001") != nullptr,
          "normalized lookup still finds the student", __LINE__);
    alice = *context.find_student(1001);
    alice.student_number = "S2023001";
    check_success(service.update_student(f.admin, alice), "restore number",
                  __LINE__);

    // 课程代码同样统一比较：只差大小写或首尾空格都算重复。
    before = capture_state(context);
    result = expect_failure(
        service.create_course_with_teaching_class(
            f.admin, Course{4, " se101 ", "Physics", 3},
            make_class(50, 4, 200, 20, 5, 540, 660, "J2-210")),
        ResultCode::DuplicateIdentifier, "course code with spaces", __LINE__);
    check_contains(result.message, "already exists", "course code message",
                   __LINE__);
    check_unchanged(context, before, "course code clash keeps state", __LINE__);

    Course course_three{3, " se101 ", "Computer Science Basics", 5};
    result = expect_failure(service.update_course(f.admin, course_three),
                            ResultCode::DuplicateIdentifier,
                            "update course code with spaces", __LINE__);
    check_contains(result.message, "is used by course_id", "update code message",
                   __LINE__);
    check_unchanged(context, before, "update code clash keeps state", __LINE__);

    // 被拒绝的创建没有新增任何账号。
    check(static_cast<int>(context.students().size()) == 3,
          "student count unchanged", __LINE__);
    check(static_cast<int>(context.teachers().size()) == 3,
          "teacher count unchanged", __LINE__);

    std::vector<std::string> problems;
    check_success(service.check_data_integrity(f.admin, problems),
                  "integrity after duplicate tests", __LINE__);
    check(problems.empty(), "no integrity problems", __LINE__,
          problems.empty() ? std::string() : problems.front());
}

// ---------------------------------------------------------------------------
// 四、无效时段：选课时段格式/顺序、上课时段取值、教师与学生冲突
// ---------------------------------------------------------------------------

void test_invalid_time_slots() {
    std::cout << "-- invalid time slots\n";
    Fixture f;
    SystemContext& context = f.context;
    AdministratorService& service = f.admin_service;
    context.set_now_for_tests("2026-11-28T10:00");

    // Alice 同时在教学班 10（周一 09:00-11:00）与 20（周二）。
    // 把 20 改到周一 10:00-11:40 会和她的 10 冲突，必须拒绝并保留原时段。
    check_success(context.registration().enroll(1001, 10), "enroll alice in 10",
                  __LINE__);
    check_success(context.registration().enroll(1001, 20), "enroll alice in 20",
                  __LINE__);
    TeachingClass class20;
    check(context.find_teaching_class(20, class20), "load class 20", __LINE__);
    class20.meetings.clear();
    class20.meetings.push_back(make_meeting(1, 600, 700, "J2-201"));
    std::string before = capture_state(context);
    OperationResult result = expect_failure(
        service.update_teaching_class(f.admin, class20), ResultCode::TimeConflict,
        "student timetable conflict", __LINE__);
    check_contains(result.message, "already taken by student",
                   "student conflict message", __LINE__);
    check_unchanged(context, before, "student conflict keeps state", __LINE__);
    TeachingClass reloaded;
    check(context.find_teaching_class(20, reloaded), "reload class 20",
          __LINE__);
    check(reloaded.meetings.size() == 1u &&
              reloaded.meetings[0].day_of_week == 2,
          "old meetings kept", __LINE__);

    // 教师冲突：教师 100 周一 09:00-11:00 已有教学班 10。
    TeachingClass conflict = make_class(40, 2, 100, 20, 1, 540, 660, "J2-202");
    before = capture_state(context);
    expect_failure(service.create_teaching_class(f.admin, conflict),
                   ResultCode::TimeConflict, "teacher timetable conflict",
                   __LINE__);
    check_unchanged(context, before, "teacher conflict keeps state", __LINE__);

    // 上课时段取值非法：星期 0/8、起点不小于终点、结束超过 1440、空时段。
    std::vector<TeachingClass> invalid = {
        make_class(41, 2, 200, 20, 0, 540, 660, "J2-203"),
        make_class(42, 2, 200, 20, 8, 540, 660, "J2-204"),
        make_class(43, 2, 200, 20, 2, 660, 660, "J2-205"),
        make_class(44, 2, 200, 20, 2, 700, 600, "J2-206"),
        make_class(45, 2, 200, 20, 2, 1400, 1441, "J2-207"),
        make_class(46, 2, 200, 20, 2, -5, 60, "J2-208"),
    };
    for (const TeachingClass& teaching_class : invalid) {
        before = capture_state(context);
        expect_failure(service.create_teaching_class(f.admin, teaching_class),
                       ResultCode::InvalidInput, "invalid meeting", __LINE__);
        check_unchanged(context, before, "invalid meeting keeps state",
                        __LINE__);
    }
    TeachingClass empty = make_class(47, 2, 200, 20, 2, 540, 660, "J2-209");
    empty.meetings.clear();
    expect_failure(service.create_teaching_class(f.admin, empty),
                   ResultCode::InvalidInput, "empty meeting list", __LINE__);
    check(static_cast<int>(context.teaching_classes().size()) == 4,
          "no teaching class added", __LINE__);

    // 选课时段格式与顺序：用一份"选课尚未开放"的上下文（规则开关为关、
    // 起止时间为空），这样被拒绝的设置若留下"本轮已开放"的副作用，
    // 后面改学分上限就会失败。这条用例专门守住这个顺序。
    Fixture quiet(false);
    quiet.context.set_now_for_tests("2026-11-28T10:00");
    std::string quiet_before = capture_state(quiet.context);
    result = expect_failure(
        quiet.admin_service.set_registration_period(quiet.admin, "2026-02-30T09:00",
                                                    "2026-03-05T18:00"),
        ResultCode::InvalidInput, "impossible date", __LINE__);
    check_contains(result.message, "YYYY-MM-DDTHH:MM", "date format message",
                   __LINE__);
    check(!quiet.context.round_opened(),
          "failed period does not open the round", __LINE__);
    check_unchanged(quiet.context, quiet_before, "impossible date keeps state",
                    __LINE__);

    result = expect_failure(
        quiet.admin_service.set_registration_period(quiet.admin, "2026-11-30T9:00",
                                                    "2026-12-05T18:00"),
        ResultCode::InvalidInput, "single digit hour", __LINE__);
    check_contains(result.message, "YYYY-MM-DDTHH:MM", "width message",
                   __LINE__);

    result = expect_failure(
        quiet.admin_service.set_registration_period(quiet.admin, "2026-11-30T09:00",
                                                    "2026-11-30T09:00"),
        ResultCode::InvalidInput, "same start and end", __LINE__);
    check_contains(result.message, "must start before it ends",
                   "period order message", __LINE__);

    expect_failure(
        quiet.admin_service.set_registration_period(quiet.admin, "2026-12-01T09:00",
                                                    "2026-11-30T09:00"),
        ResultCode::InvalidInput, "reversed period", __LINE__);
    check(!quiet.context.round_opened(), "still not opened after rejects",
          __LINE__);
    check_unchanged(quiet.context, quiet_before, "rejected periods keep state",
                    __LINE__);

    // 既然本轮从未真正开放，学分上限此时仍然可以改 —— 证明上面四个失败
    // 都没有把 round_opened 置真。
    check_success(quiet.admin_service.set_maximum_credits(quiet.admin, 24),
                  "credits can still change", __LINE__);
    check(quiet.context.rule().maximum_credits == 24, "credits updated",
          __LINE__);

    // 合法时段：起止边界与"本轮已开放"的记录。
    check_success(service.set_registration_period(
                      f.admin, "2026-11-30T09:00", "2026-12-05T18:00"),
                  "valid period", __LINE__);
    check(context.registration_window_open_at("2026-11-30T09:00"),
          "window open at start", __LINE__);
    check(context.registration_window_open_at("2026-12-05T17:59"),
          "window open before end", __LINE__);
    check(!context.registration_window_open_at("2026-12-05T18:00"),
          "window closed at end", __LINE__);
    check(context.round_opened(), "round recorded as opened", __LINE__);

    result = expect_failure(service.set_maximum_credits(f.admin, 18),
                            ResultCode::InvalidInput,
                            "credits locked after opening", __LINE__);
    check_contains(result.message, "before this round", "locked credits message",
                   __LINE__);
    check(context.rule().maximum_credits == 18, "credits kept", __LINE__);

    // 关闭选课：清空时段，并且不会因为时间刷新被重新打开。
    check_success(service.close_registration(f.admin), "close registration",
                  __LINE__);
    check(context.rule().opens_at.empty() && context.rule().closes_at.empty(),
          "period cleared", __LINE__);
    check(!context.registration_window_open(), "window closed", __LINE__);
    context.refresh_registration_window();
    check(!context.registration_window_open(), "manual close survives refresh",
          __LINE__);

    // 关闭选课之后学生不能再选课，但管理员维护不受影响。
    expect_failure(context.registration().enroll(1003, 30),
                   ResultCode::RegistrationClosed, "enroll after close",
                   __LINE__);

    std::vector<std::string> problems;
    check_success(service.check_data_integrity(f.admin, problems),
                  "integrity after time tests", __LINE__);
    check(problems.empty(), "no integrity problems", __LINE__,
          problems.empty() ? std::string() : problems.front());
}

// ---------------------------------------------------------------------------
// 五、越权访问：教师只能读本人教学班，学生不能走教师/管理员入口
// ---------------------------------------------------------------------------

void test_privilege_boundaries() {
    std::cout << "-- privilege boundaries\n";
    Fixture f;
    SystemContext& context = f.context;
    AdministratorService& service = f.admin_service;
    TeacherService& teacher_service = f.teacher_service;

    check_success(context.registration().enroll(1001, 10), "enroll alice",
                  __LINE__);
    check_success(context.registration().enroll(1002, 10), "enroll bob",
                  __LINE__);

    // 教师不能做管理员操作。
    std::string before = capture_state(context);
    OperationResult result = expect_failure(
        service.create_course_with_teaching_class(
            f.dr_wang, Course{4, "PH101", "Physics", 3},
            make_class(50, 4, 100, 20, 5, 540, 660, "J1-103")),
        ResultCode::PermissionDenied, "teacher creates course", __LINE__);
    check_contains(result.message, "requires a different role",
                   "teacher create message", __LINE__);
    check_unchanged(context, before, "teacher create keeps state", __LINE__);

    expect_failure(service.remove_course(f.dr_wang, 3),
                   ResultCode::PermissionDenied, "teacher removes course",
                   __LINE__);
    check_unchanged(context, before, "teacher remove keeps state", __LINE__);

    result = expect_failure(
        service.set_registration_period(f.dr_wang, "2026-11-30T09:00",
                                        "2026-12-05T18:00"),
        ResultCode::PermissionDenied, "teacher sets period", __LINE__);
    check_unchanged(context, before, "teacher period keeps state", __LINE__);
    check(!context.round_opened(), "denied period does not open the round",
          __LINE__);

    expect_failure(service.create_student(
                       f.dr_wang, Student{1004, "S2023100", "Eve", "Physics",
                                          true, {}}),
                   ResultCode::PermissionDenied, "teacher creates student",
                   __LINE__);
    check_unchanged(context, before, "teacher people keeps state", __LINE__);

    // 学生不能做任何维护操作。
    expect_failure(service.set_maximum_credits(f.alice, 30),
                   ResultCode::PermissionDenied, "student credits", __LINE__);
    expect_failure(service.close_registration(f.alice),
                   ResultCode::PermissionDenied, "student close", __LINE__);
    expect_failure(service.remove_teaching_class(f.alice, 11),
                   ResultCode::PermissionDenied, "student removes class",
                   __LINE__);
    check_unchanged(context, before, "student maintenance keeps state",
                    __LINE__);

    // 教师只能读自己的教学班：100 有 10、11，读不到 20。
    std::vector<TeachingClassSummary> mine;
    check_success(teacher_service.assigned_teaching_classes(f.dr_wang, mine),
                  "teacher lists own classes", __LINE__);
    check(mine.size() == 2u, "teacher sees two classes", __LINE__,
          "got " + std::to_string(mine.size()));
    check(!mine.empty() && mine[0].teaching_class_id == 10,
          "first class is 10", __LINE__);

    TeachingClassSummary summary;
    result = expect_failure(teacher_service.class_summary(f.dr_wang, 20, summary),
                            ResultCode::PermissionDenied,
                            "teacher reads other class", __LINE__);
    check_contains(result.message, "Permission denied", "read denied message",
                   __LINE__);

    course_registration::ClassRoster roster;
    expect_failure(teacher_service.class_roster(f.dr_wang, 20, roster),
                   ResultCode::PermissionDenied, "teacher reads other roster",
                   __LINE__);

    // 另一个教师同样读不到。
    expect_failure(teacher_service.class_summary(f.dr_li, 10, summary),
                   ResultCode::PermissionDenied, "other teacher reads class",
                   __LINE__);

    // 自己的教学班：容量、人数、余量与名单都对得上选课记录。
    check_success(teacher_service.class_summary(f.dr_wang, 10, summary),
                  "teacher reads own class", __LINE__);
    check(summary.active_enrollments == 2, "teacher sees two students",
          __LINE__);
    check(summary.capacity == 2 && summary.remaining_seats == 0,
          "teacher sees no seat left", __LINE__);
    check_success(teacher_service.class_roster(f.dr_wang, 10, roster),
                  "teacher reads own roster", __LINE__);
    check(roster.students.size() == 2u, "roster has two entries", __LINE__);
    check(!roster.students.empty() &&
              roster.students[0].student_number == "S2023001",
          "roster sorted by number", __LINE__);

    // 管理员可以读任意教学班。
    check_success(teacher_service.class_summary(f.admin, 20, summary),
                  "admin reads any class", __LINE__);
    check(summary.course_code == "MA101", "admin sees the right course",
          __LINE__);
    check(summary.teacher_name == "Li", "admin sees the right teacher",
          __LINE__);
    expect_failure(teacher_service.class_summary(f.admin, 999, summary),
                   ResultCode::TeachingClassNotFound, "admin reads unknown class",
                   __LINE__);

    // 学生走教师入口一律拒绝，而不是返回空列表让调用方自己猜。
    TeachingClassSummary student_summary;
    result = expect_failure(
        teacher_service.assigned_teaching_classes(f.alice, mine),
        ResultCode::PermissionDenied, "student lists classes", __LINE__);
    check_contains(result.message, "Permission denied", "student read message",
                   __LINE__);
    expect_failure(teacher_service.class_summary(f.alice, 10, student_summary),
                   ResultCode::PermissionDenied, "student reads a class",
                   __LINE__);
    course_registration::ClassRoster student_roster;
    expect_failure(teacher_service.class_roster(f.alice, 10, student_roster),
                   ResultCode::PermissionDenied, "student reads a roster",
                   __LINE__);

    std::vector<std::string> problems;
    check_success(service.check_data_integrity(f.admin, problems),
                  "integrity after privilege tests", __LINE__);
    check(problems.empty(), "no integrity problems", __LINE__,
          problems.empty() ? std::string() : problems.front());
}

}  // namespace

int main() {
    std::cout << "admin failure acceptance tests (week 4)\n";
    test_capacity_adjustment();
    test_existing_enrollments();
    test_duplicate_accounts();
    test_invalid_time_slots();
    test_privilege_boundaries();

    if (g_failures == 0) {
        std::cout << "PASS: " << g_checks << " checks, 0 failure(s)\n";
        return 0;
    }
    std::cout << "FAIL: " << g_checks << " checks, " << g_failures
              << " failure(s)\n";
    return 1;
}
