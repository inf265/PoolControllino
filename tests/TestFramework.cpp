#include "TestFramework.hpp"
#include <iostream>

int TestFramework::testsRun = 0;
int TestFramework::testsPassed = 0;
int TestFramework::testsFailed = 0;
std::vector<std::string> TestFramework::failures;

void TestFramework::assert(bool condition, const std::string &message)
{
    testsRun++;
    if (condition)
    {
        testsPassed++;
        std::cout << ".";
    }
    else
    {
        testsFailed++;
        recordFailure(message);
        std::cout << "F";
    }
}

void TestFramework::assertEqual(int expected, int actual, const std::string &message)
{
    assert(expected == actual, message + " (expected: " + std::to_string(expected) + 
                                ", actual: " + std::to_string(actual) + ")");
}

void TestFramework::assertEqual(uint32_t expected, uint32_t actual, const std::string &message)
{
    assert(expected == actual, message + " (expected: " + std::to_string(expected) + 
                                ", actual: " + std::to_string(actual) + ")");
}

void TestFramework::assertTrue(bool condition, const std::string &message)
{
    assert(condition, message);
}

void TestFramework::assertFalse(bool condition, const std::string &message)
{
    assert(!condition, message);
}

void TestFramework::recordFailure(const std::string &message)
{
    failures.push_back(message);
}

int TestFramework::runTests()
{
    std::cout << "Running tests...\n";
    
    // This will be called from the test file
    // All tests should have been registered via RUN_TEST macros
    
    std::cout << "\n\n";
    std::cout << "Tests run: " << testsRun << "\n";
    std::cout << "Tests passed: " << testsPassed << "\n";
    std::cout << "Tests failed: " << testsFailed << "\n";
    
    if (failures.size() > 0)
    {
        std::cout << "\nFailures:\n";
        for (const auto &failure : failures)
        {
            std::cout << "  - " << failure << "\n";
        }
    }
    
    return testsFailed > 0 ? 1 : 0;
}

