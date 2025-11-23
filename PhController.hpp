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
                                   PoolControlContext::instance()->config.phPumpMaxRuntime) {}
    void init()
    {
        pinMode(PHPUMP_PIN, OUTPUT);
        digitalWrite(PHPUMP_PIN, LOW);
        PoolControlContext::instance()->data.phPumpState = 0;
        injectionPump.init(PHPUMP_PIN,
                           PoolControlContext::instance()->config.phPumpCycleRunTime,
                           PoolControlContext::instance()->config.phPumpCyclePauseTime,
                           PoolControlContext::instance()->config.phPumpMaxRuntime);
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
        values.add(PoolControlContext::instance()->data.phValue);
        if (!values.isReady())
        {
            return;
        }
        PoolControlContext::instance()->data.phValueMedian = values.get();

        float phValue = values.get();

        // Handle invalid measurements (pH > 8.8 indicates sensor failure)
        if (phValue > (float)8.8)
        {
            injectionPump.off();
        }
        // pH is too high - need to inject acid to lower it
        else if (phValue > PoolControlContext::instance()->config.phTargetValue)
        {
            // Request injection - will only run if water pump is active
            injectionPump.on();
        }
        // pH is at or below target - hysteresis - stop injection
        else if (phValue <= PoolControlContext::instance()->config.phTargetValue - 
                 PoolControlContext::instance()->config.phTargetValueHysterese)
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
            PoolControlContext::instance()->data.phPumpState = 1;
        }
        else
        {
            PoolControlContext::instance()->data.phPumpState = 0;
        }
    }

private:
    MedianValue values;
    InjectionPumpControl injectionPump;
};