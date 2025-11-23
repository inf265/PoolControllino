cmake_minimum_required(VERSION 3.16)
project(PoolControllinoTests LANGUAGES CXX)

# Use standard C++ compiler (not Arduino toolchain)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directories - mocks directory first so it's found before system Arduino.h
# This ensures our mocks override real Arduino/Controllino headers
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../libraries/ArduinoJson/src
)

# Compiler definitions to help with mocking
add_definitions(-DARDUINO=10800)
add_definitions(-DTEST_BUILD=1)

# Add mock library
add_library(mocks STATIC
    mocks/Arduino.cpp
)

# Test executable
add_executable(test_injection_pump
    TestInjectionPumpControl.cpp
    TestFramework.cpp
)

# Link mocks
target_link_libraries(test_injection_pump PRIVATE mocks)

# Enable testing
enable_testing()
add_test(NAME InjectionPumpControlTests COMMAND test_injection_pump)

