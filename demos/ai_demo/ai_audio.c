#define MINIMP3_IMPLEMENTATION

#include "ai_audio.h"

#include "minimp3.h"

#define MP3_STREAM_BUFF_MAX_LEN (1024 * 64 * 2)

#define MAINBUF_SIZE 1940

#define MAX_NGRAN 2   /* max granules */
#define MAX_NCHAN 2   /* max channels */
#define MAX_NSAMP 576 /* max samples per channel, per granule */

#define MP3_PCM_SIZE_MAX           (MAX_NSAMP * MAX_NCHAN * MAX_NGRAN * 2)
#define PLAYING_NO_DATA_TIMEOUT_MS (5 * 1000)

#define TY_RINGBUF_PSRAM_FLAG 0x80
#define OVERFLOW_PSRAM_STOP_TYPE (OVERFLOW_STOP_TYPE | TY_RINGBUF_PSRAM_FLAG)
#define OVERFLOW_PSRAM_COVERAGE_TYPE (OVERFLOW_COVERAGE_TYPE | TY_RINGBUF_PSRAM_FLAG)

#define AI_AUDIO_PLAYER_STAT_CHANGE(last_stat, new_stat)                              \
    do {                                                                              \
        if(last_stat != new_stat) {                                                   \
            PR_DEBUG("ai audio player stat changed: %d->%d", last_stat, new_stat);    \
        }                                                                             \
    } while (0)     
    

uint8_t ai_audio_player_is_playing(void);

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
void* tkl_system_psram_malloc(const size_t size)
{
    return malloc(size);
}

void tkl_system_psram_free(void *ptr)
{
    free(ptr);
}

typedef enum {
    AI_AUDIO_PLAYER_STAT_IDLE = 0,
    AI_AUDIO_PLAYER_STAT_START,
    AI_AUDIO_PLAYER_STAT_PLAY,
    AI_AUDIO_PLAYER_STAT_FINISH,
    AI_AUDIO_PLAYER_STAT_PAUSE,
    AI_AUDIO_PLAYER_STAT_MAX,
} AI_AUDIO_PLAYER_STATE_E;

typedef struct {
    bool                    is_playing;
    bool                    is_writing;
    bool                    is_initialized;    // 添加初始化标志
    AI_AUDIO_PLAYER_STATE_E stat;

    TDL_AUDIO_HANDLE_T      audio_hdl;
    MUTEX_HANDLE            mutex;
    THREAD_HANDLE           thrd_hdl;

    char                   *id;
    TUYA_RINGBUFF_T         rb_hdl;
    MUTEX_HANDLE            spk_rb_mutex;
    uint8_t                 is_eof;
    TIMER_ID                tm_id;

    mp3dec_t               *mp3_dec;
    mp3dec_frame_info_t     mp3_frame_info;
    uint8_t                *mp3_raw;
    uint8_t                *mp3_raw_head;
    uint32_t                mp3_raw_used_len;
    uint8_t                *mp3_pcm; // mp3 decode to pcm buffer

} APP_PLAYER_T;

static APP_PLAYER_T sg_player;

#define GET_MIN_LEN(a, b) ((a) < (b) ? (a) : (b))

static OPERATE_RET __ai_audio_player_mp3_start(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == sg_player.mp3_dec) {
        sg_player.mp3_dec = (mp3dec_t *)tkl_system_psram_malloc(sizeof(mp3dec_t));
        if (NULL == sg_player.mp3_dec) {
            PR_ERR("malloc mp3dec_t failed");
            return OPRT_MALLOC_FAILED;
        }

        mp3dec_init(sg_player.mp3_dec);
    }

    sg_player.mp3_raw_used_len = 0;

    return rt;
}

static OPERATE_RET __ai_audio_player_mp3_playing(void)
{
    OPERATE_RET rt = OPRT_OK;
    APP_PLAYER_T *ctx = &sg_player;

    if (NULL == ctx->mp3_dec) {
        PR_ERR("mp3 decoder is NULL");
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(sg_player.spk_rb_mutex);
    uint32_t rb_used_len = tuya_ring_buff_used_size_get(ctx->rb_hdl);
    tal_mutex_unlock(sg_player.spk_rb_mutex);
    if (0 == rb_used_len && 0 == ctx->mp3_raw_used_len) {
        // PR_DEBUG("mp3 data is empty");
        rt = OPRT_RECV_DA_NOT_ENOUGH;
        goto __EXIT;
    }

    if (NULL != ctx->mp3_raw_head && ctx->mp3_raw_used_len > 0 && ctx->mp3_raw_head != ctx->mp3_raw) {
        // PR_DEBUG("move data, offset=%d, used_len=%d", ctx->mp3_raw_head - ctx->mp3_raw, ctx->mp3_raw_used_len);
        memmove(ctx->mp3_raw, ctx->mp3_raw_head, ctx->mp3_raw_used_len);
    }
    ctx->mp3_raw_head = ctx->mp3_raw;

    // read new data
    if (rb_used_len > 0 && ctx->mp3_raw_used_len < MAINBUF_SIZE) {
        uint32_t read_len = ((MAINBUF_SIZE - ctx->mp3_raw_used_len) > rb_used_len) ?\
                             rb_used_len : (MAINBUF_SIZE - ctx->mp3_raw_used_len);
          
        tal_mutex_lock(sg_player.spk_rb_mutex);                     
        uint32_t rt_len = tuya_ring_buff_read(ctx->rb_hdl, ctx->mp3_raw + ctx->mp3_raw_used_len, read_len);
        tal_mutex_unlock(sg_player.spk_rb_mutex);  
        //  PR_DEBUG("read_len=%d rt_len: %d", read_len, rt_len);

        ctx->mp3_raw_used_len += rt_len;
    }

    int samples = mp3dec_decode_frame(ctx->mp3_dec, ctx->mp3_raw_head, ctx->mp3_raw_used_len,
                                      (mp3d_sample_t *)ctx->mp3_pcm, &ctx->mp3_frame_info);
    if (samples == 0) {
        ctx->mp3_raw_used_len = 0;
        ctx->mp3_raw_head = ctx->mp3_raw;
        rt = OPRT_COM_ERROR;
        goto __EXIT;
    }

    static alsa_init = 0;
    
    if (!alsa_init) {
        PR_DEBUG("frame info bitrate: %d hz: %d channel: %d", ctx->mp3_frame_info.bitrate_kbps, ctx->mp3_frame_info.hz, ctx->mp3_frame_info.channels);
        if (alsa_device_open(ctx->mp3_frame_info.channels, ctx->mp3_frame_info.hz, 16) < 0) {
            PR_ERR("alsa open failed...");
        }
        alsa_init = 1;
    }

    ctx->mp3_raw_used_len -= ctx->mp3_frame_info.frame_bytes;
    ctx->mp3_raw_head += ctx->mp3_frame_info.frame_bytes;

    // 对于单声道MP3，mp3dec_decode_frame返回的samples就是帧数
    // 对于立体声MP3，mp3dec_decode_frame返回的samples是每声道的样本数，也等于帧数
    // 所以samples直接就是ALSA需要的帧数
    // PR_DEBUG("Writing %d frames to ALSA (channels: %d)", samples, ctx->mp3_frame_info.channels);
    
    // 写入ALSA设备
    int write_result = alsa_device_write((int16_t *)ctx->mp3_pcm, samples);
    if (write_result < 0) {
        PR_ERR("ALSA write failed: %d", write_result);
    } else {
        // PR_DEBUG("Successfully wrote %d frames", write_result);
    }

__EXIT:
    return rt;
}

static OPERATE_RET __ai_audio_player_mp3_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("app player mp3 init...");

    sg_player.mp3_raw = (uint8_t *)tkl_system_psram_malloc(MAINBUF_SIZE);
    TUYA_CHECK_NULL_GOTO(sg_player.mp3_raw, __ERR);

    sg_player.mp3_pcm = (uint8_t *)tkl_system_psram_malloc(MP3_PCM_SIZE_MAX);
    TUYA_CHECK_NULL_GOTO(sg_player.mp3_pcm, __ERR);

    return rt;

__ERR:
    if (sg_player.mp3_pcm) {
        tkl_system_psram_free(sg_player.mp3_pcm);
        sg_player.mp3_pcm = NULL;
    }

    if (sg_player.mp3_raw) {
        tkl_system_psram_free(sg_player.mp3_raw);
        sg_player.mp3_raw = NULL;
    }

    return OPRT_COM_ERROR;
}

static void __ai_audio_player_task(void *arg)
{
    OPERATE_RET rt = OPRT_OK;
    APP_PLAYER_T *ctx = &sg_player;
    static AI_AUDIO_PLAYER_STATE_E last_state = 0xFF;
    uint32_t sleep_ms = 10;

    ctx->stat = AI_AUDIO_PLAYER_STAT_IDLE;

    for (;;) {
        tal_mutex_lock(sg_player.mutex);

        AI_AUDIO_PLAYER_STAT_CHANGE(last_state, ctx->stat);
        last_state = ctx->stat;

        switch (ctx->stat) {
        case AI_AUDIO_PLAYER_STAT_IDLE: {
            if (tal_sw_timer_is_running(ctx->tm_id)) {
                tal_sw_timer_stop(ctx->tm_id);
            }
            ctx->is_eof     = 0;
        } break;
        case AI_AUDIO_PLAYER_STAT_START: {
            rt = __ai_audio_player_mp3_start();
            if (rt != OPRT_OK) {
                ctx->stat = AI_AUDIO_PLAYER_STAT_IDLE;
            } else {
                ctx->stat = AI_AUDIO_PLAYER_STAT_PLAY;
                sleep_ms = 0;
            }
        } break;
        case AI_AUDIO_PLAYER_STAT_PLAY: {
            rt = __ai_audio_player_mp3_playing();
            if (OPRT_RECV_DA_NOT_ENOUGH == rt) {
                tal_sw_timer_start(ctx->tm_id, PLAYING_NO_DATA_TIMEOUT_MS, TAL_TIMER_ONCE);
            } else if (OPRT_OK == rt) {
                if (tal_sw_timer_is_running(ctx->tm_id)) {
                    tal_sw_timer_stop(ctx->tm_id);
                }
            }
            tal_mutex_lock(ctx->spk_rb_mutex);
            uint32_t rb_used_len = tuya_ring_buff_used_size_get(ctx->rb_hdl);
            tal_mutex_unlock(ctx->spk_rb_mutex);
            if (rb_used_len == 0 && 0 == ctx->mp3_raw_used_len && ctx->is_eof) {
                PR_DEBUG("app player end");
                ctx->stat = AI_AUDIO_PLAYER_STAT_FINISH;
                sleep_ms = 10;
            }
        } break;
        case AI_AUDIO_PLAYER_STAT_FINISH: {
            tal_sw_timer_stop(ctx->tm_id);

            ctx->is_playing = false;
            ctx->stat       = AI_AUDIO_PLAYER_STAT_IDLE;
            ctx->is_eof     = 0;
        } break;
        case AI_AUDIO_PLAYER_STAT_PAUSE:
            // do nothing
        break;
        default:
            break;
        }

        tal_mutex_unlock(sg_player.mutex);

        tal_system_sleep(sleep_ms);
    }
}

static void __app_playing_tm_cb(TIMER_ID timer_id, void *arg)
{
    PR_DEBUG("app player timeout cb, stop playing");
    tal_mutex_lock(sg_player.mutex);
    sg_player.stat = AI_AUDIO_PLAYER_STAT_FINISH;
    tal_mutex_unlock(sg_player.mutex);
    return;
}


/**
 * @brief Starts the audio player with the specified identifier.
 *
 *
 * @param id        The identifier for the current playback session. 
 *                  If NULL, no specific ID is set.
 * 
 * @return          Returns OPRT_OK if the player is successfully started.
 */
OPERATE_RET ai_audio_player_start(char *id)
{
    if (!sg_player.is_initialized || sg_player.mutex == NULL) {
        PR_ERR("Audio player not initialized yet, cannot start");
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(sg_player.mutex);
 
    if(true == sg_player.is_playing) {
        PR_NOTICE("player is already start");
        tal_mutex_unlock(sg_player.mutex);
        return OPRT_OK;
    }

    if(sg_player.id) {
        tkl_system_free(sg_player.id);
        sg_player.id = NULL;
    }

    if(id) {
        size_t id_len = strlen(id);
        if (id_len == 0) {
            PR_ERR("Empty id string provided");
            tal_mutex_unlock(sg_player.mutex);
            return OPRT_INVALID_PARM;
        }
        if (id_len > 256) {
            PR_ERR("ID string too long: %zu", id_len);
            tal_mutex_unlock(sg_player.mutex);
            return OPRT_INVALID_PARM;
        }
        
        sg_player.id = tkl_system_malloc(id_len + 1);
        if(sg_player.id == NULL) {
            PR_ERR("Failed to allocate memory for player id (size: %zu)", id_len + 1);
            tal_mutex_unlock(sg_player.mutex);
            return OPRT_MALLOC_FAILED;
        }
        
        // Validate the returned pointer
        if ((uintptr_t)sg_player.id < 0x1000) {
            PR_ERR("Invalid malloc result: %p (size: %zu)", sg_player.id, id_len + 1);
            tkl_system_free(sg_player.id);
            sg_player.id = NULL;
            tal_mutex_unlock(sg_player.mutex);
            return OPRT_MALLOC_FAILED;
        }
        
        strncpy(sg_player.id, id, id_len);
        sg_player.id[id_len] = '\0';
        
        PR_DEBUG("Successfully allocated and copied id: %s", sg_player.id);
    } else {
        PR_DEBUG("id is NULL, skipping allocation");
    }

    sg_player.is_playing = true;
    sg_player.stat = AI_AUDIO_PLAYER_STAT_START;

    tal_mutex_unlock(sg_player.mutex);

    PR_NOTICE("ai audio player start");

    return OPRT_OK;
}

static bool __app_player_compare_id(char *id_1, char *id_2)
{

    if(NULL == id_1 && NULL == id_2) {
        return true;
    }

    if(id_1 && id_2) {
        if(0 == strcmp(id_1, id_2)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Writes audio data to the ring buffer and sets the end-of-file flag if necessary.
 * 
 * @param id        The identifier to validate against the current player's ID.
 * @param data      Pointer to the audio data to be written into the buffer.
 * @param len       Length of the audio data to be written.
 * @param is_eof    Flag indicating whether this block of data is the end of the stream (1 for true, 0 for false).
 * 
 * @return          Returns OPRT_OK if the data was successfully written to the buffer, otherwise returns an error code.
 */
OPERATE_RET ai_audio_player_data_write(char *id, uint8_t *data, uint32_t len, uint8_t is_eof)
{
    uint32_t write_len = 0, alreay_write_len = 0;

    tal_mutex_lock(sg_player.mutex);

    if (AI_AUDIO_PLAYER_STAT_PLAY != sg_player.stat &&\
        AI_AUDIO_PLAYER_STAT_START != sg_player.stat){
        tal_mutex_unlock(sg_player.mutex);
        return OPRT_COM_ERROR;
    }

    if(false == __app_player_compare_id(id, sg_player.id)) {
        PR_NOTICE("the id:%s is not match... curr id:%s", id, sg_player.id);
        tal_mutex_unlock(sg_player.mutex);
        return OPRT_INVALID_PARM; 
    }


    if (NULL != data && len > 0) {    
        while((alreay_write_len < len) && \
              (AI_AUDIO_PLAYER_STAT_PLAY == sg_player.stat ||\
               AI_AUDIO_PLAYER_STAT_START == sg_player.stat)) {

            sg_player.is_writing = true;
            tal_mutex_lock(sg_player.spk_rb_mutex);
            uint32_t rb_free_len = tuya_ring_buff_free_size_get(sg_player.rb_hdl);
            tal_mutex_unlock(sg_player.spk_rb_mutex);
            // PR_DEBUG("rb_feee_len: %d", rb_free_len);
            if(0 == rb_free_len) {
                //need unlock mutex before sleep
                tal_mutex_unlock(sg_player.mutex);
                tal_system_sleep(3);
                tal_mutex_lock(sg_player.mutex);
                continue;
            }
    
            write_len = GET_MIN_LEN(rb_free_len, (len - alreay_write_len));
    
            tal_mutex_lock(sg_player.spk_rb_mutex);
            tuya_ring_buff_write(sg_player.rb_hdl, data + alreay_write_len, write_len);
            tal_mutex_unlock(sg_player.spk_rb_mutex);
    
            alreay_write_len += write_len;
        };
        sg_player.is_writing = false;
    }

    sg_player.is_eof = is_eof;
    tal_mutex_unlock(sg_player.mutex);

    return OPRT_OK;
}

/**
 * @brief Stops the audio player and clears the audio output buffer.
 *
 * @param None
 * @return OPERATE_RET - Returns OPRT_OK if the player is successfully stopped, otherwise returns an error code.
 */
OPERATE_RET ai_audio_player_stop(void)
{
    OPERATE_RET rt = OPRT_OK;

    tal_mutex_lock(sg_player.mutex);

    if (false == sg_player.is_playing) {
        tal_mutex_unlock(sg_player.mutex);
        return OPRT_OK;
    }

    sg_player.stat = AI_AUDIO_PLAYER_STAT_PAUSE;

    if(sg_player.id) {
        tkl_system_free(sg_player.id);
        sg_player.id = NULL;
    }

    while(sg_player.is_writing) {
        tal_mutex_unlock(sg_player.mutex);
        tal_system_sleep(3);
        tal_mutex_lock(sg_player.mutex);
    }

    tal_mutex_lock(sg_player.spk_rb_mutex);
    tuya_ring_buff_reset(sg_player.rb_hdl);
    tal_mutex_unlock(sg_player.spk_rb_mutex);

    tdl_audio_play_stop(sg_player.audio_hdl);

    sg_player.is_playing = false;
    sg_player.stat = AI_AUDIO_PLAYER_STAT_IDLE;

    tal_mutex_unlock(sg_player.mutex);

    PR_NOTICE("ai audio player stop");

    return rt;
}

/**
 * @brief Plays an alert sound based on the specified alert type.
 *
 * @param type - The type of alert to play, defined by the APP_ALERT_TYPE_E enum.
 * @return OPERATE_RET - Returns OPRT_OK if the alert sound is successfully played, otherwise returns an error code.
 */
OPERATE_RET ai_audio_player_play_alert(AI_AUDIO_ALERT_TYPE_E type)
{
    OPERATE_RET rt = OPRT_OK;
    char alert_id[64] = {0};

    if (type < 0 || type >= 32768) {
        PR_ERR("Invalid alert type: %d", type);
        return OPRT_INVALID_PARM;
    }

    int ret = snprintf(alert_id, sizeof(alert_id), "alert_%d", type);
    if (ret < 0 || ret >= sizeof(alert_id)) {
        PR_ERR("Failed to format alert_id for type: %d", type);
        return OPRT_COM_ERROR;
    }
    
    rt = ai_audio_player_start(alert_id);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to start audio player: %d", rt);
        return rt;
    }
    
    uint8_t *audio_data = NULL;
    size_t audio_size = 0;
    
    switch (type) {
    case AI_AUDIO_ALERT_POWER_ON:
        audio_data = (uint8_t *)media_src_power_on;
        audio_size = sizeof(media_src_power_on);
        break;
    case AI_AUDIO_ALERT_NOT_ACTIVE:
        audio_data = (uint8_t *)media_src_not_active;
        audio_size = sizeof(media_src_not_active);
        break;
    case AI_AUDIO_ALERT_NETWORK_CFG:
        audio_data = (uint8_t *)media_src_netcfg_mode;
        audio_size = sizeof(media_src_netcfg_mode);
        break;
    case AI_AUDIO_ALERT_NETWORK_CONNECTED:
        audio_data = (uint8_t *)media_src_network_conencted;
        audio_size = sizeof(media_src_network_conencted);
        break;
    case AI_AUDIO_ALERT_NETWORK_FAIL:
        audio_data = (uint8_t *)media_src_network_fail;
        audio_size = sizeof(media_src_network_fail);
        break;
    case AI_AUDIO_ALERT_NETWORK_DISCONNECT:
        audio_data = (uint8_t *)media_src_network_disconnect;
        audio_size = sizeof(media_src_network_disconnect);
        break;
    case AI_AUDIO_ALERT_BATTERY_LOW:
        audio_data = (uint8_t *)media_src_battery_low;
        audio_size = sizeof(media_src_battery_low);
        break;
    case AI_AUDIO_ALERT_PLEASE_AGAIN:
        audio_data = (uint8_t *)media_src_please_again;
        audio_size = sizeof(media_src_please_again);
        break;
    case AI_AUDIO_ALERT_WAKEUP:
        audio_data = (uint8_t *)media_src_wakeup;
        audio_size = sizeof(media_src_wakeup);
        break;
    case AI_AUDIO_ALERT_LONG_KEY_TALK:
        audio_data = (uint8_t *)media_src_long_press_dialogue;
        audio_size = sizeof(media_src_long_press_dialogue);
        break;
    case AI_AUDIO_ALERT_KEY_TALK:
        audio_data = (uint8_t *)media_src_key_dialogue;
        audio_size = sizeof(media_src_key_dialogue);
        break;
    case AI_AUDIO_ALERT_WAKEUP_TALK:
        audio_data = (uint8_t *)media_src_wake_dialogue;
        audio_size = sizeof(media_src_wake_dialogue);
        break;
    case AI_AUDIO_ALERT_FREE_TALK:
        audio_data = (uint8_t *)media_src_free_dialogue;
        audio_size = sizeof(media_src_free_dialogue);
        break;
    default:
        PR_ERR("Unknown alert type: %d", type);
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("audio_size: %d", audio_size);
    if (audio_data && audio_size > 0) {
        rt = ai_audio_player_data_write(alert_id, audio_data, audio_size, 1);
        if (rt != OPRT_OK) {
            PR_ERR("Failed to write audio data: %d", rt);
        }
    }

    return rt;
}

/**
 * @brief Plays an alert sound synchronously based on the specified alert type.
 * @param type The type of alert to play, defined by the AI_AUDIO_ALERT_TYPE_E enum.
 * @return OPERATE_RET - OPRT_OK if the alert sound is successfully played, otherwise an error code.
 */
OPERATE_RET ai_audio_player_play_alert_syn(AI_AUDIO_ALERT_TYPE_E type)
{
    ai_audio_player_play_alert(type);

    while (ai_audio_player_is_playing()) {
        tal_system_sleep(10);
    }

    return OPRT_OK;
}

/**
 * @brief Checks if the audio player is currently playing audio.
 *
 * @param None
 * @return uint8_t - Returns 1 if the player is playing, 0 otherwise.
 */
uint8_t ai_audio_player_is_playing(void)
{
    return sg_player.is_playing;
}

/**
 * @brief Initializes the audio player module, setting up necessary resources
 *        such as mutexes, queues, timers, ring buffers, and threads.
 *
 * @param None
 * @return OPERATE_RET - Returns OPRT_OK if initialization is successful, otherwise returns an error code.
 */
OPERATE_RET ai_audio_player_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    memset(&sg_player, 0, sizeof(APP_PLAYER_T));

    PR_DEBUG("app player init...");

    TUYA_CALL_ERR_GOTO(tdl_audio_find(AUDIO_DRIVER_NAME, &sg_player.audio_hdl), __ERR);

    // create mutex
    TUYA_CALL_ERR_GOTO(tal_mutex_create_init(&sg_player.mutex), __ERR);

    TUYA_CALL_ERR_GOTO(tal_sw_timer_create(__app_playing_tm_cb, NULL, &sg_player.tm_id), __ERR);

    TUYA_CALL_ERR_GOTO(__ai_audio_player_mp3_init(), __ERR);
    // ring buffer init
    TUYA_CALL_ERR_GOTO(tuya_ring_buff_create(MP3_STREAM_BUFF_MAX_LEN, \
                                             OVERFLOW_PSRAM_STOP_TYPE,\
                                             &sg_player.rb_hdl), __ERR);
    //ring buffer mutex init
    TUYA_CALL_ERR_GOTO(tal_mutex_create_init(&sg_player.spk_rb_mutex), __ERR);

    // thread init
    TUYA_CALL_ERR_GOTO(tkl_thread_create(&sg_player.thrd_hdl, \
                                          "ai_player", \
                                          1024 * 256,\
                                          THREAD_PRIO_2,
                                          __ai_audio_player_task, NULL), __ERR);

    sg_player.is_initialized = true;
    
    PR_DEBUG("app player init success");

    return rt;

__ERR:
    if (sg_player.mutex) {
        tal_mutex_release(sg_player.mutex);
        sg_player.mutex = NULL;
    }

    if (sg_player.spk_rb_mutex) {
        tal_mutex_release(sg_player.spk_rb_mutex);
        sg_player.spk_rb_mutex = NULL;
    }

    if (sg_player.rb_hdl) {
        tuya_ring_buff_free(sg_player.rb_hdl);
        sg_player.rb_hdl = NULL;
    }

    return rt;
}