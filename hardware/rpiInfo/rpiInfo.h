#ifndef  __RPIINFO_H
#define  __RPIINFO_H
#include <stdint.h>

/**********Select display temperature type**************/
#define CELSIUS        0
#define FAHRENHEIT     1
#define TEMPERATURE_TYPE  FAHRENHEIT
/**********Select display temperature type**************/

/**********Select display network IP type**************/
#define ETH0_ADDRESS    0
#define WLAN0_ADDRESS   1
#define CUSTOM_ADDRESS  2

// Set your NIC name here when using CUSTOM_ADDRESS
#define CUSTOM_IFNAME   "end0"

// Choose the source for the IP shown on the OLED
#define IPADDRESS_TYPE  CUSTOM_ADDRESS   /* ETH0_ADDRESS | WLAN0_ADDRESS | CUSTOM_ADDRESS */
/**********Select display network IP type**************/

/************************IP display switch****************/
#define IP_DISPLAY_OPEN   0
#define IP_DISPLAY_CLOSE  1
#define IP_SWITCH         IP_DISPLAY_OPEN
#define CUSTOM_DISPLAY    "WIKI SERVER"
/************************IP display switch****************/

char* get_ip_address(void);
char* get_ip_address_new(void);
void get_sd_memory(uint32_t *MemSize, uint32_t *freesize);
void get_cpu_memory(float *Totalram, float *freeram);
uint8_t get_temperature(void);
uint8_t get_cpu_message(void);
uint8_t get_hard_disk_memory(uint16_t *diskMemSize, uint16_t *useMemSize);

#endif /*__RPIINFO_H*/

