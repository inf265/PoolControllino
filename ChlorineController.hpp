#pragma once
#include <Arduino.h>
#include <Controllino.h>
#include "PoolControl_Config.h"
#include "PoolControlContext.hpp"
#include "MedianValue.hpp"
#include "InjectionPumpControl.hpp"

#define CHLORINEPUMP_PIN CONTROLLINO_R13

/* Chlor liquide 48: 1-1,5 l pro Tag für 100m3
   --> für 75m3: 0,75 - 1,0 l / Tag
   --> nicht gleichzeitig mit ph minus
    Pumpe hat 1,5l/h
    --> sind 30 - 45 minuten pro Tag
    --> sind 3 bis 4 mal 10min über die Waterpump dauer verteilt
 */

class ChlorineController
{
public:
    ChlorineController() : injectionPump(CHLORINEPUMP_PIN,
                                         PoolControlContext::instance()->config.chlorinePumpCycleRunTime,
                                         PoolControlContext::instance()->config.chlorinePumpCyclePauseTime,
                                         PoolControlContext::instance()->config.chlorinePumpMaxRuntime) {}
    void init()
    {
        pinMode(CHLORINEPUMP_PIN, OUTPUT);
        digitalWrite(CHLORINEPUMP_PIN, LOW);
        PoolControlContext::instance()->data.redoxPumpState = 0;
        injectionPump.init(CHLORINEPUMP_PIN,
                           PoolControlContext::instance()->config.chlorinePumpCycleRunTime,
                           PoolControlContext::instance()->config.chlorinePumpCyclePauseTime,
                           PoolControlContext::instance()->config.chlorinePumpMaxRuntime);
    }
    /**
     * @brief Run chlorine injection control
     * 
     * Chlorine injection strategy:
     * - Maximum 45 minutes per day (enforced by InjectionPumpControl)
     * - Distributed over water pump runtime using cycle run/pause times
     * - Only active during configured time window (switchChlorOn to switchChlorOff)
     * - Automatically stops when water pump is off (handled by InjectionPumpControl)
     * 
     * The injection pump will cycle: run for cycleRunTime, pause for cyclePauseTime
     * This distributes the max 45 minutes over the water pump operating period.
     */
    void run()
    {
        auto *ctx = PoolControlContext::instance();
        
        // Check for manual override first
        if (ctx->data.chlorinePumpManualOverride)
        {
            // Check if manual override timeout has elapsed
            if (ctx->data.chlorinePumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.chlorinePumpManualOverrideSince.unixtime());
                if (elapsedSeconds >= ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    // Timeout reached - automatically return to auto mode
                    ctx->data.chlorinePumpManualOverride = false;
                }
            }
            
            if (ctx->data.chlorinePumpManualOverride)
            {
                injectionPump.setManualState(ctx->data.chlorinePumpManualState);
                ctx->data.redoxPumpState = injectionPump.isOn() ? 1 : 0;
                return;
            }
            // If timeout occurred, fall through to automatic control
        }
        
        // Automatic control based on time window
        values.add(ctx->data.redoxValue);
        if (!values.isReady())
        {
            return;
        }

        ctx->data.redoxValueMedian = values.get();

        // Chlorine injection is time-window based and distributed over water pump runtime
        // The InjectionPumpControl ensures it only runs when water pump is active
        TimeOfDay now = ctx->data.date;
        if (ctx->config.switchChlorOn < now && now < ctx->config.switchChlorOff)
        {
            // Request injection - will only run if water pump is active
            // Max 45 minutes per day is enforced by InjectionPumpControl
            injectionPump.on();
        }
        else
        {
            injectionPump.off();
        }

        // Update state for monitoring
        if (injectionPump.isOn())
        {
            ctx->data.redoxPumpState = 1;
        }
        else
        {
            ctx->data.redoxPumpState = 0;
        }
    }
    
    /**
     * @brief Set manual override state for chlorine pump
     * @param override Enable manual override (true) or return to automatic (false)
     * @param state Desired pump state when override is active (only used if override=true)
     */
    void setManualOverride(bool override, bool state = false)
    {
        auto *ctx = PoolControlContext::instance();
        ctx->data.chlorinePumpManualOverride = override;
        ctx->data.chlorinePumpManualState = state;
    }

private:
    MedianValue values;
    InjectionPumpControl injectionPump;
};