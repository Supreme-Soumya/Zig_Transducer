/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Zigbee Gateway Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include <fcntl.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_coexist.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_usb_serial_jtag.h"
#include "esp_vfs_eventfd.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_zigbee_gateway.h"
#include "zb_config_platform.h"
#include <stdlib.h>

static const char *TAG = "ESP_ZB_GATEWAY";


/* Note: Please select the correct console output port based on the development board in menuconfig */
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
esp_err_t esp_zb_gateway_console_init(void)
{
    esp_err_t ret = ESP_OK;
    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

    /* Enable non-blocking mode on stdin and stdout */
    fcntl(fileno(stdout), F_SETFL, O_NONBLOCK);
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);

    usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ret = usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    usb_serial_jtag_vfs_use_driver();
    uart_vfs_dev_register();
    return ret;
}
#endif

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee bdb commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p         = signal_struct->p_app_signal;
    esp_err_t err_status     = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started, attempting to join network...");
            // Router just steers (scans and joins), never forms
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGE(TAG, "Zigbee stack init failed (status: %s)", esp_err_to_name(err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
	if (err_status == ESP_OK) {
	        esp_zb_ieee_addr_t extended_pan_id;
	        esp_zb_get_extended_pan_id(extended_pan_id);
	        ESP_LOGI(TAG, "Joined network successfully!");
	        ESP_LOGI(TAG, "PAN ID: 0x%04hx, Channel: %d, Short Addr: 0x%04hx",
	                 esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());

	        // DELETE these two lines:
	        // s_network_joined = true;
	        // esp_zb_scheduler_alarm((esp_zb_callback_t)send_value_cb, 0, 2000);

	    } else {
	        ESP_LOGW(TAG, "Steering failed, retrying...");
	        esp_zb_scheduler_alarm(
	            (esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
	            ESP_ZB_BDB_MODE_NETWORK_STEERING,
	            1000
	        );
	    }
	    break;

    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        {
            esp_zb_zdo_signal_device_annce_params_t *dev_annce_params =
                (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
            ESP_LOGI(TAG, "Device announced, short addr: 0x%04hx", dev_annce_params->device_short_addr);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
        ESP_LOGI(TAG, "Production config: %s", err_status == ESP_OK ? "ready" : "not present");
        esp_zb_set_node_descriptor_manufacturer_code(ESP_MANUFACTURER_CODE);
        break;

    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
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
        esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint           = ESP_ZB_GATEWAY_ENDPOINT,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);
    esp_zb_device_register(ep_list);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
    vTaskDelete(NULL);
}

void configure_reporting(void)
{
    esp_zb_zcl_reporting_info_t reporting_info = {
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .ep = ESP_ZB_GATEWAY_ENDPOINT,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_OUTPUT,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attr_id = ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_PRESENT_VALUE_ID,
        .u.send_info = {
            .min_interval = 1,
            .max_interval = 60,
            .delta.u16 = 0,       // report on any change
            .def_min_interval = 1,
            .def_max_interval = 60,
        },
        .dst = {
            .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        },
        .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
    };

    esp_zb_zcl_update_reporting_info(&reporting_info);
}



// This runs INSIDE the Zigbee stack context — safe to call ZCL APIs
static void send_value_cb(uint8_t param)
{
    if (esp_zb_bdb_dev_joined() == false) {
        ESP_LOGW(TAG, "Not joined, skipping...");
        esp_zb_scheduler_alarm((esp_zb_callback_t)send_value_cb, 0, 5000);
        return;
    }

    // Build JSON string with fake sensor values for testing
    float sensor1 = (float)(rand() % 100);
    float sensor2 = (float)(rand() % 100);
    float sensor3 = (float)(rand() % 100);

    // ZCL character string: first byte = length, then ASCII bytes
    uint8_t zcl_str[CUSTOM_JSON_MAX_LEN] = {0};
    char *json_body = (char *)(zcl_str + 1);  // skip first length byte

    int json_len = snprintf(json_body, CUSTOM_JSON_MAX_LEN - 1,
        "{\"sensor1\":%.1f,\"sensor2\":%.1f,\"sensor3\":%.1f}",
        sensor1, sensor2, sensor3);

    zcl_str[0] = (uint8_t)json_len;  // set ZCL string length byte

    ESP_LOGI(TAG, "Sending: %s", json_body);

    esp_zb_zcl_write_attr_cmd_t write_cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,
            .dst_endpoint = 1,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID    = CUSTOM_CLUSTER_ID,
        .attr_number  = 1,
        .attr_field   = (esp_zb_zcl_attribute_t[]) {
            {
                .id   = CUSTOM_ATTR_JSON_ID,
                .data = {
                    .type  = ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                    .value = zcl_str,  // ZCL string with length prefix
                },
            }
        },
    };

    esp_err_t ret = esp_zb_zcl_write_attr_cmd_req(&write_cmd);
    ESP_LOGI(TAG, "Write status: %s", esp_err_to_name(ret));

    esp_zb_scheduler_alarm((esp_zb_callback_t)send_value_cb, 0, 5000);
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_ERROR_CHECK(esp_zb_gateway_console_init());
#endif
    xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL);
}

