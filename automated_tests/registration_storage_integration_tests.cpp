#include "persistent_registration_service.hpp"
#include "student_query_and_timetable_services.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
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
void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void code(const OperationResult& result, ResultCode expected) {
    require(result.code == expected, "unexpected result: " + result.message);
}
std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "cannot read test file");
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}
void write(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    require(bool(stream), "cannot write test file");
}
StorageOptions options(const fs::path& path) {
    StorageOptions result;
    result.path = path.string();
    return result;
}
SystemContext make_context() {
    auto context = make_seed_context();
    context.set_now_for_tests("2026-11-18T10:00");
    return context;
}

// 独立快照覆盖所有业务字段，用来检测拒绝/保存失败后的部分写入。
std::string state(const SystemContext& context) {
    std::ostringstream out;
    const auto rule = context.rule();
    out << rule.registration_open << ':' << rule.maximum_credits << ':'
        << rule.opens_at << ':' << rule.closes_at << ':' << context.round_opened();
    for (const auto& c : context.courses())
        out << "|C" << c.course_id << ':' << c.course_code << ':'
            << c.course_name << ':' << c.credits;
    for (const auto& c : context.teaching_classes()) {
        out << "|T" << c.teaching_class_id << ':' << c.course_id << ':'
            << c.teacher_id << ':' << c.capacity;
        for (const auto& m : c.meetings)
            out << ':' << m.day_of_week << ':' << m.start_minute << ':'
                << m.end_minute << ':' << m.room;
    }
    for (const auto& s : context.students())
        out << "|S" << s.student_id << ':' << s.student_number << ':' << s.name
            << ':' << s.major << ':' << s.account_active;
    for (const auto& t : context.teachers())
        out << "|P" << t.teacher_id << ':' << t.teacher_number << ':' << t.name
            << ':' << t.department << ':' << t.account_active;
    for (const auto& a : context.administrators())
        out << "|A" << a.administrator_id << ':' << a.account_number << ':'
            << a.name << ':' << a.account_active;
    for (const auto& e : context.all_enrollments())
        out << "|E" << e.student_id << ':' << e.teaching_class_id << ':'
            << static_cast<int>(e.status);
    return out.str();
}
void views(SystemContext& context, StudentQueryService& query,
           TeacherService& teacher) {
    const auto records = context.all_enrollments();
    for (const auto& c : context.teaching_classes()) {
        int expected = 0;
        for (const auto& e : records)
            if (e.teaching_class_id == c.teaching_class_id &&
                e.status == EnrollmentStatus::Active) ++expected;
        require(context.enrollment_count(c.teaching_class_id) == expected,
                "enrollment count differs from history");
        require(context.remaining_seats(c.teaching_class_id) == c.capacity - expected,
                "remaining seats differ from history");
        TeachingClassSummary summary{};
        ClassRoster roster{};
        code(teacher.class_summary(admin, c.teaching_class_id, summary), ResultCode::Success);
        code(teacher.class_roster(admin, c.teaching_class_id, roster), ResultCode::Success);
        require(summary.active_enrollments == expected &&
                    static_cast<int>(roster.students.size()) == expected,
                "teacher views differ from history");
        for (const auto& e : records) {
            if (e.teaching_class_id != c.teaching_class_id ||
                e.status != EnrollmentStatus::Active) continue;
            bool found = false;
            for (const auto& student : roster.students)
                if (student.student_id == e.student_id) found = true;
            require(found, "teacher roster has wrong student ownership");
        }
    }
    for (const auto& student : context.students()) {
        std::vector<TimetableEntry> timetable;
        code(query.view_timetable({student.student_id, student.student_number,
                                   Role::Student}, timetable), ResultCode::Success);
        std::size_t expected = 0;
        for (const auto& e : records) {
            if (e.student_id != student.student_id ||
                e.status != EnrollmentStatus::Active) continue;
            TeachingClass c{};
            require(context.find_teaching_class(e.teaching_class_id, c),
                    "missing class in test fixture");
            expected += c.meetings.size();
            for (const auto& meeting : c.meetings) {
                bool found = false;
                for (const auto& row : timetable)
                    if (row.teaching_class_id == c.teaching_class_id &&
                        row.meeting.day_of_week == meeting.day_of_week &&
                        row.meeting.start_minute == meeting.start_minute &&
                        row.meeting.end_minute == meeting.end_minute &&
                        row.meeting.room == meeting.room) found = true;
                require(found, "student timetable has wrong class or time");
            }
        }
        require(timetable.size() == expected, "student timetable count is stale");
    }
    std::vector<std::string> problems;
    code(validate_state(context, problems), ResultCode::Success);
    require(problems.empty(), "state integrity failed");
}
struct Fixture {
    SystemContext context = make_context();
    StudentQueryService query{context};
    TeacherService teacher{context};
    StorageOptions storage;
    PersistentRegistrationService registration;
    explicit Fixture(const fs::path& path)
        : storage(options(path)), registration(context, storage) {
        code(save_state(context, storage), ResultCode::Success);
    }
    void check_views() { views(context, query, teacher); }
};
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
        {
            Fixture f(root / "roundtrip.json");
            code(f.registration.enroll(alice, 10), ResultCode::Success);
            code(f.registration.enroll(bob, 10), ResultCode::Success);
            f.check_views();
            code(f.registration.drop(alice, 10), ResultCode::Success);
            f.check_views();
            require(f.context.all_enrollments().size() == 2 &&
                        f.context.all_enrollments()[0].status == EnrollmentStatus::Dropped,
                    "drop must preserve history");
            auto restarted = make_context();
            code(load_state(f.storage, restarted), ResultCode::Success);
            require(state(restarted) == state(f.context), "restart changed business fields");
            StudentQueryService restarted_query(restarted);
            TeacherService restarted_teacher(restarted);
            views(restarted, restarted_query, restarted_teacher);
            PersistentRegistrationService restarted_registration(restarted, f.storage);
            code(restarted_registration.enroll(alice, 10), ResultCode::Success);
            require(restarted.all_enrollments().size() == 2,
                    "reenroll must reactivate the same historical record");
            views(restarted, restarted_query, restarted_teacher);
            passed("enroll/drop/restart/reactivation and shared views");
        }
        {
            Fixture f(root / "business.json");
            code(f.registration.enroll(alice, 10), ResultCode::Success);
            const auto before = state(f.context);
            const auto disk = read(f.storage.path);
            const auto backup = read(f.storage.path + ".bak");
            code(f.registration.enroll(alice, 11), ResultCode::AlreadyEnrolled);
            code(f.registration.enroll(alice, 20), ResultCode::TimeConflict);
            code(f.registration.drop(alice, 30), ResultCode::ActiveEnrollmentNotFound);
            code(f.registration.enroll(alice, 999), ResultCode::TeachingClassNotFound);
            require(state(f.context) == before && read(f.storage.path) == disk &&
                        read(f.storage.path + ".bak") == backup,
                    "business rejection wrote state or files");
            f.check_views();
            passed("business failures preserve state and skip storage");
        }
        {
            Fixture f(root / "identity.json");
            const auto before = state(f.context);
            const auto disk = read(f.storage.path);
            code(f.registration.enroll(admin, 10), ResultCode::PermissionDenied);
            code(f.registration.enroll({1001, "T2015007", Role::Teacher}, 10),
                 ResultCode::PermissionDenied);
            code(f.registration.enroll({1001, bob.account_number, Role::Student}, 10),
                 ResultCode::PermissionDenied);
            code(f.registration.enroll({999, alice.account_number, Role::Student}, 10),
                 ResultCode::RecordNotFound);
            code(f.registration.enroll({1001, "  ", Role::Student}, 10),
                 ResultCode::PermissionDenied);
            require(state(f.context) == before && read(f.storage.path) == disk,
                    "identity rejection changed data");
            code(f.registration.enroll({1001, " s2023001 ", Role::Student}, 10),
                 ResultCode::Success);
            auto disabled = *f.context.find_student(1001);
            disabled.account_active = false;
            f.context.apply_student_change(disabled);
            code(save_state(f.context, f.storage), ResultCode::Success);
            const auto inactive_state = state(f.context);
            const auto inactive_disk = read(f.storage.path);
            code(f.registration.drop(alice, 10), ResultCode::PermissionDenied);
            code(f.registration.enroll(alice, 30), ResultCode::PermissionDenied);
            require(state(f.context) == inactive_state &&
                        read(f.storage.path) == inactive_disk,
                    "inactive account changed data");
            auto restarted = make_context();
            code(load_state(f.storage, restarted), ResultCode::Success);
            require(!restarted.find_student(1001)->account_active &&
                        restarted.all_enrollments().size() == 1,
                    "restart lost inactive account or enrollment");
            PersistentRegistrationService service(restarted, f.storage);
            code(service.drop(alice, 10), ResultCode::PermissionDenied);
            passed("role/account/disabled checks and normalized account identity");
        }
        {
            auto context = make_context();
            StudentQueryService query(context);
            TeacherService teacher(context);
            const fs::path parent = root / "blocked_parent";
            write(parent, "not a directory");
            PersistentRegistrationService blocked(context, options(parent / "state.json"));
            const auto before = state(context);
            code(blocked.enroll(alice, 10), ResultCode::StorageIoError);
            require(state(context) == before && !fs::exists(parent / "state.json"),
                    "failed save published candidate or round-opened flag");
            views(context, query, teacher);
            code(context.registration().enroll(1001, 10), ResultCode::Success);
            const auto enrolled = state(context);
            code(blocked.drop(alice, 10), ResultCode::StorageIoError);
            require(state(context) == enrolled, "failed drop save changed history");
            views(context, query, teacher);
            code(blocked.enroll(alice, 11), ResultCode::AlreadyEnrolled);
            code(blocked.drop(alice, 30), ResultCode::ActiveEnrollmentNotFound);
            passed("save failures keep enroll/drop and lock state unchanged");
        }
        {
            Fixture f(root / "window.json");
            RegistrationRule rule{true, 18, "2026-11-18T09:00", "2026-11-18T11:00"};
            f.context.apply_rule_change(rule);
            f.context.set_now_for_tests("2026-11-18T09:00");
            code(f.registration.enroll(alice, 10), ResultCode::Success);
            require(f.context.round_opened(), "automatic opening was not persisted");
            f.context.set_now_for_tests("2026-11-18T11:00");
            const auto before = state(f.context);
            const auto disk = read(f.storage.path);
            code(f.registration.drop(alice, 10), ResultCode::RegistrationClosed);
            code(f.registration.enroll(bob, 10), ResultCode::RegistrationClosed);
            require(state(f.context) == before && read(f.storage.path) == disk,
                    "closed-window request changed data");
            auto restarted = make_context();
            code(load_state(f.storage, restarted), ResultCode::Success);
            AdministratorService administration(restarted);
            code(administration.close_registration(admin), ResultCode::Success);
            code(save_state(restarted, f.storage), ResultCode::Success);
            auto paused = make_context();
            code(load_state(f.storage, paused), ResultCode::Success);
            paused.set_now_for_tests("2026-11-18T10:00");
            AdministratorService paused_admin(paused);
            code(paused_admin.set_maximum_credits(admin, 30), ResultCode::InvalidInput);
            code(paused_admin.set_registration_period(admin, "2030-01-01T09:00",
                                                      "2030-01-02T11:00"), ResultCode::Success);
            code(save_state(paused, f.storage), ResultCode::Success);
            auto future = make_context();
            code(load_state(f.storage, future), ResultCode::Success);
            future.set_now_for_tests("2026-11-18T10:00");
            AdministratorService future_admin(future);
            code(future_admin.set_maximum_credits(admin, 30), ResultCode::InvalidInput);
            require(future.round_opened() && future.all_enrollments().size() == 1,
                    "restart lost round history or active record");
            passed("opening/closing boundaries and restart credit-limit lock");
        }
        {
            Fixture f(root / "invalid_state.json");
            auto bad = *f.context.find_teacher(100);
            bad.teacher_number = alice.account_number;
            f.context.apply_teacher_change(bad); // 模拟未经存储校验的旧数据。
            const auto before = state(f.context);
            const auto disk = read(f.storage.path);
            code(f.registration.enroll(alice, 10), ResultCode::StorageIntegrityError);
            require(state(f.context) == before && read(f.storage.path) == disk,
                    "candidate validation failure changed original data");
            passed("storage validation failure is returned without publication");
        }
#ifdef _WIN32
        {
            Fixture f(root / "locked.json");
            const auto before = state(f.context);
            const auto disk = read(f.storage.path);
            HANDLE lock = CreateFileW(fs::path(f.storage.path).c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(lock != INVALID_HANDLE_VALUE, "cannot lock test file");
            const auto rejected = f.registration.enroll(alice, 10);
            CloseHandle(lock);
            code(rejected, ResultCode::StorageIoError);
            require(state(f.context) == before && read(f.storage.path) == disk,
                    "replace failure published candidate");
            code(f.registration.enroll(alice, 10), ResultCode::Success);
            const auto enrolled = state(f.context);
            const auto enrolled_disk = read(f.storage.path);
            lock = CreateFileW(fs::path(f.storage.path).c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(lock != INVALID_HANDLE_VALUE, "cannot lock drop test file");
            const auto rejected_drop = f.registration.drop(alice, 10);
            CloseHandle(lock);
            code(rejected_drop, ResultCode::StorageIoError);
            require(state(f.context) == enrolled && read(f.storage.path) == enrolled_disk,
                    "replace failure committed a drop");
            code(f.registration.drop(alice, 10), ResultCode::Success);
            f.check_views();
            passed("locked-file failures and enroll/drop retry");
        }
#endif
        std::cout << "Storage integration: " << checks << " checks, " << scenarios
                  << " scenarios passed\n";
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Storage integration FAILED: " << error.what()
                  << "\nTest data kept at " << root.string() << '\n';
        return 1;
    }
}
