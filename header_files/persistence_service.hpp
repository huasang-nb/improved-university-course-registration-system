#pragma once

// ============================================================================
// 持久化模块（Zhang Yikai）
//
// 把 SystemContext 整体存成一个 JSON 快照，进程退出后重启可以恢复：
//   * save_state()        把当前状态写成 <path>.tmp，备份旧文件为 <path>.bak，
//                         再原子替换 <path>；
//   * load_state()        读文件 -> 解析 -> 校验 -> 才替换 out_context；
//   * restore_state()     优先从 <path>.bak 恢复；没有备份时返回 StorageBackupMissing；
//   * initialize_storage() 文件不存在时用内置 seed 初始化并立即落盘。
//
// 写入永远先落临时文件，失败时不动内存里的 SystemContext，也不删原文件。
// 只依赖标准库（filesystem / fstream），不引入第三方 JSON 库。
// ============================================================================

#include <string>

#include "teacher_and_administration_services.hpp"

namespace course_registration {

struct StorageOptions {
    // 默认放在工作目录的 data/ 下，和 build/ 同级；data/ 进仓库，build/ 不进。
    std::string path = "data/state.json";
    // 目录不存在时是否创建（默认创建）。
    bool create_dirs = true;
};

// 启动入口：文件存在则载入，不存在则用 seed 初始化并落盘。
OperationResult initialize_storage(const StorageOptions& options,
                                   SystemContext& out_context);

// 载入：解析 + 校验 + 构造新的 SystemContext；成功才替换 out_context。
OperationResult load_state(const StorageOptions& options,
                           SystemContext& out_context);

// 保存：序列化到 tmp -> 备份旧文件为 .bak -> 原子替换目标文件。
OperationResult save_state(const SystemContext& context,
                           const StorageOptions& options);

// 恢复：优先从 .bak 载入；没有 .bak 时返回 StorageBackupMissing。
// 恢复成功后同时把结果写回主文件。
OperationResult restore_state(const StorageOptions& options,
                              SystemContext& out_context);

// 首次运行 / 测试用的种子数据（与 documentation/interfaces.md 4.1 一致）。
SystemContext make_seed_context();

}  // namespace course_registration
