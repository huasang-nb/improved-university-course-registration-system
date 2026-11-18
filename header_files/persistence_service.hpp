#pragma once

// ============================================================================
// 持久化模块（Zhang Yikai）
//
// 把 SystemContext 整体存成一个 JSON 快照，进程退出后重启可以恢复：
//   * save_state()        先观察"本轮是否已开放"，再校验整份数据，序列化到
//                         <path>.tmp，备份旧文件为 <path>.bak，最后原子替换；
//   * load_state()        解析 -> 校验 -> 全部通过后才替换 out_context；
//   * restore_state()     从 <path>.bak 恢复；恢复时**不覆盖 .bak**，失败可以重试；
//   * initialize_storage() 文件不存在时用 seed 初始化，写盘成功后才发布；
//   * validate_state()    按现有业务约束检查整份状态（选退课不重放 enroll）。
//
// 所有入口失败时都不修改调用方的 SystemContext。
// 只依赖标准库（filesystem / fstream），不引入第三方 JSON 库。
// ============================================================================

#include <string>
#include <vector>

#include "teacher_and_administration_services.hpp"

namespace course_registration {

struct StorageOptions {
    // 默认放在工作目录的 data/ 下，和 build/ 同级；data/ 进仓库，build/ 不进。
    std::string path = "data/state.json";
    // 目录不存在时是否创建（默认创建）。
    bool create_dirs = true;
};

// 启动入口：文件存在则载入，不存在则用 seed 初始化，写盘成功后才发布。
OperationResult initialize_storage(const StorageOptions& options,
                                   SystemContext& out_context);

// 载入：解析 + 校验 + 构造新的 SystemContext；全部通过才替换 out_context。
OperationResult load_state(const StorageOptions& options,
                           SystemContext& out_context);

// 保存：先 observe_registration_round()，再校验整份状态，最后原子写盘。
// 副作用：会更新**传入对象**的 round_opened（观察"本轮是否已开放"）。
// 调用方必须传临时状态（staging）：业务操作和保存都成功后才把 staging 发布为
// 正式共享状态；失败则丢弃 staging，正式状态保持不变。
// 失败时不改写文件，也不破坏已有文件与 .bak。
OperationResult save_state(SystemContext& context, const StorageOptions& options);

// 恢复：从 .bak 载入；成功后用原子替换写回主文件，**不覆盖 .bak**，
// 因此失败后可以再次恢复。没有 .bak 时返回 StorageBackupMissing。
OperationResult restore_state(const StorageOptions& options,
                              SystemContext& out_context);

// 按现有业务入口的规则校验整份状态；problems 为空表示一致。
// 检查项：课程字段/学分/代码唯一、教学班容量/时间/教师排课冲突、
//        账号按 normalize_account_key 在角色内与跨角色唯一（含停用账号）、
//        有效选课不超容量、同一学生同一教学班不重复 active、同课程多班、
//        学生时间冲突、学分上限（用 long long 累加避免溢出）、
//        选课规则日期、Active 与 Dropped 引用完整。
//        同一教学班内部的时段重叠、不同角色相同数字 ID 仍然允许。
OperationResult validate_state(const SystemContext& context,
                               std::vector<std::string>& problems);

// 首次运行 / 测试用的种子数据（与 documentation/interfaces.md 4.1 一致）。
SystemContext make_seed_context();

}  // namespace course_registration
