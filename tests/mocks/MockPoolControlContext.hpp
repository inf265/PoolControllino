#pragma once

#include "../TimeOfDay.hpp"
#include "../PoolControlContext.hpp"
#include <cstring>
#include <string>

// Mock PoolControlContext for testing
class MockPoolControlContext
{
public:
    static MockPoolControlContext *instance()
    {
        if (s_instance == nullptr)
        {
            s_instance = new MockPoolControlContext();
        }
        return s_instance;
    }

    static void reset()
    {
        if (s_instance != nullptr)
        {
            delete s_instance;
            s_instance = nullptr;
        }
    }

    // Mock data
    DateTime date;
    bool waterPumpState = true;
    bool waterFlowSwitch = true;
    bool error = false;
    DateTime waterPumpRunningSince;

    // Mock config
    TimeSpan waterPumpRuntimeBeforeInjection;
    bool warning = false;
    std::string warningText;
    char warningTimestamp[32]{0};

    // Set current date/time for testing
    void setCurrentDate(DateTime dt)
    {
        date = dt;
    }

    void setWaterPumpState(bool state)
    {
        waterPumpState = state;
        if (state && waterPumpRunningSince == DateTime(0))
        {
            waterPumpRunningSince = date;
        }
        else if (!state)
        {
            waterPumpRunningSince = DateTime(0);
        }
    }

    void advanceTime(TimeSpan duration)
    {
        date = date + duration;
    }

private:
    static MockPoolControlContext *s_instance;
    MockPoolControlContext()
    {
        date = DateTime(2024, 1, 1, 0, 0, 0); // Start at a known date
        waterPumpRunningSince = DateTime(0);
        waterPumpRuntimeBeforeInjection = TimeSpan(120); // 2 minutes default
    }
};

MockPoolControlContext *MockPoolControlContext::s_instance = nullptr;

// Redirect PoolControlContext::instance() to return our mock
// We'll need to create a test version of PoolControlContext that uses the mock

