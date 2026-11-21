// 管理员修改接入持久化的测试（Zhu Ziliang）
//
// 覆盖：成功写入后重新加载一致；业务拒绝、保存路径不可写、主文件被占用时
// 正式内存与文件都不变；原子创建两种非法输入；整体删除与历史保护；
// 时段/学分上限锁定在重启后仍生效；人员资料与启停状态；权限与身份校验。

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "persistence_service.hpp"
#include "persistent_administration_service.hpp"
#include "teacher_and_administration_services.hpp"

namespace fs = std::filesystem;
using namespace course_registration;

namespace {

int g_checks = 0;
int g_failures = 0;

void report(bool ok, const std::string& what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "FAIL line " << line << ": " << what << "\n";
    }
}

#define CHECK(expr) report((expr), #expr, __LINE__)

#define CHECK_CODE(call, expected)                                            \
    do {                                                                      \
        const OperationResult r_ = (call);                                    \
        report(r_.code == (expected),                                         \
               std::string(#call) + " -> expected " +                         \
                   std::to_string(static_cast<int>(expected)) + ", got " +    \
                   std::to_string(static_cast<int>(r_.code)) + " \"" +        \
                   r_.message + "\"",                                         \
               __LINE__);                                                     \
    } while (false)

std::string text_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

struct StorageFixture {
    SystemContext context;
    StorageOptions options;
    UserIdentity admin{900, "A2020001", Role::Administrator};
    UserIdentity alice{1001, "S2023001", Role::Student};
    UserIdentity dr_wang{100, "T2015007", Role::Teacher};
    fs::path base;

    explicit StorageFixture(const std::string& name) : context(make_seed_context()) {
        base = fs::path("build") / "persist_tests" / name;
        fs::remove_all(base);
        fs::create_directories(base);
        options.path = (base / "state.json").string();
        options.create_dirs = true;
        context.set_now_for_tests("2026-11-03T10:00");
        const OperationResult saved = save_state(context, options);
        CHECK_CODE(saved, ResultCode::Success);
    }

    ~StorageFixture() { fs::remove_all(base); }
};

TeachingClass make_class(int id, int course_id, int teacher_id, int capacity,
                         int day, int start, int end, const std::string& room) {
    TeachingClass teaching_class{};
    teaching_class.teaching_class_id = id;
    teaching_class.course_id = course_id;
    teaching_class.teacher_id = teacher_id;
    teaching_class.capacity = capacity;
    teaching_class.meetings = {ClassMeeting{day, start, end, room}};
    return teaching_class;
}

// 1. 原子创建成功 -> 已发布 -> 重新加载一致
void test_create_course_and_class_persist() {
    StorageFixture fixture("create");
    PersistentAdministratorService gateway(fixture.context, fixture.options);

    const Course course{4, "PH101", "Physics", 3};
    const TeachingClass first_class =
        make_class(40, 4, 100, 2, 4, 600, 660, "A201");
    CHECK_CODE(gateway.create_course_with_teaching_class(fixture.admin, course,
                                                         first_class),
               ResultCode::Success);

    // 正式共享状态已发布
    Course stored{};
    TeachingClass stored_class{};
    CHECK(fixture.context.find_course(4, stored));
    CHECK(fixture.context.find_teaching_class(40, stored_class));
    CHECK(stored.course_code == "PH101");

    // 重新加载后完全一致（编号、字段、关联）
    SystemContext reloaded = make_seed_context();
    CHECK_CODE(load_state(fixture.options, reloaded), ResultCode::Success);
    Course after{};
    TeachingClass after_class{};
    CHECK(reloaded.find_course(4, after));
    CHECK(after.course_code == "PH101");
    CHECK(after.course_name == "Physics");
    CHECK(after.credits == 3);
    CHECK(reloaded.find_teaching_class(40, after_class));
    CHECK(after_class.course_id == 4);
    CHECK(after_class.teacher_id == 100);
    CHECK(after_class.capacity == 2);
    CHECK(after_class.meetings.size() == 1);
}

// 2. 业务拒绝：正式状态与文件都不变
void test_business_rejection_keeps_state_and_file() {
    StorageFixture fixture("reject");
    PersistentAdministratorService gateway(fixture.context, fixture.options);
    const std::string before = text_of(fixture.options.path);
    const std::size_t courses_before = fixture.context.courses().size();
    const std::size_t classes_before = fixture.context.teaching_classes().size();

    // 课程字段非法
    CHECK_CODE(gateway.create_course_with_teaching_class(
                   fixture.admin, {4, "  ", "Physics", 3},
                   make_class(40, 4, 100, 2, 4, 600, 660, "A201")),
               ResultCode::InvalidInput);
    // 首班非法（课程引用不匹配）
    CHECK_CODE(gateway.create_course_with_teaching_class(
                   fixture.admin, {4, "PH101", "Physics", 3},
                   make_class(40, 1, 100, 2, 4, 600, 660, "A201")),
               ResultCode::InvalidInput);

    CHECK(fixture.context.courses().size() == courses_before);
    CHECK(fixture.context.teaching_classes().size() == classes_before);
    CHECK(text_of(fixture.options.path) == before);
}

// 3. 保存路径不可写：不发布、文件保持原样
void test_unwritable_path_keeps_state() {
    StorageFixture fixture("unwritable");
    const std::string before = text_of(fixture.options.path);

    StorageOptions broken = fixture.options;
    broken.path = (fixture.base / "missing" / "nested" / "state.json").string();
    broken.create_dirs = false;  // 目录不存在且不创建 -> 写盘必须失败
    PersistentAdministratorService gateway(fixture.context, broken);

    const int credits_before =
        [&] {
            Course course{};
            fixture.context.find_course(1, course);
            return course.credits;
        }();

    const OperationResult result = gateway.create_student(
        fixture.admin, {1004, "S2023004", "Dan Zhou", "Mathematics", true});
    CHECK(result.code != ResultCode::Success);
    CHECK(result.message.find("not applied") != std::string::npos);

    // 正式状态没有被改动：学生不存在，原有课程数据也保持原样
    CHECK(fixture.context.find_student(1004) == nullptr);
    Course course{};
    CHECK(fixture.context.find_course(1, course));
    CHECK(course.credits == credits_before);
    CHECK(fixture.context.rule().maximum_credits == 18);
    CHECK(text_of(fixture.options.path) == before);
}

// 4. 主文件被占用：保存失败，正式状态不变
void test_locked_file_keeps_state() {
    StorageFixture fixture("locked");
    const std::string before = text_of(fixture.options.path);

    // 独占打开主文件，使原子替换无法进行。
    std::ofstream lock(fixture.options.path,
                       std::ios::binary | std::ios::app);
    CHECK(lock.is_open());

    PersistentAdministratorService gateway(fixture.context, fixture.options);
    const OperationResult result = gateway.create_student(
        fixture.admin, {1004, "S2023004", "Dan Zhou", "Mathematics", true});
    lock.close();

    CHECK(result.code != ResultCode::Success);
    CHECK(fixture.context.find_student(1004) == nullptr);
    CHECK(text_of(fixture.options.path) == before);

    // 解锁后同样的操作可以成功，说明前一次失败只与文件占用有关
    const OperationResult retry = gateway.create_student(
        fixture.admin, {1004, "S2023004", "Dan Zhou", "Mathematics", true});
    CHECK_CODE(retry, ResultCode::Success);
    CHECK(fixture.context.find_student(1004) != nullptr);
}

// 5. 身份校验：只有当前启用的管理员可以改
void test_only_active_administrator_can_change() {
    StorageFixture fixture("identity");
    PersistentAdministratorService gateway(fixture.context, fixture.options);

    CHECK_CODE(gateway.set_maximum_credits(fixture.alice, 20),
               ResultCode::PermissionDenied);
    CHECK_CODE(gateway.set_maximum_credits(fixture.dr_wang, 20),
               ResultCode::PermissionDenied);

    UserIdentity unknown{999, "A9999999", Role::Administrator};
    CHECK_CODE(gateway.set_maximum_credits(unknown, 20),
               ResultCode::PermissionDenied);

    // 编号对但 id 不一致：视为伪造会话
    UserIdentity wrong_id{999, "A2020001", Role::Administrator};
    CHECK_CODE(gateway.set_maximum_credits(wrong_id, 20),
               ResultCode::PermissionDenied);

    CHECK(fixture.context.rule().maximum_credits == 18);
}

// 6. 停用的管理员不能改（构造一个停用管理员的数据）
void test_disabled_administrator_cannot_change() {
    SystemContext context = make_seed_context();
    std::vector<Administrator> admins = context.administrators();
    for (Administrator& admin : admins) {
        admin.account_active = false;
    }
    context.apply_administrator_change(admins.front());

    StorageOptions options;
    fs::path base = fs::path("build") / "persist_tests" / "disabled_admin";
    fs::remove_all(base);
    fs::create_directories(base);
    options.path = (base / "state.json").string();

    UserIdentity disabled{admins.front().administrator_id,
                          admins.front().account_number, Role::Administrator};
    PersistentAdministratorService gateway(context, options);
    CHECK_CODE(gateway.set_maximum_credits(disabled, 20),
               ResultCode::PermissionDenied);
    CHECK(context.rule().maximum_credits == 18);
    fs::remove_all(base);
}

// 7. 整体删除：无历史时成功并持久化；有 Active / Dropped 时拒绝
void test_course_delete_with_and_without_history() {
    StorageFixture fixture("delete");
    PersistentAdministratorService gateway(fixture.context, fixture.options);

    // 新课程 + 首班，删课程应连同教学班一起删
    CHECK_CODE(gateway.create_course_with_teaching_class(
                   fixture.admin, {4, "PH101", "Physics", 3},
                   make_class(40, 4, 100, 2, 4, 600, 660, "A201")),
               ResultCode::Success);
    CHECK_CODE(gateway.remove_course(fixture.admin, 4), ResultCode::Success);

    SystemContext reloaded = make_seed_context();
    CHECK_CODE(load_state(fixture.options, reloaded), ResultCode::Success);
    Course course{};
    TeachingClass teaching_class{};
    CHECK(reloaded.find_course(4, course) == false);
    CHECK(reloaded.find_teaching_class(40, teaching_class) == false);
    CHECK(reloaded.find_course(1, course));  // 其他课程保留

    // 有 Active 记录：拒绝，且没有保存任何改动
    CHECK_CODE(fixture.context.registration().enroll(1001, 10),
               ResultCode::Success);
    CHECK_CODE(gateway.remove_course(fixture.admin, 1), ResultCode::CatalogInUse);

    // 退课后只剩 Dropped：仍然拒绝
    CHECK_CODE(fixture.context.registration().drop(1001, 10),
               ResultCode::Success);
    const OperationResult dropped =
        gateway.remove_course(fixture.admin, 1);
    CHECK_CODE(dropped, ResultCode::CatalogInUse);
    CHECK(dropped.message.find("does not make the course deletable") !=
          std::string::npos);
}

// 8. 选课时段与学分上限锁定在重启后仍生效
void test_round_lock_survives_restart() {
    StorageFixture fixture("round");
    PersistentAdministratorService gateway(fixture.context, fixture.options);

    // 设置一个覆盖当前时间的时段 -> 本轮开放，保存后重新加载
    CHECK_CODE(gateway.set_registration_period(fixture.admin,
                                               "2026-11-03T09:00",
                                               "2026-11-03T11:00"),
               ResultCode::Success);

    SystemContext reloaded = make_seed_context();
    CHECK_CODE(load_state(fixture.options, reloaded), ResultCode::Success);
    CHECK(reloaded.round_opened());

    // 重启后的管理员仍然不能改学分上限（锁定事实随状态保存）
    PersistentAdministratorService after_restart(reloaded, fixture.options);
    UserIdentity admin{900, "A2020001", Role::Administrator};
    CHECK_CODE(after_restart.set_maximum_credits(admin, 12),
               ResultCode::InvalidInput);
    CHECK(reloaded.rule().maximum_credits == 18);
}

// 9. 人员资料与启停状态：保存后重新加载一致
void test_people_changes_persist() {
    StorageFixture fixture("people");
    PersistentAdministratorService gateway(fixture.context, fixture.options);

    CHECK_CODE(gateway.create_student(
                   fixture.admin, {1004, "S2023004", "Dan Zhou", "Mathematics", true}),
               ResultCode::Success);
    CHECK_CODE(gateway.update_student(
                   fixture.admin,
                   {1004, "S2023004", "Dan Zhou", "Mathematics", false}),
               ResultCode::Success);
    CHECK_CODE(gateway.create_teacher(
                   fixture.admin, {410, "T2020410", "Dr. New", "Physics", true}),
               ResultCode::Success);
    CHECK_CODE(gateway.update_teacher(
                   fixture.admin, {410, "T2020410", "Dr. New", "Physics", false}),
               ResultCode::Success);
    CHECK_CODE(gateway.remove_teacher(fixture.admin, 410),
               ResultCode::Success);

    SystemContext reloaded = make_seed_context();
    CHECK_CODE(load_state(fixture.options, reloaded), ResultCode::Success);

    const Student* dan = reloaded.find_student(1004);
    CHECK(dan != nullptr);
    CHECK(dan != nullptr && dan->student_number == "S2023004");
    CHECK(dan != nullptr && dan->name == "Dan Zhou");
    CHECK(dan != nullptr && dan->account_active == false);
    CHECK(reloaded.find_teacher(410) == nullptr);
}

// 10. 修改成功后，教师与学生看到的是同一份共享状态
void test_views_share_the_published_state() {
    StorageFixture fixture("views");
    PersistentAdministratorService gateway(fixture.context, fixture.options);
    TeacherService teacher_service(fixture.context);

    CHECK_CODE(gateway.create_course_with_teaching_class(
                   fixture.admin, {4, "PH101", "Physics", 3},
                   make_class(40, 4, 100, 2, 4, 600, 660, "A201")),
               ResultCode::Success);
    CHECK_CODE(fixture.context.registration().enroll(1001, 40),
               ResultCode::Success);
    // 把选课记录也保存一次（管理员改容量触发保存）
    CHECK_CODE(gateway.set_teaching_class_capacity(fixture.admin, 40, 3),
               ResultCode::Success);

    TeachingClassSummary summary{};
    CHECK_CODE(teacher_service.class_summary(fixture.admin, 40, summary),
               ResultCode::Success);
    CHECK(summary.capacity == 3);
    CHECK(summary.active_enrollments == 1);
    CHECK(summary.remaining_seats == 2);
    CHECK(fixture.context.remaining_seats(40) == 2);

    SystemContext reloaded = make_seed_context();
    CHECK_CODE(load_state(fixture.options, reloaded), ResultCode::Success);
    TeacherService reloaded_teacher(reloaded);
    TeachingClassSummary after{};
    CHECK_CODE(reloaded_teacher.class_summary(fixture.admin, 40, after),
               ResultCode::Success);
    CHECK(after.capacity == 3);
    CHECK(after.active_enrollments == 1);
    CHECK(after.remaining_seats == 2);
    CHECK(reloaded.remaining_seats(40) == 2);
}

// 资料维护不能覆盖已有凭据：update_* 只改资料字段，password_hash 必须保留。
void test_profile_update_preserves_password_hash() {
    StorageFixture fixture("credentials");
    PersistentAdministratorService gateway(fixture.context, fixture.options);

    const std::string student_hash =
        "$argon2id$v=19$m=65536,t=2,p=1$c2FsdHN0dWQ$aGFzaHN0dWQ";
    const std::string teacher_hash =
        "$argon2id$v=19$m=65536,t=2,p=1$c2FsZHRlYWNo$aGFzaHRlYWNo";

    // 凭据入口尚未实现；测试用底层构造准备"已有哈希"的账号。
    Student student = *fixture.context.find_student(1001);
    student.password_hash = student_hash;
    fixture.context.apply_student_change(student);
    Teacher teacher = *fixture.context.find_teacher(100);
    teacher.password_hash = teacher_hash;
    fixture.context.apply_teacher_change(teacher);
    CHECK_CODE(save_state(fixture.context, fixture.options), ResultCode::Success);

    // 1) 改姓名与专业，调用方故意传空哈希
    Student edited_student = student;
    edited_student.name = "Alice Chen (renamed)";
    edited_student.major = "Physics";
    edited_student.password_hash.clear();
    CHECK_CODE(gateway.update_student(fixture.admin, edited_student),
               ResultCode::Success);

    // 2) 改学号，仍传空哈希
    Student renumbered = edited_student;
    renumbered.student_number = "S2023999";
    CHECK_CODE(gateway.update_student(fixture.admin, renumbered),
               ResultCode::Success);

    // 3) 停用账号，仍传空哈希
    Student deactivated = renumbered;
    deactivated.account_active = false;
    CHECK_CODE(gateway.update_student(fixture.admin, deactivated),
               ResultCode::Success);

    // 教师：改姓名/系、改工号、停用，同样都传空哈希
    Teacher edited_teacher = teacher;
    edited_teacher.name = "Dr. Wang (renamed)";
    edited_teacher.department = "Mathematics";
    edited_teacher.password_hash.clear();
    CHECK_CODE(gateway.update_teacher(fixture.admin, edited_teacher),
               ResultCode::Success);

    Teacher renumbered_teacher = edited_teacher;
    renumbered_teacher.teacher_number = "T2099999";
    CHECK_CODE(gateway.update_teacher(fixture.admin, renumbered_teacher),
               ResultCode::Success);

    Teacher deactivated_teacher = renumbered_teacher;
    deactivated_teacher.account_active = false;
    CHECK_CODE(gateway.update_teacher(fixture.admin, deactivated_teacher),
               ResultCode::Success);

    // 正式状态里哈希仍在
    CHECK(fixture.context.find_student(1001)->password_hash == student_hash);
    CHECK(fixture.context.find_teacher(100)->password_hash == teacher_hash);

    // 4) 重新加载 JSON：资料与哈希都一致
    SystemContext reloaded = make_seed_context();
    CHECK_CODE(load_state(fixture.options, reloaded), ResultCode::Success);
    const Student* stored_student = reloaded.find_student(1001);
    CHECK(stored_student != nullptr);
    CHECK(stored_student != nullptr &&
          stored_student->password_hash == student_hash);
    CHECK(stored_student != nullptr && stored_student->name == "Alice Chen (renamed)");
    CHECK(stored_student != nullptr && stored_student->major == "Physics");
    CHECK(stored_student != nullptr && stored_student->student_number == "S2023999");
    CHECK(stored_student != nullptr && stored_student->account_active == false);

    const Teacher* stored_teacher = reloaded.find_teacher(100);
    CHECK(stored_teacher != nullptr);
    CHECK(stored_teacher != nullptr && stored_teacher->password_hash == teacher_hash);
    CHECK(stored_teacher != nullptr && stored_teacher->teacher_number == "T2099999");
    CHECK(stored_teacher != nullptr && stored_teacher->department == "Mathematics");
    CHECK(stored_teacher != nullptr && stored_teacher->account_active == false);

    // 5) 业务拒绝：改成教师工号 -> 资料与哈希都不变，文件字节不变
    const std::string before = text_of(fixture.options.path);
    Student conflicting = deactivated;
    conflicting.student_number = "T2099999";
    conflicting.name = "Should Not Apply";
    const OperationResult rejected =
        gateway.update_student(fixture.admin, conflicting);
    CHECK_CODE(rejected, ResultCode::DuplicateIdentifier);
    CHECK(fixture.context.find_student(1001)->password_hash == student_hash);
    CHECK(fixture.context.find_student(1001)->name == "Alice Chen (renamed)");
    CHECK(text_of(fixture.options.path) == before);

    // 6) 保存失败（路径不可写）：资料与哈希都不变
    StorageOptions broken = fixture.options;
    broken.path = (fixture.base / "nowhere" / "deeper" / "state.json").string();
    broken.create_dirs = false;
    PersistentAdministratorService failing_gateway(fixture.context, broken);
    Student unsaved = deactivated;
    unsaved.name = "Never Saved";
    const OperationResult save_failed =
        failing_gateway.update_student(fixture.admin, unsaved);
    CHECK(save_failed.code != ResultCode::Success);
    CHECK(fixture.context.find_student(1001)->name == "Alice Chen (renamed)");
    CHECK(fixture.context.find_student(1001)->password_hash == student_hash);
    CHECK(text_of(fixture.options.path) == before);

    // 7) 新建入口不接受调用方提供的哈希（密码只能走独立入口）
    CHECK_CODE(gateway.create_student(
                   fixture.admin,
                   {1004, "S2023004", "Dan Zhou", "Mathematics", true,
                    "$argon2id$v=19$m=65536,t=2,p=1$bGVhaw$aGFzaA"}),
               ResultCode::Success);
    CHECK(fixture.context.find_student(1004)->password_hash.empty());

    // 新建教师同样清空
    CHECK_CODE(gateway.create_teacher(
                   fixture.admin,
                   {410, "T2020410", "Dr. New", "Physics", true,
                    "$argon2id$v=19$m=65536,t=2,p=1$bGVhaGI$aGFzaGI"}),
               ResultCode::Success);
    CHECK(fixture.context.find_teacher(410)->password_hash.empty());

    // 8) 更新时传"不同的非空哈希"也不能覆盖原值
    Student forged = deactivated;
    forged.password_hash =
        "$argon2id$v=19$m=65536,t=2,p=1$Zm9yZ2Vk$Zm9yZ2Vk";
    CHECK_CODE(gateway.update_student(fixture.admin, forged),
               ResultCode::Success);
    CHECK(fixture.context.find_student(1001)->password_hash == student_hash);

    // 9) 一次普通的选课保存不能把哈希清掉（v1 写入器曾经会）
    CHECK_CODE(fixture.context.registration().enroll(1002, 11),
               ResultCode::Success);
    CHECK_CODE(save_state(fixture.context, fixture.options),
               ResultCode::Success);
    const std::string saved = text_of(fixture.options.path);
    CHECK(saved.find("\"schema_version\": 2") != std::string::npos);
    CHECK(saved.find(student_hash) != std::string::npos);
    CHECK(saved.find(teacher_hash) != std::string::npos);

    SystemContext after_enrolment = make_seed_context();
    CHECK_CODE(load_state(fixture.options, after_enrolment),
               ResultCode::Success);
    CHECK(after_enrolment.find_student(1001)->password_hash == student_hash);
    CHECK(after_enrolment.find_teacher(100)->password_hash == teacher_hash);
    CHECK(after_enrolment.active_enrollments(1002).size() == 1);
}

}  // namespace

int main() {
    test_create_course_and_class_persist();
    test_business_rejection_keeps_state_and_file();
    test_unwritable_path_keeps_state();
    test_locked_file_keeps_state();
    test_only_active_administrator_can_change();
    test_disabled_administrator_cannot_change();
    test_course_delete_with_and_without_history();
    test_round_lock_survives_restart();
    test_people_changes_persist();
    test_views_share_the_published_state();
    test_profile_update_preserves_password_hash();

    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << ": " << g_checks
              << " checks, " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
