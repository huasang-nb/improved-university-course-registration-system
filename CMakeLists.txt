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
# [Zhu Ziliang 追加] 第 4 周（11/23-11/29）失败路径验收测试：
# 容量调整、既有选课记录、重复账号、无效时段、越权访问。
# 每个被拒操作同时断言返回码、可读消息，以及操作前后 SystemContext 快照一致。
# ---------------------------------------------------------------------------
add_executable(admin_failure_acceptance_tests
    automated_tests/admin_failure_acceptance_tests.cpp)
target_link_libraries(admin_failure_acceptance_tests
    PRIVATE teacher_and_administration_services)
add_test(NAME admin_failure_acceptance COMMAND admin_failure_acceptance_tests)
if (MSVC)
    target_compile_options(admin_failure_acceptance_tests PRIVATE /W4)
else()
    target_compile_options(admin_failure_acceptance_tests PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# [Zhu Ziliang 追加] 可选依赖：libsodium（Argon2id 密码哈希），默认关闭。
# 本阶段只验收 **Windows x64 + MSVC 2022 + 官方动态包（dynamic）**：
#   * 静态链接、Linux/GCC 都未验证，配置阶段直接拒绝；
#   * 需要 CMake 3.21+（用到 TARGET_RUNTIME_DLLS 拷贝 DLL）。
#
# 启用：
#   cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 -DCOURSE_REGISTRATION_USE_SODIUM=ON -DSODIUM_ROOT=C:\path\to\libsodium
# SODIUM_ROOT 指向官方 libsodium-*-msvc.zip 解压目录，需包含
#   include\sodium.h
#   x64\Debug\v143\dynamic\libsodium.lib 与 libsodium.dll
#   x64\Release\v143\dynamic\libsodium.lib 与 libsodium.dll
#
# 需要密码哈希的目标调用 course_registration_link_sodium(<target>)：链接并拷贝 DLL。
# ---------------------------------------------------------------------------
option(COURSE_REGISTRATION_USE_SODIUM
       "Link libsodium for Argon2id password hashing" OFF)

function(course_registration_link_sodium target_name)
    target_link_libraries(${target_name} PRIVATE ${COURSE_REGISTRATION_SODIUM_TARGET})
    if (WIN32)
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
    # ---- 平台与工具链 ----
    if (NOT MSVC)
        message(FATAL_ERROR
            "本阶段只支持 Windows x64 + MSVC 2022 的 libsodium 动态链接；"
            "静态链接与 Linux/GCC 均未验证，未实现。")
    endif()
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "本阶段只支持 64 位构建（-A x64），当前指针大小 "
                            "${CMAKE_SIZEOF_VOID_P} 字节。")
    endif()
    if (CMAKE_VERSION VERSION_LESS 3.21)
        message(FATAL_ERROR
            "开启 libsodium 需要 CMake 3.21 或更新版本（依赖 TARGET_RUNTIME_DLLS 拷贝 DLL）；"
            "当前版本 ${CMAKE_VERSION}。")
    endif()
    if (NOT DEFINED SODIUM_ROOT)
        message(FATAL_ERROR
            "本阶段请显式指定 -DSODIUM_ROOT=<官方 libsodium-*-msvc 解压目录>。")
    endif()

    # ---- 目录与头文件 ----
    if (NOT IS_DIRECTORY "${SODIUM_ROOT}")
        message(FATAL_ERROR "SODIUM_ROOT 不存在：${SODIUM_ROOT}")
    endif()
    if (NOT EXISTS "${SODIUM_ROOT}/include/sodium.h")
        message(FATAL_ERROR
            "SODIUM_ROOT 缺少头文件：${SODIUM_ROOT}/include/sodium.h "
            "（应指向官方 libsodium-*-msvc.zip 的解压目录）")
    endif()

    # ---- 逐配置检查动态库（.lib 与 .dll 都要有）----
    set(_sodium_missing "")
    set(_sodium_static_found FALSE)
    foreach(_cfg Debug Release)
        set(_dir "${SODIUM_ROOT}/x64/${_cfg}/v143/dynamic")
        foreach(_file libsodium.lib libsodium.dll)
            if (NOT EXISTS "${_dir}/${_file}")
                list(APPEND _sodium_missing "${_dir}/${_file}")
            endif()
        endforeach()
        if (EXISTS "${SODIUM_ROOT}/x64/${_cfg}/v143/static/libsodium.lib")
            set(_sodium_static_found TRUE)
        endif()
    endforeach()

    if (_sodium_missing)
        string(REPLACE ";" "\n  " _missing_text "${_sodium_missing}")
        # 区分两种情况：动态库存在但缺文件（包不完整），与包里根本没有动态库（只有 static）。
        set(_sodium_has_dynamic FALSE)
        foreach(_cfg Debug Release)
            if (EXISTS "${SODIUM_ROOT}/x64/${_cfg}/v143/dynamic/libsodium.lib")
                set(_sodium_has_dynamic TRUE)
            endif()
        endforeach()
        if (_sodium_has_dynamic)
            message(FATAL_ERROR
                "libsodium 动态包不完整，缺少以下文件：\n  ${_missing_text}\n"
                "请补齐官方 libsodium-*-msvc.zip 中对应的 dynamic 文件"
                "（x64/{Debug,Release}/v143/dynamic/libsodium.lib 与 libsodium.dll）。")
        elseif (_sodium_static_found)
            message(FATAL_ERROR
                "libsodium 动态包不完整，缺少以下文件：\n  ${_missing_text}\n"
                "检测到包内只有 static 库：本阶段只验收官方动态包（v143/dynamic），"
                "请改用官方 libsodium-*-msvc.zip 的 dynamic 目录。")
        else()
            message(FATAL_ERROR
                "libsodium 动态包不完整，缺少以下文件：\n  ${_missing_text}\n"
                "请使用官方 libsodium-*-msvc.zip（含 x64/{Debug,Release}/v143/dynamic/"
                "libsodium.lib 与 libsodium.dll）。")
        endif()
    endif()

    add_library(sodium::sodium SHARED IMPORTED)
    set_target_properties(sodium::sodium PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG     "${SODIUM_ROOT}/x64/Debug/v143/dynamic/libsodium.lib"
        IMPORTED_IMPLIB_RELEASE   "${SODIUM_ROOT}/x64/Release/v143/dynamic/libsodium.lib"
        IMPORTED_LOCATION_DEBUG   "${SODIUM_ROOT}/x64/Debug/v143/dynamic/libsodium.dll"
        IMPORTED_LOCATION_RELEASE "${SODIUM_ROOT}/x64/Release/v143/dynamic/libsodium.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_ROOT}/include")
    set(COURSE_REGISTRATION_SODIUM_TARGET sodium::sodium)

    # ---- 配置期实际链接（Debug 与 Release 各一次）----
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
                "libsodium 在 ${_cfg} 配置下链接失败："
                "${SODIUM_ROOT}/x64/${_cfg}/v143/dynamic/libsodium.lib")
        endif()
    endforeach()

    add_executable(libsodium_probe automated_tests/libsodium_probe_tests.cpp)
    course_registration_link_sodium(libsodium_probe)
    add_test(NAME libsodium_probe COMMAND libsodium_probe)

    message(STATUS
        "libsodium: 官方动态包 x64/v143/dynamic（Debug/Release 文件齐全，链接检查通过）")
endif()
