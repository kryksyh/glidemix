cmake_minimum_required(VERSION 3.20)
project(MixHouseBezierLaneTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Quick)
add_executable(bezier-lane-tests
    BezierLaneTests.cpp
    ../../view/bezierautomationlane.cpp
    ../../view/bezierautomationlane.h)
target_include_directories(bezier-lane-tests PRIVATE ../../../../au3/libraries)
target_link_libraries(bezier-lane-tests PRIVATE Qt6::Quick)
enable_testing()
add_test(NAME bezier-lane-tests COMMAND bezier-lane-tests)
set_tests_properties(bezier-lane-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
