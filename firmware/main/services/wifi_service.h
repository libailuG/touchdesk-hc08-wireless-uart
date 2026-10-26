#ifndef TOUCHDESK_WIFI_SERVICE_H
#define TOUCHDESK_WIFI_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define WIFI_NETWORK_LIMIT 16
#define WIFI_SAVED_LIMIT 8
#define WIFI_NAME_SIZE 100
typedef enum { NET_OPEN, NET_WPA2, NET_WPA3, NET_WPA2_WPA3, NET_UNSUPPORTED } net_security_t;
typedef enum {
    NET_OFF, NET_IDLE, NET_STOPPING, NET_CONNECTING, NET_GETTING_IP,
    NET_CONNECTED, NET_RETRY, NET_ERROR
} net_state_t;
typedef enum {
    NET_ERR_NONE, NET_ERR_INIT, NET_ERR_AUTH, NET_ERR_TIMEOUT,
    NET_ERR_NOT_FOUND, NET_ERR_SCAN, NET_ERR_DRIVER, NET_ERR_OVERFLOW
} net_error_t;
typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t security;
    int8_t rssi;
    bool remembered;
    char name[WIFI_NAME_SIZE];
} wifi_network_t;
/* Private credential payload. Never include this in logs or public snapshots. */
typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t security;
    char password[64];
} wifi_profile_t;
typedef struct {
    unsigned revision, scan_revision, saved_revision, saved_count;
    bool available, enabled, scanning, has_saved, save_warning;
    net_state_t state;
    net_error_t error;
    int8_t rssi;
    uint8_t ssid[32],ssid_len,security;
    unsigned retry_seconds;
    char name[WIFI_NAME_SIZE], saved_name[WIFI_NAME_SIZE], ip[16];
} wifi_status_t;
esp_err_t wifi_service_init(void);
esp_err_t wifi_service_deinit(void);
bool wifi_service_enable(bool enabled);
bool wifi_service_scan(void);
bool wifi_service_connect(const wifi_network_t *network,const char *password);
bool wifi_service_connect_saved(void);
bool wifi_service_connect_known(const wifi_network_t *network);
bool wifi_service_forget_known(const wifi_network_t *network);
size_t wifi_service_saved_networks(wifi_network_t *out,size_t capacity,unsigned *revision);
bool wifi_security_compatible(unsigned saved,unsigned visible);
bool wifi_service_disconnect(void);
bool wifi_service_forget(void);
void wifi_service_status(wifi_status_t *out);
size_t wifi_service_networks(wifi_network_t *out,size_t capacity,unsigned *revision);
const char *wifi_state_text(const wifi_status_t *status);
const char *wifi_security_text(unsigned security);
/* Portable policy functions shared by firmware and regression tests. */
bool wifi_profile_valid(const wifi_profile_t *profile);
void wifi_secret_clear(void *memory,size_t size);
void wifi_display_name(const uint8_t *ssid,size_t length,char *out,size_t capacity);
unsigned wifi_retry_delay(unsigned attempt);
size_t wifi_network_merge(wifi_network_t *list,size_t count,size_t limit,const wifi_network_t *item);
#endif
