#include "wav_writer.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// WAV文件头结构
typedef struct {
    // RIFF头
    char riff_id[4];        // "RIFF"
    uint32_t file_size;     // 文件大小 - 8
    char wave_id[4];        // "WAVE"
    
    // fmt子块
    char fmt_id[4];         // "fmt "
    uint32_t fmt_size;      // fmt子块大小，通常为16
    uint16_t format_tag;    // 格式标签，PCM为1
    uint16_t channels;      // 声道数
    uint32_t sample_rate;   // 采样率
    uint32_t byte_rate;     // 字节率 = 采样率 * 声道数 * 位深/8
    uint16_t block_align;   // 块对齐 = 声道数 * 位深/8
    uint16_t bits_per_sample; // 位深
    
    // data子块头
    char data_id[4];        // "data"
    uint32_t data_size;     // 数据大小
} __attribute__((packed)) wav_header_t;

// 创建WAV文件并写入头部
wav_writer_t* wav_writer_open(const char *filename, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample)
{
    wav_writer_t *writer = malloc(sizeof(wav_writer_t));
    if (!writer) {
        return NULL;
    }
    
    writer->file = fopen(filename, "wb");
    if (!writer->file) {
        free(writer);
        return NULL;
    }
    
    writer->sample_rate = sample_rate;
    writer->channels = channels;
    writer->bits_per_sample = bits_per_sample;
    writer->data_size = 0;
    
    // 创建WAV头
    wav_header_t header = {0};
    
    // RIFF头
    memcpy(header.riff_id, "RIFF", 4);
    header.file_size = 0; // 稍后更新
    memcpy(header.wave_id, "WAVE", 4);
    
    // fmt子块
    memcpy(header.fmt_id, "fmt ", 4);
    header.fmt_size = 16;
    header.format_tag = 1; // PCM
    header.channels = channels;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * channels * bits_per_sample / 8;
    header.block_align = channels * bits_per_sample / 8;
    header.bits_per_sample = bits_per_sample;
    
    // data子块头
    memcpy(header.data_id, "data", 4);
    header.data_size = 0; // 稍后更新
    
    // 写入头部
    if (fwrite(&header, sizeof(header), 1, writer->file) != 1) {
        fclose(writer->file);
        free(writer);
        return NULL;
    }
    
    // 记录data_size在文件中的位置，用于稍后更新
    writer->data_size_pos = ftell(writer->file) - sizeof(uint32_t);
    
    printf("WAV file '%s' created (%.1fkHz, %dch, %dbit)\n", 
           filename, (float)sample_rate/1000, channels, bits_per_sample);
    
    return writer;
}

// 写入PCM数据
int wav_writer_write(wav_writer_t *writer, const int16_t *data, size_t frames)
{
    if (!writer || !writer->file || !data || frames == 0) {
        return -1;
    }
    
    size_t samples = frames * writer->channels;
    size_t bytes_to_write = samples * sizeof(int16_t);
    
    size_t written = fwrite(data, sizeof(int16_t), samples, writer->file);
    if (written != samples) {
        return -1;
    }
    
    writer->data_size += bytes_to_write;
    
    return frames;
}

// 关闭WAV文件并更新头部信息
void wav_writer_close(wav_writer_t *writer)
{
    if (!writer || !writer->file) {
        return;
    }
    
    // 更新data_size
    fseek(writer->file, writer->data_size_pos, SEEK_SET);
    fwrite(&writer->data_size, sizeof(uint32_t), 1, writer->file);
    
    // 更新file_size (总文件大小 - 8)
    uint32_t file_size = sizeof(wav_header_t) + writer->data_size - 8;
    fseek(writer->file, 4, SEEK_SET); // RIFF头中file_size的位置
    fwrite(&file_size, sizeof(uint32_t), 1, writer->file);
    
    fclose(writer->file);
    
    printf("WAV file closed. Data size: %u bytes (%.2f seconds)\n", 
           writer->data_size, 
           (float)writer->data_size / (writer->sample_rate * writer->channels * sizeof(int16_t)));
    
    free(writer);
}

// 生成基于时间戳的文件名
char* get_timestamp_filename(const char *prefix, const char *extension)
{
    time_t now;
    struct tm *tm_info;
    static char filename[256];
    
    time(&now);
    tm_info = localtime(&now);
    
    snprintf(filename, sizeof(filename), "%s_%04d%02d%02d_%02d%02d%02d.%s",
             prefix,
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec,
             extension);
    
    return filename;
} 