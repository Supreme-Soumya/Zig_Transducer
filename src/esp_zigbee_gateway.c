/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Zigbee Router — external power, no USB/serial dependency
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/adc.h"
#include "driver/uart.h"
#include "esp_zigbee_core.h"
#include "zb_config_platform.h"
#include "esp_zigbee_gateway.h"

static void send_value_cb(uint8_t param);

static adc_channel_t s_adc_channels[3] = {
    ADC1_CHANNEL_1,  /* GPIO1 */
    ADC1_CHANNEL_2,  /* GPIO2 */
    ADC1_CHANNEL_4,  /* GPIO4 */
};

static void adc_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_12);
    adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12);
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    esp_zb_bdb_start_top_level_commissioning(mode_mask);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p                  = signal_struct->p_app_signal;
    esp_err_t err_status              = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_scheduler_alarm((esp_zb_callback_t)send_value_cb, 0, 2000);
        } else {
            esp_zb_scheduler_alarm(
                (esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                ESP_ZB_BDB_MODE_NETWORK_STEERING,
                1000
            );
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
        esp_zb_set_node_descriptor_manufacturer_code(ESP_MANUFACTURER_CODE);
        break;

    default:
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZC_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ESP_MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, ESP_MODEL_IDENTIFIER);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_cluster_list_add_identify_cluster(cluster_list,
        esp_zb_identify_cluster_create(NULL),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    uint8_t init_str[CUSTOM_JSON_MAX_LEN] = {0};
    esp_zb_attribute_list_t *custom_cluster = esp_zb_zcl_attr_list_create(CUSTOM_CLUSTER_ID);
    esp_zb_custom_cluster_add_custom_attr(
        custom_cluster,
        CUSTOM_ATTR_JSON_ID,
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        init_str
    );
    esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint           = ESP_ZB_GATEWAY_ENDPOINT,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);

    esp_zb_device_register(ep_list);

    /* Remove ESP_ERROR_CHECK — just call directly */
    esp_zb_start(false);
    esp_zb_stack_main_loop();
    vTaskDelete(NULL);
}

static void send_value_cb(uint8_t param)
{
    if (!esp_zb_bdb_dev_joined()) {
        esp_zb_scheduler_alarm((esp_zb_callback_t)send_value_cb, 0, 5000);
        return;
    }

    /* Discard reads for ADC channel settling */
    adc1_get_raw(ADC1_CHANNEL_1);
    adc1_get_raw(ADC1_CHANNEL_2);
    adc1_get_raw(ADC1_CHANNEL_4);
    adc1_get_raw(ADC1_CHANNEL_1);
    adc1_get_raw(ADC1_CHANNEL_2);
    adc1_get_raw(ADC1_CHANNEL_4);

    /* Actual reads */
    int raw1 = adc1_get_raw(ADC1_CHANNEL_1);
    int raw2 = adc1_get_raw(ADC1_CHANNEL_2);
    int raw3 = adc1_get_raw(ADC1_CHANNEL_4);

    float sensor1 = (float)raw1;
    float sensor2 = (float)raw2;
    float sensor3 = (float)raw3;

    /* ZCL character string: first byte = length, then ASCII */
    uint8_t zcl_str[CUSTOM_JSON_MAX_LEN] = {0};
    char *json_body = (char *)(zcl_str + 1);

    int json_len = snprintf(json_body, CUSTOM_JSON_MAX_LEN - 1,
        "{\"sensor4\":%.1f,\"sensor5\":%.1f,\"sensor6\":%.1f}",
        sensor1, sensor2, sensor3);

    zcl_str[0] = (uint8_t)json_len;

    esp_zb_zcl_write_attr_cmd_t write_cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,
            .dst_endpoint          = 1,
            .src_endpoint          = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID    = CUSTOM_CLUSTER_ID,
        .attr_number  = 1,
        .attr_field   = (esp_zb_zcl_attribute_t[]) {
            {
                .id   = CUSTOM_ATTR_JSON_ID,
                .data = {
                    .type  = ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                    .value = zcl_str,
                },
            }
        },
    };

    esp_zb_zcl_write_attr_cmd_req(&write_cmd);

    esp_zb_scheduler_alarm((esp_zb_callback_t)send_value_cb, 0, 5000);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    adc_init();

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    xTaskCreate(esp_zb_task, "Zigbee_main", 16384, NULL, 5, NULL);
}