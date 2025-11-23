# PoolControllino Tests

This directory contains unit tests that can be run on a PC without flashing to the Arduino controller.

## Structure

- `mocks/` - Mock implementations of Arduino-specific code (Arduino.h, digitalWrite, etc.)
- `TestFramework.*` - Simple test framework for running tests
- `TestInjectionPumpControl.cpp` - Tests for the InjectionPumpControl runtime tracking

## Building and Running Tests

From the project root:

```bash
cd tests
mkdir -p build
cd build
cmake ..
make
./test_injection_pump
```

Or use ctest:

```bash
cd tests/build
cmake ..
make
ctest
```

## Current Tests

### InjectionPumpControl Tests

1. **daily_runtime_reset** - Verifies that runtime resets on first switch-on each day
2. **runtime_accumulation** - Tests that runtime accumulates correctly across cycles
3. **max_runtime_enforcement** - Ensures pump stops when daily max runtime is reached
4. **water_pump_dependency** - Verifies pump doesn't run when water pump is off

## Adding New Tests

1. Create a new test file (e.g., `TestSomething.cpp`)
2. Include the test framework and mocks
3. Use the `TEST(name)` macro to define tests
4. Use `RUN_TEST(name)` to execute them
5. Add the test executable to `tests/CMakeLists.txt`

## Mocking

The tests use mocks for Arduino-specific functionality:
- `mocks/Arduino.h` - Mocks digitalWrite, pinMode, String class, etc.
- `PoolControlContext` - Uses the actual singleton, but can be reset between tests

## Notes

- Tests run on PC (Linux/Windows/Mac) without Arduino hardware
- Mocks provide the minimal Arduino API needed for testing
- The test framework is lightweight and doesn't require external dependencies

