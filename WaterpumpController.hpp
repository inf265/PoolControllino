#pragma once

#include <Arduino.h>
#include <Controllino.h>
#include "PoolControl_Config.h"
#include "PoolControlContext.hpp"
#include "TimeOfDay.hpp"

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
     * If manual override is active, uses manual state instead of automatic schedule
     */
    void run()
    {
        auto *ctx = PoolControlContext::instance();
        
        // Check for manual override first
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
        
        // Automatic control based on time schedule
        if (ctx->config.switchOn == ctx->config.switchOff)
        {
            return;
        }
        if (ctx->config.switchOn > ctx->config.switchOff)
        {
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
        // next "if" will not switch the pump off when it just started but
        // the flowswitch is still of for a small amount of time. Anyway, after that
        // time, when the flowswitch goes off suddely, we will switch the pump off
        // and go into error state
        if (ctx->data.waterPumpState == 1 &&
            ctx->data.waterFlowSwitch == 0 &&
            flowSwitchTooLongOff())
        {
            ctx->data.waterPumpState = 0;
            ctx->data.error = true;
            ctx->data.errorText = "Flowswitch still off when pump started";
            RealTimeClock::getFullDateTimeString(ctx->data.date, ctx->data.errorTimestamp);
        }

        if (ctx->data.error)
        {
            ctx->data.waterPumpState = 0;
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