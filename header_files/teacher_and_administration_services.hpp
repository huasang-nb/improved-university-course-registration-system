#pragma once

// ============================================================================
// 教师与管理员模块（Zhu Ziliang）
//
// 本文件不重新定义 Course、TeachingClass、Enrollment、RegistrationRule，
// 一律复用 course_registration_business_rules.hpp 中的定义，保证教师端、
// 学生端和管理员端看到同一份课程、教学班、容量与选课记录。
//
// 提供的内容：
//   1. Student / Teacher / Administrator 基础资料与 Role 角色。
//   2. PermissionRules：教师与管理员权限规则（交给登录与各端接线）。
//   3. SystemContext：目录、账号与选课规则的共享上下文。
//   4. TeacherService：UC09 教师只读查询（本人教学班、名单、人数）。
//   5. AdministratorService：UC13/14/15/16 管理员维护。
// ============================================================================

#include <string>
#include <vector>

#include "course_registration_business_rules.hpp"

namespace course_registration {

// ---------------------------------------------------------------------------
// 角色与基础资料
// ---------------------------------------------------------------------------

enum class Role { Student, Teacher, Administrator };

// 登录成功后（Zhang Yuhua 的登录模块）交给本模块的身份标识。
// user_id 与 Student::student_id、Teacher::teacher_id、
// Administrator::administrator_id 使用同一套编号，便于与 Enrollment 对齐。
struct UserIdentity {
    int user_id;
    std::string account_number;
    Role role;
};

struct Student {
    int student_id;
    std::string student_number;  // 学号，对外唯一标识
    std::string name;
    std::string major;
    bool account_active;         // 停用账号不得选课，由登录模块拦截
};

struct Teacher {
    int teacher_id;
    std::string teacher_number;  // 工号，对外唯一标识
    std::string name;
    std::string department;
    bool account_active;
};

struct Administrator {
    int administrator_id;
    std::string account_number;
    std::string name;
    bool account_active;
};

// ---------------------------------------------------------------------------
// 权限规则
//
// 规则一览（与 documentation/interfaces.md 的权限矩阵一致）：
//   教师：只能读取本人负责的教学班、名单和人数，不能修改课程目录与容量。
//   管理员：可以维护课程、教学班、容量、选课时段与人员资料；可读取任意教学班。
//   学生：不能通过教师入口读取任何教学班，只能查看本人课表（Yikai 模块）。
// ---------------------------------------------------------------------------

class PermissionRules {
public:
    static bool can_view_teaching_class(const UserIdentity& user,
                                        const TeachingClass& teaching_class);
    static bool can_manage_catalog(const UserIdentity& user);
    static bool can_set_registration_period(const UserIdentity& user);
    static bool can_manage_people(const UserIdentity& user);
    static bool is_administrator(const UserIdentity& user);
    static bool is_teacher(const UserIdentity& user);
    static bool is_student(const UserIdentity& user);
};

// ---------------------------------------------------------------------------
// 教师与学生看到的读取结果（均按有效选课记录现算，不另存计数）
// ---------------------------------------------------------------------------

struct TeachingClassSummary {
    int teaching_class_id;
    int course_id;
    std::string course_code;
    std::string course_name;
    int credits;
    int teacher_id;
    std::string teacher_name;
    int capacity;
    int active_enrollments;
    int remaining_seats;
    std::vector<ClassMeeting> meetings;
};

struct RosterEntry {
    int student_id;
    std::string student_number;
    std::string name;
};

struct ClassRoster {
    int teaching_class_id;
    int capacity;
    int active_enrollments;
    int remaining_seats;
    std::vector<RosterEntry> students;  // 只含 active 记录，按学号排序
};

// ---------------------------------------------------------------------------
// SystemContext：共享上下文
//
// 目录、教学班、选课记录和选课规则只有一份，由 RegistrationService 持有；
// 本类只补充账号资料，并把管理员校验后的改动写回该服务。因此：
//   教师看到的人数 == 学生看到的余量反算人数 == 管理员看到的人数。
// ---------------------------------------------------------------------------

class SystemContext {
public:
    SystemContext(std::vector<Student> students,
                  std::vector<Teacher> teachers,
                  std::vector<Administrator> administrators,
                  std::vector<Course> courses,
                  std::vector<TeachingClass> teaching_classes,
                  RegistrationRule rule);

    RegistrationService& registration();
    const RegistrationService& registration() const;

    // 目录与规则按快照返回，避免出现第二份可写副本。
    std::vector<Course> courses() const;
    std::vector<TeachingClass> teaching_classes() const;
    RegistrationRule rule() const;

    const std::vector<Student>& students() const;
    const std::vector<Teacher>& teachers() const;
    const std::vector<Administrator>& administrators() const;

    const Student* find_student(int student_id) const;
    const Teacher* find_teacher(int teacher_id) const;
    const Student* find_student_by_number(const std::string& number) const;
    const Teacher* find_teacher_by_number(const std::string& number) const;

    // 以 bool + 输出参数代替返回指针，因为目录是快照。
    bool find_course(int course_id, Course& course) const;
    bool find_teaching_class(int teaching_class_id,
                            TeachingClass& teaching_class) const;

    // 选课记录的唯一读取口。
    std::vector<Enrollment> all_enrollments() const;
    std::vector<Enrollment> active_enrollments(int student_id) const;
    int enrollment_count(int teaching_class_id) const;
    int remaining_seats(int teaching_class_id) const;

    // 供 AdministratorService 校验通过后调用；不要在其他位置直接调用。
    void apply_course_change(const Course& course);
    void remove_course_record(int course_id);
    void apply_teaching_class_change(const TeachingClass& teaching_class);
    void remove_teaching_class_record(int teaching_class_id);
    void apply_rule_change(const RegistrationRule& rule);
    void apply_student_change(const Student& student);
    void remove_student_record(int student_id);
    void apply_teacher_change(const Teacher& teacher);
    void remove_teacher_record(int teacher_id);
    void apply_administrator_change(const Administrator& administrator);

    // 选课时段。默认取本机当前时间；测试用 set_now_for_tests 固定时间，
    // 否则不同的运行日期会得到不同结果。
    std::string now() const;
    void set_now_for_tests(const std::string& iso_minute);

    // 时段是否开放：按给定时刻与已设置的 opens_at / closes_at 现算，
    // 不在管理员保存时缓存结果，避免时间走过去以后状态仍是旧值。
    // 未设置起止时间时，沿用管理员的手动开关。
    bool registration_window_open_at(const std::string& moment) const;
    bool registration_window_open() const;

    // 把现算结果写回 RegistrationService，供 enroll()/drop() 读取。
    // 每次选课或退课请求前调用一次；调用位置（集成层或 enroll() 入口）
    // 待三人确认，见 documentation/interface_change_request.md。
    void refresh_registration_window();

private:
    std::vector<Student> students_;
    std::vector<Teacher> teachers_;
    std::vector<Administrator> administrators_;
    RegistrationService registration_;
    std::string fixed_now_;  // 空表示使用系统时间
};

// ---------------------------------------------------------------------------
// TeacherService：UC09 教师只读查询
// ---------------------------------------------------------------------------

class TeacherService {
public:
    explicit TeacherService(const SystemContext& context);

    // UC09：列出本人负责的教学班（管理员调用时列出全部）。
    OperationResult assigned_teaching_classes(
        const UserIdentity& user,
        std::vector<TeachingClassSummary>& result) const;

    // 单个教学班的容量、人数与余量；越权返回 PermissionDenied。
    OperationResult class_summary(const UserIdentity& user,
                                  int teaching_class_id,
                                  TeachingClassSummary& result) const;

    // 学生名单：只列 active 记录；已退课学生不出现。
    OperationResult class_roster(const UserIdentity& user,
                                 int teaching_class_id,
                                 ClassRoster& result) const;

private:
    const SystemContext& context_;
};

// ---------------------------------------------------------------------------
// AdministratorService：UC13 / UC14 / UC15 / UC16 管理员维护
//
// 所有操作先校验角色，再校验输入与数据关系；失败时返回原因且不改动任何数据。
// ---------------------------------------------------------------------------

class AdministratorService {
public:
    explicit AdministratorService(SystemContext& context);

    // UC13 Manage Courses and Teaching Classes
    OperationResult create_course(const UserIdentity& user, const Course& course);
    OperationResult update_course(const UserIdentity& user, const Course& course);
    OperationResult remove_course(const UserIdentity& user, int course_id);
    OperationResult create_teaching_class(const UserIdentity& user,
                                          const TeachingClass& teaching_class);
    OperationResult update_teaching_class(const UserIdentity& user,
                                          const TeachingClass& teaching_class);
    OperationResult remove_teaching_class(const UserIdentity& user,
                                          int teaching_class_id);

    // UC14 Set Teaching Class Capacity
    OperationResult set_teaching_class_capacity(const UserIdentity& user,
                                                int teaching_class_id,
                                                int capacity);

    // UC15 Set Registration Period
    OperationResult set_registration_period(const UserIdentity& user,
                                            const std::string& opens_at,
                                            const std::string& closes_at);
    OperationResult set_maximum_credits(const UserIdentity& user,
                                        int maximum_credits);
    OperationResult close_registration(const UserIdentity& user);

    // UC16 Manage Student / Teacher Information
    OperationResult create_student(const UserIdentity& user,
                                   const Student& student);
    OperationResult update_student(const UserIdentity& user,
                                   const Student& student);
    OperationResult remove_student(const UserIdentity& user, int student_id);
    OperationResult create_teacher(const UserIdentity& user,
                                   const Teacher& teacher);
    OperationResult update_teacher(const UserIdentity& user,
                                   const Teacher& teacher);
    OperationResult remove_teacher(const UserIdentity& user, int teacher_id);

    // 一致性检查：课程/教学班/选课记录之间的关系，以及容量是否足够。
    // 返回 Success 且 problems 为空表示一致；否则逐条写入问题描述。
    OperationResult check_data_integrity(
        const UserIdentity& user,
        std::vector<std::string>& problems) const;

private:
    SystemContext& context_;
};

}  // namespace course_registration
