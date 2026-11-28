#include "password_service.hpp"

#include <sodium.h>

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace course_registration {
namespace {

void initialize_sodium() {
    if (sodium_init() < 0) {
        throw std::runtime_error("cannot initialize libsodium");
    }
}

// UserIdentity 中的数字编号要按角色解释。这里同时检查账号，避免读到另一人的哈希。
std::string current_password_hash(const SystemContext& context,
                                  const UserIdentity& identity) {
    const auto key = normalize_account_key(identity.account_number);
    switch (identity.role) {
    case Role::Student: {
        const Student* student = context.find_student(identity.user_id);
        if (student && student->account_active &&
            normalize_account_key(student->student_number) == key) {
            return student->password_hash;
        }
        break;
    }
    case Role::Teacher: {
        const Teacher* teacher = context.find_teacher(identity.user_id);
        if (teacher && teacher->account_active &&
            normalize_account_key(teacher->teacher_number) == key) {
            return teacher->password_hash;
        }
        break;
    }
    case Role::Administrator:
        for (const auto& administrator : context.administrators()) {
            if (administrator.administrator_id == identity.user_id &&
                administrator.account_active &&
                normalize_account_key(administrator.account_number) == key) {
                return administrator.password_hash;
            }
        }
        break;
    }
    return {};
}

std::string create_password_hash(std::string_view password) {
    initialize_sodium();
    char hash[crypto_pwhash_STRBYTES]{};
    // 使用 libsodium 的交互式参数。生成的字符串中已经包含算法、盐和计算参数。
    if (crypto_pwhash_str_alg(hash, password.data(), password.size(),
                             crypto_pwhash_OPSLIMIT_INTERACTIVE,
                             crypto_pwhash_MEMLIMIT_INTERACTIVE,
                             crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("cannot calculate password hash");
    }
    return hash;
}

}  // namespace

std::string stored_password_hash(const SystemContext& context,
                                 const UserIdentity& actor) {
    return current_password_hash(context, actor);
}

OperationResult validate_new_password(std::string_view password) {
    if (password.size() < 8) {
        return {ResultCode::InvalidInput, "new password must contain at least 8 characters"};
    }
    if (password.size() > crypto_pwhash_PASSWD_MAX) {
        return {ResultCode::InvalidInput, "new password is too long"};
    }
    bool uppercase = false;
    bool lowercase = false;
    bool digit = false;
    for (unsigned char character : password) {
        // 33 到 126 是英文半角可见字符，空格、中文和控制字符都不在这个范围。
        if (character < 33 || character > 126) {
            return {ResultCode::InvalidInput,
                    "new password allows only ASCII letters, digits and symbols; whitespace is not allowed"};
        }
        uppercase = uppercase || (character >= 'A' && character <= 'Z');
        lowercase = lowercase || (character >= 'a' && character <= 'z');
        digit = digit || (character >= '0' && character <= '9');
    }
    if (!uppercase || !lowercase || !digit) {
        return {ResultCode::InvalidInput,
                "new password must include an uppercase letter, a lowercase letter and a digit"};
    }
    return {ResultCode::Success, "new password meets the requirements"};
}

OperationResult find_password_account(const SystemContext& context,
                                      const std::string& account_number,
                                      PasswordAccount& account) {
    account = {};
    const auto key = normalize_account_key(account_number);
    if (key.empty()) {
        return {ResultCode::InvalidInput, "account is required"};
    }
    int matches = 0;
    const auto consider = [&](Role role, int id, const std::string& number,
                              const std::string& hash) {
        if (normalize_account_key(number) == key) {
            ++matches;
            account = {{id, number, role}, !hash.empty()};
        }
    };
    for (const auto& student : context.students()) {
        consider(Role::Student, student.student_id, student.student_number,
                 student.password_hash);
    }
    for (const auto& teacher : context.teachers()) {
        consider(Role::Teacher, teacher.teacher_id, teacher.teacher_number,
                 teacher.password_hash);
    }
    for (const auto& administrator : context.administrators()) {
        consider(Role::Administrator, administrator.administrator_id,
                 administrator.account_number, administrator.password_hash);
    }
    if (matches == 1) {
        return {ResultCode::Success, "account found"};
    }
    account = {};
    if (matches == 0) {
        return {ResultCode::RecordNotFound, "account not found"};
    }
    return {ResultCode::DuplicateIdentifier, "account is not unique"};
}

bool verify_password_hash(const std::string& password_hash,
                          std::string_view password) {
    // 哈希必须是完整的字符串，不能只验证隐藏在 NUL 字符前面的那一段。
    if (password.empty() || password.size() > crypto_pwhash_PASSWD_MAX ||
        password_hash.rfind("$argon2id$", 0) != 0 ||
        password_hash.size() >= crypto_pwhash_STRBYTES ||
        password_hash.find('\0') != std::string::npos) {
        return false;
    }
    initialize_sodium();
    return crypto_pwhash_str_verify(password_hash.c_str(), password.data(),
                                    password.size()) == 0;
}

PasswordVerifier make_stored_password_verifier(const SystemContext& context) {
    initialize_sodium();
    return [&context](const UserIdentity& identity, std::string_view password) {
        return verify_password_hash(current_password_hash(context, identity), password);
    };
}

OperationResult set_account_password(SystemContext& context,
                                     const StorageOptions& storage,
                                     const std::string& account_number,
                                     std::string_view password,
                                     bool replace_existing) {
    const auto policy = validate_new_password(password);
    if (policy.code != ResultCode::Success) {
        return policy;
    }
    PasswordAccount account;
    const auto found = find_password_account(context, account_number, account);
    if (found.code != ResultCode::Success) {
        return found;
    }
    if (account.password_set && !replace_existing) {
        return {ResultCode::InvalidInput,
                "password already exists; replacement must be explicitly allowed"};
    }

    SystemContext staging = context;
    const std::string hash = create_password_hash(password);
    // 这是专门设置密码的位置。只替换哈希，其余人员资料保持副本中的原值。
    switch (account.identity.role) {
    case Role::Student: {
        auto student = *staging.find_student(account.identity.user_id);
        student.password_hash = hash;
        staging.apply_student_change(student);
        break;
    }
    case Role::Teacher: {
        auto teacher = *staging.find_teacher(account.identity.user_id);
        teacher.password_hash = hash;
        staging.apply_teacher_change(teacher);
        break;
    }
    case Role::Administrator:
        for (auto administrator : staging.administrators()) {
            if (administrator.administrator_id == account.identity.user_id) {
                administrator.password_hash = hash;
                staging.apply_administrator_change(administrator);
                break;
            }
        }
        break;
    }
    const auto saved = save_state(staging, storage);
    if (saved.code != ResultCode::Success) {
        return saved;
    }
    // 文件已经保存，放回内存这一步要保证不会再抛异常。
    static_assert(std::is_nothrow_move_assignable<SystemContext>::value,
                  "password publication must not throw");
    context = std::move(staging);
    return {ResultCode::Success, "password saved"};
}

OperationResult prepare_password_change(const SystemContext& snapshot,
                                         const UserIdentity& actor,
                                         std::string_view current_password,
                                         std::string_view new_password,
                                         PreparedPasswordChange& prepared) {
    prepared = {};
    PasswordAccount account;
    const auto found = find_password_account(snapshot, actor.account_number, account);
    const std::string hash = current_password_hash(snapshot, actor);
    if (found.code != ResultCode::Success || hash.empty() ||
        account.identity.role != actor.role || account.identity.user_id != actor.user_id) {
        return {ResultCode::AuthenticationRequired, "account is no longer valid"};
    }
    if (!verify_password_hash(hash, current_password)) {
        return {ResultCode::InvalidInput, "current password is incorrect"};
    }
    const auto policy = validate_new_password(new_password);
    if (policy.code != ResultCode::Success) {
        return policy;
    }
    prepared = {actor, hash, create_password_hash(new_password)};
    return {ResultCode::Success, "password change prepared"};
}

OperationResult apply_password_change(SystemContext& context,
                                       const StorageOptions& storage,
                                       const PreparedPasswordChange& prepared) {
    const auto current_hash = current_password_hash(context, prepared.actor);
    if (current_hash.empty()) {
        return {ResultCode::AuthenticationRequired, "account is no longer valid"};
    }
    if (current_hash != prepared.previous_hash) {
        return {ResultCode::InvalidInput, "password changed during this request; please retry"};
    }
    if (prepared.new_hash.rfind("$argon2id$", 0) != 0) {
        throw std::invalid_argument("password change has not been prepared");
    }
    // 使用刚取得业务锁后的最新状态复制，不能用计算哈希之前的旧副本覆盖其他业务修改。
    SystemContext staging = context;
    switch (prepared.actor.role) {
    case Role::Student: {
        auto student = *staging.find_student(prepared.actor.user_id);
        student.password_hash = prepared.new_hash;
        staging.apply_student_change(student);
        break;
    }
    case Role::Teacher: {
        auto teacher = *staging.find_teacher(prepared.actor.user_id);
        teacher.password_hash = prepared.new_hash;
        staging.apply_teacher_change(teacher);
        break;
    }
    case Role::Administrator:
        for (auto administrator : staging.administrators()) {
            if (administrator.administrator_id == prepared.actor.user_id) {
                administrator.password_hash = prepared.new_hash;
                staging.apply_administrator_change(administrator);
                break;
            }
        }
        break;
    }
    const auto saved = save_state(staging, storage);
    if (saved.code != ResultCode::Success) {
        return saved;
    }
    context = std::move(staging);
    return {ResultCode::Success, "password changed; please sign in again"};
}

OperationResult change_password(SystemContext& context,
                                 const StorageOptions& storage,
                                 const UserIdentity& actor,
                                 std::string_view current_password,
                                 std::string_view new_password) {
    PreparedPasswordChange prepared;
    const auto result = prepare_password_change(context, actor, current_password,
                                                 new_password, prepared);
    if (result.code != ResultCode::Success) {
        return result;
    }
    return apply_password_change(context, storage, prepared);
}

}  // namespace course_registration
