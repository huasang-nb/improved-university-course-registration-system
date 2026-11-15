#include "student_query_and_timetable_services.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace course_registration;

namespace {

int checks = 0;

// 使用显式检查，发布构建定义 NDEBUG 时也会执行每一项验收。
void require(bool condition, const std::string& description) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(description);
    }
}

void expect_code(const OperationResult& result, ResultCode expected) {
    require(result.code == expected,
            "expected code " + std::to_string(static_cast<int>(expected)) +
            ", received " + std::to_string(static_cast<int>(result.code)) +
            ": " + result.message);
}

SystemContext make_context() {
    return SystemContext(
        {{1001, "S001", "Alice", "SE", true},
         {1002, "S002", "Bob", "SE", true}},
        {{100, "T100", "Wang", "SE", true},
         {200, "T200", "Li", "MA", true},
         {300, "T300", "Zhao", "CS", true}},
        {{900, "A900", "Admin", true}},
        {{1, "SE101", "Software Engineering", 3},
         {2, "MA101", "Mathematics", 4},
         {3, "CS101", "Programming", 5},
         {4, "SE102", "Adjacent Session", 3}},
        {{10, 1, 100, 1, {{1, 540, 600, "A101"}}},
         {11, 1, 200, 1, {{2, 540, 600, "A102"}}},
         {20, 2, 200, 1, {{1, 570, 630, "B101"}}},
         {30, 3, 300, 1, {{3, 540, 600, "C101"}}},
         {40, 4, 100, 1, {{1, 600, 660, "A103"}}}},
        {false, 6, "2026-11-15T09:00", "2026-11-15T11:00"});
}

struct Fixture {
    SystemContext context;
    StudentQueryService student_query;
    TeacherService teacher_query;
    AdministratorService administration;
    UserIdentity admin{900, "A900", Role::Administrator};

    Fixture()
        : context(make_context()), student_query(context),
          teacher_query(context), administration(context) {
        context.set_now_for_tests("2026-11-15T10:00");
    }
};

using ActivePair = std::pair<int, int>;  // 学生编号与教学班编号。

// 保存完整选退课记录，失败后可发现误插入、误删除或误改状态。
std::string enrollment_state(const SystemContext& context) {
    std::ostringstream state;
    for (const Enrollment& entry : context.all_enrollments()) {
        state << entry.student_id << '/' << entry.teaching_class_id << '/'
              << static_cast<int>(entry.status) << '\n';
    }
    return state.str();
}

void expect_records_unchanged(const Fixture& fixture, const std::string& before) {
    require(enrollment_state(fixture.context) == before,
            "rejected operation changed enrollment records");
}

// 期望名单由场景直接给出，独立核对三个模块的公开查询入口。
void expect_views(Fixture& fixture, std::vector<ActivePair> expected,
                  std::size_t total_records) {
    const auto records = fixture.context.all_enrollments();
    require(records.size() == total_records, "unexpected total history count");
    std::vector<ActivePair> active;
    for (const auto& entry : records) {
        if (entry.status == EnrollmentStatus::Active) {
            active.push_back({entry.student_id, entry.teaching_class_id});
        }
    }
    std::sort(active.begin(), active.end());
    std::sort(expected.begin(), expected.end());
    require(active == expected, "active enrollment owners differ from scenario");

    for (const auto& teaching_class : fixture.context.teaching_classes()) {
        std::vector<int> expected_students;
        for (const auto& entry : expected) {
            if (entry.second == teaching_class.teaching_class_id) {
                expected_students.push_back(entry.first);
            }
        }
        const int expected_count = static_cast<int>(expected_students.size());
        const int expected_remaining = teaching_class.capacity - expected_count;
        SeatsView seats{};
        expect_code(fixture.student_query.view_available_seats(
                        teaching_class.teaching_class_id, seats), ResultCode::Success);
        require(seats.active_enrollments == expected_count &&
                    seats.remaining_seats == expected_remaining,
                "student seats differ from expected owners");

        const UserIdentity teacher{teaching_class.teacher_id, "", Role::Teacher};
        TeachingClassSummary summary{};
        ClassRoster roster{};
        expect_code(fixture.teacher_query.class_summary(
                        teacher, teaching_class.teaching_class_id, summary),
                    ResultCode::Success);
        expect_code(fixture.teacher_query.class_roster(
                        teacher, teaching_class.teaching_class_id, roster),
                    ResultCode::Success);
        std::vector<int> actual_students;
        for (const auto& person : roster.students) {
            actual_students.push_back(person.student_id);
        }
        std::sort(actual_students.begin(), actual_students.end());
        require(actual_students == expected_students, "teacher roster owners differ");
        require(summary.active_enrollments == expected_count &&
                    summary.remaining_seats == expected_remaining &&
                    roster.active_enrollments == expected_count &&
                    roster.remaining_seats == expected_remaining &&
                    fixture.context.enrollment_count(teaching_class.teaching_class_id) == expected_count &&
                    fixture.context.remaining_seats(teaching_class.teaching_class_id) == expected_remaining,
                "teacher summary and shared counts differ");
    }
    for (const auto& student : fixture.context.students()) {
        std::vector<int> expected_classes;
        for (const auto& entry : expected) {
            if (entry.first == student.student_id) expected_classes.push_back(entry.second);
        }
        std::vector<TimetableEntry> timetable;
        expect_code(fixture.student_query.view_timetable(
                        {student.student_id, student.student_number, Role::Student}, timetable),
                    ResultCode::Success);
        std::vector<int> actual_classes;
        for (const auto& entry : timetable) actual_classes.push_back(entry.teaching_class_id);
        std::sort(actual_classes.begin(), actual_classes.end());
        std::sort(expected_classes.begin(), expected_classes.end());
        require(actual_classes == expected_classes, "timetable differs from expected enrollments");
    }
    std::vector<std::string> problems;
    expect_code(fixture.administration.check_data_integrity(fixture.admin, problems),
                ResultCode::Success);
    require(problems.empty(), "integrity check reported problems");
}

void history_and_seat_transfer() {
    Fixture f;
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_views(f, {{1001, 10}}, 1);
    expect_code(f.context.registration().drop(1001, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1002, 10), ResultCode::Success);
    expect_views(f, {{1002, 10}}, 2);
    const auto before = enrollment_state(f.context);
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::TeachingClassFull);
    expect_records_unchanged(f, before);
    expect_views(f, {{1002, 10}}, 2);
    expect_code(f.context.registration().drop(1002, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_views(f, {{1001, 10}}, 2);  // 重选恢复原记录，历史总数保持 2。
    const auto reentered = enrollment_state(f.context);
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::AlreadyEnrolled);
    expect_records_unchanged(f, reentered);
    expect_views(f, {{1001, 10}}, 2);
}

void duplicate_before_full() {
    Fixture f;
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1002, 11), ResultCode::Success);
    const auto before = enrollment_state(f.context);
    expect_code(f.context.registration().enroll(1001, 11), ResultCode::AlreadyEnrolled);
    expect_records_unchanged(f, before);
    expect_views(f, {{1001, 10}, {1002, 11}}, 2);
}

void conflict_before_credits_and_full() {
    Fixture f;
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1002, 20), ResultCode::Success);
    const auto before = enrollment_state(f.context);
    // 数学同时满足时间冲突、总学分 7 超过上限 6、教学班已满。
    expect_code(f.context.registration().enroll(1001, 20), ResultCode::TimeConflict);
    expect_records_unchanged(f, before);
    expect_views(f, {{1001, 10}, {1002, 20}}, 2);
}

void credits_before_full() {
    Fixture f;
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1002, 30), ResultCode::Success);
    const auto before = enrollment_state(f.context);
    expect_code(f.context.registration().enroll(1001, 30), ResultCode::CreditLimitExceeded);
    expect_records_unchanged(f, before);
    expect_views(f, {{1001, 10}, {1002, 30}}, 2);
}

void opening_boundary() {
    Fixture f;
    f.context.set_now_for_tests("2026-11-15T08:59");
    const auto before = enrollment_state(f.context);
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::RegistrationClosed);
    expect_records_unchanged(f, before);
    expect_views(f, {}, 0);
    f.context.set_now_for_tests("2026-11-15T09:00");
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_views(f, {{1001, 10}}, 1);
}

void closing_boundary_keeps_active_record() {
    Fixture f;
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1002, 30), ResultCode::Success);
    f.context.set_now_for_tests("2026-11-15T11:00");
    const auto before = enrollment_state(f.context);
    expect_code(f.context.registration().drop(1001, 10), ResultCode::RegistrationClosed);
    expect_code(f.context.registration().enroll(1002, 10), ResultCode::RegistrationClosed);
    expect_records_unchanged(f, before);
    expect_views(f, {{1001, 10}, {1002, 30}}, 2);
}

void adjacent_times_and_exact_credit_limit() {
    Fixture f;
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    // 10:00 刚下课即可接 10:00 的下一节课，总学分恰好达到上限 6。
    expect_code(f.context.registration().enroll(1001, 40), ResultCode::Success);
    expect_views(f, {{1001, 10}, {1001, 40}}, 2);
}

void invalid_class_preserves_other_students() {
    Fixture f;
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1002, 30), ResultCode::Success);
    const auto before = enrollment_state(f.context);
    expect_code(f.context.registration().enroll(1001, 9999), ResultCode::TeachingClassNotFound);
    expect_code(f.context.registration().drop(1001, 30), ResultCode::ActiveEnrollmentNotFound);
    expect_records_unchanged(f, before);
    expect_views(f, {{1001, 10}, {1002, 30}}, 2);
}

void rejected_capacity_change_keeps_seats() {
    Fixture f;
    expect_code(f.administration.set_teaching_class_capacity(f.admin, 10, 2), ResultCode::Success);
    expect_code(f.context.registration().enroll(1001, 10), ResultCode::Success);
    expect_code(f.context.registration().enroll(1002, 10), ResultCode::Success);
    const auto before = enrollment_state(f.context);
    expect_code(f.administration.set_teaching_class_capacity(f.admin, 10, 1),
                ResultCode::CapacityBelowEnrollment);
    TeachingClass current{};
    require(f.context.find_teaching_class(10, current) && current.capacity == 2,
            "rejected capacity update changed catalog");
    expect_records_unchanged(f, before);
    expect_views(f, {{1001, 10}, {1002, 10}}, 2);
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> scenarios[] = {
        {"history and seat transfer", history_and_seat_transfer},
        {"duplicate before full", duplicate_before_full},
        {"time conflict before credits and full", conflict_before_credits_and_full},
        {"credit limit before full", credits_before_full},
        {"opening boundary without refresh", opening_boundary},
        {"closing boundary keeps active records", closing_boundary_keeps_active_record},
        {"adjacent times and exact credit limit", adjacent_times_and_exact_credit_limit},
        {"invalid requests preserve other students", invalid_class_preserves_other_students},
        {"rejected capacity change keeps seats", rejected_capacity_change_keeps_seats},
    };
    int failures = 0;
    for (const auto& scenario : scenarios) {
        try {
            scenario.second();
            std::cout << "PASS: " << scenario.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL: " << scenario.first << ": " << error.what() << '\n';
        }
    }
    std::cout << "Acceptance: " << checks << " checks, " << failures
              << " failed scenario(s), 9 scenario(s)\n";
    return failures == 0 ? 0 : 1;
}
