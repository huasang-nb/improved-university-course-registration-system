cmake_minimum_required(VERSION 3.16)
project(course_registration_starter LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(course_registration_business_rules source_code/course_registration_business_rules.cpp)
target_include_directories(course_registration_business_rules PUBLIC header_files)
add_executable(course_registration_console_demonstration source_code/course_registration_console_demonstration.cpp)
target_link_libraries(course_registration_console_demonstration PRIVATE course_registration_business_rules)
enable_testing()
add_executable(course_registration_business_rules_tests automated_tests/course_registration_business_rules_tests.cpp)
target_link_libraries(course_registration_business_rules_tests PRIVATE course_registration_business_rules)
add_test(NAME course_registration_business_rules COMMAND course_registration_business_rules_tests)

# ---------------------------------------------------------------------------
# [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 教师与管理员模块。
# 只新增目标，不改动上面的目标名称与依赖。
# ---------------------------------------------------------------------------
add_library(teacher_and_administration_services
    source_code/teacher_and_administration_services.cpp)
target_link_libraries(teacher_and_administration_services PUBLIC course_registration_business_rules)
target_include_directories(teacher_and_administration_services PUBLIC header_files)

add_executable(teacher_and_administration_demonstration
    source_code/teacher_and_administration_demonstration.cpp)
target_link_libraries(teacher_and_administration_demonstration PRIVATE teacher_and_administration_services)

add_executable(teacher_and_administration_services_tests
    automated_tests/teacher_and_administration_services_tests.cpp)
target_link_libraries(teacher_and_administration_services_tests PRIVATE teacher_and_administration_services)
add_test(NAME teacher_and_administration_services COMMAND teacher_and_administration_services_tests)

if (MSVC)
    target_compile_options(course_registration_business_rules PRIVATE /W4)
    target_compile_options(teacher_and_administration_services PRIVATE /W4)
else()
    target_compile_options(course_registration_business_rules PRIVATE -Wall -Wextra)
    target_compile_options(teacher_and_administration_services PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# [Zhang Yikai 追加，待 Zhu Ziliang 复核] 学生查询模块（UC02 / UC03 / UC06）。
# 只新增目标，不改动上面的目标名称与依赖。
# ---------------------------------------------------------------------------
add_library(student_query_and_timetable_services
    source_code/student_query_and_timetable_services.cpp)
target_link_libraries(student_query_and_timetable_services
    PUBLIC teacher_and_administration_services)
target_include_directories(student_query_and_timetable_services
    PUBLIC header_files)

add_executable(student_query_demonstration
    source_code/student_query_demonstration.cpp)
target_link_libraries(student_query_demonstration
    PRIVATE student_query_and_timetable_services)

add_executable(student_query_and_timetable_tests
    automated_tests/student_query_and_timetable_tests.cpp)
target_link_libraries(student_query_and_timetable_tests
    PRIVATE student_query_and_timetable_services)
add_test(NAME student_query_and_timetable COMMAND student_query_and_timetable_tests)

if (MSVC)
    target_compile_options(student_query_and_timetable_services PRIVATE /W4)
else()
    target_compile_options(student_query_and_timetable_services PRIVATE -Wall -Wextra)
endif()
