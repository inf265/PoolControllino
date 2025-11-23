#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

class TestFramework
{
public:
    static int runTests();

    static void assert(bool condition, const std::string &message);
    static void assertEqual(int expected, int actual, const std::string &message);
    static void assertEqual(uint32_t expected, uint32_t actual, const std::string &message);
    static void assertTrue(bool condition, const std::string &message);
    static void assertFalse(bool condition, const std::string &message);

    static void recordFailure(const std::string &message);

private:
    static int testsRun;
    static int testsPassed;
    static int testsFailed;
    static std::vector<std::string> failures;
};

#define TEST(name) \
    void test_##name(); \
    void test_##name()

#define RUN_TEST(name) \
    do { \
        try { \
            test_##name(); \
        } catch (const std::exception &e) { \
            TestFramework::recordFailure("Exception in " #name ": " + std::string(e.what())); \
        } \
    } while(0)

