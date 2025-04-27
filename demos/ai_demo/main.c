#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

#include "cJSON.h"

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_kv.h"

#include "tkl_output.h"
#include "netmgr.h"

#include "tuya_iot.h"
#include "tuya_iot_dp.h"

#include "ai_audio.h"
#include "audio/alsa.h"
#include "audio/wav_writer.h"

/* Tuya device handle */
tuya_iot_client_t client;

// 录音相关全局变量
static pthread_t record_thread;
static volatile bool recording = false;
static volatile bool record_thread_running = false;
static wav_writer_t *wav_writer = NULL;

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

#define DPID_VOLUME 3

OPERATE_RET ai_audio_status_upload(void)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    dp_obj_t dp_obj = {0};

    // uint8_t volume = ai_audio_get_volume();
    uint8_t volume = 100;

    dp_obj.id = DPID_VOLUME;
    dp_obj.type = PROP_VALUE;
    dp_obj.value.dp_value = volume;

    PR_DEBUG("DP upload volume:%d", volume);

    return tuya_iot_dp_obj_report(client, client->activate.devid, &dp_obj, 1, 0);
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
        tal_event_publish(EVENT_MQTT_CONNECTED, NULL);

        static uint8_t first = 1;
        if (first) {
            first = 0;
#if defined(ENABLE_CHAT_DISPLAY) && (ENABLE_CHAT_DISPLAY == 1)
            app_display_send_msg(TY_DISPLAY_TP_STATUS, (uint8_t *)CONNECTED_TO, strlen(CONNECTED_TO));
            app_system_info_loop_start();
#endif
            ai_audio_player_play_alert(AI_AUDIO_ALERT_NETWORK_CONNECTED);
            ai_audio_status_upload();
        }

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

// 音频数据上传回调函数
static void upload_audio_data(const int16_t *data, size_t frames)
{
    // 这里实现音频数据上传逻辑
    // 可以通过Tuya DP或者其他方式上传
    // printf("Uploading %zu frames of audio data...\n", frames);
    
    // 示例：可以将音频数据转换为base64并通过DP上传
    // 或者发送到云端进行语音识别等处理
    
    // 简单打印前几个样本值作为示例
    // printf("Audio samples: ");
    // for (size_t i = 0; i < (frames > 10 ? 10 : frames); i++) {
    //     printf("%d ", data[i]);
    // }
    // printf("...\n");
    
    size_t channels = 1;
    size_t samples = frames * channels;
    size_t bytes_to_write = samples * sizeof(int16_t);
    ai_audio_agent_upload_data((uint8_t *)data, bytes_to_write);

    // 同时保存到WAV文件
    if (wav_writer) {
        if (wav_writer_write(wav_writer, data, frames) < 0) {
            printf("Warning: Failed to write to WAV file\n");
        }
    }
}

// 录音线程函数
static void* record_thread_func(void *arg)
{
    const size_t frames_per_100ms = 1600; // 16kHz * 0.1s = 1600 frames
    int16_t *buffer = malloc(frames_per_100ms * sizeof(int16_t));
    
    if (!buffer) {
        printf("Failed to allocate record buffer\n");
        return NULL;
    }
    
    // 生成时间戳文件名
    char *filename = get_timestamp_filename("record", "wav");
    printf("Creating WAV file: %s\n", filename);
    
    // 创建WAV文件
    wav_writer = wav_writer_open(filename, 16000, 1, 16);
    if (!wav_writer) {
        printf("Failed to create WAV file: %s\n", filename);
        free(buffer);
        return NULL;
    }
    
    // 初始化录音设备：单声道，16kHz，16bit
    if (alsa_record_open(1, 16000, 16) < 0) {
        printf("Failed to open record device\n");
        wav_writer_close(wav_writer);
        wav_writer = NULL;
        free(buffer);
        return NULL;
    }
    
    // 启动AI agent音频上传
    if (ai_audio_agent_upload_start(true) != OPRT_OK) {
        printf("Failed to start AI audio agent upload\n");
        alsa_record_close();
        wav_writer_close(wav_writer);
        wav_writer = NULL;
        free(buffer);
        return NULL;
    }
    
    printf("Recording started (16bit, 16kHz, mono). Press 'p' to stop.\n");
    record_thread_running = true;
    
    while (recording) {
        // 读取100ms的音频数据
        int read_frames = alsa_record_read(buffer, frames_per_100ms);
        if (read_frames > 0) {
            // 上传音频数据（包含WAV文件写入）
            upload_audio_data(buffer, read_frames);
        } else if (read_frames < 0) {
            printf("Record error occurred\n");
            break;
        }
        
        // 检查是否需要停止
        if (!recording) {
            break;
        }
    }
    
    alsa_record_close();
    
    // 关闭WAV文件
    if (wav_writer) {
        wav_writer_close(wav_writer);
        wav_writer = NULL;
    }
    
    free(buffer);
    record_thread_running = false;
    printf("Recording stopped\n");
    
    return NULL;
}

// 开始录音
static int start_recording(void)
{
    if (recording) {
        printf("Recording is already in progress\n");
        return -1;
    }
    
    recording = true;
    
    if (pthread_create(&record_thread, NULL, record_thread_func, NULL) != 0) {
        printf("Failed to create record thread\n");
        recording = false;
        return -1;
    }
    
    return 0;
}

// 停止录音
static void stop_recording(void)
{
    if (!recording) {
        return;
    }
    
    recording = false;
    
    // 等待录音线程结束
    if (record_thread_running) {
        pthread_join(record_thread, NULL);
    }
    
    // 停止AI agent音频上传
    ai_audio_agent_upload_stop();
}

// 检查键盘输入（非阻塞）
static int check_keyboard_input(void)
{
    int ch = 0;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    ch = getchar();
    
    fcntl(STDIN_FILENO, F_SETFL, flags);
    return ch;
}

// 处理用户输入
static void handle_user_input(int ch)
{
    switch (ch) {
    case 's':
    case 'S':
        printf("Starting recording...\n");
        start_recording();
        break;
    case 'p':
    case 'P':
        printf("Stopping recording...\n");
        stop_recording();
        break;
    case 'q':
    case 'Q':
        printf("Exiting...\n");
        stop_recording();
        exit(0);
        break;
    default:
        break;
    }
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

    app_audio_driver_init(AUDIO_DRIVER_NAME);

    // 确保音频初始化完成后再启动IoT服务
    rt = ai_audio_init();
    if (OPRT_OK != rt) {
        PR_ERR("ai_audio_init error:%d", rt);
        return -1;
    }
    PR_DEBUG("ai_audio_init success");
    
    PR_DEBUG("tuya_iot_init success");
    /* Start tuya iot task */
    tuya_iot_start(&client);

    // 打印使用说明
    printf("\n=== Audio Record Control ===\n");
    printf("Commands:\n");
    printf("  's' - Start recording (16bit, 16kHz, mono PCM)\n");
    printf("  'p' - Stop recording\n");
    printf("  'q' - Quit program\n");
    printf("============================\n\n");

    for (;;) {
        /* Loop to receive packets, and handles client keepalive */
        tuya_iot_yield(&client);
        
        // 检查键盘输入
        int ch = check_keyboard_input();
        if (ch != EOF && ch != -1) {
            handle_user_input(ch);
        }
        
        // 短暂休眠避免CPU占用过高
        usleep(10000); // 10ms
    }

    return 0;
}