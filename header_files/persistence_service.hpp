#pragma once

// ============================================================================
// 持久化模块（Zhang Yikai）
//
// SystemContext 保存学生、教师和管理员共同使用的数据。
// 本模块把其中的人员、课程、教学班、规则和选退课记录写入 JSON 文件，供程序下次启动时读取。
// 读取文件时，会先检查内容是否合法，通过后才替换程序当前使用的数据。
// 保存文件前，应先复制一份 SystemContext，在副本上修改并保存。
// 只有保存成功，才把副本放回原来的对象，避免文件保存失败时内存中却已经完成了操作。
// 文件读写使用标准库中的 filesystem 和 fstream。
// ============================================================================

#include <string>
#include <vector>

#include "teacher_and_administration_services.hpp"

namespace course_registration {

struct StorageOptions {
    // 路径相对于程序运行时的工作目录。默认写入该目录下的 data/state.json。
    std::string path = "data/state.json";
    // 目录不存在时是否创建（默认创建）。
    bool create_dirs = true;
};

// 程序启动时调用。如果文件存在，就读取它。如果文件不存在，就创建示例数据并保存。
// 读取或保存成功后，才替换 out_context 中的数据。
OperationResult initialize_storage(const StorageOptions& options,
                                   SystemContext& out_context);

// 读取 JSON 文件，在临时对象中还原各个字段，并检查数据是否合法。
// 全部通过后，才用读到的数据替换 out_context。失败时保留 out_context 原来的内容。
OperationResult load_state(const StorageOptions& options,
                           SystemContext& out_context);

// 先检查本轮是否已经开放过，更新 context 中的 round_opened 标记，再检查整份数据是否合法。
// 检查通过后，先把 JSON 内容写到 .tmp 文件，备份仍然有效的旧主文件，再替换主文件。
// 即使最终保存失败，传进来的 context 的 round_opened 也可能已经变化。
// 因此处理业务请求时应传入临时副本，等业务操作和文件保存都成功后，再更新原来的对象。
// 备份在替换主文件之前更新。如果后续步骤失败，主文件仍是旧内容，但 .bak 可能已经变化。
OperationResult save_state(SystemContext& context, const StorageOptions& options);

// 从 .bak 备份中读取数据，检查通过后写回主文件，再替换 out_context 中的数据。
// 恢复过程中保留原来的 .bak，所以恢复失败后仍然可以使用它重试。
// 如果找不到 .bak，就返回 StorageBackupMissing。
OperationResult restore_state(const StorageOptions& options,
                              SystemContext& out_context);

// 检查整份数据，把发现的问题写入 problems。没有问题时，problems 为空并返回 Success。
// 检查课程字段、学分、课程代码是否重复，以及教学班的容量、上课时间和教师排课是否有效。
// 检查学生、教师和管理员的账号是否重名。比较前会去掉首尾空白并转成小写，停用账号也要检查。
// 检查已选人数是否超过容量，学生是否重复选同一班、选同一课程的多个班，或存在课表冲突。
// 用 long long 计算总学分，检查是否超过上限，并检查选课规则中的日期是否有效。
// 检查 Active 和 Dropped 记录对应的学生和教学班是否存在。
// 按现有约定，同一班内部可以有重叠的上课时间，不同角色也可以使用相同的数字 ID。
OperationResult validate_state(const SystemContext& context,
                               std::vector<std::string>& problems);

// 创建首次运行和测试使用的示例数据，内容见 documentation/interfaces.md 的 4.1 节。
SystemContext make_seed_context();

}  // namespace course_registration
