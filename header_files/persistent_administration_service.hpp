#pragma once

// ============================================================================
// 管理员修改的持久化入口（Zhu Ziliang）
//
// 解决的问题：AdministratorService 直接改共享内存，改动一旦执行就无法撤销。
// 本入口把"改内存"与"落盘"合成一个整体：
//   1. 用可信会话身份确认调用者当前是启用的管理员（不只看请求自称的角色）；
//   2. 在**临时 SystemContext 副本**上执行业务操作（沿用原有全部校验）；
//   3. 业务通过后调用 persistence::save_state() 保存副本；
//   4. 保存成功才用副本替换正式共享状态；否则丢弃副本。
//
// 任一步失败：正式状态与磁盘文件都保持不变，并返回明确原因。
// 页面与后续接口应调用本类，而不是直接 new AdministratorService。
// ============================================================================

#include <string>
#include <utility>

#include "persistence_service.hpp"
#include "teacher_and_administration_services.hpp"

namespace course_registration {

class PersistentAdministratorService {
public:
    PersistentAdministratorService(SystemContext& official,
                                   const StorageOptions& options);

    // ---- 课程与教学班 ----
    OperationResult create_course_with_teaching_class(
        const UserIdentity& actor, const Course& course,
        const TeachingClass& first_teaching_class);
    OperationResult update_course(const UserIdentity& actor,
                                  const Course& course);
    OperationResult remove_course(const UserIdentity& actor, int course_id);
    OperationResult create_teaching_class(const UserIdentity& actor,
                                          const TeachingClass& teaching_class);
    OperationResult update_teaching_class(const UserIdentity& actor,
                                          const TeachingClass& teaching_class);
    OperationResult remove_teaching_class(const UserIdentity& actor,
                                          int teaching_class_id);
    OperationResult set_teaching_class_capacity(const UserIdentity& actor,
                                                int teaching_class_id,
                                                int capacity);

    // ---- 选课规则 ----
    OperationResult set_registration_period(const UserIdentity& actor,
                                            const std::string& opens_at,
                                            const std::string& closes_at);
    OperationResult set_maximum_credits(const UserIdentity& actor,
                                        int maximum_credits);
    OperationResult close_registration(const UserIdentity& actor);

    // ---- 人员资料与启停状态 ----
    OperationResult create_student(const UserIdentity& actor,
                                   const Student& student);
    OperationResult update_student(const UserIdentity& actor,
                                   const Student& student);
    OperationResult remove_student(const UserIdentity& actor, int student_id);
    OperationResult create_teacher(const UserIdentity& actor,
                                   const Teacher& teacher);
    OperationResult update_teacher(const UserIdentity& actor,
                                   const Teacher& teacher);
    OperationResult remove_teacher(const UserIdentity& actor, int teacher_id);

    // 只读访问正式状态（页面查询用同一份共享状态）。
    const SystemContext& context() const { return official_; }

private:
    // 统一的"校验身份 -> 副本执行业务 -> 保存 -> 发布"流程。
    template <class Operation>
    OperationResult commit(const UserIdentity& actor, Operation&& operation) {
        const OperationResult identity = check_actor(actor);
        if (identity.code != ResultCode::Success) {
            return identity;
        }
        // 临时状态：业务检查与保存都在副本上进行。
        SystemContext staging = official_;
        AdministratorService service(staging);
        const OperationResult result = operation(service);
        if (result.code != ResultCode::Success) {
            return result;  // 业务拒绝：正式状态未被动过
        }
        const OperationResult saved = save_state(staging, options_);
        if (saved.code != ResultCode::Success) {
            return {saved.code,
                    "change not applied: " + saved.message};
        }
        official_ = std::move(staging);  // 保存成功后才发布
        return result;
    }

    // 可信会话身份校验：必须是当前存在且启用的管理员。
    OperationResult check_actor(const UserIdentity& actor) const;

    SystemContext& official_;
    StorageOptions options_;
};

}  // namespace course_registration
