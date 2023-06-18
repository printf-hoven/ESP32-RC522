#include "Wifi.h"
#include "main.h"
#include "esp_wifi.h"
#include "string.h"
#include "esp_sntp.h"

const char *Wifi::TAGWIFI = "tag:Wifi station";

Wifi::Wifi(const char *ssid, const char *pwd)
{
    // -------------- configure wifi ----------//

    esp_netif_create_default_wifi_sta();
    {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        wifi_config_t wifi_config = {

            .sta = {
                .ssid = 0,
                .password = 0,
                .scan_method = WIFI_FAST_SCAN,
                .bssid_set = false,
                .bssid = 0,
                .channel = 0,
                .listen_interval = 0,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,

                .threshold{
                    .rssi = 0,
                    // threshold auth mode WPA2 for home wifi
                    .authmode = WIFI_AUTH_WPA2_PSK,
                },
                .pmf_cfg = {
                    .capable = false,
                    .required = false,
                },
                .rm_enabled = 0,
                .btm_enabled = 0,
                .mbo_enabled = 0,
                .ft_enabled = 0,
                .owe_enabled = 0,
                .transition_disable = 0,
                .reserved = 0,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
                .failure_retry_cnt = 0,

            },

        };

        strcpy((char *)wifi_config.sta.ssid, ssid);
        strcpy((char *)wifi_config.sta.password, pwd);

        // also saves to non-volatile-memory
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &_instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &_instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_start());
}

Wifi::~Wifi()
{
    // cleanup - unregister wifi events
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &_instance_any_id);

    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &_instance_got_ip);

    esp_wifi_stop();

    // hope it is nop if esp_init not called
    esp_sntp_stop();
}

void Wifi::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const int ESP_MAXIMUM_RETRY = 15;

    static int s_retry_num = 0;

    if (WIFI_EVENT == event_base)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START: // station started
        {
            esp_wifi_connect();
        }
        break;

        case WIFI_EVENT_STA_DISCONNECTED: // wifi could not connect at all OR if it was connected, then some disruption occurred
        {
            if (s_retry_num < ESP_MAXIMUM_RETRY)
            {
                vTaskDelay(15000 / portTICK_PERIOD_MS);

                esp_wifi_connect();

                s_retry_num++;

                ESP_LOGI(TAGWIFI, "wifi state is disconnected, retrying...");
            }
            else
            {
                // raise failure event
                queue_message(MSG_WIFI_FAILED, 0);
            }
        }
        break;

        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;

            ESP_LOGI(TAGWIFI, "Connected to: %s", (char *)event->ssid);

            s_retry_num = 0;
        }
        break;

        default:
            break;
        }
    }
    else if (IP_EVENT == event_base)
    {
        if (IP_EVENT_STA_GOT_IP == event_id)
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

            ESP_LOGI(TAGWIFI, "Connected as IP: " IPSTR, IP2STR(&event->ip_info.ip));

            s_retry_num = 0;

            queue_message(MSG_WIFI_CONNECTED, 0);
        }
    }
}

void Wifi::start_ntp_time_sync()
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

    esp_sntp_setservername(0, "pool.ntp.org");

    sntp_set_time_sync_notification_cb(Wifi::time_sync_notification_cb);

    esp_sntp_init();
}

void Wifi::time_sync_notification_cb(struct timeval *tv)
{
    queue_message(MSG_NTP_TIME_SYNCED, 0);
}
