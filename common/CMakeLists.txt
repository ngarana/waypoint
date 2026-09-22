# libwl-common — shared core for the qypr desktop suite.
#
# Consumers (waylaunch, qypr/lockscreen) graft this repo at
# third-party/libwl-common via git subtree and build selected units
# themselves; each repo keeps its own codegen rules, loop wiring and test
# harness. This build exists only for the units testable without a
# compositor (currently: render/BackdropBlur, cairo-only).
cmake_minimum_required(VERSION 3.20)
project(libwl-common CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)

option(BUILD_TESTING "Build the shared unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(blur_test tests/blur_test.cpp render/BackdropBlur.cpp)
    target_include_directories(blur_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CAIRO_INCLUDE_DIRS})
    target_link_libraries(blur_test PRIVATE ${CAIRO_LIBRARIES})
    target_compile_options(blur_test PRIVATE ${CAIRO_CFLAGS} -Wall -Wextra -UNDEBUG)
    add_test(NAME blur_test COMMAND blur_test)

    pkg_check_modules(RSVG REQUIRED librsvg-2.0)
    add_executable(icon_test tests/icon_test.cpp render/IconResolver.cpp)
    target_include_directories(icon_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CAIRO_INCLUDE_DIRS}
        ${RSVG_INCLUDE_DIRS})
    target_link_libraries(icon_test PRIVATE ${CAIRO_LIBRARIES} ${RSVG_LIBRARIES})
    target_compile_options(icon_test PRIVATE ${CAIRO_CFLAGS} ${RSVG_CFLAGS} -Wall -Wextra -UNDEBUG)
    add_test(NAME icon_test COMMAND icon_test)

    add_executable(desktop_test tests/desktop_test.cpp system/DesktopIndex.cpp
        core/Process.cpp core/EventLoop.cpp)
    target_include_directories(desktop_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(desktop_test PRIVATE -Wall -Wextra -UNDEBUG)
    add_test(NAME desktop_test COMMAND desktop_test)

    add_executable(solar_test tests/solar_test.cpp core/SolarCalc.cpp)
    target_include_directories(solar_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(solar_test PRIVATE -Wall -Wextra -UNDEBUG)
    add_test(NAME solar_test COMMAND solar_test)

    add_executable(process_test tests/process_test.cpp core/Process.cpp core/EventLoop.cpp)
    target_include_directories(process_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(process_test PRIVATE -Wall -Wextra -UNDEBUG)
    add_test(NAME process_test COMMAND process_test)

    add_executable(toplevel_state_test tests/toplevel_state_test.cpp toplevel/ToplevelStates.cpp)
    target_include_directories(toplevel_state_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(toplevel_state_test PRIVATE -Wall -Wextra -UNDEBUG)
    add_test(NAME toplevel_state_test COMMAND toplevel_state_test)
endif()
