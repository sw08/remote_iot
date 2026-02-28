/* ***************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "st_dev.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "iot_uart_cli.h"
#include "iot_cli_cmd.h"

#include "caps_button.h"

#include "driver/rmt_rx.h"

// onboarding_config_start is null-terminated string
extern const uint8_t onboarding_config_start[] asm("_binary_onboarding_config_json_start");
extern const uint8_t onboarding_config_end[] asm("_binary_onboarding_config_json_end");

// device_info_start is null-terminated string
extern const uint8_t device_info_start[] asm("_binary_device_info_json_start");
extern const uint8_t device_info_end[] asm("_binary_device_info_json_end");

static st_device_status g_device_status = ST_DEVICE_STATUS_INIT;

IOT_CTX *iot_ctx = NULL;

#define SET_PIN_NUMBER_CONFRIM

#define NEC_LEADING_CODE_DURATION_HIGH 9000
#define NEC_LEADING_CODE_DURATION_LOW 4500
#define NEC_BIT_ONE_DURATION_LOW 1687
#define NEC_BIT_ZERO_DURATION_LOW 562
#define NEC_MARGIN 500

static caps_button_data_t *cap_button_data;

#define IR_RX_GPIO_NUM 21

#define RMT_RX_RESOLUTION_HZ 1000000 // 1us resolution for IR signal timing

static void capability_init()
{
    cap_button_data = caps_button_initialize(iot_ctx, "main", NULL, NULL);
}

static void iot_status_cb(st_device_status device_status, void *usr_data)
{
    printf("Device status %d\n", device_status);
    g_device_status = device_status;
    switch (device_status)
    {
    case ST_DEVICE_STATUS_INIT:
        printf("Device is initializing\n");
        break;
    case ST_DEVICE_STATUS_ONBOARDING_READY:
        printf("Device is ready to be onboarded\n");
        break;
    case ST_DEVICE_STATUS_ONBOARDING_START:
        printf("Device onboarding has started\n");
        break;
    case ST_DEVICE_STATUS_ONBOARDING_NEED_CONFIRM:
        printf("Device onboarding needs confirmation\n");
        break;
    case ST_DEVICE_STATUS_ONBOARDING_ONBOARDED:
        printf("Device onboarding is successful\n");
        break;
    case ST_DEVICE_STATUS_CLOUD_DISCONNECTED:
        printf("Device is disconnected from the cloud\n");
        break;
    case ST_DEVICE_STATUS_CLOUD_CONNECTED:
        printf("Device is connected to the cloud\n");
        break;
    }
}

#if defined(SET_PIN_NUMBER_CONFRIM)
void *pin_num_memcpy(void *dest, const void *src, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; i++)
        *((char *)dest + i) = *((char *)src + i);
    return dest;
}
#endif

static void connection_start(void)
{
    iot_pin_t *pin_num = NULL;
    int err;

#if defined(SET_PIN_NUMBER_CONFRIM)
    pin_num = (iot_pin_t *)malloc(sizeof(iot_pin_t));
    if (!pin_num)
        printf("failed to malloc for iot_pin_t\n");

    // to decide the pin confirmation number(ex. "12345678"). It will use for easysetup.
    //    pin confirmation number must be 8 digit number.
    pin_num_memcpy(pin_num, "12345678", sizeof(iot_pin_t));
#endif

    // process on-boarding procedure. There is nothing more to do on the app side than call the API.
    err = st_conn_start(iot_ctx, (st_status_cb)&iot_status_cb, NULL, pin_num);
    if (err)
    {
        printf("fail to start connection. err:%d\n", err);
    }
    if (pin_num)
    {
        free(pin_num);
    }
}

static void iot_noti_cb(iot_noti_data_t *noti_data, void *noti_usr_data)
{
    printf("Notification message received\n");

    if (noti_data->type == IOT_NOTI_TYPE_DEV_DELETED)
    {
        printf("[device deleted]\n");
    }
    else if (noti_data->type == IOT_NOTI_TYPE_RATE_LIMIT)
    {
        printf("[rate limit] Remaining time:%d, sequence number:%d\n",
               noti_data->raw.rate_limit.remainingTime, noti_data->raw.rate_limit.sequenceNumber);
    }
}

static int get_symbol_data(rmt_symbol_word_t symbol)
{
    // printf("Symbol: duration0=%d level0=%d duration1=%d level1=%d\n", symbol.duration0, symbol.level0, symbol.duration1, symbol.level1);
    int duration = symbol.level0 == 1 ? symbol.duration1 : symbol.duration0;
    return (duration >= NEC_BIT_ONE_DURATION_LOW - NEC_MARGIN && duration <= NEC_BIT_ONE_DURATION_LOW + NEC_MARGIN) ? 1 : 0;
}

static int process_ir_data(rmt_symbol_word_t *symbols, size_t symbol_num)
{
    if (symbol_num < 34)
    {
        return -1;
    }
    // printf("Symbol 0: duration0=%d level0=%d duration1=%d level1=%d\n", symbols[0].duration0, symbols[0].level0, symbols[0].duration1, symbols[0].level1);
    if (symbols[0].level0 == 1 && symbols[0].level1 == 0)
    {
        if (symbols[0].duration0 < NEC_LEADING_CODE_DURATION_HIGH - NEC_MARGIN ||
            symbols[0].duration0 > NEC_LEADING_CODE_DURATION_HIGH + NEC_MARGIN ||
            symbols[0].duration1 < NEC_LEADING_CODE_DURATION_LOW - NEC_MARGIN ||
            symbols[0].duration1 > NEC_LEADING_CODE_DURATION_LOW + NEC_MARGIN)
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    uint32_t result = 0;
    for (int i = 0; i < 32; i++)
    {
        if (get_symbol_data(symbols[1 + i]))
        {
            result |= (1UL << (31 - i));
        }
        else
        {
            result &= ~(1UL << (31 - i));
        }
    }
    return (int)result;
}

static bool rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void app_main_task(void *arg)
{
    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_RX_GPIO_NUM,
        .resolution_hz = RMT_RX_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .flags.invert_in = true,
    };
    rmt_channel_handle_t rx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));

    printf("register RX done callback");
    QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    assert(receive_queue);
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, receive_queue));

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };
    rmt_symbol_word_t raw_symbols[64];
    rmt_rx_done_event_data_t rx_data;

    ESP_ERROR_CHECK(rmt_enable(rx_chan));

    ESP_ERROR_CHECK(rmt_receive(rx_chan, raw_symbols, sizeof(raw_symbols), &receive_config));
    /*
    "pushed","held","double","pushed_2x", "pushed_3x", "pushed_4x", "pushed_5x", "pushed_6x", "down", "down_2x",
    "down_3x", "down_4x", "down_5x", "down_6x", "down_hold", "up", "up_2x", "up_3x", "up_4x", "up_5x", "up_6x",
    "up_hold", "swipe_up", "swipe_down", "swipe_left", "swipe_right"
    */

    /*
    0x5D 0x9D 0x1D
    0xDD 0xFD 0x3D
    0x1F 0x57 0x6F
    0x97 0x67 0x4F
    0xCF 0xE7 0x85
    0xEF 0xC7 0xA5
    0xBD 0xB5 0xAD
    */
    while (1)
    {
        if (xQueueReceive(receive_queue, &rx_data, pdMS_TO_TICKS(10)) == pdPASS)
        {
            int ir_value = process_ir_data(raw_symbols, 64);
            if (ir_value != -1)
            {
                printf("Received IR value: %d / 0x%X, %s\n", ir_value, ir_value & 0b11111111, ((ir_value & 0b11111111) | ((ir_value & 0b1111111100000000) >> 8)) == 0b11111111 ? "Valid" : "Invalid");
                switch (ir_value & 0b11111111)
                {
                case 0x5D:
                    cap_button_data->set_button_value(cap_button_data, "pushed");
                    break;
                case 0x9D:
                    cap_button_data->set_button_value(cap_button_data, "double");
                    break;
                case 0x1D:
                    cap_button_data->set_button_value(cap_button_data, "held");
                    break;
                case 0xDD:
                    cap_button_data->set_button_value(cap_button_data, "pushed_2x");
                    break;
                case 0xFD:
                    cap_button_data->set_button_value(cap_button_data, "pushed_3x");
                    break;
                case 0x3D:
                    cap_button_data->set_button_value(cap_button_data, "pushed_4x");
                    break;
                case 0x1F:
                    cap_button_data->set_button_value(cap_button_data, "pushed_5x");
                    break;
                case 0x57:
                    cap_button_data->set_button_value(cap_button_data, "pushed_6x");
                    break;
                case 0x6F:
                    cap_button_data->set_button_value(cap_button_data, "down");
                    break;
                case 0x97:
                    cap_button_data->set_button_value(cap_button_data, "down_2x");
                    break;
                case 0x67:
                    cap_button_data->set_button_value(cap_button_data, "down_3x");
                    break;
                case 0x4F:
                    cap_button_data->set_button_value(cap_button_data, "down_4x");
                    break;
                case 0xCF:

                    cap_button_data->set_button_value(cap_button_data, "down_5x");
                    break;
                case 0xE7:
                    cap_button_data->set_button_value(cap_button_data, "down_6x");

                    break;
                case 0x85:
                    cap_button_data->set_button_value(cap_button_data, "down_hold");
                    break;
                case 0xEF:
                    cap_button_data->set_button_value(cap_button_data, "up");
                    break;
                case 0xC7:
                    cap_button_data->set_button_value(cap_button_data, "up_2x");
                    break;
                case 0xA5:
                    cap_button_data->set_button_value(cap_button_data, "up_3x");

                    break;
                case 0xBD:
                    cap_button_data->set_button_value(cap_button_data, "up_4x");
                    break;
                case 0xB5:
                    cap_button_data->set_button_value(cap_button_data, "up_5x");
                    break;
                case 0xAD:
                    cap_button_data->set_button_value(cap_button_data, "up_6x");
                    break;
                }
                cap_button_data->attr_button_send(cap_button_data);
            }
            ESP_ERROR_CHECK(rmt_receive(rx_chan, raw_symbols, sizeof(raw_symbols), &receive_config));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    /**
      SmartThings Device SDK(STDK) aims to make it easier to develop IoT devices by providing
      additional st_iot_core layer to the existing chip vendor SW Architecture.

      That is, you can simply develop a basic application
      by just calling the APIs provided by st_iot_core layer like below.

      // create a iot context
      1. st_conn_init();

      // create a handle to process capability
      2. st_cap_handle_init(); (called in function 'capability_init')

      // register a callback function to process capability command when it comes from the SmartThings Server.
      3. st_cap_cmd_set_cb(); (called in function 'capability_init')

      // process on-boarding procedure. There is nothing more to do on the app side than call the API.
      4. st_conn_start(); (called in function 'connection_start')
     */

    unsigned char *onboarding_config = (unsigned char *)onboarding_config_start;
    unsigned int onboarding_config_len = onboarding_config_end - onboarding_config_start;
    unsigned char *device_info = (unsigned char *)device_info_start;
    unsigned int device_info_len = device_info_end - device_info_start;

    int iot_err;

    // create a iot context
    iot_ctx = st_conn_init(onboarding_config, onboarding_config_len, device_info, device_info_len);
    if (iot_ctx != NULL)
    {
        iot_err = st_conn_set_noti_cb(iot_ctx, iot_noti_cb, NULL);
        if (iot_err)
            printf("fail to set notification callback function\n");
    }
    else
    {
        printf("fail to create the iot_context\n");
    }

    // create a handle to process capability and initialize capability info
    capability_init();

    register_iot_cli_cmd();
    uart_cli_main();
    xTaskCreate(app_main_task, "app_main_task", 4096, NULL, 10, NULL);

    // connect to server
    connection_start();
}
