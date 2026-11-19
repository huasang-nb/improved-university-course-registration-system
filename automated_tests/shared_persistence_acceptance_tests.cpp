#include "persistent_administration_service.hpp"
#include "persistent_registration_service.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace course_registration;
namespace fs = std::filesystem;
namespace {
int checks = 0;
void require(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void expect(const OperationResult& result, ResultCode expected) {
    require(result.code == expected, result.message);
}
std::string bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main() {
    const auto token = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path directory = fs::temp_directory_path() /
        ("team4_shared_persistence_" + std::to_string(token));
    try {
        fs::create_directory(directory);
        StorageOptions options{(directory / "state.json").string(), true};
        SystemContext context = make_seed_context();
        context.set_now_for_tests("2026-11-03T10:00");
        expect(save_state(context, options), ResultCode::Success);
        const UserIdentity admin{900, "A2020001", Role::Administrator};
        const UserIdentity student{1001, "S2023001", Role::Student};
        const UserIdentity teacher{100, "T2015007", Role::Teacher};
        PersistentRegistrationService registration(context, options);
        PersistentAdministratorService administration(context, options);
        TeacherService views(context);

        expect(registration.enroll(student, 10), ResultCode::Success);
        expect(administration.set_teaching_class_capacity(admin, 10, 3), ResultCode::Success);
        TeachingClassSummary summary{};
        expect(views.class_summary(teacher, 10, summary), ResultCode::Success);
        require(summary.active_enrollments == 1 && summary.remaining_seats == 2,
                "teacher view must reflect both published transactions");
        SystemContext loaded = make_seed_context();
        expect(load_state(options, loaded), ResultCode::Success);
        require(loaded.active_enrollments(1001).size() == 1 && loaded.remaining_seats(10) == 2,
                "administrator save must retain student's persisted enrollment");

        const std::string enrolled = bytes(options.path);
        expect(administration.set_teaching_class_capacity(admin, 10, 0), ResultCode::InvalidInput);
        require(bytes(options.path) == enrolled && context.remaining_seats(10) == 2,
                "rejected administrator operation must preserve student state and file");
        expect(administration.create_student(admin,
                   {1004, " t2015007 ", "Duplicate", "Test", true}),
               ResultCode::DuplicateIdentifier);
        require(bytes(options.path) == enrolled, "cross-role duplicate must not write");

        StorageOptions broken{(directory / "missing" / "state.json").string(), false};
        PersistentAdministratorService failing(context, broken);
        require(failing.set_teaching_class_capacity(admin, 10, 4).code != ResultCode::Success,
                "save failure must be reported");
        require(context.remaining_seats(10) == 2 && bytes(options.path) == enrolled,
                "failed administrator save must not replace shared student state");

        expect(registration.drop(student, 10), ResultCode::Success);
        require(context.remaining_seats(10) == 3, "drop after administrator failure must release seat");
        expect(administration.remove_student(admin, 1001), ResultCode::CatalogInUse);
        Student disabled = *context.find_student(1001);
        disabled.account_active = false;
        expect(administration.update_student(admin, disabled), ResultCode::Success);
        const std::string disabled_snapshot = bytes(options.path);
        expect(registration.enroll(student, 10), ResultCode::PermissionDenied);
        require(bytes(options.path) == disabled_snapshot,
                "existing student gateway must observe administrator account disable");

        expect(load_state(options, loaded), ResultCode::Success);
        loaded.set_now_for_tests("2026-11-03T10:00");
        require(loaded.find_student(1001) != nullptr && !loaded.find_student(1001)->account_active,
                "restart must retain disabled account");
        require(loaded.active_enrollments(1001).empty() && loaded.all_enrollments().size() == 1,
                "restart must retain dropped history without active enrollment");
        require(loaded.all_enrollments().front().status == EnrollmentStatus::Dropped,
                "persisted enrollment must remain Dropped");
        require(loaded.remaining_seats(10) == 3 && loaded.round_opened(),
                "restart must retain administrator capacity and opened-round fact");
        PersistentRegistrationService restarted_registration(loaded, options);
        expect(restarted_registration.enroll(student, 10), ResultCode::PermissionDenied);
        PersistentAdministratorService restarted_admin(loaded, options);
        expect(restarted_admin.remove_student(admin, 1001), ResultCode::CatalogInUse);
        fs::remove_all(directory);
        std::cout << "Shared persistence acceptance: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\nEvidence directory: " << directory << '\n';
        return 1;
    }
}
