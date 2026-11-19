#include "persistent_registration_service.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace course_registration;
int main(int argc, char** argv) {
    StorageOptions storage;
    int index = 1;
    if (argc >= 3 && std::string(argv[1]) == "--state") {
        storage.path = argv[2];
        index = 3;
    }
    if (argc < index + 2) {
        std::cerr << "Local integration demonstration (simulated student session).\n"
                  << "Usage: registration_storage_demonstration [--state PATH] "
                  << "status|enroll|drop STUDENT_ACCOUNT [CLASS_ID]\n";
        return 2;
    }
    const std::string action = argv[index];
    if ((action != "status" && action != "enroll" && action != "drop") ||
        argc != index + (action == "status" ? 2 : 3)) {
        std::cerr << "Invalid command or number of arguments\n";
        return 2;
    }
    int class_id = 0;
    if (action != "status") {
        try {
            std::size_t used = 0;
            const std::string number = argv[index + 2];
            class_id = std::stoi(number, &used);
            if (used != number.size() || class_id <= 0) throw std::invalid_argument("id");
        } catch (const std::exception&) {
            std::cerr << "CLASS_ID must be a positive int\n";
            return 2;
        }
    }
    auto context = make_seed_context();
    const auto initialized = initialize_storage(storage, context);
    if (initialized.code != ResultCode::Success) {
        std::cerr << initialized.message << '\n';
        return 1;
    }
    const Student* student = context.find_student_by_number(argv[index + 1]);
    if (student == nullptr) {
        std::cerr << "Student account not found\n";
        return 1;
    }
    // 仅演示：通过已恢复的账号构造模拟会话，真实 Web 必须从登录会话取身份。
    const UserIdentity identity{student->student_id, student->student_number, Role::Student};
    if (action != "status") {
        PersistentRegistrationService service(context, storage);
        const auto result = action == "enroll" ? service.enroll(identity, class_id)
                                               : service.drop(identity, class_id);
        if (result.code != ResultCode::Success) {
            std::cerr << "Rejected: " << result.message << '\n';
            return 1;
        }
        std::cout << result.message << '\n';
    }
    int active = 0;
    int dropped = 0;
    for (const auto& record : context.all_enrollments()) {
        if (record.student_id != identity.user_id) continue;
        if (record.status == EnrollmentStatus::Active) ++active;
        else ++dropped;
    }
    std::cout << "account=" << identity.account_number << " active=" << active
              << " dropped=" << dropped << " round_opened=" << context.round_opened() << '\n';
    for (const auto& item : context.teaching_classes())
        std::cout << "class=" << item.teaching_class_id
                  << " remaining=" << context.remaining_seats(item.teaching_class_id) << '\n';
    return 0;
}
