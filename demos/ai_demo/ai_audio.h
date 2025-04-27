#ifndef __AI_AUDIO_H__
#define __AI_AUDIO_H__

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "tuya_cloud_types.h"

#include "tal_api.h"
#include "tuya_ringbuf.h"
#include "tkl_memory.h"
#include "tkl_thread.h"

#include "tdl_audio_manage.h"
#include "ai_media_alert.h"

// 函数声明
OPERATE_RET ai_audio_init(void);
void ai_audio_cleanup(void);
OPERATE_RET ai_audio_player_init(void);
OPERATE_RET ai_audio_player_start(char *id);
OPERATE_RET ai_audio_player_stop(void);
OPERATE_RET ai_audio_player_data_write(char *id, uint8_t *data, uint32_t len, uint8_t is_eof);
uint8_t ai_audio_player_is_playing(void);

#endif /* __AI_AUDIO_H__ */