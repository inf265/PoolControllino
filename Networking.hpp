#pragma once

#include <Ethernet.h>
#include <EthernetUdp.h>
#include "MdnsResponder.hpp"
#include "Identification.hpp"
#include "PoolControlContext.hpp"
#include "version.h"
#include "RealTimeClock.hpp"

namespace HC
{

    // Minimal, zero-heap mDNS responder (replaces ArduinoMDNS which caused
    // memory problems). Answers A queries for "<mdnsName>.local".
    MdnsResponder mdns;

    EthernetUDP Udp;
    IPAddress multicastAddress(239, 255, 0, 1); // Local network multicast address
    uint16_t multicastPort = 13000; // Same port as remote server

    char *IPAddress2String(IPAddress address, char *result)
    {
        int len = sprintf(result, "%d", address[0]);
        result[len] = '.';
        ++len;
        len += sprintf(result + len, "%d", address[1]);
        result[len] = '.';
        ++len;
        len += sprintf(result + len, "%d", address[2]);
        result[len] = '.';
        ++len;
        len += sprintf(result + len, "%d", address[3]);
        return result;
    }

    class Networking
    {
    public:
        void setup(uint8_t *mac)
        {
            // Static IP comes from the configuration (editable via <IP>/config),
            // defaulting to 192.168.42.220 (see Configuration::ipAddress).
            IPAddress ipa;
            ipa.fromString(PoolControlContext::instance()->config.ipAddress);
            Ethernet.begin(mac, ipa);
            LOG(F("IP static: "));
            LOGN(Ethernet.localIP());
            memset(PoolControlContext::instance()->data.clientIP, 0, 16);
            IPAddress2String(Ethernet.localIP(), PoolControlContext::instance()->data.clientIP);

            if (mdns.begin(mdnsName))
            {
                LOGN(F("MDNS initialized (minimal responder)"));
            }

            Udp.begin(13001);
        }

        void run()
        {
            mdns.run();
            if (Ethernet.maintain())
            {
                LOG(F("DHCP maintain failed."));
                memset(PoolControlContext::instance()->data.clientIP, 0, 16);
            }
            else
            {
                if (PoolControlContext::instance()->data.clientIP[0] == 0)
                {
                    IPAddress2String(Ethernet.localIP(), PoolControlContext::instance()->data.clientIP);
                }
            }
            if ((millis() - lastTime) > PoolControlContext::instance()->config.updateTime)
            {
                char data[768]{0};
                LOGN(getSensorReadings(data, 768));
                
                // Send version to remote server
                if (!versionSent && strlen(PoolControlContext::instance()->data.clientIP) != 0)
                {
                    Udp.beginPacket("89.163.135.79", 13000);
                    LOGN(gitVersion);
                    Udp.write(gitVersion);
                    Udp.endPacket();
                    Udp.flush();
                    versionSent = true;
                }
                
                // Send sensor data to remote server
                if (!Udp.beginPacket("89.163.135.79", 13000))
                    LOGN(F("remote begin failure"));
                if (!Udp.write((uint8_t *)data, strlen(data)))
                    LOGN(F("remote write failure"));
                Udp.endPacket();
                Udp.flush();
                
                // Also send sensor data to local multicast address
                if (!Udp.beginPacket(multicastAddress, multicastPort))
                    LOGN(F("multicast begin failure"));
                if (!Udp.write((uint8_t *)data, strlen(data)))
                    LOGN(F("multicast write failure"));
                Udp.endPacket();
                Udp.flush();
                
                lastTime = millis();
            }
        }

        static char *getSensorReadings(char *buffer, size_t size)
        {
            // Build JSON string manually to avoid heap allocations (more memory-efficient for embedded systems)
            auto *ctx = PoolControlContext::instance();
            char date[20]{0};
            RealTimeClock::getFullDateTimeString(ctx->data.date, date);
            
            // Calculate remaining time until auto mode for each pump
            unsigned long waterRem = 0;
            if (ctx->data.waterPumpManualOverride && ctx->data.waterPumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.waterPumpManualOverrideSince.unixtime());
                if (elapsedSeconds < ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    waterRem = ctx->config.pumpManualOverrideTimeoutSeconds - elapsedSeconds;
                }
            }
            
            unsigned long phRem = 0;
            if (ctx->data.phPumpManualOverride && ctx->data.phPumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.phPumpManualOverrideSince.unixtime());
                if (elapsedSeconds < ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    phRem = ctx->config.pumpManualOverrideTimeoutSeconds - elapsedSeconds;
                }
            }
            
            unsigned long redoxRem = 0;
            if (ctx->data.chlorinePumpManualOverride && ctx->data.chlorinePumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.chlorinePumpManualOverrideSince.unixtime());
                if (elapsedSeconds < ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    redoxRem = ctx->config.pumpManualOverrideTimeoutSeconds - elapsedSeconds;
                }
            }
            
            // Build JSON string manually using dtostrf for floats (AVR doesn't support %f in snprintf)
            char tempFloat[10];
            int len = 0;
            
            // Start JSON object
            len += snprintf(buffer + len, size - len, "{\"date\":\"%s\",", date);
            
            // Float values using dtostrf
            dtostrf(ctx->data.waterTemperature, 0, 2, tempFloat);
            len += snprintf(buffer + len, size - len, "\"temperature\":%s,", tempFloat);
            
            dtostrf(ctx->data.housingTemperature, 0, 2, tempFloat);
            len += snprintf(buffer + len, size - len, "\"housingtemperature\":%s,", tempFloat);
            
            dtostrf(ctx->data.phValue, 0, 2, tempFloat);
            len += snprintf(buffer + len, size - len, "\"ph\":%s,", tempFloat);
            
            dtostrf(ctx->data.phValueMedian, 0, 2, tempFloat);
            len += snprintf(buffer + len, size - len, "\"phmedian\":%s,", tempFloat);
            
            dtostrf(ctx->data.redoxValue, 0, 2, tempFloat);
            len += snprintf(buffer + len, size - len, "\"redox\":%s,", tempFloat);
            
            dtostrf(ctx->data.redoxValueMedian, 0, 2, tempFloat);
            len += snprintf(buffer + len, size - len, "\"redoxmedian\":%s,", tempFloat);
            
            // Integer and boolean values
            len += snprintf(buffer + len, size - len,
                "\"ph-pomp\":%d,\"redox-pomp\":%d,\"water-pomp\":%d,"
                "\"ph-man\":%d,\"redox-man\":%d,\"water-man\":%d,"
                "\"ph-man-rem\":%lu,\"redox-man-rem\":%lu,\"water-man-rem\":%lu,"
                "\"waterflowswitch\":%d,\"powersupply\":%d,"
                "\"clientip\":\"%s\",\"error\":%d,\"errortext\":\"%s\","
                "\"errortimestamp\":\"%s\",\"warning\":%d,\"warningtext\":\"%s\","
                "\"warningtimestamp\":\"%s\",\"phadcvalue\":%lu,\"redoxadcvalue\":%lu,"
                "\"waterflowswitchadcvalue\":%lu,\"powersupplyadcvalue\":%lu,\"uptime\":%lu}",
                ctx->data.phPumpState ? 1 : 0,
                ctx->data.redoxPumpState ? 1 : 0,
                ctx->data.waterPumpState ? 1 : 0,
                ctx->data.phPumpManualOverride ? 1 : 0,
                ctx->data.chlorinePumpManualOverride ? 1 : 0,
                ctx->data.waterPumpManualOverride ? 1 : 0,
                phRem,
                redoxRem,
                waterRem,
                ctx->data.waterFlowSwitch ? 1 : 0,
                ctx->data.powerSupply ? 1 : 0,
                ctx->data.clientIP,
                ctx->data.error ? 1 : 0,
                ctx->data.errorText,
                ctx->data.errorTimestamp,
                ctx->data.warning ? 1 : 0,
                ctx->data.warningText,
                ctx->data.warningTimestamp,
                ctx->data.phAdcValue,
                ctx->data.redoxAdcValue,
                ctx->data.waterflowSwitchAdcValue,
                ctx->data.powerSupplyAdcValue,
                ctx->data.uptimeSeconds
            );
            
            // Ensure null termination
            if (len < 0 || (size_t)len >= size)
            {
                buffer[size - 1] = '\0';
            }
            else
            {
                buffer[len] = '\0';
            }
            
            return buffer;
        }

    private:
        unsigned long lastTime{0};
        bool versionSent{false};
    };
}