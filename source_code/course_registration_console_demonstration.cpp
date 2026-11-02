#include "course_registration_business_rules.hpp"

#include <iostream>

using namespace course_registration;

int main() {
    Course software_engineering{1, "SE101", "Software Engineering", 3};
    TeachingClass software_class{
        10, 1, 100, 1,
        {{1, 540, 600, "A101"}}  // 周一 09:00—10:00，容量 1 人
    };

    RegistrationRule rule{true, 18};
    RegistrationService registration(
        {software_engineering}, {software_class}, rule);

    std::cout << registration.enroll(1000, 10).message << '\n';
    std::cout << "Remaining seats: "
              << registration.remaining_seats(10) << '\n';
    std::cout << registration.drop(1000, 10).message << '\n';
    std::cout << "Remaining seats after drop: "
              << registration.remaining_seats(10) << '\n';
}
