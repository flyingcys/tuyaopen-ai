#include "ai_agent.h"
#include "tkl_memory.h"
#include "ai_audio.h"
#include <malloc.h>
#include <stdint.h>


#define TY_BIZCODE_AI_CHAT     0x00010001 // 聊天场景可支持打断

static AI_AGENT_SESSION_T sg_ai = {0};
#define TY_AI_CHAT_ID_DS_CNT   4
#define TY_AI_CHAT_ID_DS_AUDIO 1
#define TY_AI_CHAT_ID_DS_VIDEO 3
#define TY_AI_CHAT_ID_DS_TEXT  5
#define TY_AI_CHAT_ID_DS_IMAGE 7
#define TY_AI_CHAT_ID_US_CNT   2
#define TY_AI_CHAT_ID_US_AUDIO 2
#define TY_AI_CHAT_ID_US_TEXT  4


/**
 * @brief Starts the AI audio upload process.
 * @param enable_vad Flag to enable cloud vad.
 * @return OPERATE_RET - OPRT_OK on success, or an error code on failure.
 */
OPERATE_RET ai_audio_agent_upload_start(uint8_t enable_vad)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(AI_AUDIO_DEBUG) && (AI_AUDIO_DEBUG == 1)
    ai_audio_debug_start();
#endif

    PR_DEBUG("tuya ai upload start...");

    // send start event
    memset(sg_ai.event_id, 0, AI_UUID_V4_LEN);

    char attr_asr_enable_vad[64] = {0};
    if (enable_vad) {
        snprintf(attr_asr_enable_vad, sizeof(attr_asr_enable_vad),
                 "{\"asr.enableVad\":true,\"processing.interrupt\":true}");
    } else {
        snprintf(attr_asr_enable_vad, sizeof(attr_asr_enable_vad), "{\"asr.enableVad\":true}");
    }

    AI_ATTRIBUTE_T attr[] = {{
        .type = 1003,
        .payload_type = ATTR_PT_STR,
        .length = strlen(attr_asr_enable_vad),
        .value.str = attr_asr_enable_vad, // tts.order.supports
    }};
    uint8_t *out = NULL;
    uint32_t out_len = 0;
    tuya_pack_user_attrs(attr, CNTSOF(attr), &out, &out_len);
    rt = tuya_ai_event_start(sg_ai.session_id, sg_ai.event_id, out, out_len);
    tal_free(out);
    if (rt) {
        PR_ERR("start event failed, rt:%d", rt);
        return rt;
    }

    sg_ai.is_audio_upload_first_frame = true;
    PR_DEBUG("upload start event_id:%s", sg_ai.event_id);

    return rt;
}

/**
 * @brief Uploads audio data to the AI service.
 * @param data Pointer to the audio data buffer.
 * @param len Length of the audio data in bytes.
 * @return OPERATE_RET - OPRT_OK on success, or an error code on failure.
 */
OPERATE_RET ai_audio_agent_upload_data(uint8_t *data, uint32_t len)
{
    OPERATE_RET rt = OPRT_OK;
    static bool is_first = true;

#if defined(AI_AUDIO_DEBUG) && (AI_AUDIO_DEBUG == 1)
    ai_audio_debug_data((char *)data, len);
#endif

    // send data use tuya_ai_send_biz_pkt, pcm
    AI_BIZ_ATTR_INFO_T attr = {
        .flag = AI_HAS_ATTR,
        .type = AI_PT_AUDIO,
        .value.audio =
            {
                .base.codec_type = AUDIO_CODEC_PCM,
                .base.sample_rate = 16000,
                .base.channels = AUDIO_CHANNELS_MONO,
                .base.bit_depth = 16,
                .option.user_len = 0,
                .option.user_data = NULL,
                .option.session_id_list = NULL,
            },
    };
    AI_BIZ_HEAD_INFO_T head = {
        .value.audio =
            {
                .timestamp = tal_system_get_millisecond(),
                .pts = 0,
            },
        .len = len,
    };

    if (sg_ai.is_audio_upload_first_frame) {
        head.stream_flag = AI_STREAM_START;
        sg_ai.is_audio_upload_first_frame = false;
    } else if (NULL == data) {
        head.stream_flag = AI_STREAM_END;
        sg_ai.is_audio_upload_first_frame = true;
    } else {
        head.stream_flag = AI_STREAM_ING;
    }

    PR_DEBUG("tuya ai upload data[%d][%d]...", head.stream_flag, len);

    TUYA_CALL_ERR_RETURN(tuya_ai_send_biz_pkt(TY_AI_CHAT_ID_DS_AUDIO, &attr, AI_PT_AUDIO, &head, (char *)data));

    return rt;
}

/**
 * @brief Stops the AI audio upload process.
 * @param None
 * @return OPERATE_RET - OPRT_OK on success, or an error code on failure.
 */
OPERATE_RET ai_audio_agent_upload_stop(void)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("tuya ai upload stop...");

#if defined(AI_AUDIO_DEBUG) && (AI_AUDIO_DEBUG == 1)
    ai_audio_debug_stop();
#endif

    TUYA_CALL_ERR_RETURN(ai_audio_agent_upload_data(NULL, 0));

    AI_ATTRIBUTE_T attr[] = {{
        .type = 1002,
        .payload_type = ATTR_PT_U16,
        .length = 2,
        .value.u16 = TY_AI_CHAT_ID_DS_AUDIO,
    }};
    uint8_t *out = NULL;
    uint32_t out_len = 0;
    tuya_pack_user_attrs(attr, CNTSOF(attr), &out, &out_len);
    rt = tuya_ai_event_payloads_end(sg_ai.session_id, sg_ai.event_id, out, out_len);
    tal_free(out);
    if (rt != OPRT_OK) {
        PR_ERR("upload stop failed, rt:%d", rt);
        return rt;
    }

    TUYA_CALL_ERR_RETURN(tuya_ai_event_end(sg_ai.session_id, sg_ai.event_id, NULL, 0));

    return rt;
}

static OPERATE_RET __ai_agent_audio_recv(AI_BIZ_ATTR_INFO_T *attr, AI_BIZ_HEAD_INFO_T *head, char *data, void *usr_data)
{

    if (!head || !attr || !data) {
        PR_ERR("invalid param");
        return OPRT_COM_ERROR;
    }

    if (NULL == sg_ai.cbs.ai_agent_msg_cb) {
        PR_ERR("msg_cb is NULL");
        return OPRT_COM_ERROR;
    }

    uint32_t len = head->len;
    
    switch (head->stream_flag) {
    case AI_STREAM_START: {
        AI_AGENT_MSG_T ai_msg = {
            .type = AI_AGENT_MSG_TP_AUDIO_START,
            .data_len = len,
            .data = (uint8_t *)data,
        };
        sg_ai.cbs.ai_agent_msg_cb(&ai_msg);
    } break;
    case AI_STREAM_ING:
    case AI_STREAM_END: {
        AI_AGENT_MSG_T ai_msg;

        if (len > 0) {
            ai_msg.type = AI_AGENT_MSG_TP_AUDIO_DATA;
            ai_msg.data_len = len;
            ai_msg.data = (uint8_t *)data;

            sg_ai.cbs.ai_agent_msg_cb(&ai_msg);
        }

        if (AI_STREAM_END == head->stream_flag) {
            ai_msg.type = AI_AGENT_MSG_TP_AUDIO_STOP;
            ai_msg.data_len = 0;
            ai_msg.data = NULL;

            sg_ai.cbs.ai_agent_msg_cb(&ai_msg);
        }

    } break;
    default:
        PR_ERR("unknown stream flag: %d", head->stream_flag);
        break;
    }

    return OPRT_OK;
}


static OPERATE_RET _parse_asr(cJSON *json)
{
    cJSON *node;
    AI_AGENT_MSG_T ai_msg = {0};

    node = cJSON_GetObjectItem(json, "data");
    node = cJSON_GetObjectItem(node, "text");
    const char *text = cJSON_GetStringValue(node);
    if (text == NULL || text[0] == '\0') {
        PR_DEBUG("ASR empty");
        ai_msg.data = NULL;
        ai_msg.data_len = 0;
    } else {
        PR_DEBUG("ASR text: %s", text);
        ai_msg.data = (uint8_t *)text;
        ai_msg.data_len = strlen(text);
    }
    ai_msg.type = AI_AGENT_MSG_TP_TEXT_ASR;

    if (sg_ai.cbs.ai_agent_msg_cb) {
        sg_ai.cbs.ai_agent_msg_cb(&ai_msg);
    }

    return OPRT_OK;
}

static OPERATE_RET _parse_nlg(cJSON *json, uint8_t eof)
{
    cJSON *node;
    AI_AGENT_MSG_T ai_msg = {0};

    node = cJSON_GetObjectItem(json, "data");
    node = cJSON_GetObjectItem(node, "content");
    const char *content = cJSON_GetStringValue(node);

    if (AI_AGENT_CHAT_STREAM_START == sg_ai.stream_status) {
        sg_ai.stream_status = AI_AGENT_CHAT_STREAM_DATA;

        ai_msg.type = AI_AGENT_MSG_TP_TEXT_NLG_START;
        ai_msg.data_len = strlen(sg_ai.stream_event_id);
        ai_msg.data = (uint8_t *)sg_ai.stream_event_id;

        if (sg_ai.cbs.ai_agent_msg_cb) {
            sg_ai.cbs.ai_agent_msg_cb(&ai_msg);
        }
    }

    if (AI_AGENT_CHAT_STREAM_DATA == sg_ai.stream_status) {
        ai_msg.type = (eof ? AI_AGENT_MSG_TP_TEXT_NLG_STOP : AI_AGENT_MSG_TP_TEXT_NLG_DATA);
        ai_msg.data_len = strlen(content);
        ai_msg.data = (uint8_t *)content;

        if (sg_ai.cbs.ai_agent_msg_cb) {
            sg_ai.cbs.ai_agent_msg_cb(&ai_msg);
        }

        if (eof) {
            sg_ai.stream_status = AI_AGENT_CHAT_STREAM_STOP;
        }
    }

    return OPRT_OK;
}

typedef struct {
    char *name;
    char *text;
} AI_AUDIO_EMOTION_T;

static OPERATE_RET _parse_skill_emo(cJSON *json)
{
    AI_AUDIO_EMOTION_T ai_emotion = {
        .name = NULL,
        .text = NULL,
    };

    cJSON *emotion = cJSON_GetObjectItem(json, "emotion");
    cJSON *emotion_name = cJSON_GetArrayItem(emotion, 0);
    if (NULL == emotion_name) {
        PR_ERR("emotion is NULL");
        ai_emotion.name = NULL;
    } else {
        PR_DEBUG("emotion name: %s", emotion_name->valuestring);
        ai_emotion.name = emotion_name->valuestring;
    }

    cJSON *emo_text = cJSON_GetObjectItem(json, "text");
    emo_text = cJSON_GetArrayItem(emo_text, 0);
    if (NULL == emo_text) {
        PR_ERR("emo text is NULL");
        ai_emotion.text = NULL;
    } else {
        PR_DEBUG("emo text: %s", emo_text->valuestring);
        ai_emotion.text = emo_text->valuestring;
    }

    AI_AGENT_MSG_T ai_msg = {
        .type = AI_AGENT_MSG_TP_EMOTION,
        .data_len = sizeof(AI_AUDIO_EMOTION_T),
        .data = (uint8_t *)&ai_emotion,
    };

    if (sg_ai.cbs.ai_agent_msg_cb) {
        sg_ai.cbs.ai_agent_msg_cb(&ai_msg);
    }
}

static OPERATE_RET _parse_skill(cJSON *json)
{
    cJSON *node, *code;
    const char *code_str;

    // {"bizId":"xxx","bizType":"SKILL","eof":1,"data":{"code":"emo","skillContent":{"emotion":["NEUTRAL"],"text":["😐"]}}}
    node = cJSON_GetObjectItem(json, "data");
    code = cJSON_GetObjectItem(node, "code");
    code_str = cJSON_GetStringValue(code);
    if (!code_str)
        return OPRT_OK;

    PR_DEBUG("skill code: %s", code_str);
    cJSON *skillContent = cJSON_GetObjectItem(node, "skillContent");

    if (strcmp(code_str, "emo") == 0) {
        _parse_skill_emo(skillContent);
    }

    return OPRT_OK;
}

static OPERATE_RET __ai_agent_txt_recv(AI_BIZ_ATTR_INFO_T *attr, AI_BIZ_HEAD_INFO_T *head, char *data, void *usr_data)
{
    cJSON *json, *node;

    if ((json = cJSON_Parse(data)) == NULL) {
        return OPRT_OK;
    }

    // PR_DEBUG("content: %s", data);

    // parse bizType
    node = cJSON_GetObjectItem(json, "bizType");
    const char *bizType = cJSON_GetStringValue(node);
    // parse eof
    node = cJSON_GetObjectItem(json, "eof");
    uint8_t eof = cJSON_GetNumberValue(node);
    // PR_DEBUG("bizType: %s, eof: %d", bizType, eof);

    if (eof && strcmp(bizType, "ASR") == 0) {
        // parse data.text
        _parse_asr(json);
    } else if (strcmp(bizType, "NLG") == 0) {
        _parse_nlg(json, eof);
    } else if (eof && strcmp(bizType, "SKILL") == 0) {
        _parse_skill(json);
    }

    cJSON_Delete(json);
    return OPRT_OK;
}

static OPERATE_RET __ai_agent_event_recv(AI_EVENT_TYPE type, AI_SESSION_ID session_id, AI_EVENT_ID event_id,
                                         uint8_t *attr, uint32_t attr_len)
{
    PR_DEBUG("recv event type:%d, session_id:%s, event_id:%s", type, session_id, event_id);

    // event type: 0-chat start, 1-chat stop, 2-data finish
    if (type == AI_EVENT_START) {
        // update stream event id
        strncpy(sg_ai.stream_event_id, event_id, AI_UUID_V4_LEN);
        sg_ai.stream_status = AI_AGENT_CHAT_STREAM_START;
    } else if (type == AI_EVENT_PAYLOADS_END) {
        // clear stream event id
    } else if (type == AI_EVENT_END) {
        // stream end
    } else if (type == AI_EVENT_CHAT_BREAK || type == AI_EVENT_SERVER_VAD) {
        if (strcmp(event_id, sg_ai.stream_event_id) != 0) {
            PR_DEBUG("recv chat break or srv vad, but current stream is empty");
            return OPRT_OK;
        } else {
            // clear stream event id
            memset(sg_ai.stream_event_id, 0, AI_UUID_V4_LEN);
        }
    }

    // notify event
    if (sg_ai.cbs.ai_agent_event_cb) {
        sg_ai.cbs.ai_agent_event_cb(type, event_id);
    }

    return OPRT_OK;
}

static OPERATE_RET __ai_agent_session_create(void)
{
    OPERATE_RET rt = OPRT_OK;

    AI_SESSION_CFG_T cfg;

    memset(&cfg, 0, sizeof(AI_SESSION_CFG_T));
    cfg.send_num = TY_AI_CHAT_ID_DS_CNT;
    cfg.send[0].type = AI_PT_AUDIO;
    cfg.send[0].id = TY_AI_CHAT_ID_DS_AUDIO;
    cfg.send[0].get_cb = NULL;
    cfg.send[0].free_cb = NULL;
    cfg.send[1].type = AI_PT_VIDEO;
    cfg.send[1].id = TY_AI_CHAT_ID_DS_VIDEO;
    cfg.send[1].get_cb = NULL;
    cfg.send[1].free_cb = NULL;
    cfg.send[2].type = AI_PT_TEXT;
    cfg.send[2].id = TY_AI_CHAT_ID_DS_TEXT;
    cfg.send[2].get_cb = NULL;
    cfg.send[2].free_cb = NULL;
    cfg.send[3].type = AI_PT_IMAGE;
    cfg.send[3].id = TY_AI_CHAT_ID_DS_IMAGE;
    cfg.send[3].get_cb = NULL;
    cfg.send[3].free_cb = NULL;

    cfg.recv_num = TY_AI_CHAT_ID_US_CNT;
    cfg.recv[1].id = TY_AI_CHAT_ID_US_AUDIO;
    cfg.recv[1].cb = __ai_agent_audio_recv;
    cfg.recv[1].usr_data = NULL;
    cfg.recv[0].id = TY_AI_CHAT_ID_US_TEXT;
    cfg.recv[0].cb = __ai_agent_txt_recv;
    cfg.recv[0].usr_data = NULL;

    cfg.event_cb = __ai_agent_event_recv;

    // 支持的tts格式
    char attr_tts_order[128] = {0};
    snprintf(attr_tts_order, 128,
             "{\"tts.order.supports\":[{\"format\":\"mp3\",\"container\":\"\",\"sampleRate\":16000,\"bitDepth\":\"16\","
             "\"channels\":1}]}");

    AI_ATTRIBUTE_T attr[2] = {{
                                  .type = 1003,
                                  .payload_type = ATTR_PT_U8,
                                  .length = 1,
                                  .value.u8 = 2 // 2表示设备
                              },
                              {
                                  .type = 1004,
                                  .payload_type = ATTR_PT_STR,
                                  .length = strlen(attr_tts_order),
                                  .value.str = attr_tts_order, // tts.order.supports
                              }};
    uint8_t *out = NULL;
    uint32_t out_len = 0;
    tuya_pack_user_attrs(attr, CNTSOF(attr), &out, &out_len);

    memset(sg_ai.session_id, 0, AI_UUID_V4_LEN);
    rt = tuya_ai_biz_crt_session(TY_BIZCODE_AI_CHAT, &cfg, out, out_len, sg_ai.session_id);
    tal_free(out);
    if (rt) {
        PR_ERR("create session failed, rt:%d", rt);
        return rt;
    }
    PR_DEBUG("create session id:%s", sg_ai.session_id);
    return OPRT_OK;
}

static OPERATE_RET __ai_agent_session_destroy(void)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("ai chat session destroy...");

    if (sg_ai.session_id[0] == 0) {
        PR_DEBUG("session id is null, ignore");
        return OPRT_OK;
    }
    TUYA_CALL_ERR_RETURN(tuya_ai_biz_del_session(sg_ai.session_id, AI_CODE_OK));
    memset(sg_ai.session_id, 0, AI_UUID_V4_LEN);

    // TODO: notify session destroy

    return OPRT_OK;
}

static int __ai_agent_session_new(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("ai session is ready...");

    TUYA_CALL_ERR_RETURN(__ai_agent_session_create());

    sg_ai.is_online = TRUE;

    return OPRT_OK;
}

static int __ai_agent_session_close(void *data)
{
    PR_DEBUG("ai session close...session id = %s", sg_ai.session_id);

    sg_ai.is_online = FALSE;

    return OPRT_OK;
}

static OPERATE_RET __ai_agent_init(void *data)
{
    PR_DEBUG("%s...", __func__);

#if defined(AI_AUDIO_DEBUG) && (AI_AUDIO_DEBUG == 1)
    ai_audio_debug_init();
#endif

    tal_event_subscribe(EVENT_AI_SESSION_NEW, "ai_session_new", __ai_agent_session_new, SUBSCRIBE_TYPE_NORMAL);
    tal_event_subscribe(EVENT_AI_SESSION_CLOSE, "ai_session_close", __ai_agent_session_close, SUBSCRIBE_TYPE_NORMAL);

    return tuya_ai_client_init();
}

/**
 * @brief Initializes the AI service module.
 * @param msg_cb Callback function for handling AI messages.
 * @return OPERATE_RET - OPRT_OK on success, or an error code on failure.
 */
OPERATE_RET ai_audio_agent_init(AI_AGENT_CBS_T *cbs)
{
    OPERATE_RET rt = OPRT_OK;

    memset(&sg_ai, 0, sizeof(AI_AGENT_SESSION_T));

    if (cbs) {
        memcpy(&sg_ai.cbs, cbs, sizeof(AI_AGENT_CBS_T));
    }

    PR_DEBUG("ai session wait for mqtt connected...");

    tal_event_subscribe(EVENT_MQTT_CONNECTED, "ai_agent_init", __ai_agent_init, SUBSCRIBE_TYPE_ONETIME);

    return rt;
}


typedef uint8_t AI_AUDIO_WORK_MODE_E;
#define AI_AUDIO_MODE_MANUAL_SINGLE_TALK       1
#define AI_AUDIO_WORK_VAD_FREE_TALK            2
#define AI_AUDIO_WORK_ASR_WAKEUP_SINGLE_TALK   3
#define AI_AUDIO_WORK_ASR_WAKEUP_FREE_TALK     4

typedef enum {
    AI_AUDIO_EVT_NONE,
    AI_AUDIO_EVT_HUMAN_ASR_TEXT,
    AI_AUDIO_EVT_AI_REPLIES_TEXT_START,
    AI_AUDIO_EVT_AI_REPLIES_TEXT_DATA,
    AI_AUDIO_EVT_AI_REPLIES_TEXT_END,
    AI_AUDIO_EVT_AI_REPLIES_EMO,
    AI_AUDIO_EVT_ASR_WAKEUP,
    AI_AUDIO_EVT_ASR_WAKEUP_END,
} AI_AUDIO_EVENT_E;

typedef void (*AI_AUDIO_INFORM_CB)(AI_AUDIO_EVENT_E event, uint8_t *data, uint32_t len, void *arg);

static AI_AUDIO_INFORM_CB sg_ai_audio_inform_cb = NULL;
static AI_AUDIO_WORK_MODE_E sg_ai_audio_work_mode = AI_AUDIO_MODE_MANUAL_SINGLE_TALK;
static bool sg_is_chating = false;

static void __ai_audio_agent_msg_cb(AI_AGENT_MSG_T *msg)
{
    AI_AUDIO_EVENT_E event = AI_AUDIO_EVT_NONE;
    static char *event_id = NULL;

    switch (msg->type) {
    case AI_AGENT_MSG_TP_TEXT_ASR: {
        if (msg->data_len > 0) {
            // ai_audio_cloud_stop_wait_asr();

            event = AI_AUDIO_EVT_HUMAN_ASR_TEXT;

            if(AI_AUDIO_WORK_ASR_WAKEUP_FREE_TALK == sg_ai_audio_work_mode) {
                // ai_audio_input_restart_asr_awake_timer();
            }
        }else {
            // ai_audio_cloud_asr_set_idle(true);
        }
    } break;
    case AI_AGENT_MSG_TP_AUDIO_START: {
        PR_DEBUG("===============AI_AGENT_MSG_TP_AUDIO_START===============");
        // Prepare to play mp3
        if (ai_audio_player_is_playing()) {
            PR_DEBUG("player is playing, stop it first");
            ai_audio_player_stop();
        }
        if(event_id) {
            tkl_system_free(event_id);
            event_id = NULL;
        }

        event_id = tkl_system_malloc(msg->data_len+1);
        if(event_id) {
            memcpy(event_id, msg->data, msg->data_len);
            event_id[msg->data_len] = '\0';
        }

        ai_audio_player_start(event_id);
    } break;
    case AI_AGENT_MSG_TP_AUDIO_DATA: {
        // PR_DEBUG("===============AI_AGENT_MSG_TP_AUDIO_DATA===============");
        // PR_DEBUG("data_len: %d", msg->data_len);
        ai_audio_player_data_write(event_id, msg->data, msg->data_len, 0);

        if(AI_AUDIO_WORK_ASR_WAKEUP_FREE_TALK == sg_ai_audio_work_mode) {
            // ai_audio_input_restart_asr_awake_timer();
        }
    } break;
    case AI_AGENT_MSG_TP_AUDIO_STOP: {
        ai_audio_player_data_write(event_id, msg->data, msg->data_len, 1);

        if(AI_AUDIO_WORK_ASR_WAKEUP_FREE_TALK == sg_ai_audio_work_mode) {
            // ai_audio_input_restart_asr_awake_timer();
        }else if(AI_AUDIO_WORK_ASR_WAKEUP_SINGLE_TALK == sg_ai_audio_work_mode) {
            if (sg_ai_audio_inform_cb) {
                sg_ai_audio_inform_cb(AI_AUDIO_EVT_ASR_WAKEUP_END, NULL, 0, NULL);
            }
        }

        if(event_id) {
            tkl_system_free(event_id);
            event_id = NULL;
        }
    } break;
    case AI_AGENT_MSG_TP_TEXT_NLG_START: {
        event = AI_AUDIO_EVT_AI_REPLIES_TEXT_START;
    } break;
    case AI_AGENT_MSG_TP_TEXT_NLG_DATA: {
        event = AI_AUDIO_EVT_AI_REPLIES_TEXT_DATA;
    } break;
    case AI_AGENT_MSG_TP_TEXT_NLG_STOP: {
        event = AI_AUDIO_EVT_AI_REPLIES_TEXT_END;
    } break;
    case AI_AGENT_MSG_TP_EMOTION: {
        event = AI_AUDIO_EVT_AI_REPLIES_EMO;
    } break;
    default:
        break;
    }

    if (sg_ai_audio_inform_cb && (AI_AUDIO_EVT_NONE != event)) {
        sg_ai_audio_inform_cb(event, msg->data, msg->data_len, NULL);
    }
}

static void __ai_audio_agent_event_cb(AI_EVENT_TYPE event, AI_EVENT_ID event_id)
{
   PR_DEBUG("__ai_audio_agent_event_cb event: %d", event);

    switch (event) {
    case AI_EVENT_START:
    break;

    case AI_EVENT_END:
        sg_is_chating = false;
    break;

    case AI_EVENT_CHAT_BREAK:
        PR_DEBUG("chat break");
        if (ai_audio_player_is_playing()) {
            ai_audio_player_stop();
        }
        sg_is_chating = false;
    break;

    case AI_EVENT_SERVER_VAD:
        PR_DEBUG("chat break");
        if (ai_audio_player_is_playing()) {
            ai_audio_player_stop();
        }
        sg_is_chating = false;

    break;
    }
}

OPERATE_RET ai_audio_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    AI_AGENT_CBS_T agent_cbs;

    TUYA_CALL_ERR_RETURN(ai_audio_player_init());

    agent_cbs.ai_agent_msg_cb = __ai_audio_agent_msg_cb;
    agent_cbs.ai_agent_event_cb = __ai_audio_agent_event_cb;

    TUYA_CALL_ERR_RETURN(ai_audio_agent_init(&agent_cbs));
    // sg_ai_audio_inform_cb = cfg->inform_cb;

    return OPRT_OK;
}

// 添加清理函数
void ai_audio_cleanup(void)
{

}