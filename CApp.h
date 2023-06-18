#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class CApp
{
public:
    CApp();
    ~CApp();

public:
    static const char *TAGAPP;

public:
    void add_message_to_que(uint16_t, uint16_t);

    bool get_message(uint32_t&);

private:
    QueueHandle_t _gpio_evt_queue;

};