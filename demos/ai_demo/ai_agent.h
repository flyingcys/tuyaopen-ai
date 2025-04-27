#ifndef __AI_AGENT_H__
#define __AI_AGENT_H__

#include "tuya_cloud_types.h"

#include  "tal_api.h"

#include "tuya_ai_protocol.h"
#include "tuya_ai_biz.h"
#include "tuya_ai_client.h"


typedef enum {
    AI_AGENT_CHAT_STREAM_START,
    AI_AGENT_CHAT_STREAM_DATA,
    AI_AGENT_CHAT_STREAM_STOP,
    AI_AGENT_CHAT_STREAM_ABORT,
} AI_AGENT_CHAT_STREAM_E;

typedef enum {
    AI_AGENT_MSG_TP_TEXT_ASR,       
    AI_AGENT_MSG_TP_TEXT_NLG_START, 
    AI_AGENT_MSG_TP_TEXT_NLG_DATA,  
    AI_AGENT_MSG_TP_TEXT_NLG_STOP,  
    AI_AGENT_MSG_TP_AUDIO_START,    
    AI_AGENT_MSG_TP_AUDIO_DATA,     
    AI_AGENT_MSG_TP_AUDIO_STOP,     
    AI_AGENT_MSG_TP_EMOTION,        
} AI_AGENT_MSG_TYPE_E;

typedef struct {
    AI_AGENT_MSG_TYPE_E type;
    uint32_t data_len;
    uint8_t *data;
} AI_AGENT_MSG_T;

typedef struct {
    void (*ai_agent_msg_cb)(AI_AGENT_MSG_T *msg);
    void (*ai_agent_event_cb)(AI_EVENT_TYPE event, AI_EVENT_ID event_id);
} AI_AGENT_CBS_T;

typedef struct {
    uint8_t                  is_online;
    char                     session_id[AI_UUID_V4_LEN];
    char                     event_id[AI_UUID_V4_LEN];
    char                     stream_event_id[AI_UUID_V4_LEN];
    AI_AGENT_CBS_T           cbs;
    AI_AGENT_CHAT_STREAM_E   stream_status;
    bool                     is_audio_upload_first_frame;
} AI_AGENT_SESSION_T;

#endif /* __AI_AGENT_H__ */
