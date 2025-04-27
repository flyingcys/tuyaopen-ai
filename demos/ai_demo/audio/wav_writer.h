#ifndef WAV_WRITER_H
#define WAV_WRITER_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// WAV文件写入器结构
typedef struct {
    FILE *file;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_size;
    long data_size_pos;  // 记录data_size在文件中的位置
} wav_writer_t;

// WAV文件操作函数
wav_writer_t* wav_writer_open(const char *filename, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
int wav_writer_write(wav_writer_t *writer, const int16_t *data, size_t frames);
void wav_writer_close(wav_writer_t *writer);

// 工具函数
char* get_timestamp_filename(const char *prefix, const char *extension);

#ifdef __cplusplus
}
#endif

#endif // WAV_WRITER_H 