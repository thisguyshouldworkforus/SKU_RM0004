// SPDX-License-Identifier: MIT
// Rewritten rpiInfo.c for AlmaLinux 10 / Pi-Rack Pro
// - Robust IP lookup using getifaddrs()
// - Supports custom interface name via CUSTOM_IFNAME in rpiInfo.h
// - Minimal, portable implementations for memory, disk, temperature

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

#ifndef CUSTOM_IFNAME
// Fallback if header wasn't updated
#define CUSTOM_IFNAME "end0"
#endif

#ifndef IPADDRESS_TYPE
// Fallback: default to custom to support end0 out of the box
#define IPADDRESS_TYPE CUSTOM_ADDRESS
#endif

// ---------- helpers ----------

static const char* pick_iface(void) {
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

/*
 * Return a newly-allocated string with the first IPv4 address
 * on the chosen interface. Caller must free().
 * If not found, returns strdup(CUSTOM_DISPLAY) when IP is disabled,
 * or NULL if allocation fails.
 */
static char* lookup_ipv4_for_iface(const char* ifname) {
    struct ifaddrs *ifaddr = NULL, *ifa = NULL;
    char buf[INET_ADDRSTRLEN];

    if (getifaddrs(&ifaddr) == -1) {
        return NULL;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, ifname) != 0) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (!inet_ntop(AF_INET, &(sin->sin_addr), buf, sizeof(buf))) continue;

        freeifaddrs(ifaddr);
        return strdup(buf);
    }

    freeifaddrs(ifaddr);

#if IP_SWITCH == IP_DISPLAY_CLOSE
    // If IP display is disabled, show custom text instead of empty
    return strdup(CUSTOM_DISPLAY);
#else
    return NULL;
#endif
}

static int read_first_line(const char* path, char* out, size_t outlen) {
    FILE* f = fopen(path, "r");
    if (!f) return -errno;
    int ok = (fgets(out, (int)outlen, f) != NULL) ? 0 : -EIO;
    fclose(f);
    if (ok == 0) {
        size_t n = strlen(out);
        while (n && (out[n-1] == '\n' || out[n-1] == '\r')) { out[--n] = '\0'; }
    }
    return ok;
}

// ---------- public API ----------

char* get_ip_address(void) {
    const char* ifname = pick_iface();
    char* ip = lookup_ipv4_for_iface(ifname);

#if IP_SWITCH == IP_DISPLAY_CLOSE
    if (!ip) return strdup(CUSTOM_DISPLAY);
#endif
    return ip ? ip : strdup("0.0.0.0");
}

char* get_ip_address_new(void) {
    // Same semantics as get_ip_address() for this implementation
    return get_ip_address();
}

void get_sd_memory(uint32_t *MemSize, uint32_t *freesize) {
    // Use root filesystem as SD indicator; values in MB
    if (!MemSize || !freesize) return;
    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0) {
        unsigned long long total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
        unsigned long long freeb = (unsigned long long)vfs.f_bfree   * vfs.f_frsize;
        *MemSize  = (uint32_t)(total / (1024ULL * 1024ULL));
        *freesize = (uint32_t)(freeb / (1024ULL * 1024ULL));
    } else {
        *MemSize = 0;
        *freesize = 0;
    }
}

void get_cpu_memory(float *Totalram, float *freeram) {
    // Parse /proc/meminfo (kB); report MB as float
    if (!Totalram || !freeram) return;
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) { *Totalram = 0.f; *freeram = 0.f; return; }

    long memTotalKB = 0, memAvailableKB = 0;
    char key[64];
    long val = 0;
    char unit[16];

    while (fscanf(f, "%63s %ld %15s\n", key, &val, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) memTotalKB = val;
        else if (strcmp(key, "MemAvailable:") == 0) memAvailableKB = val;
        if (memTotalKB && memAvailableKB) break;
    }
    fclose(f);

    *Totalram = (float)memTotalKB / 1024.0f;
    *freeram  = (float)memAvailableKB / 1024.0f;
}

uint8_t get_temperature(void) {
    // Read SoC temp in millidegC, convert to selected unit, clamp to [0,255]
    char buf[64] = {0};
    int rc = read_first_line("/sys/class/thermal/thermal_zone0/temp", buf, sizeof(buf));
    long milli = 0;
    if (rc == 0) {
        milli = strtol(buf, NULL, 10);
    } else {
        // Fallback: try vcgencmd style path on some kernels (rare on Alma)
        rc = read_first_line("/sys/devices/virtual/thermal/thermal_zone0/temp", buf, sizeof(buf));
        if (rc == 0) milli = strtol(buf, NULL, 10);
    }
    double c = milli / 1000.0;
#if TEMPERATURE_TYPE == FAHRENHEIT
    c = (c * 9.0 / 5.0) + 32.0;
#endif
    if (c < 0.0) c = 0.0;
    if (c > 255.0) c = 255.0;
    return (uint8_t)(c + 0.5);
}

uint8_t get_cpu_message(void) {
    // Provide a simple CPU load “bucket” based on 1-minute load average
    // 0-63: idle, 64-127: low, 128-191: med, 192-255: high
    double la1 = 0.0;
    FILE* f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf", &la1) != 1) la1 = 0.0;
        fclose(f);
    }
    // Normalize by CPU cores to keep display sensible
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;
    double ratio = la1 / (double)cores; // e.g., 1.0 == one core fully busy
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 4.0) ratio = 4.0; // clamp
    int bucket = (int)(ratio * (255.0 / 4.0) + 0.5);
    if (bucket < 0) bucket = 0;
    if (bucket > 255) bucket = 255;
    return (uint8_t)bucket;
}

uint8_t get_hard_disk_memory(uint16_t *diskMemSize, uint16_t *useMemSize) {
    // Report total/used in GB for "/"
    if (!diskMemSize || !useMemSize) return 1;
    struct statvfs vfs;
    if (statvfs("/", &vfs) != 0) {
        *diskMemSize = 0;
        *useMemSize  = 0;
        return 1;
    }
    unsigned long long total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
    unsigned long long freeb = (unsigned long long)vfs.f_bavail * vfs.f_frsize; // space available to unprivileged
    unsigned long long used  = total - freeb;

    *diskMemSize = (uint16_t)(total / (1024ULL * 1024ULL * 1024ULL));
    *useMemSize  = (uint16_t)(used  / (1024ULL * 1024ULL * 1024ULL));
    return 0;
}

