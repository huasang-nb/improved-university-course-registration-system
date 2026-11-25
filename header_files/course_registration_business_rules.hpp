#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace course_registration {

// 账号比较的统一规则：去掉首尾空白，再转小写。
// 修改账号、检查文件数据和登录查找账号时都应使用这个函数。
// 这样同一个账号即使多了首尾空格，或字母大小写不同，也会被识别为同一个账号。
inline std::string normalize_account_key(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    std::string result;
    result.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(text[i]))));
    }
    return result;
}

// 一次上课的时间。分钟从当天 00:00 起计算，例如 09:00 是 540。
struct ClassMeeting {
    int day_of_week;
    int start_minute;
    int end_minute;
    std::string room;
};

struct Course {
    int course_id;
    std::string course_code;
    std::string course_name;
    int credits;
};

// 一个 Course 可以开多个 TeachingClass；容量属于具体教学班。
struct TeachingClass {
    int teaching_class_id;
    int course_id;
    int teacher_id;
    int capacity;
    std::vector<ClassMeeting> meetings;
};

// Active 表示学生当前仍在修读，计算人数、剩余名额和课表时会包含这条记录。
// Dropped 表示已经退课，保留这条记录便于查看历史。学生重选同一个班时可改回 Active。
enum class EnrollmentStatus { Active, Dropped };

struct Enrollment {
    int student_id;
    int teaching_class_id;
    EnrollmentStatus status;
};

// 管理员配置的选退课规则，起止时间使用 "YYYY-MM-DDTHH:MM"。
// 起止时间都已填写时，从 opens_at 开始允许操作，到 closes_at 时就停止允许操作。
// 只要有一个时间没填写，就用 registration_open 这个手动开关判断是否开放。
// 两个时间字段默认是空字符串，因此原来只写 {true, 7} 的代码仍然可以使用。
struct RegistrationRule {
    bool registration_open;
    int maximum_credits;
    std::string opens_at{};
    std::string closes_at{};
};

enum class ResultCode {
    Success,
    RegistrationClosed,
    AlreadyEnrolled,
    TimeConflict,
    CreditLimitExceeded,
    TeachingClassFull,
    TeachingClassNotFound,
    ActiveEnrollmentNotFound,
    // [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 教师/管理员模块需要的失败原因。
    // 追加在末尾，上面已有编号的含义不变，现有调用方无需改动。
    PermissionDenied,       // 角色或归属不符，例如教师读取他人的教学班
    InvalidInput,           // 字段格式/取值为空、负数、非法时段等
    RecordNotFound,         // 课程、教师、学生等记录不存在
    DuplicateIdentifier,    // 编号或学号/工号重复
    CapacityBelowEnrollment,// 新容量小于当前有效选课人数
    CatalogInUse,           // 记录仍被引用（课程仍有教学班、教学班仍有有效选课）
    // [Zhang Yikai 追加，待 Zhang Yuhua 复核] 持久化层需要的失败原因。
    // 追加在末尾，上面已有编号的含义不变，现有调用方无需改动。
    StorageIoError,         // 读写文件失败（不可写、磁盘满、目录不存在等）
    StorageParseError,      // 文件内容不是合法的 state 格式
    StorageSchemaMismatch,  // schema_version 不认识
    StorageIntegrityError,  // 引用缺失、编号重复等数据一致性问题
    StorageBackupMissing    // 没有可恢复的 .bak 备份
};

struct OperationResult {
    ResultCode code;
    std::string message;
};

// 本类保存内存中的课程、教学班、选课记录和规则，并按这些数据判断能否选课或退课。
// 处理学生请求时，应调用 PersistentRegistrationService，让它先检查身份，再调用本类并保存文件。
// 如果代码直接使用本类，就需要自行确认学生身份，并保证记录中用到的学生、课程和教学班都存在。
class RegistrationService {
public:
    RegistrationService(const std::vector<Course>& courses,
                        const std::vector<TeachingClass>& teaching_classes,
                        RegistrationRule rule);

    // 依次检查是否开放、教学班是否存在、课程是否已选、时间是否冲突、学分是否超限和班级是否满员。
    // 返回最先发现的错误。全部通过后，新增 Active 记录，或把同一个班的 Dropped 记录改回 Active。
    OperationResult enroll(int student_id, int teaching_class_id);
    // 在选课开放期间，把这个学生在这个班的 Active 记录改为 Dropped。
    // 如果已经退过课，或从未选过这个班，就返回 ActiveEnrollmentNotFound。
    OperationResult drop(int student_id, int teaching_class_id);
    int remaining_seats(int teaching_class_id) const;
    int enrollment_count(int teaching_class_id) const;
    std::vector<Enrollment> active_enrollments(int student_id) const;

    // ---- [缺陷-001/003 修复，待 Zhang Yuhua 复核] 判断当前能否选课或退课 ----
    // enroll() 和 drop() 都调用 registration_open_now()，按请求发生时的时间判断是否开放。
    // 起止时间都已填写时，开始时刻允许操作，结束时刻起拒绝操作。
    // 如果有一个时间没填写，就使用管理员设置的 registration_open 手动开关。
    // 管理员手动关闭选课时会清空起止时间，因此下次检查时间也不会把选课重新打开。
    void set_fixed_now_for_tests(const std::string& iso_minute);
    bool registration_open_now() const;
    // 用传入的 moment 判断那个时间是否允许选退课，便于测试或预览。
    // registration_open_now() 也使用这个方法，保证两种检查的规则相同。
    bool registration_open_at(const std::string& moment) const;

    // ---- [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 教师/管理员模块只读接口 ----
    // 教师查名单、学生查名额、管理员检查数据时，都读取这里保存的选课记录。
    // 每次根据记录计算人数，避免各个模块分别保存人数后出现不同的结果。
    std::vector<Enrollment> all_enrollments() const;
    std::vector<Course> course_snapshot() const;
    std::vector<TeachingClass> teaching_class_snapshot() const;
    RegistrationRule registration_rule() const;

    // ---- [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 保存管理员检查通过的修改 ----
    // 管理员修改课程、教学班或规则前，先由 AdministratorService 检查修改是否合法。
    // 检查通过后才调用下面的方法。这些方法替换相应数据，但保留已有选退课记录。
    void apply_catalog_change(const std::vector<Course>& courses,
                              const std::vector<TeachingClass>& teaching_classes);
    void apply_rule_change(const RegistrationRule& rule);

    // ---- [Zhang Yikai 追加，待 Zhang Yuhua 复核] 持久化恢复入口 ----
    // 读取或恢复文件时，直接放回文件中的选课记录，由存储代码检查整份数据是否有效。
    // 如果把历史记录逐条交给 enroll()，当前选课时间已关闭时，原本保存的记录也会被拒绝。
    // 因此这个方法只给读取和恢复文件的代码使用。
    void restore_enrollments(std::vector<Enrollment> enrollments);

private:
    const Course* find_course(int course_id) const;
    const TeachingClass* find_teaching_class(int teaching_class_id) const;

    std::vector<Course> courses_;
    std::vector<TeachingClass> teaching_classes_;
    std::vector<Enrollment> enrollments_;
    RegistrationRule rule_;
    // 空字符串表示使用本机当前时间。测试可以通过 SystemContext 设置一个固定时间。
    std::string fixed_now_;
};

}  // namespace course_registration
