// 持久化模块测试（Zhang Yikai）
//
// 覆盖：seed 保存/载入往返、active/dropped 记录、round_opened 事实、
// 首次运行初始化、损坏文件、schema 版本、引用缺失、编号重复、.bak 恢复、
// 无备份时的错误、以及保存失败时不动原文件。

#include "persistence_service.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace course_registration;

namespace {

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool replace_once(std::string& text, const std::string& from,
                  const std::string& to) {
    const size_t pos = text.find(from);
    if (pos == std::string::npos) {
        return false;
    }
    text.replace(pos, from.size(), to);
    return true;
}

StorageOptions options_at(const std::string& path) {
    StorageOptions options;
    options.path = path;
    return options;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;

    std::error_code ec;
    const std::string dir = "build/persistence_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // ------------------------------------------------------------------
    // 1. seed 保存 / 载入往返：active 与 dropped 都要保住，余量要能现算。
    // ------------------------------------------------------------------
    const std::string state_path = dir + "/state.json";
    const StorageOptions state_options = options_at(state_path);

    SystemContext context = make_seed_context();
    assert(context.registration().enroll(1001, 10).code == ResultCode::Success);
    assert(context.registration().enroll(1002, 10).code == ResultCode::Success);
    assert(context.registration().drop(1002, 10).code == ResultCode::Success);
    context.restore_round_opened(true);

    assert(save_state(context, state_options).code == ResultCode::Success);
    assert(fs::exists(state_path));
    assert(!fs::exists(state_path + ".tmp"));

    SystemContext loaded = make_seed_context();
    assert(load_state(state_options, loaded).code == ResultCode::Success);
    assert(loaded.courses().size() == 3);
    assert(loaded.teaching_classes().size() == 4);
    assert(loaded.students().size() == 2);
    assert(loaded.teachers().size() == 3);
    assert(loaded.administrators().size() == 1);
    assert(loaded.all_enrollments().size() == 2);   // 1 active + 1 dropped
    assert(loaded.remaining_seats(10) == 1);        // capacity 2, active 1
    assert(loaded.rule().maximum_credits == 18);
    assert(loaded.registration_window_open() == true);
    assert(loaded.round_opened() == true);          // 历史事实一并恢复

    // 空课表 / 没有选课的学生仍然是 0 条 active 记录。
    assert(loaded.active_enrollments(1001).size() == 1);
    assert(loaded.active_enrollments(1002).empty());

    // ------------------------------------------------------------------
    // 2. 再次保存会生成 .bak；restore_state() 回退到上一版。
    // ------------------------------------------------------------------
    SystemContext second = loaded;
    // 1001 已在 class 10（周一 09:00-10:00）；class 30 在周三，不冲突。
    assert(second.registration().enroll(1001, 30).code == ResultCode::Success);
    assert(save_state(second, state_options).code == ResultCode::Success);
    assert(fs::exists(state_path + ".bak"));

    SystemContext restored = make_seed_context();
    assert(restore_state(state_options, restored).code == ResultCode::Success);
    assert(restored.all_enrollments().size() == 2);   // 回到第二版之前
    assert(restored.remaining_seats(30) == 2);        // 那一版还没选 30

    // ------------------------------------------------------------------
    // 3. 首次运行：文件不存在时用 seed 初始化并落盘。
    // ------------------------------------------------------------------
    const StorageOptions init_options = options_at(dir + "/init/state.json");
    SystemContext initialized = make_seed_context();
    assert(initialize_storage(init_options, initialized).code ==
           ResultCode::Success);
    assert(fs::exists(init_options.path));
    assert(initialized.courses().size() == 3);
    // 第二次调用走载入分支，不应重复初始化。
    assert(initialize_storage(init_options, initialized).code ==
           ResultCode::Success);

    // ------------------------------------------------------------------
    // 4. 没有 .bak 时 restore_state() 返回 StorageBackupMissing。
    // ------------------------------------------------------------------
    const std::string plain_path = dir + "/plain.json";
    assert(save_state(make_seed_context(), options_at(plain_path)).code ==
           ResultCode::Success);
    SystemContext untouched = make_seed_context();
    assert(restore_state(options_at(plain_path), untouched).code ==
           ResultCode::StorageBackupMissing);

    // ------------------------------------------------------------------
    // 5. 损坏文件：StorageParseError，且载入失败不替换 out_context。
    // ------------------------------------------------------------------
    const std::string corrupt_path = dir + "/corrupt.json";
    write_text(corrupt_path, "{ this is not json ");
    SystemContext keep = make_seed_context();
    assert(load_state(options_at(corrupt_path), keep).code ==
           ResultCode::StorageParseError);
    assert(keep.courses().size() == 3);  // 原状态未被破坏

    // ------------------------------------------------------------------
    // 6. schema 版本不认识：StorageSchemaMismatch。
    // ------------------------------------------------------------------
    const std::string schema_path = dir + "/schema.json";
    write_text(schema_path, "{\"schema_version\": 99}");
    SystemContext schema_keep = make_seed_context();
    assert(load_state(options_at(schema_path), schema_keep).code ==
           ResultCode::StorageSchemaMismatch);
    assert(schema_keep.courses().size() == 3);

    // ------------------------------------------------------------------
    // 7. 编号重复：StorageIntegrityError（学号大小写不同也算重复）。
    // ------------------------------------------------------------------
    std::string duplicate_text = read_text(state_path);
    assert(replace_once(duplicate_text, "\"S2023002\"", "\"s2023001\""));
    const std::string duplicate_path = dir + "/duplicate.json";
    write_text(duplicate_path, duplicate_text);
    SystemContext duplicate_keep = make_seed_context();
    assert(load_state(options_at(duplicate_path), duplicate_keep).code ==
           ResultCode::StorageIntegrityError);
    assert(duplicate_keep.students().size() == 2);

    // ------------------------------------------------------------------
    // 8. 引用缺失：选课记录指向不存在的教学班。
    // ------------------------------------------------------------------
    std::string broken = read_text(state_path);
    const bool replaced =
        replace_once(broken, "\"student_id\": 1002, \"teaching_class_id\": 10,",
                     "\"student_id\": 1002, \"teaching_class_id\": 999,");
    assert(replaced);
    const std::string broken_path = dir + "/broken.json";
    write_text(broken_path, broken);
    SystemContext broken_keep = make_seed_context();
    assert(load_state(options_at(broken_path), broken_keep).code ==
           ResultCode::StorageIntegrityError);

    // ------------------------------------------------------------------
    // 9. 保存失败：父路径是个文件，返回 StorageIoError，且不留下 .tmp。
    // ------------------------------------------------------------------
    const std::string blocked_parent = dir + "/blocked";
    write_text(blocked_parent, "not a directory");
    const std::string blocked_path = blocked_parent + "/state.json";
    const OperationResult blocked =
        save_state(make_seed_context(), options_at(blocked_path));
    assert(blocked.code == ResultCode::StorageIoError);
    assert(!fs::exists(blocked_path + ".tmp"));

    // 原文件仍然可读，内容没有被失败的操作改坏。
    assert(!read_text(state_path).empty());

    std::cout << "All persistence tests passed\n";
    return 0;
}
