// SPDX-License-Identifier: MIT
//
// rpiInfo.c — rewritten for AlmaLinux 10 / Pi-Rack Pro
//
// Changes in this version:
//   - Network interface selection supports a CUSTOM interface name (e.g., "end0").
//   - IP address lookup uses getifaddrs() (no shell calls).
//   - Disk usage (get_hard_disk_memory) uses statvfs("/") and no longer greps /dev/sda.
//   - Functions retain the same signatures as declared in rpiInfo.h.
//
// Notes:
//   - This file is designed to work with the original rpiInfo.h from UCTRONICS.
//   - If rpiInfo.h has not been updated with CUSTOM_ADDRESS/CUSTOM_IFNAME, we provide
//     fallback defaults here so the binary still builds and uses "end0" by default.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "rpiInfo.h"

/* ------------------------------ Fallbacks for header macros ------------------------------ */

#ifndef ETH0_ADDRESS
#define ETH0_ADDRESS 0
#endif

#ifndef WLAN0_ADDRESS
#define WLAN0_ADDRESS 1
#endif

#ifndef CUSTOM_ADDRESS
#define CUSTOM_ADDRESS 2
#endif

#ifndef CUSTOM_IFNAME
#define CUSTOM_IFNAME "end0"
#endif

#ifndef IPADDRESS_TYPE
#define IPADDRESS_TYPE CUSTOM_ADDRESS
#endif

#ifndef IP_DISPLAY_OPEN
#define IP_DISPLAY_OPEN 0
#endif

#ifndef IP_DISPLAY_CLOSE
#define IP_DISPLAY_CLOSE 1
#endif

#ifndef IP_SWITCH
#define IP_SWITCH IP_DISPLAY_OPEN
#endif

#ifndef CUSTOM_DISPLAY
#define CUSTOM_DISPLAY "UCTRONICS"
#endif

#ifndef CELSIUS
#define CELSIUS 0
#endif

#ifndef FAHRENHEIT
#define FAHRENHEIT 1
#endif

#ifndef TEMPERATURE_TYPE
#define TEMPERATURE_TYPE CELSIUS
#endif

/* ------------------------------ Internal helpers ------------------------------ */

static const char* pick_iface(void)
{
#if IPADDRESS_TYPE == ETH0_ADDRESS
    return "eth0";
#elif IPADDRESS_TYPE == WLAN0_ADDRESS
    return "wlan0";
#elif IPADDRESS_TYPE == CUSTOM_ADDRESS
    return CUSTOM_IFNAME;
#else
    return "eth0";
#endif
}

/* Returns a newly-allocated string containing the primary IPv4 address of `ifname`.
 * Caller must free(). Returns NULL on error.
 * If IP display is disabled (IP_SWITCH == IP_DISPLAY_CLOSE), this will return
 * strdup(CUSTOM_DISPLAY) when no IP is found so the caller can still show text. */
static char* lookup_ipv4_for_iface(const char* ifname)
{
    struct ifaddrs *ifaddr = NULL, *ifa = NULL;
    char buf[INET_ADDRSTRLEN];

    if (!ifname || getifaddrs(&ifaddr) == -1)
        return NULL;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_name || !ifa->ifa_addr)
            continue;

        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        if (strcmp(ifa->ifa_name, ifname) != 0)
            continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (!inet_ntop(AF_INET, &(sin->sin_addr), buf, sizeof(buf)))
            continue;

        freeifaddrs(ifaddr);
        return strdup(buf);
    }

    freeifaddrs(ifaddr);

#if IP_SWITCH == IP_DISPLAY_CLOSE
    return strdup(CUSTOM_DISPLAY);
#else
    return NULL;
#endif
}

/* Reads the first line of a file into out (strips newline). Returns 0 on success, -errno on failure. */
static int read_first_line(const char* path, char* out, size_t outlen)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return -errno;

    int ok = (fgets(out, (int)outlen, f) != NULL) ? 0 : -EIO;
    fclose(f);

    if (ok == 0)
    {
        size_t n = strlen(out);
        while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        {
            out[--n] = '\0';
        }
    }
    return ok;
}

/* ------------------------------ Public API (as per rpiInfo.h) ------------------------------ */

char* get_ip_address(void)
{
    const char* ifname = pick_iface();
    char* ip = lookup_ipv4_for_iface(ifname);

#if IP_SWITCH == IP_DISPLAY_CLOSE
    if (!ip)
        return strdup(CUSTOM_DISPLAY);
#endif

    return ip ? ip : strdup("0.0.0.0");
}

char* get_ip_address_new(void)
{
    /* Same semantics as get_ip_address() */
    return get_ip_address();
}

/* SD memory: fill total/free in MB for the root filesystem.
 * The original code used statfs and right-shifted into GB; we keep MB here for finer granularity. */
void get_sd_memory(uint32_t *MemSize, uint32_t *freesize)
{
    if (!MemSize || !freesize)
        return;

    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0)
    {
        unsigned long long block = (unsigned long long)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
        unsigned long long total = (unsigned long long)vfs.f_blocks * block;
        unsigned long long free_all = (unsigned long long)vfs.f_bfree * block;   /* includes root-reserved */
        unsigned long long used = total - free_all;

        /* Report MB to keep percent math consistent (units don’t matter as long as both match) */
        *MemSize  = (uint32_t)(total / (1024ULL * 1024ULL));  /* total MB */
        *freesize = (uint32_t)(used  / (1024ULL * 1024ULL));  /* USED MB (name is historical) */
    }
    else
    {
        *MemSize  = 0;
        *freesize = 0;
    }
}

/* CPU memory: report total/available RAM in MB as floats (from /proc/meminfo). */
void get_cpu_memory(float *Totalram, float *freeram)
{
    if (!Totalram || !freeram)
        return;

    FILE* f = fopen("/proc/meminfo", "r");
    if (!f)
    {
        *Totalram = 0.f;
        *freeram  = 0.f;
        return;
    }

    long memTotalKB = 0, memAvailableKB = 0;
    char key[64];
    long val = 0;
    char unit[16];

    while (fscanf(f, "%63s %ld %15s\n", key, &val, unit) == 3)
    {
        if (strcmp(key, "MemTotal:") == 0)       memTotalKB     = val;
        else if (strcmp(key, "MemAvailable:") == 0) memAvailableKB = val;

        if (memTotalKB && memAvailableKB)
            break;
    }
    fclose(f);

    *Totalram = (float)memTotalKB     / 1024.0f; /* MB */
    *freeram  = (float)memAvailableKB / 1024.0f; /* MB */
}

/* Temperature: read SoC temp in millidegC and convert to uint8_t degrees (C or F),
 * clamped to [0,255] for compact display. */
uint8_t get_temperature(void)
{
    char buf[64] = {0};
    long milli = 0;

    int rc = read_first_line("/sys/class/thermal/thermal_zone0/temp", buf, sizeof(buf));
    if (rc == 0)
    {
        milli = strtol(buf, NULL, 10);
    }
    else
    {
        /* Fallback path on some kernels */
        rc = read_first_line("/sys/devices/virtual/thermal/thermal_zone0/temp", buf, sizeof(buf));
        if (rc == 0)
            milli = strtol(buf, NULL, 10);
    }

    double t = milli / 1000.0; /* °C */
#if TEMPERATURE_TYPE == FAHRENHEIT
    t = (t * 9.0 / 5.0) + 32.0; /* °F */
#endif

    if (t < 0.0)   t = 0.0;
    if (t > 255.0) t = 255.0;

    return (uint8_t)(t + 0.5);
}

/* CPU “message” as a simple load bucket (0..255) based on 1-minute load average normalized by core count. */
uint8_t get_cpu_message(void)
{
    double la1 = 0.0;
    FILE* f = fopen("/proc/loadavg", "r");
    if (f)
    {
        if (fscanf(f, "%lf", &la1) != 1)
            la1 = 0.0;
        fclose(f);
    }

    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;

    double ratio = la1 / (double)cores; /* 1.0 == one core fully utilized */
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 4.0) ratio = 4.0;       /* clamp */

    int bucket = (int)(ratio * (255.0 / 4.0) + 0.5);
    if (bucket < 0)   bucket = 0;
    if (bucket > 255) bucket = 255;

    return (uint8_t)bucket;
}

/* Disk usage for root filesystem:
 *   - diskMemSize: total size in GB (rounded)
 *   - useMemSize : used size  in GB (rounded)
 * Uses statvfs("/") and ignores device names (/dev/root, NVMe, LVM, etc.). */
uint8_t get_hard_disk_memory(uint16_t *diskMemSize, uint16_t *useMemSize)
{
    if (!diskMemSize || !useMemSize)
        return 1;

    struct statvfs vfs;
    if (statvfs("/", &vfs) != 0)
    {
        *diskMemSize = 0;
        *useMemSize  = 0;
        return 1;
    }

    unsigned long long block = (unsigned long long)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
    unsigned long long total = (unsigned long long)vfs.f_blocks * block;
    unsigned long long avail = (unsigned long long)vfs.f_bavail * block; /* space available to non-root */
    unsigned long long used  = total - avail;

    /* Round to GB */
    *diskMemSize = (uint16_t)((total + (1ULL << 29)) >> 30);
    *useMemSize  = (uint16_t)((used  + (1ULL << 29)) >> 30);

    return 0;
}
