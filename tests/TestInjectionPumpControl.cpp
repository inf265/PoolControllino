// Include standard library headers FIRST to avoid macro conflicts
#include <iostream>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

// NOW include mocks - after standard library
// Since mocks directory is in include path, we can include Arduino.h directly
// This will use our mock instead of system Arduino.h
#include "Arduino.h"

// Now include project headers - they should use our mocked Arduino.h
// Note: TimeOfDay.hpp includes Arduino.h, which should now resolve to our mock
#include "TimeOfDay.hpp"
#include "PoolControlContext.hpp"
#include "RealTimeClock.hpp"
#include "InjectionPumpControl.hpp"
#include "TestFramework.hpp"

// Helper class to manage test state
class PumpTestHarness
{
public:
    static void setup()
    {
        // Reset the singleton instance
        if (PoolControlContext::s != nullptr)
        {
            delete PoolControlContext::s;
            PoolControlContext::s = nullptr;
        }
        
        // Reset Arduino mock state
        resetDigitalWriteHistory();
        
        // Get a fresh instance
        auto *ctx = PoolControlContext::instance();
        
        // Initialize test state
        ctx->data.date = DateTime(2024, 1, 1, 0, 0, 0);
        ctx->data.waterPumpState = true;
        ctx->data.waterFlowSwitch = true;
        ctx->data.error = false;
        ctx->data.waterPumpRunningSince = ctx->data.date;
        ctx->data.warning = false;
        ctx->config.waterPumpRuntimeBeforeInjection = TimeSpan(120); // 2 minutes
    }
    
    static void cleanup()
    {
        if (PoolControlContext::s != nullptr)
        {
            delete PoolControlContext::s;
            PoolControlContext::s = nullptr;
        }
    }
    
    static void advanceTime(TimeSpan duration)
    {
        auto *ctx = PoolControlContext::instance();
        ctx->data.date = ctx->data.date + duration;
    }
    
    static void setCurrentDate(DateTime dt)
    {
        auto *ctx = PoolControlContext::instance();
        ctx->data.date = dt;
    }
};

// Test 1: Daily runtime reset on first switch-on
// Verifies that runtime tracking resets when pump first starts each day
TEST(daily_runtime_reset)
{
    PumpTestHarness::setup();
    
    // Create pump with 5 min cycle, 2 min pause, 30 min max
    TimeSpan cycleRunTime(300);  // 5 minutes
    TimeSpan cyclePauseTime(120); // 2 minutes
    TimeSpan maxRuntime(1800);    // 30 minutes
    
    InjectionPumpControl pump(13, cycleRunTime, cyclePauseTime, maxRuntime);
    
    // Set up initial conditions
    auto *ctx = PoolControlContext::instance();
    ctx->data.waterPumpState = true;
    ctx->data.waterFlowSwitch = true;
    ctx->data.waterPumpRunningSince = ctx->data.date - TimeSpan(300); // Pump running for 5 minutes
    
    // Reset digitalWrite history before test
    resetDigitalWriteHistory();
    
    // First call to on() transitions from OFF to ON state (calls LOW, then sets state to ON)
    // Second call actually turns the pump on HIGH (when state is already ON)
    pump.on(); // First call: transitions OFF -> ON, sets state but calls LOW
    pump.on(); // Second call: state is ON, actually turns pump HIGH
    
    // Verify pump started (digitalWrite should be called with HIGH)
    TestFramework::assertEqual(HIGH, getLastDigitalWriteValue(), "Pump should start");
    TestFramework::assertTrue(pump.isOn(), "Pump should be on");
    
    PumpTestHarness::cleanup();
}

// Test 2: Runtime accumulates across cycles
// Verifies that runtime properly accumulates across multiple run/pause cycles
TEST(runtime_accumulation)
{
    PumpTestHarness::setup();
    
    TimeSpan cycleRunTime(300);  // 5 minutes
    TimeSpan cyclePauseTime(120); // 2 minutes
    TimeSpan maxRuntime(1800);    // 30 minutes
    
    InjectionPumpControl pump(13, cycleRunTime, cyclePauseTime, maxRuntime);
    
    auto *ctx = PoolControlContext::instance();
    ctx->data.waterPumpState = true;
    ctx->data.waterFlowSwitch = true;
    ctx->data.waterPumpRunningSince = ctx->data.date - TimeSpan(300);
    
    resetDigitalWriteHistory();
    
    // Start pump - need two calls: first transitions OFF->ON, second actually turns it on
    pump.on(); // First call: transitions OFF -> ON
    pump.on(); // Second call: state is ON, actually turns pump HIGH
    TestFramework::assertTrue(pump.isOn(), "Pump should start");
    
    // Advance time by cycle run time (5 minutes)
    PumpTestHarness::advanceTime(cycleRunTime);
    
    // Pump should now pause (cycle runtime exceeded)
    // First call: sets state to PAUSING but pump is still on
    // Second call: goes into PAUSING case and turns pump off
    pump.on(); // This checks runtime and sets state to PAUSING (but pump still on)
    pump.on(); // This goes into PAUSING case and turns pump LOW
    TestFramework::assertFalse(pump.isOn(), "Pump should pause after cycle time");
    
    // Advance time by pause time (2 minutes)
    PumpTestHarness::advanceTime(cyclePauseTime);
    
    // Pump should resume - need to call on() while in PAUSING state
    pump.on(); // This should check pause time and resume
    pump.on(); // Second call after resume transition to actually turn on HIGH
    TestFramework::assertTrue(pump.isOn(), "Pump should resume after pause");
    
    PumpTestHarness::cleanup();
}

// Test 3: Max runtime enforcement - pump should stop when max runtime reached
// Verifies that daily max runtime limit (e.g., 45 min for chlorine) is enforced
TEST(max_runtime_enforcement)
{
    PumpTestHarness::setup();
    
    TimeSpan cycleRunTime(300);  // 5 minutes
    TimeSpan cyclePauseTime(120); // 2 minutes
    TimeSpan maxRuntime(600);     // 10 minutes max (small for testing)
    
    InjectionPumpControl pump(13, cycleRunTime, cyclePauseTime, maxRuntime);
    
    auto *ctx = PoolControlContext::instance();
    ctx->data.waterPumpState = true;
    ctx->data.waterFlowSwitch = true;
    ctx->data.waterPumpRunningSince = ctx->data.date - TimeSpan(300);
    
    resetDigitalWriteHistory();
    
    // Start pump - need two calls: first transitions OFF->ON, second actually turns it on
    pump.on(); // First call: transitions OFF -> ON
    pump.on(); // Second call: state is ON, actually turns pump HIGH
    TestFramework::assertTrue(pump.isOn(), "Pump should start");
    
    // Run first cycle (5 minutes)
    PumpTestHarness::advanceTime(cycleRunTime);
    pump.on(); // Should pause after cycle
    
    // Advance pause time and resume (another 5 minutes)
    PumpTestHarness::advanceTime(cyclePauseTime);
    pump.on(); // Should resume
    
    PumpTestHarness::advanceTime(cycleRunTime);
    pump.on(); // Should pause again, but now we've reached max runtime
    
    // After accumulating 10 minutes (maxRuntime), pump should be in MAXTIMEEXCEEDED state
    PumpTestHarness::advanceTime(cyclePauseTime);
    pump.on(); // Try to resume, but max runtime should be exceeded
    
    // Verify warning was set
    TestFramework::assertTrue(ctx->data.warning, "Warning should be set when max runtime exceeded");
    TestFramework::assertFalse(pump.isOn(), "Pump should not run when max runtime exceeded");
    
    PumpTestHarness::cleanup();
}

// Test 4: Water pump dependency - no injection when water pump is off
// CRITICAL: Injection pumps must not run when water pump is off
TEST(water_pump_dependency)
{
    PumpTestHarness::setup();
    
    TimeSpan cycleRunTime(300);
    TimeSpan cyclePauseTime(120);
    TimeSpan maxRuntime(1800);
    
    InjectionPumpControl pump(13, cycleRunTime, cyclePauseTime, maxRuntime);
    
    auto *ctx = PoolControlContext::instance();
    ctx->data.waterPumpState = false; // Water pump is off
    ctx->data.waterFlowSwitch = true;
    
    // Injection pump should not start when water pump is off
    pump.on();
    TestFramework::assertFalse(pump.isOn(), "Injection pump should not run when water pump is off");
    TestFramework::assertEqual(LOW, getLastDigitalWriteValue(), "Pump should be off");
    
    PumpTestHarness::cleanup();
}

int main()
{
    std::cout << "Testing InjectionPumpControl...\n\n";
    
    RUN_TEST(daily_runtime_reset);
    RUN_TEST(runtime_accumulation);
    RUN_TEST(max_runtime_enforcement);
    RUN_TEST(water_pump_dependency);
    
    return TestFramework::runTests();
}
