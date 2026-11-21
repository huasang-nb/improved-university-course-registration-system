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

# ---------------------------------------------------------------------------
# [Zhu Ziliang 追加] 可选依赖：libsodium（Argon2id 密码哈希），默认关闭。
# 本阶段只验收 **Windows x64 + MSVC 2022**；Linux/GCC 的接入未验证、未实现。
#
# 启用：
#   cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 -DCOURSE_REGISTRATION_USE_SODIUM=ON -DSODIUM_ROOT=C:\path\to\libsodium
# SODIUM_ROOT 指向官方 libsodium-*-msvc.zip 的解压目录（含 include\sodium.h 与
# x64\{Debug,Release}\v143\{dynamic,static}\libsodium.lib）。
#
# 配置阶段会校验目录、头文件、架构、Debug/Release 两套库文件，并实际链接一次；
# 任一不满足直接失败，不会出现"配置成功、构建才失败"。
# 需要密码哈希的目标调用 course_registration_link_sodium(<target>) 链接。
# ---------------------------------------------------------------------------
option(COURSE_REGISTRATION_USE_SODIUM
       "Link libsodium for Argon2id password hashing" OFF)

# 供需要密码哈希的目标调用：链接 +（动态）拷贝 DLL +（静态）统一运行库。
function(course_registration_link_sodium target_name)
    target_link_libraries(${target_name} PRIVATE ${COURSE_REGISTRATION_SODIUM_TARGET})
    if (COURSE_REGISTRATION_SODIUM_STATIC AND MSVC)
        # 静态 libsodium 用 /MT 构建；目标也改用 /MT，避免 LNK4098。
        set_property(TARGET ${target_name} PROPERTY MSVC_RUNTIME_LIBRARY
                     "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    if (WIN32 AND NOT COURSE_REGISTRATION_SODIUM_STATIC)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${target_name}>
                    $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()

if (DEFINED SODIUM_ROOT AND NOT COURSE_REGISTRATION_USE_SODIUM)
    message(FATAL_ERROR
        "设置了 SODIUM_ROOT 但没有开启 COURSE_REGISTRATION_USE_SODIUM=ON。"
        "请同时加上 -DCOURSE_REGISTRATION_USE_SODIUM=ON，或去掉 SODIUM_ROOT。")
endif()

if (COURSE_REGISTRATION_USE_SODIUM)
    if (NOT MSVC)
        message(FATAL_ERROR
            "本阶段只支持 Windows x64 + MSVC 2022 构建 libsodium 目标；"
            "Linux/GCC 的接入尚未验证，未实现。")
    endif()
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR
            "本阶段只支持 64 位构建（-A x64）；当前指针大小 "
            "${CMAKE_SIZEOF_VOID_P} 字节。")
    endif()
    if (NOT DEFINED SODIUM_ROOT)
        message(FATAL_ERROR
            "本阶段请显式指定 -DSODIUM_ROOT=<libsodium-*-msvc 解压目录>；"
            "Linux/GCC 的 pkg-config 方案未验证，未实现。")
    endif()

    if (NOT IS_DIRECTORY "${SODIUM_ROOT}")
        message(FATAL_ERROR "SODIUM_ROOT 不存在：${SODIUM_ROOT}")
    endif()
    if (NOT EXISTS "${SODIUM_ROOT}/include/sodium.h")
        message(FATAL_ERROR
            "SODIUM_ROOT 缺少头文件：${SODIUM_ROOT}/include/sodium.h "
            "（应指向官方 libsodium-*-msvc.zip 的解压目录）")
    endif()

    set(_sodium_arch "x64")
    set(_dyn_debug   "${SODIUM_ROOT}/${_sodium_arch}/Debug/v143/dynamic")
    set(_dyn_release "${SODIUM_ROOT}/${_sodium_arch}/Release/v143/dynamic")
    set(_static_debug   "${SODIUM_ROOT}/${_sodium_arch}/Debug/v143/static/libsodium.lib")
    set(_static_release "${SODIUM_ROOT}/${_sodium_arch}/Release/v143/static/libsodium.lib")

    # 动态库优先（运行库在 DLL 内，天然不冲突）；否则要求两套静态库齐全。
    if (EXISTS "${_dyn_release}/libsodium.lib" AND EXISTS "${_dyn_debug}/libsodium.lib")
        add_library(sodium::sodium SHARED IMPORTED)
        set_target_properties(sodium::sodium PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_IMPLIB_DEBUG     "${_dyn_debug}/libsodium.lib"
            IMPORTED_IMPLIB_RELEASE   "${_dyn_release}/libsodium.lib"
            IMPORTED_LOCATION_DEBUG   "${_dyn_debug}/libsodium.dll"
            IMPORTED_LOCATION_RELEASE "${_dyn_release}/libsodium.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_ROOT}/include")
        set(COURSE_REGISTRATION_SODIUM_STATIC OFF)
        set(COURSE_REGISTRATION_SODIUM_NAME "MSVC 动态库 x64/v143/dynamic")
    elseif (EXISTS "${_static_debug}" AND EXISTS "${_static_release}")
        add_library(sodium::sodium UNKNOWN IMPORTED)
        set_target_properties(sodium::sodium PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG   "${_static_debug}"
            IMPORTED_LOCATION_RELEASE "${_static_release}"
            INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_ROOT}/include"
            INTERFACE_COMPILE_DEFINITIONS SODIUM_STATIC)
        set(COURSE_REGISTRATION_SODIUM_STATIC ON)
        set(COURSE_REGISTRATION_SODIUM_NAME "MSVC 静态库 x64/v143/static（/MT）")
    else()
        message(FATAL_ERROR
            "在 ${SODIUM_ROOT} 下找不到完整的 libsodium 库。需要其中之一：\n"
            "  ${_dyn_debug}/libsodium.lib 与 ${_dyn_release}/libsodium.lib\n"
            "  ${_static_debug} 与 ${_static_release}")
    endif()
    set(COURSE_REGISTRATION_SODIUM_TARGET sodium::sodium)

    # 配置期实际链接：Debug 与 Release 各检查一次。
    file(WRITE "${CMAKE_BINARY_DIR}/sodium_probe.cpp"
         "#include <sodium.h>\nint main() { return sodium_init() < 0 ? 1 : 0; }\n")
    foreach(_cfg Debug Release)
        set(CMAKE_TRY_COMPILE_CONFIGURATION ${_cfg})
        try_compile(_sodium_link_${_cfg}
            "${CMAKE_BINARY_DIR}/sodium_probe_${_cfg}"
            "${CMAKE_BINARY_DIR}/sodium_probe.cpp"
            LINK_LIBRARIES ${COURSE_REGISTRATION_SODIUM_TARGET}
            CXX_STANDARD 17)
        if (NOT _sodium_link_${_cfg})
            message(FATAL_ERROR
                "libsodium 在 ${_cfg} 配置下链接失败（${COURSE_REGISTRATION_SODIUM_NAME}）。"
                "请确认库文件架构为 x64、Debug/Release 两套库齐全。")
        endif()
    endforeach()

    add_executable(libsodium_probe automated_tests/libsodium_probe_tests.cpp)
    course_registration_link_sodium(libsodium_probe)
    add_test(NAME libsodium_probe COMMAND libsodium_probe)

    message(STATUS "libsodium: ${COURSE_REGISTRATION_SODIUM_NAME}（Debug/Release 链接检查通过）")
endif()
