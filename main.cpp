#define BUILD_FOR_RELEASE
#undef BUILD_FOR_RELEASE

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"

#include "main.h"

#include "CApp.h"
#include "Wifi.h"
#include "RC522.h"

// --- tcp --- //
#include "nvs_flash.h"
#include "esp_http_client.h"

void tcp_server_loop(void *);

// auth mode WPA2 for home wifi
#define ESP_WIFI_SSID "--your-own-wifi-ssid" 
#define ESP_WIFI_PASS "your-own-wifi-password"

// -------- forward declarations ---//

void start_rc522_loop(void *);

// -------- modules -----------//
CApp *g_app;
Wifi *g_wifi;
RC522 *g_rc522;

// ----------------- main -----------------//
extern "C"
{
    void app_main(void)
    {
        // ------------- //
        g_app = new CApp();

        g_wifi = new Wifi(ESP_WIFI_SSID, ESP_WIFI_PASS);

        g_wifi->start_ntp_time_sync();

        // --------- RC522 and its loop -------------------- //

        g_rc522 = new RC522();

        xTaskCreate(start_rc522_loop, "RC522LOOPTASK", 8192, NULL, 5, NULL);

        // ------------TCP Server -------------------------//

        xTaskCreate(tcp_server_loop, "tcp_server", 4096, NULL, 5, NULL);

        //------- start the message loop -----------------------------//

        esp_register_shutdown_handler(do_shutdown);

        uint32_t message_id;

        while (g_app->get_message(message_id))
        {
            uint16_t msg = (message_id & 0x00ff);

            switch (msg)
            {
            case MSG_WIFI_CONNECTED:
            {
                ESP_LOGI(CApp::TAGAPP, "starting communication now...");
            }
            break;

            case MSG_WIFI_FAILED:
            {
                ESP_LOGE(CApp::TAGAPP, "wifi failed after many attempts");
            }
            break;

            case MSG_NTP_TIME_SYNCED:
            {
                ESP_LOGD(CApp::TAGAPP, "ntp time synced");
            }
            break;

            default:
                break;
            }
        }

        esp_restart();
    }
}

// ------------ loop for rc522 listener for cards -------------//

#include <map>
#include <sstream>
#include <chrono>
#include <ctime>
std::map<std::string, time_t> g_cards;

void start_rc522_loop(void *parameters)
{
    ESP_LOGD(CApp::TAGAPP, "[APP] RC522 version: 0x%02x", g_rc522->GetRC522Version());

    // upto 10 bytes UID - 2 chars for each
    char uidString[20 + 1] = {0};

    while (true)
    {
        if (g_rc522->GetUID(uidString))
        {
            ESP_LOGI(CApp::TAGAPP, "UID = %s", uidString);

            // use uidString now! time is UTC
            std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

            ESP_LOGI(CApp::TAGAPP, "Time = %s", std::ctime(&time));

            g_cards.insert_or_assign(uidString, time);
        }

        // 200 millisecond delay
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

void tcp_server_loop(void *parameters)
{
    const char *TAGTCP = "tag:tcp";

    // value hard-coded in android app
    const uint8_t CMD_QUERY_STATE = 225;

    int keepAlive = 1;
    int keepIdle = 5;
    int keepInterval = 5;
    int keepCount = 3;
    int PORT = 50000;
    struct sockaddr_storage dest_addr;
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);

    int attempts = 0;

    while (attempts++ < 5)
    {
        int listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

        if (listen_socket >= 0)
        {
            ESP_LOGI(TAGTCP, "Socket created");

            attempts = 0;

            int opt = 1;

            setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            int err = bind(listen_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            if (0 == err)
            {

                ESP_LOGI(TAGTCP, "Socket bound, port %d", PORT);

                err = listen(listen_socket, 1);

                if (0 == err)
                {

                    int sock = -1;

                    do
                    {
                        ESP_LOGI(TAGTCP, "waiting for connection...");

                        struct sockaddr_storage source_addr;

                        socklen_t addr_len = sizeof(source_addr);

                        sock = accept(listen_socket, (struct sockaddr *)&source_addr, &addr_len);

                        if (sock < 0)
                        {
                            ESP_LOGE(TAGTCP, "Unable to accept connection: errno %d", errno);
                        }
                        else
                        {
                            ESP_LOGI(TAGTCP, "client connected!");

                            // Set tcp keepalive option
                            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
                            setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
                            setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
                            setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

                            unsigned char data;

                            while (recv(sock, &data, sizeof(data), 0) > 0)
                            {
                                // signals from client are negative numbers
                                if (data & 128)
                                {
                                    if (data == CMD_QUERY_STATE) // 1-1-1-x [don't care] read g_value
                                    {
                                        // get the json string
                                        std::stringstream ss;

                                        ss << "[";

                                        for (auto i = g_cards.begin(); i != g_cards.end(); i++)
                                        {
                                            ss << "{\"card\":"
                                               << "\"" << i->first << "\", \"time\":" << i->second << "},";
                                        }

                                        ss << "]";

                                        std::string json = ss.str();

                                        const char *data = json.c_str();

                                        int total_sent = 0;

                                        while (total_sent < json.length())
                                        {
                                            int sent = send(sock, data, strlen(data), 0);

                                            if (sent > 0)
                                            {
                                                total_sent += sent;

                                                data += sent;
                                            }
                                            else
                                            {
                                                total_sent = INT_MAX;
                                            }
                                        }

                                        if (total_sent != json.length())
                                            break;
                                    }
                                    else // it's a ping
                                    {
                                        unsigned char resp = 0x1;

                                        if (send(sock, &resp, sizeof(resp), 0) <= 0)
                                            break;
                                    }
                                }
                                else
                                {
                                }
                            };

                            ESP_LOGI(TAGTCP, "client disconnected");

                            ESP_LOGI(TAGTCP, "shutting down connection...");

                            shutdown(sock, 0);

                            close(sock);
                        }

                    } while (sock >= 0);
                }
                else
                {
                    ESP_LOGE(TAGTCP, "Error occurred during listen: errno %d", errno);
                }
            }
            else
            {
                ESP_LOGE(TAGTCP, "Socket unable to bind: errno %d", errno);
            }

            close(listen_socket);
        }

        ESP_LOGE(TAGTCP, "Socket closed. Retrying after 8 seconds...");

        // very rarely expected to reach here. so wait 8 seconds
        vTaskDelay(8000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

void queue_message(uint16_t msg, uint16_t data)
{
    g_app->add_message_to_que(msg, data);
}

void do_shutdown()
{
    if (NULL == g_app)
        return;

    ESP_LOGI(CApp::TAGAPP, "cleanup shutdown . . .");

    delete g_rc522;

    delete g_wifi;

    delete g_app;

    g_app = NULL;

    esp_unregister_shutdown_handler(do_shutdown);
}
