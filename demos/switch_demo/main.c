#include <stdio.h>
#include <assert.h>

#include "cJSON.h"

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_kv.h"

#include "tkl_output.h"
#include "netmgr.h"

#include "tuya_iot.h"
#include "tuya_iot_dp.h"

/* Tuya device handle */
tuya_iot_client_t client;

#define PROJECT_VERSION         "1.0.0"

extern void example_qrcode_string(const char *string, void (*fputs)(const char *str), int invert);

/**
 * @brief user defined upgrade notify callback, it will notify device a OTA
 * request received
 *
 * @param client device info
 * @param upgrade the upgrade request info
 * @return void
 */
void user_upgrade_notify_on(tuya_iot_client_t *client, cJSON *upgrade)
{
    if (upgrade == NULL) {
        PR_ERR("upgrade parameter is NULL");
        return;
    }
    
    PR_INFO("----- Upgrade information -----");
    
    cJSON *type_item = cJSON_GetObjectItem(upgrade, "type");
    if (type_item && cJSON_IsNumber(type_item)) {
        PR_INFO("OTA Channel: %d", type_item->valueint);
    }
    
    cJSON *version_item = cJSON_GetObjectItem(upgrade, "version");
    if (version_item && cJSON_IsString(version_item) && version_item->valuestring) {
        PR_INFO("Version: %s", version_item->valuestring);
    }
    
    cJSON *size_item = cJSON_GetObjectItem(upgrade, "size");
    if (size_item && cJSON_IsString(size_item) && size_item->valuestring) {
        PR_INFO("Size: %s", size_item->valuestring);
    }
    
    cJSON *md5_item = cJSON_GetObjectItem(upgrade, "md5");
    if (md5_item && cJSON_IsString(md5_item) && md5_item->valuestring) {
        PR_INFO("MD5: %s", md5_item->valuestring);
    }
    
    cJSON *hmac_item = cJSON_GetObjectItem(upgrade, "hmac");
    if (hmac_item && cJSON_IsString(hmac_item) && hmac_item->valuestring) {
        PR_INFO("HMAC: %s", hmac_item->valuestring);
    }
    
    cJSON *url_item = cJSON_GetObjectItem(upgrade, "url");
    if (url_item && cJSON_IsString(url_item) && url_item->valuestring) {
        PR_INFO("URL: %s", url_item->valuestring);
    }
    
    cJSON *https_url_item = cJSON_GetObjectItem(upgrade, "httpsUrl");
    if (https_url_item && cJSON_IsString(https_url_item) && https_url_item->valuestring) {
        PR_INFO("HTTPS URL: %s", https_url_item->valuestring);
    }
}

/**
 * @brief user defined log output api, in this demo, it will use uart0 as log-tx
 *
 * @param str log string
 * @return void
 */
void user_log_output_cb(const char *str)
{
    tkl_log_output(str);
}

/**
 * @brief user defined event handler
 *
 * @param client device info
 * @param event the event info
 * @return void
 */
void user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    PR_DEBUG("Tuya Event ID:%d(%s)", event->id, EVENT_ID2STR(event->id));
    PR_INFO("Device Free heap %d", tal_system_get_free_heap_size());
    switch (event->id) {
    case TUYA_EVENT_BIND_START:
        PR_INFO("Device Bind Start!");
        break;

    /* Print the QRCode for Tuya APP bind */
    case TUYA_EVENT_DIRECT_MQTT_CONNECTED: {
        char buffer[255];
        int ret = snprintf(buffer, sizeof(buffer), "https://smartapp.tuya.com/s/p?p=%s&uuid=%s&v=2.0", TUYA_PRODUCT_ID, TUYA_OPENSDK_UUID);
        if (ret < 0 || ret >= sizeof(buffer)) {
            PR_ERR("Failed to format QR code URL, string too long");
            break;
        }
        example_qrcode_string(buffer, user_log_output_cb, 0);
    } break;

    /* MQTT with tuya cloud is connected, device online */
    case TUYA_EVENT_MQTT_CONNECTED:
        PR_INFO("Device MQTT Connected!");
        break;

    /* RECV upgrade request */
    case TUYA_EVENT_UPGRADE_NOTIFY:
        user_upgrade_notify_on(client, event->value.asJSON);
        break;

    /* Sync time with tuya Cloud */
    case TUYA_EVENT_TIMESTAMP_SYNC:
        PR_INFO("Sync timestamp:%d", event->value.asInteger);
        tal_time_set_posix(event->value.asInteger, 1);
        break;
    case TUYA_EVENT_RESET:
        PR_INFO("Device Reset:%d", event->value.asInteger);
        break;

    /* RECV OBJ DP */
    case TUYA_EVENT_DP_RECEIVE_OBJ: {
        dp_obj_recv_t *dpobj = event->value.dpobj;
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d CNT:%u", dpobj->cmd_tp, dpobj->dtt_tp, dpobj->dpscnt);
        if (dpobj->devid != NULL) {
            PR_DEBUG("devid.%s", dpobj->devid);
        }

        uint32_t index = 0;
        for (index = 0; index < dpobj->dpscnt; index++) {
            dp_obj_t *dp = dpobj->dps + index;
            PR_DEBUG("idx:%d dpid:%d type:%d ts:%u", index, dp->id, dp->type, dp->time_stamp);
            switch (dp->type) {
            case PROP_BOOL: {
                PR_DEBUG("bool value:%d", dp->value.dp_bool);
                break;
            }
            case PROP_VALUE: {
                PR_DEBUG("int value:%d", dp->value.dp_value);
                break;
            }
            case PROP_STR: {
                PR_DEBUG("str value:%s", dp->value.dp_str);
                break;
            }
            case PROP_ENUM: {
                PR_DEBUG("enum value:%u", dp->value.dp_enum);
                break;
            }
            case PROP_BITMAP: {
                PR_DEBUG("bits value:0x%X", dp->value.dp_bitmap);
                break;
            }
            default: {
                PR_ERR("idx:%d dpid:%d type:%d ts:%u is invalid", index, dp->id, dp->type, dp->time_stamp);
                break;
            }
            } // end of switch
        }

        tuya_iot_dp_obj_report(client, dpobj->devid, dpobj->dps, dpobj->dpscnt, 0);

    } break;

    /* RECV RAW DP */
    case TUYA_EVENT_DP_RECEIVE_RAW: {
        dp_raw_recv_t *dpraw = event->value.dpraw;
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d", dpraw->cmd_tp, dpraw->dtt_tp);
        if (dpraw->devid != NULL) {
            PR_DEBUG("devid.%s", dpraw->devid);
        }

        uint32_t index = 0;
        dp_raw_t *dp = &dpraw->dp;
        PR_DEBUG("dpid:%d type:RAW len:%d data:", dp->id, dp->len);
        for (index = 0; index < dp->len; index++) {
            PR_DEBUG_RAW("%02x", dp->data[index]);
        }

        tuya_iot_dp_raw_report(client, dpraw->devid, &dpraw->dp, 3);

    } break;

        /* TBD.. add other event if necessary */

    default:
        break;
    }
}


/**
 * @brief user defined network check callback, it will check the network every
 * 1sec, in this demo it alwasy return ture due to it's a wired demo
 *
 * @return true
 * @return false
 */
bool user_network_check(void)
{
    netmgr_status_e status = NETMGR_LINK_DOWN;
    netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &status);
    return status == NETMGR_LINK_DOWN ? false : true;
}
int main(int argc, char *argv[])
{
    OPERATE_RET rt = OPRT_OK;
    tuya_iot_license_t license;
    
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_malloc, .free_fn = tal_free});

    /* basic init */
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);
    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init();

    license.uuid = TUYA_OPENSDK_UUID;
    license.authkey = TUYA_OPENSDK_AUTHKEY;

    /* Initialize Tuya device configuration */
    rt = tuya_iot_init(&client, &(const tuya_iot_config_t){
                                     .software_ver = PROJECT_VERSION,
                                     .productkey = TUYA_PRODUCT_ID,
                                     .uuid = license.uuid,
                                     .authkey = license.authkey,
                                     .event_handler = user_event_handler_on,
                                     .network_check = user_network_check,
                                 });
    if (OPRT_OK != rt) {
        PR_ERR("tuya_iot_init error:%d", rt);
        return -1;
    }

    // network init
    netmgr_type_e type = 0;
    #if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
        type |= NETCONN_WIFI;
    #endif
    #if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
        type |= NETCONN_WIRED;
    #endif

    netmgr_init(type);

    PR_DEBUG("tuya_iot_init success");
    /* Start tuya iot task */
    tuya_iot_start(&client);

    for (;;) {
        /* Loop to receive packets, and handles client keepalive */
        tuya_iot_yield(&client);
    }

    return 0;
}