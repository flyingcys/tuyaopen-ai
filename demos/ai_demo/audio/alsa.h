#ifndef ALSA_H
#define ALSA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 播放相关函数
int alsa_device_open(unsigned int channels, unsigned int sample_rate, unsigned int format_bits);
int alsa_device_write(int16_t *pcm, size_t frames);
void alsa_device_close(void);

// 录音相关函数
int alsa_record_open(unsigned int channels, unsigned int sample_rate, unsigned int format_bits);
int alsa_record_read(int16_t *buffer, size_t frames);
void alsa_record_close(void);

#ifdef __cplusplus
}
#endif

#endif // ALSA_H 