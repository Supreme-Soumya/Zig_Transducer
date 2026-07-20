/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Zigbee Router — external power, no USB/serial dependency
 */

#pragma once

#include "esp_err.h"
#include "esp_zigbee_core.h"

/* Zigbee configuration */
#define MAX_CHILDREN                    10
#define INSTALLCODE_POLICY_ENABLE       false
#define ESP_ZB_PRIMARY_CHANNEL_MASK     (1l << 13)
#define ESP_ZB_GATEWAY_ENDPOINT         1
#define APP_PROD_CFG_CURRENT_VERSION    0x0001

/* Basic manufacturer information */
#define ESP_MANUFACTURER_CODE           0x131B
#define ESP_MANUFACTURER_NAME           "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER            "\x07"CONFIG_IDF_TARGET

/* RCP connection pins */
#define HOST_RX_PIN_TO_RCP_TX           4
#define HOST_TX_PIN_TO_RCP_RX           5

#define ESP_ZB_ZC_CONFIG()                                                              \
    {                                                                                   \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                                       \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,                               \
        .nwk_cfg.zczr_cfg = {                                                           \
            .max_children = MAX_CHILDREN,                                               \
        },                                                                              \
    }

#if CONFIG_ZB_RADIO_NATIVE
#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }
#else
#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_UART_RCP,                   \
        .radio_uart_config = {                                  \
            .port = 1,                                          \
            .uart_config = {                                    \
                .baud_rate = 460800,                            \
                .data_bits = UART_DATA_8_BITS,                  \
                .parity    = UART_PARITY_DISABLE,               \
                .stop_bits = UART_STOP_BITS_1,                  \
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,          \
                .rx_flow_ctrl_thresh = 0,                       \
                .source_clk = UART_SCLK_DEFAULT,                \
            },                                                  \
            .rx_pin = HOST_RX_PIN_TO_RCP_TX,                    \
            .tx_pin = HOST_TX_PIN_TO_RCP_RX,                    \
        },                                                      \
    }
#endif

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }

/* Custom cluster for JSON string data */
#define CUSTOM_CLUSTER_ID               0xFF00
#define CUSTOM_ATTR_JSON_ID             0x0001
#define CUSTOM_JSON_MAX_LEN             128

/* ADC channel assignments for ESP32-C6 */
#define SENSOR1_ADC_CHAN                ADC_CHANNEL_1
#define SENSOR2_ADC_CHAN                ADC_CHANNEL_2
#define SENSOR3_ADC_CHAN                ADC_CHANNEL_6
#define SENSOR_ADC_ATTEN                ADC_ATTEN_DB_12