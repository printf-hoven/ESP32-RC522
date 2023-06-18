#include "CApp.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

const char *CApp::TAGAPP = "tag:App";

void CApp::add_message_to_que(uint16_t msg, uint16_t data)
{
    uint32_t m = ((data << 16) + msg);

    // RTOS https://www.freertos.org/a00119.html
    xQueueSendFromISR(_gpio_evt_queue, (void *)&m, NULL);
}

CApp::CApp()
{
    //---------------- log and basic parameters -------//

    ESP_LOGI(TAGAPP, "[APP] Startup..");
    ESP_LOGI(TAGAPP, "[APP] Free memory: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAGAPP, "[APP] IDF version: %s", esp_get_idf_version());

// levels NONE, ERROR, WARN, INFO, DEBUG, VERBOSE
#ifdef BUILD_FOR_RELEASE
    esp_log_level_set("*", ESP_LOG_NONE);
#else
    esp_log_level_set("*", ESP_LOG_DEBUG);
#endif

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    _gpio_evt_queue = xQueueCreate(/*MAX_QUE_SIZE*/ 10, sizeof(uint32_t));
}

CApp::~CApp()
{
    vQueueDelete(_gpio_evt_queue);
}

bool CApp::get_message(uint32_t &message_id)
{
    return (xQueueReceive(_gpio_evt_queue, &message_id, portMAX_DELAY));
}

