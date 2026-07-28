#define PL_LOG_ID PL_LOG_COMMON
#include "av_sync.h"

#include <sys/time.h>
#include <unistd.h>

#define AV_SYNC_THRESHOLD_MIN 40
#define AV_SYNC_THRESHOLD_MAX 100
#define AV_NOSYNC_THRESHOLD 10000

#define AVMAX(a, b) ((a) > (b) ? (a) : (b))
#define AVMIN(a, b) ((a) > (b) ? (b) : (a))

#ifdef __RISCV__
#define SUPPORT_SW_CYCLE_PERF
#endif

#ifdef SUPPORT_SW_CYCLE_PERF
static inline unsigned long long metal_timer_get_cyclecount(unsigned long long* mcc) {
    unsigned long long cycles;
    __asm__ volatile("rdtime %0" : "=r"(cycles));
    *mcc = cycles;

    return cycles;
}
#endif

#ifdef SUPPORT_SW_CYCLE_PERF
unsigned long long esclock() {
    unsigned long long mcc;
    metal_timer_get_cyclecount(&mcc);
    return mcc;
}
#else
unsigned long long esclock() {
    struct timeval tmp;
    unsigned long long mcc = 0;
    gettimeofday(&tmp, NULL);
    mcc = tmp.tv_sec * 1000000 + tmp.tv_usec;
    return mcc;
}
#endif

static void send_qdata(AVDataQueue_S* queue, void* data) {
    pthread_mutex_lock(&queue->mutex);
    while (1) {
        if (queue->size < queue->maxCapacity) {
            AVDataNode_S* node = (AVDataNode_S*)malloc(sizeof(AVDataNode_S));
            memset(node, 0, sizeof(AVDataNode_S));
            if (queue->nodeList != NULL) {
                queue->nodeList->prev = node;
            }
            node->next = queue->nodeList;
            node->prev = NULL;
            node->data = data;
            queue->nodeList = node;
            if (queue->nodeTail == NULL) {
                queue->nodeTail = node;
            }
            queue->size += 1;
            break;
        } else {
            pthread_cond_wait(&queue->cond, &queue->mutex);
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    pthread_cond_broadcast(&queue->cond);
    return;
}

static void* get_qdata(AVDataQueue_S* queue) {
    void* data = NULL;
    pthread_mutex_lock(&queue->mutex);
    while (1) {
        if (queue->size > 0) {
            AVDataNode_S* node;
            node = queue->nodeTail;
            if (node->prev) {
                node->prev->next = NULL;
            } else {
                queue->nodeList = NULL;
            }
            queue->nodeTail = node->prev;
            queue->size -= 1;
            data = node->data;
            free(node);
            // printf("new node:%p, head:%p, tail:%p, size:%d \n",node,queue->nodeList,queue->nodeTail,queue->size);
            break;
        } else {
            pthread_cond_wait(&queue->cond, &queue->mutex);
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    pthread_cond_signal(&queue->cond);
    return data;
}

static void* peek_qdata(AVDataQueue_S* queue) {
    void* data = NULL;
    pthread_mutex_lock(&queue->mutex);
    while (1) {
        if (queue->size > 0) {
            AVDataNode_S* node;
            node = queue->nodeTail;
            data = node->data;
            break;
        } else {
            pthread_cond_wait(&queue->cond, &queue->mutex);
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    return data;
}

int default_video_display(AVSyncContext_S* ctx, void* data, int skip) {
    free(data);
    return 0;
}

int default_audio_display(AVSyncContext_S* ctx, void* data) {
    free(data);
    return 0;
}

timestamp default_audio_clock(AVSyncContext_S* ctx) { return ctx->audio_clock; }

void* audio_send_thread(void* data) {
    AVSyncContext_S* ctx = (AVSyncContext_S*)data;
    AVDataQueue_S* aQueue = ctx->audioQueue;
    while (!ctx->eosFlag) {
        AudioData_S* audioData = (AudioData_S*)get_qdata(aQueue);
        if (!ctx->syncStartFlag) {
            printf("audio start wait\n");
            sem_post(&ctx->audioStartSem);
            sem_wait(&ctx->videoStartSem);
            printf("audio end wait\n");
        }
        if (audioData->eosflag == 1) {
            printf("audio end of stream! \n");
            break;
        }
        ctx->aoCallback(ctx, audioData->data);
        ctx->audio_clock = audioData->pts;
        free(audioData);
    }
    return NULL;
}

void* video_sync_thread(void* data) {
    AVSyncContext_S* ctx = (AVSyncContext_S*)data;
    AVDataQueue_S* vQueue = ctx->videoQueue;
    while (!ctx->eosFlag) {
        timestamp ref_clock;
        long long actual_delay = 10, delay, diff, sync_threshold;
        VideoData_S* videoData = (VideoData_S*)peek_qdata(vQueue);
        if (!ctx->syncStartFlag) {
            printf("video start wait\n");
            sem_wait(&ctx->audioStartSem);
            ctx->syncStartFlag = 1;
            sem_post(&ctx->videoStartSem);
            printf("video end wait\n");
            ctx->frame_timer = esclock() / 1000;
        }
        if (videoData->eosflag == 1) {
            printf("video end of stream! \n");
            break;
        }

        // store pst and delay as the last
        delay = videoData->pts - ctx->videoLastPts;
        if (delay <= 0 || delay >= 1000) {
            /* if incorrect delay, use previous one */
            delay = ctx->videoLastDelay;
        }
        // reference audio clock
        ref_clock = ctx->audioClock(ctx);
        diff = ctx->videoLastPts - ref_clock;

        sync_threshold = AVMAX(AV_SYNC_THRESHOLD_MIN, AVMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (diff < AV_NOSYNC_THRESHOLD && diff > -AV_NOSYNC_THRESHOLD) {
            if (diff <= -sync_threshold) {
                delay = AVMAX(0, delay + diff);
            } else if (diff >= sync_threshold && delay > AV_SYNC_THRESHOLD_MAX) {
                delay = delay + diff;
            } else if (diff >= sync_threshold) {
                delay = 2 * delay;
            }
        }
        printf("nowpts:%lld,lastpts:%lld\n", videoData->pts, ctx->videoLastPts);
        timestamp time = esclock() / 1000;

        if (time < ctx->frame_timer + delay) {
            actual_delay = AVMIN(ctx->frame_timer + delay - time, actual_delay);
        } else {
            ctx->frame_timer += delay;
            if (/*delay > 0 &&*/ time - ctx->frame_timer > AV_SYNC_THRESHOLD_MAX) {
                ctx->frame_timer = time;
            }
            printf("video queue size: %d\n", vQueue->size);
            videoData = (VideoData_S*)get_qdata(vQueue);
            VideoData_S* nextVideoData = (VideoData_S*)peek_qdata(vQueue);
            int skip = 0;
            timestamp duration = nextVideoData->pts - videoData->pts;
            if (time > ctx->frame_timer + duration) {
                actual_delay = 0;
                skip = 1;
            }
            ctx->voCallback(ctx, videoData->data, skip);
            ctx->videoLastDelay =
                videoData->pts - ctx->videoLastPts > 0 ? videoData->pts - ctx->videoLastPts : ctx->videoLastDelay;
            ctx->videoLastPts = videoData->pts;
            free(videoData);
        }
        printf("actual_delay:%lld,frame_timer:%lld,time:%lld,diff:%lld,delay:%lld\n", actual_delay, ctx->frame_timer,
               time, diff, delay);
        usleep(actual_delay * 1000);
    }
    return NULL;
}

int av_sync_init(AVSyncContext_S* ctx) {
    ctx->aoCallback = default_audio_display;
    ctx->voCallback = default_video_display;
    ctx->audioClock = default_audio_clock;
    ctx->eosFlag = 0;
    ctx->syncStartFlag = 0;
    ctx->threadId[0] = 0;
    ctx->threadId[1] = 0;
    // audio queue
    ctx->audioQueue = (AVDataQueue_S*)malloc(sizeof(AVDataQueue_S));
    memset(ctx->audioQueue, 0, sizeof(AVDataQueue_S));
    ctx->audioQueue->maxCapacity = 10;
    ctx->audioQueue->size = 0;
    ctx->audioQueue->nodeList = NULL;
    ctx->audioQueue->nodeTail = NULL;
    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&ctx->audioQueue->mutex, &mutexattr);
    pthread_cond_init(&ctx->audioQueue->cond, NULL);

    // video queue
    ctx->videoQueue = (AVDataQueue_S*)malloc(sizeof(AVDataQueue_S));
    memset(ctx->videoQueue, 0, sizeof(AVDataQueue_S));
    ctx->videoQueue->maxCapacity = 10;
    ctx->videoQueue->size = 0;
    ctx->videoQueue->nodeList = NULL;
    ctx->videoQueue->nodeTail = NULL;
    pthread_mutex_init(&ctx->videoQueue->mutex, NULL);
    pthread_cond_init(&ctx->videoQueue->cond, NULL);

    sem_init(&ctx->videoStartSem, 0, 0);
    sem_init(&ctx->audioStartSem, 0, 0);
    ctx->videoLastPts = 0;
    ctx->videoLastDelay = 40;
    ctx->audio_clock = 0;
    return 0;
}

int av_sync_register_vo(AVSyncContext_S* ctx, VideoDisplay* func) {
    if (func) {
        ctx->voCallback = func;
    }
    return 0;
}

int av_sync_register_ao(AVSyncContext_S* ctx, AudioDisplay* func) {
    if (func) {
        ctx->aoCallback = func;
    }
    return 0;
}

int av_sync_register_audioclock(AVSyncContext_S* ctx, AudioClock* func) {
    if (func) {
        ctx->audioClock = func;
    }
    return 0;
}

int av_sync_start(AVSyncContext_S* ctx) {
    pthread_create(&ctx->threadId[0], 0, video_sync_thread, (void*)ctx);
    pthread_create(&ctx->threadId[1], 0, audio_send_thread, (void*)ctx);
    return 0;
}

int av_sync_wait(AVSyncContext_S* ctx) {
    pthread_join(ctx->threadId[0], NULL);
    pthread_join(ctx->threadId[1], NULL);
    return 0;
}

int av_sync_release(AVSyncContext_S* ctx) {
    sem_destroy(&ctx->videoStartSem);
    sem_destroy(&ctx->audioStartSem);
    return 0;
}

int send_video_frame(AVSyncContext_S* ctx, VideoData_S* data) {
    AVDataQueue_S* queue = ctx->videoQueue;
    VideoData_S* vdata = (VideoData_S*)malloc(sizeof(VideoData_S));
    memcpy(vdata, data, sizeof(VideoData_S));
    send_qdata(queue, (void*)vdata);
    return 0;
}

int send_audio_frame(AVSyncContext_S* ctx, AudioData_S* data) {
    AVDataQueue_S* queue = ctx->audioQueue;
    AudioData_S* adata = (AudioData_S*)malloc(sizeof(AudioData_S));
    memcpy(adata, data, sizeof(AudioData_S));
    send_qdata(queue, (void*)adata);
    return 0;
}
