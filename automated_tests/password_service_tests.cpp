#include "password_service.hpp"
#include "persistent_registration_service.hpp"

#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace course_registration;
namespace fs = std::filesystem;

namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        throw std::runtime_error(message);
    }
}
void expect(const OperationResult& result, ResultCode code) {
    ++checks;
    if (result.code != code) {
        throw std::runtime_error(result.message);
    }
}
std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot read test state");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main() {
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("team4_password_" + std::to_string(token));
    try {
        fs::create_directory(root);
        StorageOptions storage{(root / "state.json").string(), false};
        auto context = make_seed_context();
        context.set_now_for_tests("2026-11-28T10:00");
        expect(save_state(context, storage), ResultCode::Success);
        LoginService login(context, make_stored_password_verifier(context));
        require(!login.login("S2023001", "missing").identity,
                "account with an empty hash must not log in");

        // 真正生成哈希后，再通过原有 LoginService 登录。密码中的空格和中文需要原样保留。
        const std::string password = u8" 密码-test-value ";
        expect(set_account_password(context, storage, " s2023001 ", password),
               ResultCode::Success);
        const auto student_hash = context.students()[0].password_hash;
        require(student_hash.rfind("$argon2id$", 0) == 0, "password must use Argon2id");
        require(login.login("S2023001", password).identity.has_value(), "real login failed");
        require(!login.login("S2023001", "wrong").identity, "wrong password was accepted");
        require(!verify_password_hash(student_hash, ""), "empty password was accepted");
        require(!verify_password_hash("$argon2id$broken", password), "invalid hash was accepted");
        require(!verify_password_hash(student_hash + std::string("\0tail", 5), password),
                "hash with trailing hidden content was accepted");
        require(read_file(storage.path).find(password) == std::string::npos,
                "state file must not contain the plaintext password");

        // 拒绝替换、找不到账号和空密码时，不应保存，也不能覆盖已有备份。
        const auto before = read_file(storage.path);
        const auto backup = read_file(storage.path + ".bak");
        expect(set_account_password(context, storage, "S2023001", "replacement"),
               ResultCode::InvalidInput);
        expect(set_account_password(context, storage, "absent", password),
               ResultCode::RecordNotFound);
        expect(set_account_password(context, storage, "S2023001", "", true),
               ResultCode::InvalidInput);
        require(read_file(storage.path) == before &&
                    read_file(storage.path + ".bak") == backup,
                "rejected setup changed files");

        // 同一个服务对象应该读到设置后的哈希，同时保留其他角色的资料和哈希。
        expect(set_account_password(context, storage, "T2015007", "teacher-secret"),
               ResultCode::Success);
        const auto teacher_hash = context.teachers()[0].password_hash;
        expect(set_account_password(context, storage, "A2020001", "admin-secret"),
               ResultCode::Success);
        require(context.students()[0].password_hash == student_hash &&
                    context.teachers()[0].password_hash == teacher_hash,
                "setting another role's password lost existing hashes");
        require(login.login("T2015007", "teacher-secret").identity.value().role == Role::Teacher,
                "teacher login has the wrong role");
        require(login.login("A2020001", "admin-secret").identity.value().role == Role::Administrator,
                "administrator login has the wrong role");
        expect(set_account_password(context, storage, "S2023001", "new-secret", true),
               ResultCode::Success);
        require(!login.login("S2023001", password).identity &&
                    login.login("S2023001", "new-secret").identity.has_value(),
                "explicit replacement must invalidate the old password");
        require(context.students()[0].password_hash != student_hash,
                "password replacement must generate a new hash");

        // 从磁盘重新载入，再让真实登录返回的身份走选退课入口。
        auto reloaded = make_seed_context();
        expect(load_state(storage, reloaded), ResultCode::Success);
        LoginService restarted_login(reloaded, make_stored_password_verifier(reloaded));
        const auto verified = restarted_login.login("S2023001", "new-secret");
        require(verified.identity.has_value(), "restart lost the stored credential");
        PersistentRegistrationService registration(reloaded, storage);
        expect(registration.enroll(*verified.identity, 10), ResultCode::Success);
        expect(registration.drop(*verified.identity, 10), ResultCode::Success);
        auto dropped = make_seed_context();
        expect(load_state(storage, dropped), ResultCode::Success);
        require(dropped.all_enrollments().size() == 1 &&
                    dropped.all_enrollments().front().status == EnrollmentStatus::Dropped,
                "real identity did not persist the drop history");

        // 模拟实际保存时无法替换主文件。失败后旧哈希仍然有效，解除占用后可以重试。
        const auto disk_before = read_file(storage.path);
        const auto hash_before = reloaded.students()[0].password_hash;
        HANDLE file = CreateFileW(fs::path(storage.path).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(file != INVALID_HANDLE_VALUE, "cannot lock test file");
        const auto failed = set_account_password(reloaded, storage, "S2023001", "retry-secret", true);
        CloseHandle(file);
        expect(failed, ResultCode::StorageIoError);
        require(reloaded.students()[0].password_hash == hash_before &&
                    read_file(storage.path) == disk_before,
                "failed password save changed memory or the primary file");
        expect(set_account_password(reloaded, storage, "S2023001", "retry-secret", true),
               ResultCode::Success);

        // 三种角色共用数字编号 42 时，也要按各自账号设置和验证。
        SystemContext shared_ids({{42, "Student42", "Student", "Test", true}},
            {{42, "Teacher42", "Teacher", "Test", true}},
            {{42, "Admin42", "Admin", true}}, {}, {}, {true, 18});
        StorageOptions shared_storage{(root / "shared.json").string(), false};
        expect(set_account_password(shared_ids, shared_storage, "Student42", "student"),
               ResultCode::Success);
        expect(set_account_password(shared_ids, shared_storage, "Teacher42", "teacher"),
               ResultCode::Success);
        expect(set_account_password(shared_ids, shared_storage, "Admin42", "admin"),
               ResultCode::Success);
        LoginService shared_login(shared_ids, make_stored_password_verifier(shared_ids));
        require(shared_login.login("Student42", "student").identity.value().role == Role::Student &&
                    shared_login.login("Teacher42", "teacher").identity.value().role == Role::Teacher &&
                    shared_login.login("Admin42", "admin").identity.value().role == Role::Administrator,
                "same numeric ID across roles confused credentials");
        require(!shared_login.login("Teacher42", "student").identity,
                "another role's password was accepted");

        auto disabled = shared_ids.students()[0];
        disabled.account_active = false;
        shared_ids.apply_student_change(disabled);
        expect(set_account_password(shared_ids, shared_storage, "Student42", "disabled", true),
               ResultCode::Success);
        require(!shared_ids.students()[0].account_active &&
                    !shared_login.login("Student42", "disabled").identity,
                "setting a password must not activate an account");

        // 旧数据重名时，在计算和保存新密码前就拒绝操作。
        auto duplicate = shared_ids.teachers()[0];
        duplicate.teacher_number = " student42 ";
        shared_ids.apply_teacher_change(duplicate);
        const auto unchanged = read_file(shared_storage.path);
        expect(set_account_password(shared_ids, shared_storage, "Student42", "test", true),
               ResultCode::DuplicateIdentifier);
        require(read_file(shared_storage.path) == unchanged, "ambiguous account changed file");

        fs::remove_all(root);
        std::cout << "Password service: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Password service FAILED: " << error.what()
                  << "\nTest data kept at " << root.string() << '\n';
        return 1;
    }
}
