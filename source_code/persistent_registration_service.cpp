#include "persistent_registration_service.hpp"

#include <type_traits>
#include <utility>

namespace course_registration {

// 文件保存成功后，还要把副本放回 context_，这一步必须保证不会抛出异常。
// 否则文件已经更新，其他服务使用的内存数据却可能没有更新。
// static_assert 在编译时检查 SystemContext 的移动赋值是否满足这个条件。
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
    // 找到学生编号后，还要核对账号是否相同。
    // 如果管理员改过学生账号，之前保存的旧身份就不能继续使用。
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
    // 每次请求都检查当前学生资料。即使本服务早已创建，学生被停用后也会立即被拦截。
    const OperationResult identity = check_student(user);
    if (identity.code != ResultCode::Success) {
        return identity;
    }

    // 先复制全部数据，再在副本上执行选课或退课。
    // 这样可以保留其他学生的记录和管理员之前的修改，也方便在操作失败时丢弃副本。
    SystemContext staging = context_;
    const OperationResult changed = enrolling
        ? staging.registration().enroll(user.user_id, teaching_class_id)
        : staging.registration().drop(user.user_id, teaching_class_id);
    if (changed.code != ResultCode::Success) {
        // 重复选课、满班等情况直接返回，不调用 save_state。
        // 即使数据没有变化，调用保存也可能把 .bak 换成当前主文件，所以这里要提前结束。
        return changed;
    }

    // save_state 保存文件时，还会记录“本轮已经开放过”，更新副本的 round_opened。
    // 如果保存失败，就丢弃整个副本，让选退课记录和这个标记都保持操作前的值。
    const OperationResult saved = save_state(staging, options_);
    if (saved.code != ResultCode::Success) {
        return saved;
    }

    // 文件保存成功后，把副本中的数据放回原来的 context_ 对象。
    // 查询服务一直使用这个对象，因此接下来查询时就能读到刚保存的数据。
    context_ = std::move(staging);
    return changed;
}

}  // namespace course_registration
