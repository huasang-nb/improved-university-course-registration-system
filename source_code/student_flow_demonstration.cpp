// 学生流程演示（Zhang Yikai，第 5 周）
//
// 用法：student_flow_demonstration --state PATH
//                              [--account S2023001] [--password Demo2026!]
//
// 依次执行：统一认证登录 → 搜索课程 → 选课 → 我的课表 → 个人状态 → 退课。
// 演示用固定密码校验器（所有演示账号共用 Demo2026!）；真实登录应由 libsodium
// 校验 state.json 里的 password_hash，本演示只负责串联流程与演示数据。

#include "login_service.hpp"
#include "student_interface.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace course_registration;

namespace {

constexpr const char* kDemoPassword = "Demo2026!";

bool demo_verify(const UserIdentity&, std::string_view password) {
    return password == kDemoPassword;
}

void usage() {
    std::cerr << "用法：student_flow_demonstration --state PATH "
                 "[--account S2023001] [--password Demo2026!]\n"
                 "  --state 必须显式指定，不会写入默认的 data/state.json。\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string state_path;
    std::string account = "S2023001";
    std::string password = kDemoPassword;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--state" && i + 1 < argc) {
            state_path = argv[++i];
        } else if (arg == "--account" && i + 1 < argc) {
            account = argv[++i];
        } else if (arg == "--password" && i + 1 < argc) {
            password = argv[++i];
        } else {
            usage();
            return 2;
        }
    }
    if (state_path.empty()) {
        usage();
        return 2;
    }

    StorageOptions storage;
    storage.path = state_path;
    SystemContext context = make_seed_context();
    const OperationResult initialized = initialize_storage(storage, context);
    if (initialized.code != ResultCode::Success) {
        std::cerr << "初始化失败：" << initialized.message << "\n";
        return 1;
    }

    LoginService login_service(context, demo_verify);
    StudentInterface ui(context, storage);

    std::cout << "== 1. 登录 ==\n";
    const OperationResult login = ui.login(login_service, account, password);
    std::cout << "   " << login.message << "\n";
    if (login.code != ResultCode::Success) {
        return 1;
    }

    std::cout << "== 2. 搜索 \"SE\" ==\n";
    std::vector<CourseView> courses;
    const OperationResult search = ui.search("SE", courses);
    if (search.code != ResultCode::Success) {
        std::cout << "   " << search.message << "\n";
        return 1;
    }
    for (const CourseView& view : courses) {
        std::cout << "   " << view.course.course_code << " "
                  << view.course.course_name << "（" << view.course.credits
                  << " 学分）\n";
        for (const TeachingClassView& item : view.teaching_classes) {
            std::cout << "      班 " << item.teaching_class_id << " / "
                      << item.teacher_name << " / 余 "
                      << item.remaining_seats << "\n";
        }
    }

    std::cout << "== 3. 选课（班 10） ==\n";
    const OperationResult enrolled = ui.enroll(10);
    std::cout << "   " << enrolled.message << "\n";

    std::cout << "== 4. 我的课表 ==\n";
    std::vector<TimetableEntry> table;
    const OperationResult timetable = ui.timetable(table);
    if (timetable.code != ResultCode::Success) {
        std::cout << "   " << timetable.message << "\n";
        return 1;
    }
    if (table.empty()) {
        std::cout << "   （空）\n";
    }
    for (const TimetableEntry& entry : table) {
        std::cout << "   周" << entry.meeting.day_of_week << " "
                  << entry.meeting.start_minute << "-"
                  << entry.meeting.end_minute << " " << entry.course_code
                  << " " << entry.meeting.room << "\n";
    }

    int active = 0;
    int dropped = 0;
    ui.my_status(active, dropped);
    std::cout << "== 5. 个人状态 ==\n   有效 " << active << " 门，已退 "
              << dropped << " 门\n";

    std::cout << "== 6. 退课（班 10） ==\n";
    const OperationResult drop_result = ui.drop(10);
    std::cout << "   " << drop_result.message << "\n";

    ui.my_status(active, dropped);
    std::cout << "== 7. 退课后状态 ==\n   有效 " << active << " 门，已退 "
              << dropped << " 门\n";
    return 0;
}
