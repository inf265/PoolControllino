#include <Controllino.h>
#include "version.h"
#include "Logging.hpp"
#include "Serial.hpp"
#include "Networking.hpp"
#include "WebServer.hpp"
#include "Eeprom.hpp"
#include "Identification.hpp"
#include "ArduinoJson.h"
#include "TemperatureSensors.hpp"
#include "RealTimeClock.hpp"
#include "TimeOfDay.hpp"
#include "AdcController.hpp"
#include "WaterpumpController.hpp"
#include "PhController.hpp"
#include "ChlorineController.hpp"

static HC::Serial serial;
static HC::Eeprom eeprom;
static HC::Networking networking;
HC::WebServer webserver;
static RealTimeClock realTimeClock;
static TemperatureSensors temperatureSensors;
static AdcController adcController;
static WaterpumpController waterpumpController;
static PhController phController;
static ChlorineController chlorineController;

// Track system uptime
static unsigned long systemStartTime = 0;

// char switchConfigRaw[1024]{0};

int freeRam()
{
    extern int __heap_start, *__brkval;
    int v;
    return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

void setup()
{
    serial.setup();
    eeprom.setup();
    realTimeClock.init();

    // Load configuration from EEPROM BEFORE networking, so the static IP
    // (Configuration::ipAddress, editable via <IP>/config, default 192.168.42.220)
    // is available when Ethernet is brought up.
    if (!eeprom.readConfig(PoolControlContext::instance()->config.switchConfigRaw, 900))
    {
        LOGN(F("Read Config"));
    }
    else
    {
        LOG(F("No Config present. Writing initial config with defaults, len:"));
        PoolControlContext::instance()->config.toJson(PoolControlContext::instance()->config.switchConfigRaw, 900);
        eeprom.writeConfig(PoolControlContext::instance()->config.switchConfigRaw, strlen(PoolControlContext::instance()->config.switchConfigRaw));
    }
    {
        JsonDocument jsonTmp;
        deserializeJson(jsonTmp, PoolControlContext::instance()->config.switchConfigRaw);
        PoolControlContext::instance()->config.fromJson(jsonTmp);
    }
    // Re-serialize so the /config editor always reflects the full current
    // configuration, including fields (like ipAddress) that a stored config
    // predating them may not yet contain.
    PoolControlContext::instance()->config.toJson(PoolControlContext::instance()->config.switchConfigRaw, 900);
    LOGN(PoolControlContext::instance()->config.switchConfigRaw);

    networking.setup(eeprom.macAddress);
    temperatureSensors.init();
    adcController.init();
    waterpumpController.init();
    phController.init();
    chlorineController.init();

    webserver.setup(&eeprom);

    snprintf(myId, 9, "%02x-%02x-%02x", eeprom.macAddress[3], eeprom.macAddress[4], eeprom.macAddress[5]);
    pinMode(CONTROLLINO_D23, OUTPUT);    // heartbeat blinking
    pinMode(CONTROLLINO_D17, OUTPUT);    // for reset
    digitalWrite(CONTROLLINO_D17, HIGH); // for reset

    // Initialize system start time for uptime tracking
    systemStartTime = millis();
}

void loop()
{
    // Update uptime in seconds
    PoolControlContext::instance()->data.uptimeSeconds = (millis() - systemStartTime) / 1000;

    // LOGN(F("---"));
    // LOGN(freeRam());
    // LOGN(F("---"));
    realTimeClock.run();
    networking.run();
    webserver.run();
    temperatureSensors.run();
    adcController.run();
    waterpumpController.run();
    phController.run();
    chlorineController.run();

    digitalWrite(CONTROLLINO_D23, !digitalRead(CONTROLLINO_D23));
    if (webserver.doReboot)
    {
        digitalWrite(CONTROLLINO_D17, LOW);
    }
    delay(500);
}
