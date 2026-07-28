#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long timestamp;

typedef struct VideoData VideoData_S;
typedef struct AudioData AudioData_S;
typedef struct AVDataNode AVDataNode_S;
typedef struct AVDataQueue AVDataQueue_S;
typedef struct AVSyncContext AVSyncContext_S;

typedef int(VideoDisplay)(AVSyncContext_S*, void*, int);
typedef int(AudioDisplay)(AVSyncContext_S*, void*);
typedef timestamp(AudioClock)(AVSyncContext_S*);

struct VideoData {
    timestamp pts;
    int eosflag;
    void* data;
    void* privData;
};

struct AudioData {
    timestamp pts;  // ms
    int eosflag;
    void* data;
    void* privData;
};

struct AVDataNode {
    void* data;
    struct AVDataNode* prev;
    struct AVDataNode* next;
};

struct AVDataQueue {
    AVDataNode_S* nodeList;
    AVDataNode_S* nodeTail;
    int size;
    int maxCapacity;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

struct AVSyncContext {
    AVDataQueue_S* audioQueue;
    AVDataQueue_S* videoQueue;
    AudioClock* audioClock;
    VideoDisplay* voCallback;
    AudioDisplay* aoCallback;
    pthread_t threadId[2];
    int eosFlag;
    int syncStartFlag;
    timestamp audio_clock;
    timestamp video_clock;
    sem_t videoStartSem;
    sem_t audioStartSem;
    timestamp videoLastPts;
    timestamp videoLastDelay;
    timestamp frame_timer;
    void* priv;
};

int av_sync_init(AVSyncContext_S* ctx);
int av_sync_register_vo(AVSyncContext_S* ctx, VideoDisplay* func);
int av_sync_register_ao(AVSyncContext_S* ctx, AudioDisplay* func);
int av_sync_register_audioclock(AVSyncContext_S* ctx, AudioClock* func);
int av_sync_start(AVSyncContext_S* ctx);
int av_sync_wait(AVSyncContext_S* ctx);
int av_sync_release(AVSyncContext_S* ctx);
int send_video_frame(AVSyncContext_S* ctx, VideoData_S* data);
int send_audio_frame(AVSyncContext_S* ctx, AudioData_S* data);