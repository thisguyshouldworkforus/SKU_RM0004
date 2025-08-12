/* SPDX-License-Identifier: MIT
 *
 * rpiInfo.c — AlmaLinux/RHEL optimized, C89/C90-safe
 *
 * Behavior:
 *   - If IP display is ENABLED: return "hostname: ipv4" (hostname as-is).
 *   - If IP display is DISABLED: return HOSTNAME in UPPERCASE (replacing prior CUSTOM_DISPLAY).
 *
 * Improvements:
 *   - Custom NIC support (e.g., "end0") via CUSTOM_IFNAME.
 *   - Robust IPv4 lookup using getifaddrs().
 *   - Disk usage via statvfs("/") (works with /dev/root, NVMe, LVM, mmcblk).
 *   - SD usage: TOTAL MB and USED MB (OLED expects used/total for percentage).
 *   - Temperature and CPU load suitable for OLED UI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rpiInfo.h"

/* -------- Fallbacks (in case header wasn’t updated) -------- */
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

/* Returns malloc()'d IPv4 string for ifname, or NULL. */
static char* lookup_ipv4_for_iface(const char* ifname)
{
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;
    char buf[INET_ADDRSTRLEN];
    int rc;

    ifaddr = NULL;
    ifa = NULL;

    if (ifname == NULL)
        return NULL;

    rc = getifaddrs(&ifaddr);
    if (rc == -1 || ifaddr == NULL)
        return NULL;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_name == NULL || ifa->ifa_addr == NULL)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (strcmp(ifa->ifa_name, ifname) != 0)
            continue;

        if (inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
                      buf, sizeof(buf)) == NULL)
            continue;

        freeifaddrs(ifaddr);
        return strdup(buf);
    }

    freeifaddrs(ifaddr);
    return NULL;
}

/* Read first line, strip newline. Returns 0 on success, -errno otherwise. */
static int read_first_line(const char* path, char* out, size_t outlen)
{
    FILE* f;
    int ok;
    size_t n;

    f = fopen(path, "r");
    if (!f)
        return -errno;

    ok = (fgets(out, (int)outlen, f) != NULL) ? 0 : -EIO;
    fclose(f);

    if (ok == 0)
    {
        n = strlen(out);
        while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        {
            out[--n] = '\0';
        }
    }
    return ok;
}

/* Uppercase ASCII in place. */
static void upcase_ascii(char *s)
{
    char *p;
    if (!s) return;
    for (p = s; *p; ++p)
    {
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    }
}

/* ---------------- Public API ---------------- */

char* get_ip_address(void)
{
    char hostname[256];
    int rc;
    const char *ifname;
    char *ip;
    size_t needed;
    char *result;

    rc = gethostname(hostname, sizeof(hostname));
    if (rc != 0)
    {
        strcpy(hostname, "unknown");
    }
    hostname[sizeof(hostname) - 1] = '\0';

#if IP_SWITCH == IP_DISPLAY_CLOSE
    /* IP disabled: show HOSTNAME ONLY, in CAPS */
    result = strdup(hostname);
    if (result != NULL)
        upcase_ascii(result);
    return result;
#else
    /* IP enabled: "hostname: ipv4" (hostname as-is) */
    ifname = pick_iface();
    ip = lookup_ipv4_for_iface(ifname);

    if (ip != NULL)
    {
        needed = strlen(hostname) + 2 /* ": " */ + strlen(ip) + 1;
        result = (char*)malloc(needed);
        if (result != NULL)
        {
            snprintf(result, needed, "%s: %s", hostname, ip);
        }
        free(ip);
        if (result != NULL)
            return result;
    }

    /* No IP found; still return hostname to avoid blank line */
    return strdup(hostname);
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
    struct statvfs vfs;
    unsigned long long block;
    unsigned long long total;
    unsigned long long free_all;
    unsigned long long used;

    if (!MemSize || !freesize)
        return;

    if (statvfs("/", &vfs) == 0)
    {
        block    = (unsigned long long)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
        total    = (unsigned long long)vfs.f_blocks * block;
        free_all = (unsigned long long)vfs.f_bfree  * block;
        used     = total - free_all;

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
    FILE* f;
    long memTotalKB;
    long memAvailableKB;
    char key[64];
    char unit[16];
    long val;

    if (!Totalram || !freeram)
        return;

    f = fopen("/proc/meminfo", "r");
    if (!f)
    {
        *Totalram = 0.f;
        *freeram  = 0.f;
        return;
    }

    memTotalKB = 0;
    memAvailableKB = 0;
    val = 0;

    while (fscanf(f, "%63s %ld %15s\n", key, &val, unit) == 3)
    {
        if (strcmp(key, "MemTotal:") == 0)        memTotalKB     = val;
        else if (strcmp(key, "MemAvailable:") == 0) memAvailableKB = val;

        if (memTotalKB && memAvailableKB)
            break;
    }
    fclose(f);

    *Totalram = (float)memTotalKB     / 1024.0f;
    *freeram  = (float)memAvailableKB / 1024.0f;
}

/* Temperature in 0..255 (°C by default; °F if configured) */
uint8_t get_temperature(void)
{
    char buf[64];
    long milli;
    double t;

    buf[0] = '\0';
    milli = 0;

    if (read_first_line("/sys/class/thermal/thermal_zone0/temp", buf, sizeof(buf)) == 0)
    {
        milli = strtol(buf, NULL, 10);
    }
    else if (read_first_line("/sys/devices/virtual/thermal/thermal_zone0/temp", buf, sizeof(buf)) == 0)
    {
        milli = strtol(buf, NULL, 10);
    }

    t = milli / 1000.0; /* °C */
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
    FILE* f;
    double la1;
    long cores;
    double ratio;
    int bucket;

    la1 = 0.0;
    f = fopen("/proc/loadavg", "r");
    if (f)
    {
        if (fscanf(f, "%lf", &la1) != 1)
            la1 = 0.0;
        fclose(f);
    }
    cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;

    ratio = la1 / (double)cores;    /* 1.0 == one core fully used */
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 4.0) ratio = 4.0;

    bucket = (int)(ratio * (255.0 / 4.0) + 0.5);
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
    struct statvfs vfs;
    unsigned long long block;
    unsigned long long total;
    unsigned long long avail;
    unsigned long long used;

    if (!diskMemSize || !useMemSize)
        return 1;

    if (statvfs("/", &vfs) != 0)
    {
        *diskMemSize = 0;
        *useMemSize  = 0;
        return 1;
    }

    block = (unsigned long long)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
    total = (unsigned long long)vfs.f_blocks * block;
    avail = (unsigned long long)vfs.f_bavail * block; /* available to non-root */
    used  = total - avail;

    *diskMemSize = (uint16_t)((total + (1ULL << 29)) >> 30);
    *useMemSize  = (uint16_t)((used  + (1ULL << 29)) >> 30);
    return 0;
}
