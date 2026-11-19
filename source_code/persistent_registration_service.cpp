#include "persistent_registration_service.hpp"

#include <type_traits>
#include <utility>

namespace course_registration {

// 保存后发布不得再因内存分配失败留下“磁盘成功、内存失败”的普通错误返回。
static_assert(std::is_nothrow_move_assignable<SystemContext>::value,
              "SystemContext publication must be noexcept");

PersistentRegistrationService::PersistentRegistrationService(
    SystemContext& context, StorageOptions options)
    : context_(context), options_(std::move(options)) {}

OperationResult PersistentRegistrationService::check_student(
    const UserIdentity& user) const {
    if (user.role != Role::Student) {
        return {ResultCode::PermissionDenied,
                "only a signed-in student can enroll or drop"};
    }
    const Student* student = context_.find_student(user.user_id);
    if (student == nullptr) {
        return {ResultCode::RecordNotFound, "student account not found"};
    }
    const std::string account = normalize_account_key(user.account_number);
    if (account.empty() ||
        account != normalize_account_key(student->student_number)) {
        return {ResultCode::PermissionDenied,
                "student identity does not match the current account"};
    }
    if (!student->account_active) {
        return {ResultCode::PermissionDenied, "student account is inactive"};
    }
    return {ResultCode::Success, "student identity accepted"};
}

OperationResult PersistentRegistrationService::enroll(
    const UserIdentity& user, int teaching_class_id) {
    return execute(user, teaching_class_id, true);
}

OperationResult PersistentRegistrationService::drop(
    const UserIdentity& user, int teaching_class_id) {
    return execute(user, teaching_class_id, false);
}

OperationResult PersistentRegistrationService::execute(
    const UserIdentity& user, int teaching_class_id, bool enrolling) {
    const OperationResult identity = check_student(user);
    if (identity.code != ResultCode::Success) return identity;

    SystemContext staging = context_;
    const OperationResult changed = enrolling
        ? staging.registration().enroll(user.user_id, teaching_class_id)
        : staging.registration().drop(user.user_id, teaching_class_id);
    if (changed.code != ResultCode::Success) return changed;

    const OperationResult saved = save_state(staging, options_);
    if (saved.code != ResultCode::Success) return saved;

    context_ = std::move(staging);
    return changed;
}

}  // namespace course_registration
