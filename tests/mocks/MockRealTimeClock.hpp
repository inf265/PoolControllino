#pragma once

#include "../RealTimeClock.hpp"

// Mock RealTimeClock - just implements the static method used in tests
class MockRealTimeClock
{
public:
    static char *getFullDateTimeString(DateTime &dateTime, char *result)
    {
        // Simple mock implementation
        std::memset(result, 0, 32);
        snprintf(result, 31, "%04d-%02d-%02d %02d:%02d:%02d",
                 dateTime.year(), dateTime.month(), dateTime.day(),
                 dateTime.hour(), dateTime.minute(), dateTime.second());
        return result;
    }
};

