#pragma once

#include "persistence_service.hpp"

namespace course_registration {

// 学生选课和退课时使用这个类。它负责检查学生身份，并把成功的操作保存到文件。
// user 应来自登录成功后保存的身份信息，不能用网页传来的学生编号直接拼出身份。
// 每次操作都会重新检查学生账号是否匹配、是否已被停用。密码和登录会话由其他代码处理。
// 操作前先复制一份 SystemContext，在这份副本上修改。文件保存成功后，才更新原来的对象。
// 本服务和查询服务使用同一个 context 对象。只要这些服务还在使用它，这个对象就必须存在。
// 当前由一个线程依次处理请求。将来支持多个请求同时执行时，需要协调所有读写代码，
// 避免一个请求正在修改或保存数据，另一个请求又读取或修改同一份数据。
class PersistentRegistrationService {
public:
    PersistentRegistrationService(SystemContext& context, StorageOptions options);

    // 为 user 对应的学生选课或退课，teaching_class_id 表示要操作的教学班。
    // 如果业务检查失败，就返回 RegistrationService 给出的错误码。
    // 如果文件保存失败，就返回存储错误，并保留操作前的内存数据。
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
