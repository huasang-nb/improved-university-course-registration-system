#pragma once

#include "persistence_service.hpp"

namespace course_registration {

// 选退课的持久化应用入口。user 必须由可信登录会话提供，不能直接采信网页参数。
// 每次请求重新核对学生账号与停用状态。该类不实现密码验证或登录会话。
// 当前同步原型使用单线程；Web 接入时，所有读写方需共同协调共享状态访问。
class PersistentRegistrationService {
public:
    PersistentRegistrationService(SystemContext& context, StorageOptions options);

    OperationResult enroll(const UserIdentity& user, int teaching_class_id);
    OperationResult drop(const UserIdentity& user, int teaching_class_id);

private:
    OperationResult check_student(const UserIdentity& user) const;
    OperationResult execute(const UserIdentity& user, int teaching_class_id,
                            bool enrolling);

    SystemContext& context_;
    StorageOptions options_;
};

}  // namespace course_registration
