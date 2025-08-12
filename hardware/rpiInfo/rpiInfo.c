// SPDX-License-Identifier: MIT
//
// rpiInfo.c — AlmaLinux/RHEL optimized
//
// Behavior requested:
//   - If IP display is ENABLED: return "hostname: ipv4" (hostname as-is).
//   - If IP display is DISABLED: return HOSTNAME in UPPERCASE (replacing prior CUSTOM_DISPLAY).
//
// Other improvements kept:
//   - Custom NIC support (e.g., "end0") via CUSTOM_IFNAME.
//   - Robust IPv4 lookup using getifaddrs().
//   - Disk usage via statvfs("/") (works with /dev/root, NVMe, LVM, mmcblk).
//   - SD usage function returns TOTAL MB and USED MB (name freesize is historical).
//   - Temperature and CPU load implementations suitable for the OLED UI.

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

/* ---------------- Fallbacks (in case header wasn’t updated) ---------------- */

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

/* ---------------- Helpers ---------------- */

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

static char* lookup_ipv4_for_iface(const char* ifname)
{
    struct ifaddrs *ifaddr = NULL, *ifa = NULL;
    char buf[INET_ADDRSTRLEN];

    if (!ifname || getifaddrs(&ifaddr) == -1)
        return NULL;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, ifname) != 0) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (!inet_ntop(AF_INET, &(sin->sin_addr), buf, sizeof(buf))) continue;

        freeifaddrs(ifaddr);
        return strdup(buf);
    }

    freeifaddrs(ifaddr);
    return NULL;
}

static int read_first_line(const char* path, char* out, size_t outlen)
{
    FILE* f = fopen(path, "r");
    if (!f) return -errno;
    int ok = (fgets(out, (int)outlen, f) != NULL) ? 0 : -EIO;
    fclose(f);
    if (ok == 0) {
        size_t n = strlen(out);
        while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
    }
    return ok;
}

/* Uppercase a string in-place (ASCII) */
static void upcase_ascii(char *s)
{
    if (!s) return;
    for (char *p = s; *p; ++p) {
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    }
}

/* ---------------- Public API ---------------- */

char* get_ip_address(void)
{
    /* Always fetch hostname */
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "unknown", sizeof(hostname));
    }
    hostname[sizeof(hostname) - 1] = '\0';

#if IP_SWITCH == IP_DISPLAY_CLOSE
    /* IP is disabled: show HOSTNAME ONLY, in CAPS */
    char *out = strdup(hostname);
    if (!out) return NULL;
    upcase_ascii(out);
    return out;
#else
    /* IP is enabled: return "hostname: ipv4" (hostname as-is) */
    const char* ifname = pick_iface();
    char* ip = lookup_ipv4_for_iface(ifname);

    if (ip) {
        size_t needed = strlen(hostname) + 2 /*": "*/ + strlen(ip) + 1;
        char *result = (char*)malloc(needed);
        if (result) {
            snprintf(result, needed, "%s: %s", hostname, ip);
        }
        free(ip);
        return result ? result : strdup(hostname);
    } else {
        /* No IP found; still return hostname to avoid blank line */
        return strdup(hostname);
    }
#endif
}

char* get_ip_address_new(void)
{
    /* Mirror get_ip_address() behavior for consistency */
    return get_ip_address();
}

/* SD memory: report TOTAL MB and USED MB (OLED expects used/total for percentage) */
void get_sd_memory(uint32_t *MemSize, uint32_t *freesize)
{
    if (!MemSize || !freesize) return;

    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0)
    {
        unsigned long long block    = (unsigned long long)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
        unsigned long long total    = (unsigned long long)vfs.f_blocks * block;
        unsigned long long free_all = (unsigned long long)vfs.f_bfree  * block; /* includes root-reserved */
        unsigned long long used     = total - free_all;

        *MemSize  = (uint32_t)(total / (1024ULL * 1024ULL)); /* MB total */
        *freesize = (uint32_t)(used  / (1024ULL * 1024ULL)); /* MB used (name historical) */
    }
    else
    {
        *MemSize  = 0;
        *freesize = 0;
    }
}

/* CPU memory in MB (total, available) */
void get_cpu_memory(float *Totalram, float *freeram)
{
    if (!Totalram || !freeram) return;

    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) { *Totalram = 0.f; *freeram = 0.f; return; }

    long memTotalKB = 0, memAvailableKB = 0;
    char key[64], unit[16];
    long val = 0;

    while (fscanf(f, "%63s %ld %15s\n", key, &val, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0)        memTotalKB     = val;
        else if (strcmp(key, "MemAvailable:") == 0) memAvailableKB = val;
        if (memTotalKB && memAvailableKB) break;
    }
    fclose(f);

    *Totalram = (float)memTotalKB     / 1024.0f;
    *freeram  = (float)memAvailableKB / 1024.0f;
}

/* Temperature in 0..255 (°C by default; °F if configured) */
uint8_t get_temperature(void)
{
    char buf[64] = {0};
    long milli = 0;

    if (read_first_line("/sys/class/thermal/thermal_zone0/temp", buf, sizeof(buf)) == 0) {
        milli = strtol(buf, NULL, 10);
    } else if (read_first_line("/sys/devices/virtual/thermal/thermal_zone0/temp", buf, sizeof(buf)) == 0) {
        milli = strtol(buf, NULL, 10);
    }

    double t = milli / 1000.0; /* °C */
#if TEMPERATURE_TYPE == FAHRENHEIT
    t = (t * 9.0 / 5.0) + 32.0;
#endif
    if (t < 0.0) t = 0.0;
    if (t > 255.0) t = 255.0;
    return (uint8_t)(t + 0.5);
}

/* CPU load “bucket” 0..255 based on 1-min load normalized by core count */
uint8_t get_cpu_message(void)
{
    double la1 = 0.0;
    FILE* f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf", &la1) != 1) la1 = 0.0;
        fclose(f);
    }
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;

    double ratio = la1 / (double)cores;    /* 1.0 == one core fully used */
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 4.0) ratio = 4.0;

    int bucket = (int)(ratio * (255.0 / 4.0) + 0.5);
    if (bucket < 0)   bucket = 0;
    if (bucket > 255) bucket = 255;
    return (uint8_t)bucket;
}

/* Root FS usage, in GB (rounded):
 *  - diskMemSize: total GB
 *  - useMemSize : used  GB
 * Using bavail (space for non-root) to match `df`’s notion of “available”. */
uint8_t get_hard_disk_memory(uint16_t *diskMemSize, uint16_t *useMemSize)
{
    if (!diskMemSize || !useMemSize) return 1;

    struct statvfs vfs;
    if (statvfs("/", &vfs) != 0) {
        *diskMemSize = 0;
        *useMemSize  = 0;
        return 1;
    }

    unsigned long long block = (unsigned long long)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
    unsigned long long
