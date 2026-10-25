cmake_minimum_required(VERSION 3.20)
project(thu_courses_ics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "This tool currently targets Windows (uses WinHTTP / BCrypt).")
endif()

if(MSVC)
    # /utf-8 必须对所有 target 生效：源码是无 BOM 的 UTF-8，否则 MSVC 会按系统
    # 代码页解析中文，出现 C2001「常量中有换行符」之类的伪错误。
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_library(thu_ics_core STATIC
    src/util.cpp
    src/sm3.cpp
    src/sm2.cpp
    src/json.cpp
    src/http.cpp
    src/thu.cpp
    src/schedule.cpp
    src/ics.cpp
    src/location.cpp
    src/selftest.cpp
)

target_include_directories(thu_ics_core PUBLIC src)
target_compile_definitions(thu_ics_core PUBLIC _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(thu_ics_core PUBLIC winhttp bcrypt shell32)

add_executable(thu-courses-ics src/main.cpp)
target_link_libraries(thu-courses-ics PRIVATE thu_ics_core)

add_executable(thu_ics_tests tests/tests.cpp)
target_link_libraries(thu_ics_tests PRIVATE thu_ics_core)

if(MINGW)
    # Ship a single .exe without libgcc/libstdc++ DLLs next to it.
    target_link_options(thu-courses-ics PRIVATE -static)
    target_link_options(thu_ics_tests PRIVATE -static)
endif()

enable_testing()
add_test(NAME thu_ics_tests COMMAND thu_ics_tests)

install(TARGETS thu-courses-ics RUNTIME DESTINATION .)
