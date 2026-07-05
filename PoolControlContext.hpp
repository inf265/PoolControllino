#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "TimeOfDay.hpp"

class Data
{
public:
    DateTime date;
    float phValue = 0.0;
    float phValueMedian = 0.0;
    float redoxValue = 0.0;
    float redoxValueMedian = 0.0;
    float waterTemperature = 0.0;
    float housingTemperature = 0.0;

    bool phPumpState = 0;
    bool redoxPumpState = 0;

    uint32_t phAdcValue = 0;
    uint32_t redoxAdcValue = 0;
    uint32_t waterflowSwitchAdcValue = 0;
    uint32_t powerSupplyAdcValue = 0;

    bool powerSupply = 1;     // 1 - GRID, 0 - BATTERY
    bool waterFlowSwitch = 0; // 0 - OFF
    bool waterPumpState = 0;  // 0 - OFF
    char clientIP[16]{0};
    DateTime waterPumpRunningSince;
    unsigned long uptimeSeconds = 0; // Track controller uptime in seconds
    bool error{false};
    char errorText[128]{0};
    char errorTimestamp[32]{0};
    bool warning{false};
    char warningText[128]{0};
    char warningTimestamp[32]{0};
    
    // Manual override flags for pump control
    bool waterPumpManualOverride{false};
    bool waterPumpManualState{false};
    DateTime waterPumpManualOverrideSince;
    bool phPumpManualOverride{false};
    bool phPumpManualState{false};
    DateTime phPumpManualOverrideSince;
    bool chlorinePumpManualOverride{false};
    bool chlorinePumpManualState{false};
    DateTime chlorinePumpManualOverrideSince;
};

class Configuration
{
public:
    Configuration() : switchOn(7, 0, 0),
                      switchOff(19, 0, 0),
                      phPumpCycleRunTime(0, 0, 5, 0),
                      phPumpCyclePauseTime(0, 0, 5, 0),
                      phPumpMaxRuntime(0, 0, 30, 0),
                      chlorinePumpCycleRunTime(0, 0, 10, 0),
                      chlorinePumpCyclePauseTime(0, 1, 0, 0),
                      chlorinePumpMaxRuntime(0, 0, 45, 0),
                      switchChlorOn(10, 0, 0),
                      switchChlorOff(17, 0, 0),
                      waterPumpRuntimeBeforeInjection(0, 0, 2, 0),
                      waterPumpOffWhenFlowswitchOffTime(0, 0, 0, 30),
                      pumpManualOverrideTimeoutSeconds(1800)
    {
    }
    unsigned long updateTime{3000};
    TimeOfDay switchOn;
    TimeOfDay switchOff;
    float phTargetValue{7.4};
    float phTargetValueHysterese{0.1}; // lower than targetvalue to switch pomp off
    float phCalculationMValue{0};
    float phCalculationCValue{0};
    TimeSpan phPumpCycleRunTime;
    TimeSpan phPumpCyclePauseTime;
    TimeSpan phPumpMaxRuntime;
    TimeSpan chlorinePumpCycleRunTime;
    TimeSpan chlorinePumpCyclePauseTime;
    TimeSpan chlorinePumpMaxRuntime;
    uint16_t redoxTargetValue{465};
    uint16_t redoxTargetValueHysterese{50};
    float redoxCalculationMValue{0};
    float redoxCalculationCValue{0};
    TimeOfDay switchChlorOn;
    TimeOfDay switchChlorOff;
    TimeSpan waterPumpRuntimeBeforeInjection;
    TimeSpan waterPumpOffWhenFlowswitchOffTime;
    unsigned long pumpManualOverrideTimeoutSeconds{1800}; // Default: 30 minutes (1800 seconds)
    char ipAddress[16]{"192.168.42.220"};                 // Static IP, configurable via <IP>/config
    char switchConfigRaw[768]{0};

    char *toJson(char *buffer, size_t size)
    {
        JsonDocument config;
        config["updateTime"] = updateTime;
        config["switchOn"] = switchOn.toString();
        config["switchOff"] = switchOff.toString();
        config["phTargetValue"] = phTargetValue;
        config["phTargetValueHysterese"] = phTargetValueHysterese;
        config["phCalculationMValue"] = phCalculationMValue;
        config["phCalculationCValue"] = phCalculationCValue;
        config["phPumpCycleRunTime"] = phPumpCycleRunTime.totalseconds();
        config["phPumpCyclePauseTime"] = phPumpCyclePauseTime.totalseconds();
        config["phPumpMaxRuntime"] = phPumpMaxRuntime.totalseconds();
        config["chlorinePumpCycleRunTime"] = chlorinePumpCycleRunTime.totalseconds();
        config["chlorinePumpCyclePauseTime"] = chlorinePumpCyclePauseTime.totalseconds();
        config["chlorinePumpMaxRuntime"] = chlorinePumpMaxRuntime.totalseconds();
        config["redoxTargetValue"] = redoxTargetValue;
        config["redoxTargetValueHysterese"] = redoxTargetValueHysterese;
        config["redoxCalculationMValue"] = redoxCalculationMValue;
        config["redoxCalculationCValue"] = redoxCalculationCValue;
        config["switchChlorOn"] = switchChlorOn.toString();
        config["switchChlorOff"] = switchChlorOff.toString();
        config["waterPumpRuntimeBeforeInjection"] = waterPumpRuntimeBeforeInjection.totalseconds();
        config["waterPumpOffWhenFlowswitchOffTime"] = waterPumpOffWhenFlowswitchOffTime.totalseconds();
        config["pumpManualOverrideTimeoutSeconds"] = pumpManualOverrideTimeoutSeconds;
        config["ipAddress"] = ipAddress;

        serializeJson(config, buffer, size);
        return buffer;
    }

    void fromJson(const JsonDocument &config)
    {

        updateTime = config["updateTime"] ? config["updateTime"] : updateTime;

        if (config["switchOn"])
        {
            switchOn.fromString(config["switchOn"].as<String>());
        }
        if (config["switchOff"])
        {
            switchOff.fromString(config["switchOff"].as<String>());
        }

        phTargetValue = config.containsKey("phTargetValue") ? config["phTargetValue"].as<float>() : phTargetValue;
        phTargetValueHysterese = config.containsKey("phTargetValueHysterese") ? config["phTargetValueHysterese"].as<float>() : phTargetValueHysterese;
        phCalculationMValue = config.containsKey("phCalculationMValue") ? config["phCalculationMValue"].as<float>() : phCalculationMValue;
        phCalculationCValue = config.containsKey("phCalculationCValue") ? config["phCalculationCValue"].as<float>() : phCalculationCValue;
        if (config.containsKey("phPumpCycleRunTime"))
        {
            phPumpCycleRunTime.setSeconds(config["phPumpCycleRunTime"].as<uint32_t>());
        }
        if (config.containsKey("phPumpCyclePauseTime"))
        {
            phPumpCyclePauseTime.setSeconds(config["phPumpCyclePauseTime"].as<uint32_t>());
        }
        if (config.containsKey("phPumpMaxRuntime"))
        {
            phPumpMaxRuntime.setSeconds(config["phPumpMaxRuntime"].as<uint32_t>());
        }

        if (config.containsKey("chlorinePumpCycleRunTime"))
        {
            chlorinePumpCycleRunTime.setSeconds(config["chlorinePumpCycleRunTime"].as<uint32_t>());
        }
        if (config.containsKey("chlorinePumpCyclePauseTime"))
        {
            chlorinePumpCyclePauseTime.setSeconds(config["chlorinePumpCyclePauseTime"].as<uint32_t>());
        }
        if (config.containsKey("chlorinePumpMaxRuntime"))
        {
            chlorinePumpMaxRuntime.setSeconds(config["chlorinePumpMaxRuntime"].as<uint32_t>());
        }

        redoxTargetValue = config.containsKey("redoxTargetValue") ? config["redoxTargetValue"].as<uint16_t>() : redoxTargetValue;
        redoxTargetValueHysterese = config.containsKey("redoxTargetValueHysterese") ? config["redoxTargetValueHysterese"].as<float>() : redoxTargetValueHysterese;
        redoxCalculationMValue = config.containsKey("redoxCalculationMValue") ? config["redoxCalculationMValue"].as<float>() : redoxCalculationMValue;
        redoxCalculationCValue = config.containsKey("redoxCalculationCValue") ? config["redoxCalculationCValue"].as<float>() : redoxCalculationCValue;

        if (config.containsKey("switchChlorOn"))
        {
            switchChlorOn.fromString(config["switchChlorOn"].as<String>());
        }
        if (config.containsKey("switchChlorOff"))
        {
            switchChlorOff.fromString(config["switchChlorOff"].as<String>());
        }

        if (config.containsKey("waterPumpRuntimeBeforeInjection"))
        {
            waterPumpRuntimeBeforeInjection.setSeconds(config["waterPumpRuntimeBeforeInjection"]);
        }
        if (config.containsKey("waterPumpOffWhenFlowswitchOffTime"))
        {
            waterPumpOffWhenFlowswitchOffTime.setSeconds(config["waterPumpOffWhenFlowswitchOffTime"]);
        }
        
        if (config.containsKey("pumpManualOverrideTimeoutSeconds"))
        {
            pumpManualOverrideTimeoutSeconds = config["pumpManualOverrideTimeoutSeconds"].as<unsigned long>();
        }

        if (config.containsKey("ipAddress"))
        {
            const char *ip = config["ipAddress"];
            if (ip && ip[0])
            {
                strncpy(ipAddress, ip, sizeof(ipAddress) - 1);
                ipAddress[sizeof(ipAddress) - 1] = '\0';
            }
        }
    }
};

class PoolControlContext
{
public:
    PoolControlContext(PoolControlContext const &) = delete;
    PoolControlContext &operator=(PoolControlContext const &) = delete;

    static PoolControlContext *instance()
    {
        if (s == nullptr)
        {
            s = new PoolControlContext();
        }
        return s;
    }

    Data data;
    Configuration config;
    static PoolControlContext *s;

private:
    PoolControlContext() {}
};

PoolControlContext *PoolControlContext::s = nullptr;
