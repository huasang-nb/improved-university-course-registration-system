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
    PRIVATE persistent_registration_service persistent_administration_service
            student_query_and_timetable_services)
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
# [Zhu Ziliang 追加] 第 5 周：教师 / 管理员 HTTP 接口（cpp-httplib + nlohmann/json）。
#
#   http_teacher_admin_handlers：与框架无关的请求分发，可直接单测；
#   http_teacher_admin_routes  ：cpp-httplib 适配层，交给公共骨架注册；
#   http_teacher_admin_tests   ：直接调 dispatch()，验证 401/403/400/409/CSRF/落盘。
#
# httplib 需要 Windows 10 SDK；MinGW GCC 8 的头文件过旧，routes 目标只在 Windows +
# MSVC 下构建。handler 测试用 MSVC 构建（依赖持久化的 <filesystem>）。
# ---------------------------------------------------------------------------
add_library(http_teacher_admin_handlers
    source_code/http_teacher_admin_handlers.cpp)
target_link_libraries(http_teacher_admin_handlers
    PUBLIC teacher_and_administration_services persistent_administration_service)
target_include_directories(http_teacher_admin_handlers
    PUBLIC header_files)
target_include_directories(http_teacher_admin_handlers
    SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann)

add_executable(http_teacher_admin_tests
    automated_tests/http_teacher_admin_tests.cpp)
target_link_libraries(http_teacher_admin_tests
    PRIVATE http_teacher_admin_handlers)
add_test(NAME http_teacher_admin COMMAND http_teacher_admin_tests)

if (WIN32)
    add_library(http_teacher_admin_routes
        source_code/http_teacher_admin_routes.cpp)
    target_link_libraries(http_teacher_admin_routes
        PUBLIC http_teacher_admin_handlers ws2_32)
    target_include_directories(http_teacher_admin_routes
        PUBLIC header_files)
    target_include_directories(http_teacher_admin_routes
        SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib)

    # 本地验证用：把接口和 web/ 页面挂到一个真实服务器上，身份由命令行指定。
    # 不含登录与会话，仅监听 127.0.0.1，供联调与页面走查；产品入口仍由组长提供。
    add_executable(http_teacher_admin_dev_server
        source_code/http_teacher_admin_dev_server.cpp)
    target_link_libraries(http_teacher_admin_dev_server
        PRIVATE http_teacher_admin_routes ws2_32)

endif()

if (MSVC)
    target_compile_options(http_teacher_admin_handlers PRIVATE /W4)
    target_compile_options(http_teacher_admin_tests PRIVATE /W4)
    if (WIN32)
        target_compile_options(http_teacher_admin_routes PRIVATE /W4)
    endif()
else()
    target_compile_options(http_teacher_admin_handlers PRIVATE -Wall -Wextra)
    target_compile_options(http_teacher_admin_tests PRIVATE -Wall -Wextra)
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

    # Zhang Yuhua：真实密码校验和暂行的本机设置工具，复用已有 JSON 保存入口。
    add_library(password_service source_code/password_service.cpp)
    target_link_libraries(password_service PUBLIC login_service persistence_service
                          PRIVATE sodium::sodium)
    target_compile_options(password_service PRIVATE /W4)

    add_executable(password_setup source_code/password_setup.cpp)
    target_link_libraries(password_setup PRIVATE password_service)
    target_compile_options(password_setup PRIVATE /W4)
    course_registration_link_sodium(password_setup)
    add_test(NAME password_setup_help COMMAND password_setup --help)

    add_executable(password_service_tests automated_tests/password_service_tests.cpp)
    target_link_libraries(password_service_tests
        PRIVATE password_service persistent_registration_service)
    course_registration_link_sodium(password_service_tests)
    add_test(NAME password_service COMMAND password_service_tests)

    message(STATUS
        "libsodium: 官方动态包 x64/v143/dynamic（Debug/Release 文件齐全，链接检查通过）")
endif()

# 公共会话是独立阶段目标。打开它时必须同时启用真实密码库，不使用演示密码回退。
option(COURSE_REGISTRATION_WITH_SESSION "Build shared HTTP authentication and session support" OFF)
if (COURSE_REGISTRATION_WITH_SESSION)
    if (NOT COURSE_REGISTRATION_USE_SODIUM)
        message(FATAL_ERROR "公共会话需要 COURSE_REGISTRATION_USE_SODIUM=ON 和 SODIUM_ROOT。")
    endif()
    add_library(session_service source_code/session.cpp)
    target_link_libraries(session_service PUBLIC password_service
        persistent_administration_service persistent_registration_service ws2_32
        PRIVATE sodium::sodium)
    target_compile_features(session_service PUBLIC cxx_std_17)
    target_include_directories(session_service SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann)
    target_compile_options(session_service PRIVATE /W4)

    add_executable(session_auth_server source_code/session_auth_server.cpp)
    target_link_libraries(session_auth_server PRIVATE session_service)
    course_registration_link_sodium(session_auth_server)

    add_executable(session_tests automated_tests/session_tests.cpp)
    target_link_libraries(session_tests PRIVATE session_service)
    course_registration_link_sodium(session_tests)
    add_test(NAME session COMMAND session_tests)
    set_tests_properties(session PROPERTIES TIMEOUT 45)

    # 教师 / 管理员路由的正式适配器：身份与锁都来自 session_service，
    # 不另建会话表；统一认证路由仍由 runtime.register_auth_routes() 注册。
    add_library(http_teacher_admin_session_routes
        source_code/http_teacher_admin_session_routes.cpp)
    target_link_libraries(http_teacher_admin_session_routes
        PUBLIC http_teacher_admin_routes session_service ws2_32)
    target_compile_options(http_teacher_admin_session_routes PRIVATE /W4)

    # 三端验证服务器：同一个进程、同一个 SessionRuntime、同一个 SystemContext。
    add_executable(http_team4_web_server source_code/http_team4_web_server.cpp)
    target_link_libraries(http_team4_web_server
        PRIVATE http_teacher_admin_session_routes)
    course_registration_link_sodium(http_team4_web_server)

    # 正式会话 + 教师/管理员路由的端到端测试：真实本机 HTTP 连接，
    # 覆盖 401/403/CSRF/成功落盘/业务拒绝/保存失败/改密流程。
    add_executable(http_team4_integration_tests
        automated_tests/http_team4_integration_tests.cpp)
    target_link_libraries(http_team4_integration_tests
        PRIVATE http_teacher_admin_session_routes session_service)
    course_registration_link_sodium(http_team4_integration_tests)
    add_test(NAME http_team4_integration COMMAND http_team4_integration_tests)
    set_tests_properties(http_team4_integration PROPERTIES TIMEOUT 120)

# 这两套检查是用 assert 写的，Release（NDEBUG）下会被编译掉。测试自己返回 77
# 表示“检查未生效”，这里登记为 Skipped，避免把假通过算进 Release 的有效结果。
set_tests_properties(course_registration_business_rules student_query_and_timetable
                     PROPERTIES SKIP_RETURN_CODE 77)
endif()

# 前端纯文本函数的回归测试：不需要浏览器，检查教室名转义与改密弹窗模板。
# 教室名由管理员自由填写，原样进入 innerHTML 会变成标签，这条测试守住转义契约。
# 没装 Node.js 时直接跳过，不让它变成构建失败的来源。
find_program(COURSE_REGISTRATION_NODE_EXECUTABLE node)
if (COURSE_REGISTRATION_NODE_EXECUTABLE)
    add_test(NAME web_text_regression
             COMMAND ${COURSE_REGISTRATION_NODE_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/web/tests/app_text_test.js)
endif()
