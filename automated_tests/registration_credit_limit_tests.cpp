#include "course_registration_business_rules.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace course_registration;
namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
RegistrationService make_registration(int first, int second, int limit) {
    return RegistrationService(
        {{1, "C1", "First", first}, {2, "C2", "Second", second},
         {3, "C3", "Third", 3}},
        {{10, 1, 100, 2, {{1, 540, 600, "A"}}},
         {20, 2, 200, 2, {{2, 540, 600, "B"}}},
         {30, 3, 300, 2, {{3, 540, 600, "C"}}}}, {true, limit});
}
}
int main() {
    try {
        const int maximum = std::numeric_limits<int>::max();
        auto overflow = make_registration(5, maximum, 18);
        require(overflow.enroll(1, 10).code == ResultCode::Success,
                "initial enrollment must succeed");
        const auto before = overflow.all_enrollments();
        require(overflow.enroll(1, 20).code == ResultCode::CreditLimitExceeded,
                "5 + INT_MAX must be rejected before integer overflow");
        const auto after = overflow.all_enrollments();
        require(after.size() == before.size() && after[0].student_id == 1 &&
                    after[0].teaching_class_id == 10 &&
                    after[0].status == EnrollmentStatus::Active,
                "credit rejection must preserve history");
        require(overflow.remaining_seats(20) == 2,
                "credit rejection must preserve seats");

        auto exact = make_registration(maximum - 5, 5, maximum);
        require(exact.enroll(1, 10).code == ResultCode::Success,
                "large legal credit value must remain supported");
        require(exact.enroll(1, 20).code == ResultCode::Success,
                "exact INT_MAX total must be allowed");
        require(exact.enroll(1, 30).code == ResultCode::CreditLimitExceeded,
                "INT_MAX + 3 must be rejected");
        require(exact.all_enrollments().size() == 2 &&
                    exact.remaining_seats(30) == 2,
                "boundary rejection must not add a record");

        auto ordinary = make_registration(3, 4, 7);
        require(ordinary.enroll(1, 10).code == ResultCode::Success &&
                    ordinary.enroll(1, 20).code == ResultCode::Success,
                "ordinary exact limit must be allowed");
        require(ordinary.enroll(1, 30).code == ResultCode::CreditLimitExceeded,
                "ordinary excess must be rejected");
        std::cout << "Credit boundary: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Credit boundary FAILED: " << error.what() << '\n';
        return 1;
    }
}
