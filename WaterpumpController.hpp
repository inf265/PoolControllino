#pragma once

#include <Arduino.h>
#include <Controllino.h>
#include "PoolControl_Config.h"
#include "PoolControlContext.hpp"
#include "TimeOfDay.hpp"
#include "RealTimeClock.hpp"

#define WATERPUMP_PIN CONTROLLINO_R10

class WaterpumpController
{
public:
    void init()
    {
        pinMode(WATERPUMP_PIN, OUTPUT);
        digitalWrite(WATERPUMP_PIN, LOW);
        PoolControlContext::instance()->data.waterPumpState = 0;
    }
    /**
     * @brief Run automatic water pump control
     * 
     * Priority order:
     * 1. Error state: pump OFF (highest priority)
     * 2. Freeze protection: if housing temp <= 0°C, pump ON (safety override)
     * 3. Manual override: uses manual state
     * 4. Schedule: uses time-based schedule
     */
    void run()
    {
        auto *ctx = PoolControlContext::instance();
        
        // 1. Error check - if error, pump must be OFF (highest priority)
        if (ctx->data.error)
        {
            ctx->data.waterPumpState = 0;
            digitalWrite(WATERPUMP_PIN, ctx->data.waterPumpState);
            return;
        }
        
        // 2. Freeze protection - if housing temperature is 0°C or below, force pump ON
        // This is a safety feature to prevent freezing, overrides manual and schedule
        if (ctx->data.housingTemperature <= 0.0)
        {
            if (ctx->data.waterPumpState == 0)
            {
                ctx->data.waterPumpRunningSince = ctx->data.date;
            }
            ctx->data.waterPumpState = 1;
            digitalWrite(WATERPUMP_PIN, ctx->data.waterPumpState);
            return;
        }
        
        // 3. Check for manual override
        if (ctx->data.waterPumpManualOverride)
        {
            // Check if manual override timeout has elapsed
            if (ctx->data.waterPumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.waterPumpManualOverrideSince.unixtime());
                if (elapsedSeconds >= ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    // Timeout reached - automatically return to auto mode
                    ctx->data.waterPumpManualOverride = false;
                }
            }
            
            if (ctx->data.waterPumpManualOverride)
            {
                ctx->data.waterPumpState = ctx->data.waterPumpManualState ? 1 : 0;
                // Check if pump just started (waterPumpRunningSince is uninitialized)
                // Use a sentinel value check - if year is 0, it's not initialized
                if (ctx->data.waterPumpState == 1 && ctx->data.waterPumpRunningSince.year() == 0)
                {
                    ctx->data.waterPumpRunningSince = ctx->data.date;
                }
                digitalWrite(WATERPUMP_PIN, ctx->data.waterPumpState);
                return;
            }
            // If timeout occurred, fall through to automatic control
        }
        
        // 4. Automatic control based on time schedule
        if (ctx->config.switchOn == ctx->config.switchOff)
        {
            ctx->data.waterPumpState = 0;
            digitalWrite(WATERPUMP_PIN, ctx->data.waterPumpState);
            return;
        }
        if (ctx->config.switchOn > ctx->config.switchOff)
        {
            ctx->data.waterPumpState = 0;
            digitalWrite(WATERPUMP_PIN, ctx->data.waterPumpState);
            return;
        }
        TimeOfDay now = ctx->data.date;
        if (ctx->config.switchOn < now && now < ctx->config.switchOff)
        {
            if (ctx->data.waterPumpState == 0)
            {
                ctx->data.waterPumpRunningSince = ctx->data.date;
            }
            ctx->data.waterPumpState = 1;
        }
        else
        {
            ctx->data.waterPumpState = 0;
        }
        
        // Flow switch safety check: if pump is on but flow switch is off for too long,
        // turn pump off and set error state (will be caught by error check on next run)
        if (ctx->data.waterPumpState == 1 &&
            ctx->data.waterFlowSwitch == 0 &&
            flowSwitchTooLongOff())
        {
            ctx->data.waterPumpState = 0;
            ctx->data.error = true;
            ctx->data.errorText = "Flowswitch still off when pump started";
            RealTimeClock::getFullDateTimeString(ctx->data.date, ctx->data.errorTimestamp);
        }
        
        digitalWrite(WATERPUMP_PIN, ctx->data.waterPumpState);
    }
    
    /**
     * @brief Set manual override state for water pump
     * @param override Enable manual override (true) or return to automatic (false)
     * @param state Desired pump state when override is active (only used if override=true)
     */
    void setManualOverride(bool override, bool state = false)
    {
        auto *ctx = PoolControlContext::instance();
        ctx->data.waterPumpManualOverride = override;
        ctx->data.waterPumpManualState = state;
    }

private:
    bool flowSwitchTooLongOff()
    {
        TimeSpan difftime = PoolControlContext::instance()->data.date - PoolControlContext::instance()->data.waterPumpRunningSince;
        if (difftime.totalseconds() >= PoolControlContext::instance()->config.waterPumpOffWhenFlowswitchOffTime.totalseconds())
        {
            return true;
        }
        return false;
    }
};