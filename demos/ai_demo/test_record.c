#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include "audio/alsa.h"
#include "audio/wav_writer.h"

static pthread_t record_thread;
static volatile int recording = 0;
static volatile int running = 1;
static wav_writer_t *wav_writer = NULL;

// 音频数据上传回调函数
static void upload_audio_data(const int16_t *data, size_t frames)
{
    static int upload_count = 0;
    upload_count++;
    
    printf("[Upload #%d] %zu frames (%.1fms) - samples: ", 
           upload_count, frames, (float)frames * 1000 / 16000);
    
    // 打印前5个样本值
    for (size_t i = 0; i < (frames > 5 ? 5 : frames); i++) {
        printf("%d ", data[i]);
    }
    printf("...\n");
    
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
    
    printf("Recording started (16bit, 16kHz, mono). Press 'p' to stop.\n");
    
    while (recording && running) {
        // 读取100ms的音频数据
        int read_frames = alsa_record_read(buffer, frames_per_100ms);
        if (read_frames > 0) {
            // 上传音频数据（包含WAV文件写入）
            upload_audio_data(buffer, read_frames);
        } else if (read_frames < 0) {
            printf("Record error occurred\n");
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
    printf("Recording stopped\n");
    
    return NULL;
}

// 信号处理函数
void signal_handler(int sig) {
    printf("\nReceived signal %d, stopping...\n", sig);
    running = 0;
    recording = 0;
}

// 非阻塞键盘输入检查
static int check_keyboard_input(void)
{
    int ch = 0;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    ch = getchar();
    
    fcntl(STDIN_FILENO, F_SETFL, flags);
    return ch;
}

int main(int argc, char *argv[])
{
    printf("Audio Recording Test Program\n");
    printf("Commands:\n");
    printf("  s - Start recording\n");
    printf("  p - Stop recording\n");
    printf("  q - Quit\n");
    printf("============================\n\n");

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (running) {
        int ch = check_keyboard_input();
        
        if (ch != EOF && ch != -1) {
            switch (ch) {
            case 's':
            case 'S':
                if (!recording) {
                    printf("Starting recording...\n");
                    recording = 1;
                    if (pthread_create(&record_thread, NULL, record_thread_func, NULL) != 0) {
                        printf("Failed to create record thread\n");
                        recording = 0;
                    }
                } else {
                    printf("Recording is already in progress\n");
                }
                break;
                
            case 'p':
            case 'P':
                if (recording) {
                    printf("Stopping recording...\n");
                    recording = 0;
                    pthread_join(record_thread, NULL);
                } else {
                    printf("No recording in progress\n");
                }
                break;
                
            case 'q':
            case 'Q':
                printf("Quitting...\n");
                if (recording) {
                    recording = 0;
                    pthread_join(record_thread, NULL);
                }
                running = 0;
                break;
                
            default:
                break;
            }
        }
        
        usleep(10000); // 10ms
    }

    printf("Program exited\n");
    return 0;
} 