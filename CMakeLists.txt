cmake_minimum_required(VERSION 3.16)
project(course_registration_starter LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# 源码含中文注释：MSVC 缺省按本地代码页（936）读取会破坏字符串与代码结构。
if (MSVC)
    add_compile_options(/utf-8)
endif()

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

add_library(login_service source_code/login_service.cpp)
target_link_libraries(login_service PUBLIC teacher_and_administration_services)

if (MSVC)
    target_compile_options(course_registration_business_rules PRIVATE /W4)
    target_compile_options(teacher_and_administration_services PRIVATE /W4)
    target_compile_options(login_service PRIVATE /W4)
else()
    target_compile_options(course_registration_business_rules PRIVATE -Wall -Wextra)
    target_compile_options(teacher_and_administration_services PRIVATE -Wall -Wextra)
    target_compile_options(login_service PRIVATE -Wall -Wextra)
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

# ---------------------------------------------------------------------------
# [Zhang Yikai 追加，待 Zhang Yuhua 复核] 持久化模块（JSON 快照）。
# 只新增目标，不改动上面的目标名称与依赖。
# ---------------------------------------------------------------------------
add_library(persistence_service
    source_code/persistence_service.cpp)
target_link_libraries(persistence_service
    PUBLIC teacher_and_administration_services)
target_include_directories(persistence_service
    PUBLIC header_files)

add_executable(persistence_tests
    automated_tests/persistence_tests.cpp)
target_link_libraries(persistence_tests
    PRIVATE persistence_service student_query_and_timetable_services)
add_test(NAME persistence COMMAND persistence_tests)

if (MSVC)
    target_compile_options(persistence_service PRIVATE /W4)
else()
    target_compile_options(persistence_service PRIVATE -Wall -Wextra)
endif()

# GCC 8 的 std::filesystem 需要显式链接 stdc++fs；GCC 9+ 下该库为空，可忽略。
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(persistence_service PUBLIC stdc++fs)
endif()

# Zhang Yuhua：选退课溢出回归与持久化应用入口。
add_executable(registration_credit_limit_tests
    automated_tests/registration_credit_limit_tests.cpp)
target_link_libraries(registration_credit_limit_tests
    PRIVATE course_registration_business_rules)
add_test(NAME registration_credit_limit COMMAND registration_credit_limit_tests)

add_executable(registration_cross_module_acceptance
    automated_tests/registration_cross_module_acceptance.cpp)
target_link_libraries(registration_cross_module_acceptance
    PRIVATE student_query_and_timetable_services)
add_test(NAME registration_cross_module_acceptance
    COMMAND registration_cross_module_acceptance)

add_library(persistent_registration_service
    source_code/persistent_registration_service.cpp)
target_link_libraries(persistent_registration_service PUBLIC persistence_service)
if(MSVC)
    target_compile_options(persistent_registration_service PRIVATE /W4)
else()
    target_compile_options(persistent_registration_service PRIVATE -Wall -Wextra)
endif()

add_executable(registration_storage_integration_tests
    automated_tests/registration_storage_integration_tests.cpp)
target_link_libraries(registration_storage_integration_tests
    PRIVATE persistent_registration_service student_query_and_timetable_services)
add_test(NAME registration_storage_integration
    COMMAND registration_storage_integration_tests)

add_executable(login_service_tests automated_tests/login_service_tests.cpp)
target_link_libraries(login_service_tests
    PRIVATE login_service persistent_registration_service)
add_test(NAME login_service COMMAND login_service_tests)

add_executable(registration_storage_demonstration
    source_code/registration_storage_demonstration.cpp)
target_link_libraries(registration_storage_demonstration
    PRIVATE persistent_registration_service)
# ---------------------------------------------------------------------------
# [Zhu Ziliang 追加] 管理员修改的持久化入口（副本执行业务 -> 保存 -> 发布）。
# 只新增目标，不改动上面的目标名称与依赖。
# ---------------------------------------------------------------------------
add_library(persistent_administration_service
    source_code/persistent_administration_service.cpp)
target_link_libraries(persistent_administration_service
    PUBLIC persistence_service)
target_include_directories(persistent_administration_service
    PUBLIC header_files)

add_executable(persistent_administration_tests
    automated_tests/persistent_administration_tests.cpp)
target_link_libraries(persistent_administration_tests
    PRIVATE persistent_administration_service)
add_test(NAME persistent_administration
         COMMAND persistent_administration_tests)

if (MSVC)
    target_compile_options(persistent_administration_service PRIVATE /W4)
else()
    target_compile_options(persistent_administration_service PRIVATE -Wall -Wextra)
    target_compile_options(persistent_administration_tests PRIVATE -Wall -Wextra)
endif()

# 三端使用同一持久化状态的整体验收。
add_executable(shared_persistence_acceptance_tests
    automated_tests/shared_persistence_acceptance_tests.cpp)
target_link_libraries(shared_persistence_acceptance_tests
    PRIVATE persistent_registration_service persistent_administration_service)
add_test(NAME shared_persistence_acceptance COMMAND shared_persistence_acceptance_tests)

# Zhang Yikai：学生端会话接口（第 3 周：学生界面/接口接入共享存储）。
add_library(student_interface
    source_code/student_interface.cpp)
target_link_libraries(student_interface
    PUBLIC persistent_registration_service student_query_and_timetable_services)
target_include_directories(student_interface PUBLIC header_files)
if(MSVC)
    target_compile_options(student_interface PRIVATE /W4)
else()
    target_compile_options(student_interface PRIVATE -Wall -Wextra)
endif()

add_executable(student_interface_demonstration
    source_code/student_interface_demonstration.cpp)
target_link_libraries(student_interface_demonstration PRIVATE student_interface)

add_executable(student_interface_tests
    automated_tests/student_interface_tests.cpp)
target_link_libraries(student_interface_tests PRIVATE student_interface)
add_test(NAME student_interface COMMAND student_interface_tests)
