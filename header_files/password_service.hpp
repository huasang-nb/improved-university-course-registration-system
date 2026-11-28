#pragma once

#include "login_service.hpp"
#include "persistence_service.hpp"

namespace course_registration {

// 只返回设置密码所需的账号信息，密码哈希仍保留在 SystemContext 中。
struct PasswordAccount {
    UserIdentity identity{};
    bool password_set = false;
};

// 在三种角色中查找唯一的账号。账号去首尾空白并转小写后比较。
// 停用账号也可以设置密码，但设置密码本身不会把账号改成启用。
OperationResult find_password_account(const SystemContext& context,
                                      const std::string& account_number,
                                      PasswordAccount& account);

// 用已保存的 Argon2id 哈希检查密码。空密码、空哈希和损坏的哈希都返回 false。
bool verify_password_hash(const std::string& password_hash,
                          std::string_view password);

// 把真实密码检查接到现有 LoginService。返回的函数始终读取 context 当前的账号资料。
// context 必须一直存在。使用多线程时，使用这个函数的代码负责协调读取和账号修改。
// 后续 HTTP 也可以先复制账号资料，再在锁外调用 verify_password_hash。
PasswordVerifier make_stored_password_verifier(const SystemContext& context);

// 暂行的本机密码设置操作。只修改指定账号的 password_hash，并保存整份数据。
// 已设置过密码时，需要明确传入 replace_existing=true 才会替换。
// 保存成功后才把副本放回 context，保存失败时保留原来的内存数据。
// 使用这项操作前，需要停止其他使用同一数据文件的程序。
// libsodium 初始化或哈希计算失败时抛出 runtime_error，已有数据保留原样。
OperationResult set_account_password(SystemContext& context,
                                     const StorageOptions& storage,
                                     const std::string& account_number,
                                     std::string_view password,
                                     bool replace_existing = false);

}  // namespace course_registration
