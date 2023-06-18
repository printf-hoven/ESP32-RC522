#pragma once

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"

#include "esp_event.h"

class Wifi
{
public:
    Wifi(const char *, const char *);

    ~Wifi();

public:
    void start_ntp_time_sync();

    static void time_sync_notification_cb(struct timeval *tv);

private:
    esp_event_handler_instance_t _instance_any_id;

    esp_event_handler_instance_t _instance_got_ip;

private:
    static void event_handler(void *, esp_event_base_t, int32_t, void *);

private:
    static const char *TAGWIFI;
};