#include "login_service.hpp"
#include "persistent_registration_service.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace course_registration;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_no_identity(const LoginResult& login, ResultCode expected_code) {
    require(login.result.code == expected_code && !login.identity,
            "rejected login must return the expected error without an identity");
}

void expect_identity(const LoginResult& login, Role expected_role,
                     int expected_id, const std::string& expected_account) {
    require(login.result.code == ResultCode::Success && login.identity.has_value(),
            "verified login must return an identity");
    const UserIdentity& identity = *login.identity;
    require(identity.role == expected_role && identity.user_id == expected_id &&
                identity.account_number == expected_account,
            "the server must resolve the role, ID and stored account");
}

void test_account_resolution() {
    // 三种角色都用数字 ID 42，验证系统能按账号识别角色。
    SystemContext context(
        {{42, "Student42", "Student", "Test", true},
         {43, "DisabledStudent", "Disabled", "Test", false}},
        {{42, "Teacher42", "Teacher", "Test", true},
         {44, "DisabledTeacher", "Disabled", "Test", false}},
        {{42, "Admin42", "Administrator", true},
         {45, "DisabledAdmin", "Disabled", false}},
        {}, {}, {true, 18});
    int verifier_calls = 0;
    // 用测试校验器记录调用次数，并区分三个角色的凭据。
    LoginService login(context, [&verifier_calls](const UserIdentity& user,
                                                std::string_view password) {
        ++verifier_calls;
        switch (user.role) {
        case Role::Student:
            return user.account_number == "Student42" && password == " student ";
        case Role::Teacher:
            return user.account_number == "Teacher42" && password == "teacher";
        case Role::Administrator:
            return user.account_number == "Admin42" && password == "administrator";
        }
        return false;
    });

    // 账号允许大小写差异和首尾空格，密码必须保留原样。
    expect_identity(login.login("  student42 ", " student "), Role::Student, 42,
                    "Student42");
    expect_identity(login.login("TEACHER42", "teacher"), Role::Teacher, 42,
                    "Teacher42");
    expect_identity(login.login("Admin42", "administrator"), Role::Administrator,
                    42, "Admin42");
    expect_no_identity(login.login("student42", "student"),
                       ResultCode::PermissionDenied);
    expect_no_identity(login.login("Teacher42", "administrator"),
                       ResultCode::PermissionDenied);
    require(verifier_calls == 5, "each active, unique account should be verified once");

    // 账号不存在、仅部分匹配或已停用时，校验器不会被调用。
    expect_no_identity(login.login("missing", "teacher"), ResultCode::PermissionDenied);
    expect_no_identity(login.login("Student", " student "), ResultCode::PermissionDenied);
    expect_no_identity(login.login("DisabledStudent", "student"),
                       ResultCode::PermissionDenied);
    expect_no_identity(login.login("DisabledTeacher", "teacher"),
                       ResultCode::PermissionDenied);
    expect_no_identity(login.login("DisabledAdmin", "administrator"),
                       ResultCode::PermissionDenied);
    expect_no_identity(login.login(" \t", "teacher"), ResultCode::InvalidInput);
    expect_no_identity(login.login("Teacher42", ""), ResultCode::InvalidInput);
    require(verifier_calls == 5,
            "invalid, missing or disabled accounts must not be verified");
}

void test_ambiguous_accounts() {
    // 直接构造重名的旧数据，覆盖跨角色和同角色重名。
    SystemContext context(
        {{1, "Shared", "Student", "Test", true},
         {2, "Duplicate", "Student", "Test", true},
         {3, " duplicate ", "Student", "Test", true}},
        {{1, " shared ", "Teacher", "Test", false}}, {}, {}, {}, {true, 18});
    int verifier_calls = 0;
    LoginService login(context, [&verifier_calls](const UserIdentity&, std::string_view) {
        ++verifier_calls;
        return false;
    });

    expect_no_identity(login.login("SHARED", "test"), ResultCode::DuplicateIdentifier);
    expect_no_identity(login.login("duplicate", "test"), ResultCode::DuplicateIdentifier);
    require(verifier_calls == 0, "ambiguous accounts must be rejected before verification");
}

void test_registration_with_verified_identity(const StorageOptions& options) {
    SystemContext context = make_seed_context();
    require(initialize_storage(options, context).code == ResultCode::Success,
            "test fixture storage initialization failed");
    LoginService login(context, [](const UserIdentity& user, std::string_view password) {
        return user.account_number == "S2023001" && password == "test password";
    });

    // 使用认证服务返回的身份走正常的选退课入口。
    const LoginResult verified_login = login.login("s2023001", "test password");
    expect_identity(verified_login, Role::Student, 1001, "S2023001");
    const UserIdentity& student = *verified_login.identity;
    PersistentRegistrationService registration(context, options);
    require(registration.enroll(student, 10).code == ResultCode::Success,
            "verified student must be able to use persistent enrollment");
    require(registration.drop(student, 10).code == ResultCode::Success,
            "verified student must be able to use persistent drop");

    // 重新加载另一个上下文，确认退课历史和恢复的名额都已写盘。
    SystemContext reloaded_context = make_seed_context();
    require(load_state(options, reloaded_context).code == ResultCode::Success,
            "test fixture storage reload failed");
    const auto enrollments = reloaded_context.all_enrollments();
    require(enrollments.size() == 1 && enrollments.front().student_id == 1001 &&
                enrollments.front().teaching_class_id == 10 &&
                enrollments.front().status == EnrollmentStatus::Dropped &&
                reloaded_context.remaining_seats(10) == 2,
            "verified identity must persist the right student's dropped record");
}

}  // namespace

int main() {
    // 临时目录只供当前测试运行，避免接触项目的默认数据文件。
    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory =
        fs::temp_directory_path() / ("team4_login_" + std::to_string(timestamp));
    try {
        test_account_resolution();
        test_ambiguous_accounts();
        require(fs::create_directory(directory), "could not create test directory");
        test_registration_with_verified_identity({(directory / "state.json").string()});
        fs::remove_all(directory);
        std::cout << "Login and persistent registration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
