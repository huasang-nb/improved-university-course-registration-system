#include "course_registration_business_rules.hpp"

#include <cassert>
#include <iostream>

using namespace course_registration;

int main() {
    // 示例数据：两门软件工程教学班属于同一门 Course。
    Course software_engineering{1, "SE101", "Software Engineering", 3};
    Course mathematics{2, "MA101", "Mathematics", 4};
    Course programming{3, "CS101", "Programming", 5};

    TeachingClass software_class_one{10, 1, 100, 1,
                                     {{1, 540, 600, "A101"}}};
    TeachingClass software_class_two{11, 1, 100, 2,
                                     {{2, 540, 600, "A102"}}};
    TeachingClass mathematics_class{20, 2, 200, 2,
                                    {{1, 570, 630, "B201"}}};
    TeachingClass programming_class{30, 3, 300, 2,
                                    {{3, 540, 600, "C301"}}};

    RegistrationService registration(
        {software_engineering, mathematics, programming},
        {software_class_one, software_class_two,
         mathematics_class, programming_class},
        {true, 7});  // 测试用上限，并非甲方最终确认的数值。

    // 学生 1 选上容量为 1 的班；学生 2 此时不能再选它。
    assert(registration.enroll(1, 10).code == ResultCode::Success);
    assert(registration.remaining_seats(10) == 0);
    assert(registration.enroll(2, 10).code == ResultCode::TeachingClassFull);

    // 按业务规则分别检查重复课程、时间冲突和学分超限。
    assert(registration.enroll(1, 11).code == ResultCode::AlreadyEnrolled);
    assert(registration.enroll(1, 20).code == ResultCode::TimeConflict);
    assert(registration.enroll(1, 30).code == ResultCode::CreditLimitExceeded);
    assert(registration.enrollment_count(10) == 1);

    // 退课后有效人数减少，名额恢复；重复退课被拒绝。
    assert(registration.drop(1, 10).code == ResultCode::Success);
    assert(registration.remaining_seats(10) == 1);
    assert(registration.drop(1, 10).code ==
           ResultCode::ActiveEnrollmentNotFound);
    assert(registration.enroll(2, 10).code == ResultCode::Success);

    // 关闭选课时段时，选课和退课都不能进行。
    RegistrationService closed_registration(
        {software_engineering}, {software_class_one}, {false, 18});
    assert(closed_registration.enroll(1, 10).code ==
           ResultCode::RegistrationClosed);
    assert(closed_registration.drop(1, 10).code ==
           ResultCode::RegistrationClosed);

    std::cout << "All registration tests passed\n";
}
