#include "persistent_administration_service.hpp"
#include "persistent_registration_service.hpp"
#include "student_query_and_timetable_services.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace course_registration;
namespace fs = std::filesystem;
namespace {
int checks = 0;
int scenarios = 0;
const UserIdentity alice{1001, "S2023001", Role::Student};
const UserIdentity bob{1002, "S2023002", Role::Student};
const UserIdentity admin{900, "A2020001", Role::Administrator};
// 条件不满足时抛出异常，让 Debug 和 Release 都能报告测试失败。
// main 会捕获异常、输出失败原因，并保留测试文件供排查。
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void expect_result(const OperationResult& result, ResultCode expected) {
    require(result.code == expected, "unexpected result: " + result.message);
}
std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "cannot read test file");
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}
void write_file(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    require(bool(stream), "cannot write test file");
}
StorageOptions make_storage_options(const fs::path& path) {
    StorageOptions result;
    result.path = path.string();
    return result;
}
SystemContext make_context() {
    auto context = make_seed_context();
    // 示例教学班 10 有两个名额，Alice 和 Bob 都可以选择它。
    // 测试中固定使用选课开放期间的时间，避免实际运行日期影响测试结果。
    context.set_now_for_tests("2026-11-18T10:00");

    // 给每个账号填入不同的测试字符串，放在 password_hash 字段中。
    // 后面的检查会确认这些字符串在保存、读取和选退课后仍然存在。
    // 如果其中一个字段被错误地清空，测试就会失败。这些字符串只用于测试文件保存。
    for (Student student : context.students()) {
        student.password_hash = "test-student-hash-" + std::to_string(student.student_id);
        context.apply_student_change(student);
    }
    for (Teacher teacher : context.teachers()) {
        teacher.password_hash = "test-teacher-hash-" + std::to_string(teacher.teacher_id);
        context.apply_teacher_change(teacher);
    }
    for (Administrator administrator : context.administrators()) {
        administrator.password_hash = "test-admin-hash-" +
                                      std::to_string(administrator.administrator_id);
        context.apply_administrator_change(administrator);
    }
    return context;
}

// 把内存中的课程、人员、规则和选课记录拼成一个字符串，用于比较操作前后的数据。
// 这里逐个读取字段，不使用保存 JSON 的代码，避免保存代码和测试一起漏掉同一个字段。
// 文本字段加上引号，便于区分字段内容和字段之间的分隔符。
// 固定测试时间不写入文件，因此不参与比较。人数、名额和课表会在下面单独检查。
std::string describe_business_state(const SystemContext& context) {
    std::ostringstream out;
    const auto rule = context.rule();
    out << rule.registration_open << ':' << rule.maximum_credits << ':'
        << std::quoted(rule.opens_at) << ':' << std::quoted(rule.closes_at) << ':'
        << context.round_opened();
    for (const auto& course : context.courses()) {
        out << "|C" << course.course_id << ':' << std::quoted(course.course_code) << ':'
            << std::quoted(course.course_name) << ':' << course.credits;
    }
    for (const auto& teaching_class : context.teaching_classes()) {
        out << "|T" << teaching_class.teaching_class_id << ':' << teaching_class.course_id
            << ':' << teaching_class.teacher_id << ':' << teaching_class.capacity;
        for (const auto& meeting : teaching_class.meetings) {
            out << ':' << meeting.day_of_week << ':' << meeting.start_minute << ':'
                << meeting.end_minute << ':' << std::quoted(meeting.room);
        }
    }
    for (const auto& student : context.students()) {
        out << "|S" << student.student_id << ':' << std::quoted(student.student_number)
            << ':' << std::quoted(student.name) << ':' << std::quoted(student.major)
            << ':' << student.account_active << ':' << std::quoted(student.password_hash);
    }
    for (const auto& teacher : context.teachers()) {
        out << "|P" << teacher.teacher_id << ':' << std::quoted(teacher.teacher_number)
            << ':' << std::quoted(teacher.name) << ':' << std::quoted(teacher.department)
            << ':' << teacher.account_active << ':' << std::quoted(teacher.password_hash);
    }
    for (const auto& administrator : context.administrators()) {
        out << "|A" << administrator.administrator_id << ':'
            << std::quoted(administrator.account_number) << ':'
            << std::quoted(administrator.name) << ':' << administrator.account_active
            << ':' << std::quoted(administrator.password_hash);
    }
    for (const auto& enrollment : context.all_enrollments()) {
        out << "|E" << enrollment.student_id << ':' << enrollment.teaching_class_id << ':'
            << static_cast<int>(enrollment.status);
    }
    return out.str();
}
// 先数出状态为 Active 的选课记录，再检查各个查询结果是否与这些记录一致。
// 检查的内容包括已选人数、剩余名额、教师看到的学生名单，以及每个学生的课表。
void expect_consistent_views(SystemContext& context, StudentQueryService& query,
                             TeacherService& teacher) {
    const auto records = context.all_enrollments();
    for (const auto& teaching_class : context.teaching_classes()) {
        int expected_count = 0;
        for (const auto& enrollment : records) {
            if (enrollment.teaching_class_id == teaching_class.teaching_class_id &&
                enrollment.status == EnrollmentStatus::Active) {
                ++expected_count;
            }
        }
        require(context.enrollment_count(teaching_class.teaching_class_id) == expected_count,
                "enrollment count differs from history");
        require(context.remaining_seats(teaching_class.teaching_class_id) ==
                    teaching_class.capacity - expected_count,
                "remaining seats differ from history");
        TeachingClassSummary summary{};
        ClassRoster roster{};
        // 这里用管理员身份读取所有教学班，方便检查每个班的数据。
        // 教师是否只能查看自己的教学班，由教师和管理员模块的测试负责检查。
        expect_result(teacher.class_summary(admin, teaching_class.teaching_class_id, summary),
                      ResultCode::Success);
        expect_result(teacher.class_roster(admin, teaching_class.teaching_class_id, roster),
                      ResultCode::Success);
        require(summary.active_enrollments == expected_count &&
                    static_cast<int>(roster.students.size()) == expected_count,
                "teacher views differ from history");
        for (const auto& enrollment : records) {
            if (enrollment.teaching_class_id != teaching_class.teaching_class_id ||
                enrollment.status != EnrollmentStatus::Active) {
                continue;
            }
            bool found = false;
            for (const auto& student : roster.students) {
                if (student.student_id == enrollment.student_id) {
                    found = true;
                }
            }
            require(found, "teacher roster has wrong student ownership");
        }
    }
    for (const auto& student : context.students()) {
        std::vector<TimetableEntry> timetable;
        expect_result(query.view_timetable({student.student_id, student.student_number,
                                           Role::Student}, timetable), ResultCode::Success);
        std::size_t expected_rows = 0;
        for (const auto& enrollment : records) {
            if (enrollment.student_id != student.student_id ||
                enrollment.status != EnrollmentStatus::Active) {
                continue;
            }
            TeachingClass teaching_class{};
            require(context.find_teaching_class(enrollment.teaching_class_id, teaching_class),
                    "missing class in test fixture");
            expected_rows += teaching_class.meetings.size();
            for (const auto& meeting : teaching_class.meetings) {
                bool found = false;
                for (const auto& row : timetable) {
                    if (row.teaching_class_id == teaching_class.teaching_class_id &&
                        row.meeting.day_of_week == meeting.day_of_week &&
                        row.meeting.start_minute == meeting.start_minute &&
                        row.meeting.end_minute == meeting.end_minute &&
                        row.meeting.room == meeting.room) {
                        found = true;
                    }
                }
                require(found, "student timetable has wrong class or time");
            }
        }
        require(timetable.size() == expected_rows, "student timetable count is stale");
    }
    std::vector<std::string> problems;
    expect_result(validate_state(context, problems), ResultCode::Success);
    require(problems.empty(), "state integrity failed");
}
struct Fixture {
    // 选退课服务和查询服务都使用下面这个 context 对象。
    // 选课或退课成功后，这里已有的查询服务也应该立即查到修改后的数据。
    SystemContext context = make_context();
    StudentQueryService query{context};
    TeacherService teacher{context};
    StorageOptions storage;
    PersistentRegistrationService registration;
    explicit Fixture(const fs::path& path)
        : storage(make_storage_options(path)), registration(context, storage) {
        expect_result(save_state(context, storage), ResultCode::Success);
    }
    void check_views() { expect_consistent_views(context, query, teacher); }
};

struct PublishedState {
    std::string memory;
    std::string primary;
    std::string backup;
};

// 操作成功后，记下内存中的数据、主 JSON 文件内容和 .bak 备份内容。
// 接下来发出一个应当失败的请求，再用这三份内容检查有没有数据被错误地改动。
PublishedState capture_published_state(const Fixture& fixture) {
    PublishedState published{describe_business_state(fixture.context),
                             read_file(fixture.storage.path),
                             read_file(fixture.storage.path + ".bak")};
    // 主文件和备份要有不同内容，才能判断失败的请求有没有错误地调用保存。
    // 如果两份内容完全相同，即使备份被主文件覆盖，比较时也发现不了。
    require(published.primary != published.backup,
            "test needs distinct snapshots to detect an unexpected save");
    return published;
}

void expect_unchanged(const SystemContext& context, const StorageOptions& storage,
                       const PublishedState& before) {
    require(describe_business_state(context) == before.memory, "rejection changed in-memory state");
    require(read_file(storage.path) == before.primary, "rejection changed the primary snapshot");
    require(read_file(storage.path + ".bak") == before.backup, "rejection changed the backup");
}

void expect_single_seat_class(const StudentQueryService& query,
                               const TeacherService& teacher, int active) {
    SeatsView seats{};
    expect_result(query.view_available_seats(10, seats), ResultCode::Success);
    require(seats.capacity == 1 && seats.active_enrollments == active &&
                seats.remaining_seats == 1 - active,
            "student seat view differs from expected capacity or occupancy");
    TeachingClassSummary summary{};
    expect_result(teacher.class_summary(admin, 10, summary), ResultCode::Success);
    require(summary.capacity == 1 && summary.active_enrollments == active &&
                summary.remaining_seats == 1 - active,
            "teacher summary differs from expected capacity or occupancy");
}

// 检查同一个学生连续两次选择同一个班，以及连续两次退掉同一个班的情况。
// 第二次操作应返回错误，内存、主 JSON 文件和备份都应保留第一次操作后的数据。
void repeated_requests(const fs::path& path) {
    Fixture fixture(path);
    expect_result(fixture.registration.enroll(alice, 10), ResultCode::Success);
    const auto enrolled = capture_published_state(fixture);
    expect_result(fixture.registration.enroll(alice, 10), ResultCode::AlreadyEnrolled);
    expect_unchanged(fixture.context, fixture.storage, enrolled);
    require(fixture.context.all_enrollments().size() == 1 &&
                fixture.context.active_enrollments(alice.user_id).size() == 1,
            "duplicate enrollment changed the active record");
    fixture.check_views();

    expect_result(fixture.registration.drop(alice, 10), ResultCode::Success);
    const auto dropped = capture_published_state(fixture);
    expect_result(fixture.registration.drop(alice, 10), ResultCode::ActiveEnrollmentNotFound);
    expect_unchanged(fixture.context, fixture.storage, dropped);
    require(fixture.context.all_enrollments().size() == 1 &&
                fixture.context.all_enrollments().front().status == EnrollmentStatus::Dropped &&
                fixture.context.remaining_seats(10) == 2,
            "duplicate drop changed history or released another seat");

    // 重新创建 SystemContext 并读取已保存的文件，模拟关闭程序后再打开。
    // 读取后应该仍能看到退课记录，再退一次课也应该被拒绝。
    auto reloaded = make_context();
    expect_result(load_state(fixture.storage, reloaded), ResultCode::Success);
    require(describe_business_state(reloaded) == dropped.memory, "reload changed the dropped record");
    PersistentRegistrationService registration(reloaded, fixture.storage);
    expect_result(registration.drop(alice, 10), ResultCode::ActiveEnrollmentNotFound);
    expect_unchanged(reloaded, fixture.storage, dropped);
    StudentQueryService query(reloaded);
    TeacherService teacher(reloaded);
    expect_consistent_views(reloaded, query, teacher);
}

// 检查管理员减少名额后，学生选课和退课是否仍然正确。
// Alice 退课后，Bob 应该可以立即选入这个班，重新读取文件后也应保留这个结果。
void capacity_change_and_seat_transfer(const fs::path& path) {
    Fixture fixture(path);
    PersistentAdministratorService administration(fixture.context, fixture.storage);
    expect_result(fixture.registration.enroll(alice, 10), ResultCode::Success);
    // 此时只有 Alice 在这个班，把容量从 2 改为 1 应该成功。
    // 修改后一个名额已被 Alice 占用，所以这个班已经满员。
    expect_result(administration.set_teaching_class_capacity(admin, 10, 1), ResultCode::Success);
    expect_single_seat_class(fixture.query, fixture.teacher, 1);

    const auto full = capture_published_state(fixture);
    expect_result(fixture.registration.enroll(bob, 10), ResultCode::TeachingClassFull);
    expect_unchanged(fixture.context, fixture.storage, full);

    expect_result(fixture.registration.drop(alice, 10), ResultCode::Success);
    expect_single_seat_class(fixture.query, fixture.teacher, 0);
    require(fixture.context.all_enrollments().size() == 1 &&
                fixture.context.all_enrollments().front().status == EnrollmentStatus::Dropped,
            "seat release did not preserve the original enrollment history");
    expect_result(fixture.registration.enroll(bob, 10), ResultCode::Success);
    expect_single_seat_class(fixture.query, fixture.teacher, 1);

    const auto transferred = capture_published_state(fixture);
    auto reloaded = make_context();
    expect_result(load_state(fixture.storage, reloaded), ResultCode::Success);
    require(describe_business_state(reloaded) == transferred.memory,
            "reload changed capacity or enrollment history");
    require(reloaded.all_enrollments().size() == 2 &&
                reloaded.active_enrollments(alice.user_id).empty() &&
                reloaded.active_enrollments(bob.user_id).size() == 1 &&
                reloaded.active_enrollments(bob.user_id).front().teaching_class_id == 10,
            "reload did not preserve the transfer to the second student");
    StudentQueryService query(reloaded);
    TeacherService teacher(reloaded);
    expect_single_seat_class(query, teacher, 1);
    expect_consistent_views(reloaded, query, teacher);
    PersistentRegistrationService registration(reloaded, fixture.storage);
    // 虽然 Alice 以前选过这个班，但现在唯一的名额已被 Bob 占用。
    // Alice 再次选课时仍然应该收到满员错误。
    expect_result(registration.enroll(alice, 10), ResultCode::TeachingClassFull);
    expect_unchanged(reloaded, fixture.storage, transferred);
}

// 先让两个学生选入同一个班，再尝试把容量改成 1，这次修改应该被拒绝。
// 检查内存和文件都保留原来的数据，重新读取文件后，两名学生也应该仍在班上。
// 然后让一个学生退课，再把容量改成 1，这时应该能够成功。
void capacity_below_enrollment_preserves_state(const fs::path& path) {
    Fixture fixture(path);
    PersistentAdministratorService administration(fixture.context, fixture.storage);
    expect_result(fixture.registration.enroll(alice, 10), ResultCode::Success);
    expect_result(fixture.registration.enroll(bob, 10), ResultCode::Success);
    const auto full = capture_published_state(fixture);

    // 容量 1 本身是合法的正数。这次应当因为班里已有两人而失败，返回 CapacityBelowEnrollment。
    expect_result(administration.set_teaching_class_capacity(admin, 10, 1),
                  ResultCode::CapacityBelowEnrollment);
    expect_unchanged(fixture.context, fixture.storage, full);
    fixture.check_views();

    auto reloaded = make_context();
    expect_result(load_state(fixture.storage, reloaded), ResultCode::Success);
    expect_unchanged(reloaded, fixture.storage, full);
    require(reloaded.remaining_seats(10) == 0 && reloaded.enrollment_count(10) == 2,
            "rejected capacity change lost an enrollment or changed seats after reload");
    StudentQueryService query(reloaded);
    TeacherService teacher(reloaded);
    expect_consistent_views(reloaded, query, teacher);

    PersistentRegistrationService registration(reloaded, fixture.storage);
    PersistentAdministratorService restarted_administration(reloaded, fixture.storage);
    expect_result(registration.drop(bob, 10), ResultCode::Success);
    expect_result(restarted_administration.set_teaching_class_capacity(admin, 10, 1),
                  ResultCode::Success);
    expect_single_seat_class(query, teacher, 1);
    require(reloaded.all_enrollments().size() == 2 &&
                reloaded.active_enrollments(bob.user_id).empty(),
            "drop and legal capacity change must retain both students' history");
    expect_consistent_views(reloaded, query, teacher);

    // 再读一次文件，确认 Bob 的退课和容量改为 1 的结果都已经保存。
    // 此时班里应当只有 Alice，她占用剩下的这一个名额。
    auto final_context = make_context();
    expect_result(load_state(fixture.storage, final_context), ResultCode::Success);
    require(describe_business_state(final_context) == describe_business_state(reloaded),
            "reload lost the drop or the subsequent legal capacity change");
}

void passed(const char* name) {
    ++scenarios;
    std::cout << "PASS: " << name << '\n';
}
}

int main() {
    const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("team4_yuhua_registration_" + std::to_string(unique));
    try {
        fs::create_directories(root);
        repeated_requests(root / "repeated_requests.json");
        passed("same-class enroll/drop retries preserve state and snapshots");
        capacity_change_and_seat_transfer(root / "seat_transfer.json");
        passed("capacity changes and seat transfers survive reload");
        capacity_below_enrollment_preserves_state(root / "rejected_capacity.json");
        passed("capacity below enrollment preserves state and allows later valid changes");
        {
            Fixture fixture(root / "roundtrip.json");
            expect_result(fixture.registration.enroll(alice, 10), ResultCode::Success);
            expect_result(fixture.registration.enroll(bob, 10), ResultCode::Success);
            fixture.check_views();
            expect_result(fixture.registration.drop(alice, 10), ResultCode::Success);
            fixture.check_views();
            require(fixture.context.all_enrollments().size() == 2 &&
                        fixture.context.all_enrollments()[0].status == EnrollmentStatus::Dropped,
                    "drop must preserve history");
            auto restarted = make_context();
            expect_result(load_state(fixture.storage, restarted), ResultCode::Success);
            require(describe_business_state(restarted) == describe_business_state(fixture.context),
                    "restart changed business fields");
            StudentQueryService restarted_query(restarted);
            TeacherService restarted_teacher(restarted);
            expect_consistent_views(restarted, restarted_query, restarted_teacher);
            PersistentRegistrationService restarted_registration(restarted, fixture.storage);
            expect_result(restarted_registration.enroll(alice, 10), ResultCode::Success);
            require(restarted.all_enrollments().size() == 2,
                    "reenroll must reactivate the same historical record");
            expect_consistent_views(restarted, restarted_query, restarted_teacher);
            passed("enroll/drop/restart/reactivation and shared views");
        }
        {
            Fixture fixture(root / "business.json");
            expect_result(fixture.registration.enroll(alice, 10), ResultCode::Success);
            const auto before = describe_business_state(fixture.context);
            const auto disk = read_file(fixture.storage.path);
            const auto backup = read_file(fixture.storage.path + ".bak");
            expect_result(fixture.registration.enroll(alice, 11), ResultCode::AlreadyEnrolled);
            expect_result(fixture.registration.enroll(alice, 20), ResultCode::TimeConflict);
            expect_result(fixture.registration.drop(alice, 30), ResultCode::ActiveEnrollmentNotFound);
            expect_result(fixture.registration.enroll(alice, 999), ResultCode::TeachingClassNotFound);
            require(describe_business_state(fixture.context) == before &&
                        read_file(fixture.storage.path) == disk &&
                        read_file(fixture.storage.path + ".bak") == backup,
                    "business rejection wrote state or files");
            fixture.check_views();
            passed("business failures preserve state and skip storage");
        }
        {
            Fixture fixture(root / "identity.json");
            const auto before = describe_business_state(fixture.context);
            const auto disk = read_file(fixture.storage.path);
            expect_result(fixture.registration.enroll(admin, 10), ResultCode::PermissionDenied);
            expect_result(fixture.registration.enroll({1001, "T2015007", Role::Teacher}, 10),
                 ResultCode::PermissionDenied);
            expect_result(fixture.registration.enroll({1001, bob.account_number, Role::Student}, 10),
                 ResultCode::PermissionDenied);
            expect_result(fixture.registration.enroll({999, alice.account_number, Role::Student}, 10),
                 ResultCode::RecordNotFound);
            expect_result(fixture.registration.enroll({1001, "  ", Role::Student}, 10),
                 ResultCode::PermissionDenied);
            require(describe_business_state(fixture.context) == before &&
                        read_file(fixture.storage.path) == disk,
                    "identity rejection changed data");
            expect_result(fixture.registration.enroll({1001, " s2023001 ", Role::Student}, 10),
                 ResultCode::Success);
            auto disabled = *fixture.context.find_student(1001);
            disabled.account_active = false;
            fixture.context.apply_student_change(disabled);
            expect_result(save_state(fixture.context, fixture.storage), ResultCode::Success);
            const auto inactive_state = describe_business_state(fixture.context);
            const auto inactive_disk = read_file(fixture.storage.path);
            expect_result(fixture.registration.drop(alice, 10), ResultCode::PermissionDenied);
            expect_result(fixture.registration.enroll(alice, 30), ResultCode::PermissionDenied);
            require(describe_business_state(fixture.context) == inactive_state &&
                        read_file(fixture.storage.path) == inactive_disk,
                    "inactive account changed data");
            auto restarted = make_context();
            expect_result(load_state(fixture.storage, restarted), ResultCode::Success);
            require(!restarted.find_student(1001)->account_active &&
                        restarted.all_enrollments().size() == 1,
                    "restart lost inactive account or enrollment");
            PersistentRegistrationService service(restarted, fixture.storage);
            expect_result(service.drop(alice, 10), ResultCode::PermissionDenied);
            passed("role/account/disabled checks and normalized account identity");
        }
        {
            auto context = make_context();
            StudentQueryService query(context);
            TeacherService teacher(context);
            const fs::path parent = root / "blocked_parent";
            write_file(parent, "not a directory");
            PersistentRegistrationService blocked(context, make_storage_options(parent / "state.json"));
            const auto before = describe_business_state(context);
            expect_result(blocked.enroll(alice, 10), ResultCode::StorageIoError);
            require(describe_business_state(context) == before && !fs::exists(parent / "state.json"),
                    "failed save published candidate or round-opened flag");
            expect_consistent_views(context, query, teacher);
            expect_result(context.registration().enroll(1001, 10), ResultCode::Success);
            const auto enrolled = describe_business_state(context);
            expect_result(blocked.drop(alice, 10), ResultCode::StorageIoError);
            require(describe_business_state(context) == enrolled, "failed drop save changed history");
            expect_consistent_views(context, query, teacher);
            expect_result(blocked.enroll(alice, 11), ResultCode::AlreadyEnrolled);
            expect_result(blocked.drop(alice, 30), ResultCode::ActiveEnrollmentNotFound);
            passed("save failures keep enroll/drop and lock state unchanged");
        }
        {
            Fixture fixture(root / "window.json");
            RegistrationRule rule{true, 18, "2026-11-18T09:00", "2026-11-18T11:00"};
            fixture.context.apply_rule_change(rule);
            fixture.context.set_now_for_tests("2026-11-18T09:00");
            expect_result(fixture.registration.enroll(alice, 10), ResultCode::Success);
            require(fixture.context.round_opened(), "automatic opening was not persisted");
            fixture.context.set_now_for_tests("2026-11-18T11:00");
            const auto before = describe_business_state(fixture.context);
            const auto disk = read_file(fixture.storage.path);
            expect_result(fixture.registration.drop(alice, 10), ResultCode::RegistrationClosed);
            expect_result(fixture.registration.enroll(bob, 10), ResultCode::RegistrationClosed);
            require(describe_business_state(fixture.context) == before &&
                        read_file(fixture.storage.path) == disk,
                    "closed-window request changed data");
            auto restarted = make_context();
            expect_result(load_state(fixture.storage, restarted), ResultCode::Success);
            AdministratorService administration(restarted);
            expect_result(administration.close_registration(admin), ResultCode::Success);
            expect_result(save_state(restarted, fixture.storage), ResultCode::Success);
            auto paused = make_context();
            expect_result(load_state(fixture.storage, paused), ResultCode::Success);
            paused.set_now_for_tests("2026-11-18T10:00");
            AdministratorService paused_admin(paused);
            expect_result(paused_admin.set_maximum_credits(admin, 30), ResultCode::InvalidInput);
            expect_result(paused_admin.set_registration_period(admin, "2030-01-01T09:00",
                                                      "2030-01-02T11:00"), ResultCode::Success);
            expect_result(save_state(paused, fixture.storage), ResultCode::Success);
            auto future = make_context();
            expect_result(load_state(fixture.storage, future), ResultCode::Success);
            future.set_now_for_tests("2026-11-18T10:00");
            AdministratorService future_admin(future);
            expect_result(future_admin.set_maximum_credits(admin, 30), ResultCode::InvalidInput);
            require(future.round_opened() && future.all_enrollments().size() == 1,
                    "restart lost round history or active record");
            passed("opening/closing boundaries and restart credit-limit lock");
        }
        {
            Fixture fixture(root / "invalid_state.json");
            auto bad = *fixture.context.find_teacher(100);
            bad.teacher_number = alice.account_number;
            // 故意把教师账号改成与学生账号相同，检查保存时能否发现这份错误数据。
            fixture.context.apply_teacher_change(bad);
            const auto before = describe_business_state(fixture.context);
            const auto disk = read_file(fixture.storage.path);
            expect_result(fixture.registration.enroll(alice, 10), ResultCode::StorageIntegrityError);
            require(describe_business_state(fixture.context) == before &&
                        read_file(fixture.storage.path) == disk,
                    "candidate validation failure changed original data");
            passed("storage validation failure is returned without publication");
        }
#ifdef _WIN32
        {
            Fixture fixture(root / "locked.json");
            const auto before = describe_business_state(fixture.context);
            const auto disk = read_file(fixture.storage.path);
            HANDLE lock = CreateFileW(fs::path(fixture.storage.path).c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(lock != INVALID_HANDLE_VALUE, "cannot lock test file");
            const auto rejected = fixture.registration.enroll(alice, 10);
            CloseHandle(lock);
            expect_result(rejected, ResultCode::StorageIoError);
            require(describe_business_state(fixture.context) == before &&
                        read_file(fixture.storage.path) == disk,
                    "replace failure published candidate");
            expect_result(fixture.registration.enroll(alice, 10), ResultCode::Success);
            const auto enrolled = describe_business_state(fixture.context);
            const auto enrolled_disk = read_file(fixture.storage.path);
            lock = CreateFileW(fs::path(fixture.storage.path).c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(lock != INVALID_HANDLE_VALUE, "cannot lock drop test file");
            const auto rejected_drop = fixture.registration.drop(alice, 10);
            CloseHandle(lock);
            expect_result(rejected_drop, ResultCode::StorageIoError);
            require(describe_business_state(fixture.context) == enrolled &&
                        read_file(fixture.storage.path) == enrolled_disk,
                    "replace failure committed a drop");
            expect_result(fixture.registration.drop(alice, 10), ResultCode::Success);
            fixture.check_views();
            passed("locked-file failures and enroll/drop retry");
        }
#endif
        std::cout << "Storage integration: " << checks << " checks, " << scenarios
                  << " scenarios passed\n";
        // 只有所有检查都通过，才删除本次测试创建的文件和目录。
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        // 检查失败后会来到这里。输出错误和目录位置，把文件留下来供查看。
        std::cerr << "Storage integration FAILED: " << error.what()
                  << "\nTest data kept at " << root.string() << '\n';
        return 1;
    }
}
