#ifndef _ESSDK_PIPELINE_MEDIA_META_H_
#define _ESSDK_PIPELINE_MEDIA_META_H_

#include <string>
#include <vector>

#include "base_meta.h"
#include "es_audio.h"
#include "es_comm_aio.h"

using namespace std;

struct AudioStreamInfo {
    PAYLOAD_TYPE_E type;
    ES_U32 frame_size;
    ES_U32 num_channels;
    ES_U32 sample_rate;
};

class CAudioFrameMeta : public CBaseMeta {
   public:
    CAudioFrameMeta() : CBaseMeta(AUDIO_FRAME_META) {}
    virtual void release() {
        // app_ret ret = ES_MPI_ADEC_ReleaseFrame(chnId, audioFrameS);
        // // assert(ret==ES_SUCCESS);
        // if (ret != ES_SUCCESS) {
        //     printf("ES_MPI_ADEC_ReleaseFrame failed ret:%d\n", ret);
        // }

        // return APP_SUCCESS;
        delete this;
    }

   public:
    string source;              // images source info,such as IPC ,mp4 file, jpeg file
    int index = 0;              // the index framers since decoding start
    ulong pts = 0;              // the framer pts,used for the source of video.
    long long int srcTime = 0;  // the time when CFrameMeta create in source

    // AUDIO_FRAME_S* audioFrame;
    AUDIO_FRAME_INFO_S *audioFrameS = nullptr;
    ADEC_CHN chnId = 0;  // release frame need
   private:
    CAudioFrameMeta(const CAudioFrameMeta &) = delete;
    CAudioFrameMeta &operator=(const CAudioFrameMeta &) = delete;
};

class CAudioPacketMeta : public CBaseMeta {
   public:
    CAudioPacketMeta() : CBaseMeta(AUDIO_PACKET_META) {}
    virtual void release() {
        if (audioPkt != nullptr) {
            free(audioPkt);
        }
        delete this;
    }

   public:
    string source;              // images source info,such as IPC ,mp4 file, jpeg file
    int index = 0;              // the index framers since decoding start
    ulong pts = 0;              // the framer pts,used for the source of video.
    long long int srcTime = 0;  // the time when CFrameMeta create in source
    ES_BOOL bEndOfStream = ES_FALSE;
    PAYLOAD_TYPE_E payloadType = PT_BUTT;  // for example PT_AAC;

    AUDIO_STREAM_S *audioPkt = nullptr;

   private:
    CAudioPacketMeta(const CAudioPacketMeta &) = delete;
    CAudioPacketMeta &operator=(const CAudioPacketMeta &) = delete;
};

#endif