#pragma once
#include <Arduino.h>
#include <Controllino.h>
#include "PoolControl_Config.h"
#include "PoolControlContext.hpp"
#include "MedianValue.hpp"
#include "InjectionPumpControl.hpp"

#define PHPUMP_PIN CONTROLLINO_R12

/* Ph dosierung ca. 50-100ml pro 10m3 Wasser für Senkung um 0,1 pH
    Pumpe hat 1,5l/h
    --> sind 350-700 ml für 70m3 und 0,1 pH
    --> sind 14-28 minuten für 70m3 und 0,1 pH
 */

class PhController
{
public:
    PhController() : injectionPump(PHPUMP_PIN,
                                   PoolControlContext::instance()->config.phPumpCycleRunTime,
                                   PoolControlContext::instance()->config.phPumpCyclePauseTime,
                                   PoolControlContext::instance()->config.phPumpMaxRuntime,
                                   "pH") {}
    void init()
    {
        pinMode(PHPUMP_PIN, OUTPUT);
        digitalWrite(PHPUMP_PIN, LOW);
        PoolControlContext::instance()->data.phPumpState = 0;
        injectionPump.init(PHPUMP_PIN,
                           PoolControlContext::instance()->config.phPumpCycleRunTime,
                           PoolControlContext::instance()->config.phPumpCyclePauseTime,
                           PoolControlContext::instance()->config.phPumpMaxRuntime,
                           "pH");
    }
    /**
     * @brief Run pH injection control
     * 
     * pH injection strategy:
     * - Value-dependent: injects when pH is above target value
     * - Stops when pH reaches target - hysteresis
     * - Automatically stops when water pump is off (handled by InjectionPumpControl)
     * - Uses cycle run/pause times to prevent over-injection
     * - Max runtime limit prevents excessive injection
     */
    void run()
    {
        auto *ctx = PoolControlContext::instance();
        
        // Check for manual override first
        if (ctx->data.phPumpManualOverride)
        {
            // Check if manual override timeout has elapsed
            if (ctx->data.phPumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.phPumpManualOverrideSince.unixtime());
                if (elapsedSeconds >= ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    // Timeout reached - automatically return to auto mode
                    ctx->data.phPumpManualOverride = false;
                }
            }
            
            if (ctx->data.phPumpManualOverride)
            {
                injectionPump.setManualState(ctx->data.phPumpManualState);
                ctx->data.phPumpState = injectionPump.isOn() ? 1 : 0;
                return;
            }
            // If timeout occurred, fall through to automatic control
        }
        
        // Automatic control based on pH value
        values.add(ctx->data.phValue);
        if (!values.isReady())
        {
            return;
        }
        ctx->data.phValueMedian = values.get();

        float phValue = values.get();

        // Handle invalid measurements (pH > 8.8 indicates sensor failure)
        if (phValue > (float)8.8)
        {
            injectionPump.off();
        }
        // pH is too high - need to inject acid to lower it
        else if (phValue > ctx->config.phTargetValue)
        {
            // Request injection - will only run if water pump is active
            injectionPump.on();
        }
        // pH is at or below target - hysteresis - stop injection
        else if (phValue <= ctx->config.phTargetValue - ctx->config.phTargetValueHysterese)
        {
            injectionPump.off();
        }
        // pH is in hysteresis zone - maintain current state
        else
        {
            injectionPump.maintain();
        }

        // Update state for monitoring
        if (injectionPump.isOn())
        {
            ctx->data.phPumpState = 1;
        }
        else
        {
            ctx->data.phPumpState = 0;
        }
    }
    
    /**
     * @brief Set manual override state for pH pump
     * @param override Enable manual override (true) or return to automatic (false)
     * @param state Desired pump state when override is active (only used if override=true)
     */
    void setManualOverride(bool override, bool state = false)
    {
        auto *ctx = PoolControlContext::instance();
        ctx->data.phPumpManualOverride = override;
        ctx->data.phPumpManualState = state;
    }

private:
    MedianValue values;
    InjectionPumpControl injectionPump;
};