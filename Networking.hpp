#pragma once

#include <Ethernet.h>
#include <EthernetUdp.h>
#include <ArduinoMDNS.h>
#include "Identification.hpp"
#include "PoolControlContext.hpp"
#include "version.h"
#include "RealTimeClock.hpp"

namespace HC
{

    EthernetUDP mDNSUdp;
    MDNS mdns(mDNSUdp);

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
            // Start up networking
            String ipaddress = "192.168.100.240";
            IPAddress ipa;
            ipa.fromString(ipaddress);
            Ethernet.begin(mac, ipa);
            LOG(F("IP static: "));
            LOGN(Ethernet.localIP());
            memset(PoolControlContext::instance()->data.clientIP, 0, 16);
            IPAddress2String(Ethernet.localIP(), PoolControlContext::instance()->data.clientIP);

            if (mdns.begin(Ethernet.localIP(), mdnsName))
            {
                LOGN(F("MDNS initialized"));
            }

            mdns.addServiceRecord("Pool Controllino mDNS Webserver._http",
                                  80,
                                  MDNSServiceTCP);
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
                char data[1024]{0};
                LOGN(getSensorReadings(data, 1024));
                
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
            JsonDocument readings;
            char date[20]{0};
            RealTimeClock::getFullDateTimeString(PoolControlContext::instance()->data.date, date);
            readings["date"] = String(date);
            readings["temperature"] = String(PoolControlContext::instance()->data.waterTemperature);
            readings["housingtemperature"] = String(PoolControlContext::instance()->data.housingTemperature);
            readings["ph"] = String(PoolControlContext::instance()->data.phValue);
            readings["phmedian"] = String(PoolControlContext::instance()->data.phValueMedian);
            readings["redox"] = String(PoolControlContext::instance()->data.redoxValue);
            readings["redoxmedian"] = String(PoolControlContext::instance()->data.redoxValueMedian);
            readings["ph-pomp"] = String(PoolControlContext::instance()->data.phPumpState);
            readings["redox-pomp"] = String(PoolControlContext::instance()->data.redoxPumpState);
            readings["water-pomp"] = String(PoolControlContext::instance()->data.waterPumpState);
            readings["ph-man"] = String(PoolControlContext::instance()->data.phPumpManualOverride ? 1 : 0);
            readings["redox-man"] = String(PoolControlContext::instance()->data.chlorinePumpManualOverride ? 1 : 0);
            readings["water-man"] = String(PoolControlContext::instance()->data.waterPumpManualOverride ? 1 : 0);
            
            // Calculate remaining time until auto mode for each pump
            unsigned long remainingSeconds = 0;
            auto *ctx = PoolControlContext::instance();
            
            // Water pump remaining time
            if (ctx->data.waterPumpManualOverride && ctx->data.waterPumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.waterPumpManualOverrideSince.unixtime());
                if (elapsedSeconds < ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    remainingSeconds = ctx->config.pumpManualOverrideTimeoutSeconds - elapsedSeconds;
                }
            }
            readings["water-man-rem"] = String(remainingSeconds);
            
            // pH pump remaining time
            remainingSeconds = 0;
            if (ctx->data.phPumpManualOverride && ctx->data.phPumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.phPumpManualOverrideSince.unixtime());
                if (elapsedSeconds < ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    remainingSeconds = ctx->config.pumpManualOverrideTimeoutSeconds - elapsedSeconds;
                }
            }
            readings["ph-man-rem"] = String(remainingSeconds);
            
            // Chlorine pump remaining time
            remainingSeconds = 0;
            if (ctx->data.chlorinePumpManualOverride && ctx->data.chlorinePumpManualOverrideSince.year() != 0)
            {
                unsigned long elapsedSeconds = (ctx->data.date.unixtime() - ctx->data.chlorinePumpManualOverrideSince.unixtime());
                if (elapsedSeconds < ctx->config.pumpManualOverrideTimeoutSeconds)
                {
                    remainingSeconds = ctx->config.pumpManualOverrideTimeoutSeconds - elapsedSeconds;
                }
            }
            readings["redox-man-rem"] = String(remainingSeconds);
            
            readings["waterflowswitch"] = String(PoolControlContext::instance()->data.waterFlowSwitch);
            readings["powersupply"] = String(PoolControlContext::instance()->data.powerSupply);
            readings["clientip"] = String(PoolControlContext::instance()->data.clientIP);
            readings["error"] = String(PoolControlContext::instance()->data.error);
            readings["errortext"] = PoolControlContext::instance()->data.errorText;
            readings["errortimestamp"] = String(PoolControlContext::instance()->data.errorTimestamp);
            readings["warning"] = String(PoolControlContext::instance()->data.warning);
            readings["warningtext"] = PoolControlContext::instance()->data.warningText;
            readings["warningtimestamp"] = String(PoolControlContext::instance()->data.warningTimestamp);
            readings["phadcvalue"] = String(PoolControlContext::instance()->data.phAdcValue);
            readings["redoxadcvalue"] = String(PoolControlContext::instance()->data.redoxAdcValue);
            readings["waterflowswitchadcvalue"] = String(PoolControlContext::instance()->data.waterflowSwitchAdcValue);
            readings["powersupplyadcvalue"] = String(PoolControlContext::instance()->data.powerSupplyAdcValue);
            readings["uptime"] = String(PoolControlContext::instance()->data.uptimeSeconds);

            // String jsonString;
            serializeJson(readings, buffer, size);
            return buffer;
        }

    private:
        unsigned long lastTime{0};
        bool versionSent{false};
    };
}