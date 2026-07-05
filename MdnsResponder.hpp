#pragma once

#include <Ethernet.h>
#include <EthernetUdp.h>
#include <string.h>

namespace HC
{
    // Minimal mDNS responder for AVR.
    // - Answers A queries for "<hostname>.local"
    // - Sends a gratuitous A response every ~30s as an announce
    // - Zero heap allocations, single static packet buffer (~190 B)
    // - Drops packets larger than the buffer silently — clients will retry
    //
    // Limitations vs full mDNS:
    // - No service records (no _http._tcp browsing). Direct hostname resolution still works.
    // - No AAAA / IPv6
    // - Only matches the FIRST question in a packet, uncompressed, at offset 12.
    //   This covers ~95% of real-world mDNS queries (macOS, Avahi, iOS, Windows resolver).
    class MdnsResponder
    {
    public:
        static const uint16_t MDNS_PORT = 5353;
        static const uint16_t TTL = 120;
        static const unsigned long ANNOUNCE_INTERVAL_MS = 30000UL;
        static const uint8_t BUF_SIZE = 192;
        static const uint8_t MAX_HOSTNAME = 24;

        // hostname is the bare name without ".local" suffix, e.g. "poolcontrollino".
        bool begin(const char *hostname)
        {
            uint8_t hlen = strlen(hostname);
            if (hlen == 0 || hlen > MAX_HOSTNAME) return false;

            // Encode as DNS labels: [hlen]<hostname>[5]"local"[0]
            encodedName[0] = hlen;
            memcpy(encodedName + 1, hostname, hlen);
            encodedName[1 + hlen] = 5;
            memcpy(encodedName + 2 + hlen, "local", 5);
            encodedName[7 + hlen] = 0;
            encodedNameLen = 8 + hlen;

            if (!udp.beginMulticast(IPAddress(224, 0, 0, 251), MDNS_PORT))
                return false;

            sendAnnounce();
            lastAnnounce = millis();
            return true;
        }

        // Call from loop() — drains one pending packet and sends periodic announce.
        // Bounded work per call: ~one parsePacket() + ~one read() + maybe one beginPacket()/write()/endPacket().
        void run()
        {
            unsigned long now = millis();
            if (now - lastAnnounce > ANNOUNCE_INTERVAL_MS)
            {
                sendAnnounce();
                lastAnnounce = now;
            }

            int packetSize = udp.parsePacket();
            if (packetSize <= 0) return;

            if (packetSize > BUF_SIZE)
            {
                // Oversized — drop without buffering
                while (udp.available()) udp.read();
                return;
            }

            int len = udp.read(buf, BUF_SIZE);
            // Need at least DNS header (12) + our name + qtype(2) + qclass(2)
            if (len < 12 + (int)encodedNameLen + 4) return;

            uint16_t flags   = ((uint16_t)buf[2] << 8) | buf[3];
            uint16_t qdCount = ((uint16_t)buf[4] << 8) | buf[5];

            // Queries only (QR=0), at least one question
            if (flags & 0x8000) return;
            if (qdCount == 0) return;

            // Match first question name against ours. Mainstream resolvers don't compress
            // the first question, so we look at the raw bytes starting at offset 12.
            if (!nameMatches(buf, 12, len)) return;

            uint16_t typeOffset = 12 + encodedNameLen;
            uint16_t qtype     = ((uint16_t)buf[typeOffset]     << 8) | buf[typeOffset + 1];
            uint16_t qclassRaw = ((uint16_t)buf[typeOffset + 2] << 8) | buf[typeOffset + 3];
            bool unicastResponse = (qclassRaw & 0x8000) != 0;

            // Respond only to A (1) or ANY (255)
            if (qtype != 1 && qtype != 255) return;

            if (unicastResponse)
                sendAResponse(udp.remoteIP(), udp.remotePort());
            else
                sendAnnounce();
        }

    private:
        EthernetUDP udp;
        uint8_t encodedName[8 + MAX_HOSTNAME + 1];
        uint8_t encodedNameLen{0};
        uint8_t buf[BUF_SIZE];
        unsigned long lastAnnounce{0};

        static uint8_t toLower(uint8_t c)
        {
            return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c;
        }

        bool nameMatches(const uint8_t *p, uint16_t offset, int len)
        {
            if (offset + encodedNameLen > (uint16_t)len) return false;
            for (uint16_t i = 0; i < encodedNameLen; ++i)
            {
                if (toLower(p[offset + i]) != toLower(encodedName[i]))
                    return false;
            }
            return true;
        }

        void sendAResponse(IPAddress dest, uint16_t destPort)
        {
            IPAddress myIp = Ethernet.localIP();

            // Reuse buf for outgoing response (saves SRAM).
            uint16_t pos = 0;
            // Header: id=0, flags=0x8400 (QR=1, AA=1), qd=0, an=1, ns=0, ar=0
            buf[pos++] = 0;    buf[pos++] = 0;
            buf[pos++] = 0x84; buf[pos++] = 0;
            buf[pos++] = 0;    buf[pos++] = 0;
            buf[pos++] = 0;    buf[pos++] = 1;
            buf[pos++] = 0;    buf[pos++] = 0;
            buf[pos++] = 0;    buf[pos++] = 0;
            // Answer name (uncompressed)
            memcpy(buf + pos, encodedName, encodedNameLen);
            pos += encodedNameLen;
            // Type A
            buf[pos++] = 0; buf[pos++] = 1;
            // Class IN with cache-flush bit
            buf[pos++] = 0x80; buf[pos++] = 1;
            // TTL (big-endian)
            buf[pos++] = 0; buf[pos++] = 0;
            buf[pos++] = 0; buf[pos++] = TTL;
            // RDLENGTH = 4
            buf[pos++] = 0; buf[pos++] = 4;
            // RDATA: A record IP
            buf[pos++] = myIp[0]; buf[pos++] = myIp[1];
            buf[pos++] = myIp[2]; buf[pos++] = myIp[3];

            udp.beginPacket(dest, destPort);
            udp.write(buf, pos);
            udp.endPacket();
        }

        void sendAnnounce()
        {
            sendAResponse(IPAddress(224, 0, 0, 251), MDNS_PORT);
        }
    };
}
