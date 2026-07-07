#pragma once

#include <Arduino.h>
#include <Controllino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

#include "PoolControlContext.hpp"
#include "TimeOfDay.hpp"
#include "Logging.hpp"

class RealTimeClock
{
public:
    void init()
    {
        Controllino_RTC_init(0);
        // Controllino_SetTimeDateStrings(__DATE__, __TIME__);
    };

    void run()
    {
        PoolControlContext::instance()->data.date = getNow();
        maybeSyncNtp();
    }

    DateTime getNow()
    {
        return DateTime(Controllino_GetYear(), Controllino_GetMonth(), Controllino_GetDay(),
                        Controllino_GetHour(), Controllino_GetMinute(), Controllino_GetSecond());
    }

    static char *getFullDateTimeString(DateTime &dateTime, char *result)
    {
        memset(result, 0, 20);
        memcpy(result, "DD.MM.YYYY hh:mm:ss", 20);
        return dateTime.toString(result);
    }

private:
    static const unsigned long RETRY_INTERVAL_MS = 60000UL; // retry every 60 s until first sync
    static const uint32_t NTP_UNIX_DELTA = 2208988800UL;    // seconds between 1900 and 1970 epochs
    unsigned long lastAttemptMs{0};
    uint8_t lastSyncDay{0}; // RTC day-of-month of the last successful sync (0 = none yet)
    bool everSynced{false};

    // Decide whether to run an NTP sync now. The clock is only ever adjusted
    // while ALL pumps are off, so a time jump can never disturb an active
    // injection cycle or the water-pump runtime tracking.
    //   - Before the first successful sync: retry (every 60 s) as soon as the
    //     pumps are off, so a fresh/rebooted device gets the correct time.
    //   - After that: sync once per day, during the configured night hour.
    void maybeSyncNtp()
    {
        auto *ctx = PoolControlContext::instance();

        if (ctx->data.waterPumpState || ctx->data.phPumpState || ctx->data.redoxPumpState)
            return; // a pump is running -> never touch the clock

        bool due;
        if (!everSynced)
        {
            due = (lastAttemptMs == 0) || ((millis() - lastAttemptMs) >= RETRY_INTERVAL_MS);
        }
        else
        {
            due = (ctx->data.date.hour() == ctx->config.ntpSyncHour) &&
                  (ctx->data.date.day() != lastSyncDay);
        }
        if (!due) return;

        lastAttemptMs = millis();
        if (syncNtp())
        {
            everSynced = true;
            ctx->data.date = getNow(); // refresh with the freshly-set time
            lastSyncDay = ctx->data.date.day();
        }
    }

    // True if the given UTC time falls within Central European Summer Time.
    // EU rule: CEST (UTC+2) from the last Sunday of March 01:00 UTC to the
    // last Sunday of October 01:00 UTC; CET (UTC+1) otherwise.
    static bool isBerlinDst(const DateTime &utc)
    {
        uint8_t month = utc.month();
        if (month < 3 || month > 10) return false; // Jan, Feb, Nov, Dec -> CET
        if (month > 3 && month < 10) return true;   // Apr..Sep           -> CEST

        // March or October: depends on the last Sunday.
        // day-of-week of the 31st (both months have 31 days); 0 = Sunday.
        DateTime last(utc.year(), month, 31, 0, 0, 0);
        int lastSunday = 31 - last.dayOfTheWeek();
        uint8_t day = utc.day();
        uint8_t hour = utc.hour();
        if (month == 3)
            return (day > lastSunday) || (day == (uint8_t)lastSunday && hour >= 1);
        else // October
            return (day < lastSunday) || (day == (uint8_t)lastSunday && hour < 1);
    }

    // Query the configured NTP server (by IP) and, on success, set the RTC to
    // Berlin local time. Blocks up to ~1.5 s waiting for the reply. Returns
    // true on success. Zero heap: one 48-byte stack buffer, ephemeral socket.
    bool syncNtp()
    {
        IPAddress ntpIp;
        if (!ntpIp.fromString(PoolControlContext::instance()->config.ntpServer))
        {
            LOGN(F("NTP: invalid server IP"));
            return false;
        }

        EthernetUDP udp;
        if (!udp.begin(8888))
        {
            return false;
        }

        uint8_t pkt[48];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 0xE3; // LI=3 (unsynced), Version=4, Mode=3 (client)

        udp.beginPacket(ntpIp, 123);
        udp.write(pkt, sizeof(pkt));
        udp.endPacket();

        unsigned long start = millis();
        int sz = 0;
        while ((millis() - start) < 1500UL)
        {
            sz = udp.parsePacket();
            if (sz >= 48) break;
        }
        if (sz < 48)
        {
            udp.stop();
            LOGN(F("NTP: no reply"));
            return false;
        }
        udp.read(pkt, sizeof(pkt));
        udp.stop();

        // Transmit timestamp (seconds since 1900) at bytes 40..43, big-endian.
        uint32_t secs1900 = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                            ((uint32_t)pkt[42] << 8) | (uint32_t)pkt[43];
        if (secs1900 <= NTP_UNIX_DELTA)
        {
            LOGN(F("NTP: bad timestamp"));
            return false; // 0 / kiss-o'-death or pre-1970 -> reject
        }

        uint32_t utc = secs1900 - NTP_UNIX_DELTA; // Unix epoch (UTC)
        DateTime utcDt(utc);
        uint32_t localEpoch = utc + (isBerlinDst(utcDt) ? 7200UL : 3600UL);
        DateTime lt(localEpoch);

        Controllino_SetTimeDate(lt.day(), lt.dayOfTheWeek(), lt.month(),
                                (uint8_t)(lt.year() - 2000U), lt.hour(), lt.minute(), lt.second());

        LOG(F("NTP sync OK, local time set: "));
        LOGN(lt.day());
        return true;
    }
};
