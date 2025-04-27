#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

// #define PCM_DEVICE      "default"
#define PCM_DEVICE      "hw:1,0"
#define BUFFER_FRAMES   576                         // 精确匹配MP3帧大小
#define BUFFER_MULTIPLE 8                           // 足够的缓冲区倍数

// 录音相关定义
#define RECORD_DEVICE   "default"
#define RECORD_FRAMES   1600                        // 100ms @ 16kHz = 1600帧
#define RECORD_MULTIPLE 4

static snd_pcm_t *pcm_handle = NULL;
static snd_pcm_t *record_handle = NULL;

int alsa_device_open(unsigned int channels, unsigned int sample_rate, unsigned int format_bits)
{
    int rc;
    snd_pcm_hw_params_t *params;

    // 打开PCM设备 - 使用阻塞模式来自然控制播放速度
    if ((rc = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "ALSA open error: %s\n", snd_strerror(rc));
        return -1;
    }

    // 分配硬件参数对象
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle, params);

    // 设置交错模式
    if ((rc = snd_pcm_hw_params_set_access(pcm_handle, params, 
            SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Access type error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置采样格式
    snd_pcm_format_t format;
    if (format_bits == 16)
        format = SND_PCM_FORMAT_S16_LE;
    else if (format_bits == 24)
        format = SND_PCM_FORMAT_S24_LE;
    else if (format_bits == 32)
        format = SND_PCM_FORMAT_S32_LE;
    else {
        fprintf(stderr, "Unsupported bit depth: %d\n", format_bits);
        goto err;
    }
    
    if ((rc = snd_pcm_hw_params_set_format(pcm_handle, params, format)) < 0) {
        fprintf(stderr, "Format error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置声道数
    if ((rc = snd_pcm_hw_params_set_channels(pcm_handle, params, channels)) < 0) {
        fprintf(stderr, "Channels error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置采样率
    unsigned int rate = sample_rate;
    if ((rc = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0)) < 0) {
        fprintf(stderr, "Rate error: %s\n", snd_strerror(rc));
        goto err;
    }
    if (rate != sample_rate) {
        fprintf(stderr, "Warning: Sample rate adjusted from %u to %u\n", 
               sample_rate, rate);
    }

    // 设置周期大小
    snd_pcm_uframes_t period_size = BUFFER_FRAMES;
    if ((rc = snd_pcm_hw_params_set_period_size_near(pcm_handle, params, &period_size, 0)) < 0) {
        fprintf(stderr, "Period size error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置缓冲区大小
    snd_pcm_uframes_t buffer_size = period_size * BUFFER_MULTIPLE;
    if ((rc = snd_pcm_hw_params_set_buffer_size_near(pcm_handle, params, &buffer_size)) < 0) {
        fprintf(stderr, "Buffer size error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 打印实际缓冲区设置
    snd_pcm_hw_params_get_period_size(params, &period_size, 0);
    snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
    printf("ALSA buffer: period=%u frames, buffer=%lu frames (%.1fms @ %uHz)\n",
          (uint32_t)period_size, buffer_size, 
          (float)buffer_size * 1000 / rate, rate);

    // 应用参数设置
    if ((rc = snd_pcm_hw_params(pcm_handle, params)) < 0) {
        fprintf(stderr, "Param apply error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 准备PCM设备
    if ((rc = snd_pcm_prepare(pcm_handle)) < 0) {
        fprintf(stderr, "PCM prepare error: %s\n", snd_strerror(rc));
        goto err;
    }

    printf("ALSA device ready for playback\n");
    return 0;

err:
    snd_pcm_close(pcm_handle);
    return -1;
}

int alsa_device_write(int16_t *pcm, size_t frames)
{
    // 验证输入参数
    if (pcm == NULL || frames == 0) {
        fprintf(stderr, "Invalid PCM data: %p, frames: %zu\n", pcm, frames);
        return -1;
    }

    if (pcm_handle == NULL) {
        fprintf(stderr, "ALSA device not initialized\n");
        return -1;
    }

    // 获取当前ALSA参数用于验证
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_current(pcm_handle, params) < 0) {
        fprintf(stderr, "Failed to get current hw params\n");
        return -1;
    }
    
    unsigned int channels;
    snd_pcm_hw_params_get_channels(params, &channels);
    
    // 验证PCM数据大小
    size_t expected_size = frames * channels;
    if (expected_size > BUFFER_FRAMES * BUFFER_MULTIPLE * channels) {
        fprintf(stderr, "Write size %zu exceeds buffer capacity\n", expected_size);
        return -1;
    }

    // 检查ALSA状态
    snd_pcm_state_t state = snd_pcm_state(pcm_handle);
    if (state == SND_PCM_STATE_XRUN) {
        fprintf(stderr, "ALSA in XRUN state, recovering...\n");
        if (snd_pcm_prepare(pcm_handle) < 0) {
            fprintf(stderr, "Failed to recover from XRUN\n");
            return -1;
        }
    }

    // 写入音频数据
    snd_pcm_sframes_t written = snd_pcm_writei(pcm_handle, pcm, frames);
    
    if (written == -EPIPE) {  // Underrun处理
        fprintf(stderr, "Underrun occurred (frames: %zu, ch: %u) - recovering...\n", frames, channels);
        if (snd_pcm_prepare(pcm_handle) < 0) {
            fprintf(stderr, "Failed to recover from underrun\n");
            return -1;
        }
        
        // 重试写入，最多重试2次
        int retry_count = 0;
        while (retry_count < 2) {
            written = snd_pcm_writei(pcm_handle, pcm, frames);
            if (written >= 0) {
                break;
            }
            if (written == -EPIPE) {
                fprintf(stderr, "Underrun retry %d\n", retry_count + 1);
                snd_pcm_prepare(pcm_handle);
                retry_count++;
                // 添加小延迟让系统稳定
                usleep(1000); // 1ms
            } else {
                break;
            }
        }
    }
    
    if (written < 0) {
        fprintf(stderr, "Write error: %s (frames: %zu, ch: %u)\n", 
               snd_strerror(written), frames, channels);
        return -1;
    } else if ((size_t)written != frames) {
        fprintf(stderr, "Short write: %ld of %zu frames (%.1f%%)\n", 
              written, frames, (float)written/frames*100);
    }

    return written;
}

void alsa_device_close(void)
{
    if (pcm_handle != NULL) {
        // 确保所有数据都被播放完
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
    }
}

// 录音设备初始化
int alsa_record_open(unsigned int channels, unsigned int sample_rate, unsigned int format_bits)
{
    int rc;
    snd_pcm_hw_params_t *params;

    // 打开录音设备
    if ((rc = snd_pcm_open(&record_handle, RECORD_DEVICE, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "ALSA record open error: %s\n", snd_strerror(rc));
        return -1;
    }

    // 分配硬件参数对象
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(record_handle, params);

    // 设置交错模式
    if ((rc = snd_pcm_hw_params_set_access(record_handle, params, 
            SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Record access type error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置采样格式
    snd_pcm_format_t format;
    if (format_bits == 16)
        format = SND_PCM_FORMAT_S16_LE;
    else if (format_bits == 24)
        format = SND_PCM_FORMAT_S24_LE;
    else if (format_bits == 32)
        format = SND_PCM_FORMAT_S32_LE;
    else {
        fprintf(stderr, "Unsupported record bit depth: %d\n", format_bits);
        goto err;
    }
    
    if ((rc = snd_pcm_hw_params_set_format(record_handle, params, format)) < 0) {
        fprintf(stderr, "Record format error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置声道数
    if ((rc = snd_pcm_hw_params_set_channels(record_handle, params, channels)) < 0) {
        fprintf(stderr, "Record channels error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置采样率
    unsigned int rate = sample_rate;
    if ((rc = snd_pcm_hw_params_set_rate_near(record_handle, params, &rate, 0)) < 0) {
        fprintf(stderr, "Record rate error: %s\n", snd_strerror(rc));
        goto err;
    }
    if (rate != sample_rate) {
        fprintf(stderr, "Warning: Record sample rate adjusted from %u to %u\n", 
               sample_rate, rate);
    }

    // 设置周期大小
    snd_pcm_uframes_t period_size = RECORD_FRAMES;
    if ((rc = snd_pcm_hw_params_set_period_size_near(record_handle, params, &period_size, 0)) < 0) {
        fprintf(stderr, "Record period size error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 设置缓冲区大小
    snd_pcm_uframes_t buffer_size = period_size * RECORD_MULTIPLE;
    if ((rc = snd_pcm_hw_params_set_buffer_size_near(record_handle, params, &buffer_size)) < 0) {
        fprintf(stderr, "Record buffer size error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 打印实际缓冲区设置
    snd_pcm_hw_params_get_period_size(params, &period_size, 0);
    snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
    printf("ALSA record: period=%u frames, buffer=%lu frames (%.1fms @ %uHz)\n",
          (uint32_t)period_size, buffer_size, 
          (float)buffer_size * 1000 / rate, rate);

    // 应用参数设置
    if ((rc = snd_pcm_hw_params(record_handle, params)) < 0) {
        fprintf(stderr, "Record param apply error: %s\n", snd_strerror(rc));
        goto err;
    }

    // 准备录音设备
    if ((rc = snd_pcm_prepare(record_handle)) < 0) {
        fprintf(stderr, "Record prepare error: %s\n", snd_strerror(rc));
        goto err;
    }

    printf("ALSA record device ready\n");
    return 0;

err:
    snd_pcm_close(record_handle);
    record_handle = NULL;
    return -1;
}

// 录音数据读取
int alsa_record_read(int16_t *buffer, size_t frames)
{
    if (record_handle == NULL) {
        fprintf(stderr, "Record device not initialized\n");
        return -1;
    }

    if (buffer == NULL || frames == 0) {
        fprintf(stderr, "Invalid record buffer: %p, frames: %zu\n", buffer, frames);
        return -1;
    }

    // 检查ALSA状态
    snd_pcm_state_t state = snd_pcm_state(record_handle);
    if (state == SND_PCM_STATE_XRUN) {
        fprintf(stderr, "Record XRUN occurred, recovering...\n");
        if (snd_pcm_prepare(record_handle) < 0) {
            fprintf(stderr, "Failed to recover from record XRUN\n");
            return -1;
        }
    }

    // 读取音频数据
    snd_pcm_sframes_t read_frames = snd_pcm_readi(record_handle, buffer, frames);
    
    if (read_frames == -EPIPE) {  // Overrun处理
        fprintf(stderr, "Record overrun occurred - recovering...\n");
        if (snd_pcm_prepare(record_handle) < 0) {
            fprintf(stderr, "Failed to recover from overrun\n");
            return -1;
        }
        
        // 重试读取
        read_frames = snd_pcm_readi(record_handle, buffer, frames);
    }
    
    if (read_frames < 0) {
        fprintf(stderr, "Record error: %s\n", snd_strerror(read_frames));
        return -1;
    } else if ((size_t)read_frames != frames) {
        fprintf(stderr, "Short read: %ld of %zu frames\n", read_frames, frames);
    }

    return read_frames;
}

// 关闭录音设备
void alsa_record_close(void)
{
    if (record_handle != NULL) {
        snd_pcm_drop(record_handle);
        snd_pcm_close(record_handle);
        record_handle = NULL;
        printf("ALSA record device closed\n");
    }
}
