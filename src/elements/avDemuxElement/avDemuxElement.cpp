#define PL_LOG_ID PL_LOG_AVDEMUX
#include "avDemuxElement.h"

#include <sys/prctl.h>
#include <yaml-cpp/yaml.h>

#include "pipeline.h"
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
}
using namespace std;

const int sampling_frequencies[] = {
    96000,  // 0x0
    88200,  // 0x1
    64000,  // 0x2
    48000,  // 0x3
    44100,  // 0x4
    32000,  // 0x5
    24000,  // 0x6
    22050,  // 0x7
    16000,  // 0x8
    12000,  // 0x9
    11025,  // 0xa
    8000    // 0xb
            // 0xc d e f是保留的
};

static ES_BOOL isCodecSupport(enum AVCodecID id) {
    static enum AVCodecID supportCodec[] = {
        AV_CODEC_ID_H264,
        AV_CODEC_ID_HEVC,
        AV_CODEC_ID_H265,
        AV_CODEC_ID_MJPEG,
    };
    size_t num = sizeof(supportCodec) / sizeof(supportCodec[0]);
    for (size_t i = 0; i < num; i++) {
        if (id == supportCodec[i]) {
            return ES_TRUE;
        }
    }
    return ES_FALSE;
}

int create_adts_header(unsigned char* const p_adts_header, const int data_length, const int profile,
                       const int samplerate, const int channels) {
    int sampling_frequency_index = 3;  // 默认使用48000hz
    int adtsLen = data_length + 7;

    // 匹配采样率
    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i = 0;
    for (i = 0; i < frequencies_size; i++) {
        if (sampling_frequencies[i] == samplerate) {
            sampling_frequency_index = i;
            break;
        }
    }
    if (i >= frequencies_size) {
        std::cout << "没有找到支持的采样率" << std::endl;
        return -1;
    }

    p_adts_header[0] = 0xff;       // syncword:0xfff                          高8bits
    p_adts_header[1] = 0xf0;       // syncword:0xfff                          低4bits
    p_adts_header[1] |= (0 << 3);  // MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    p_adts_header[1] |= (0 << 1);  // Layer:0 2bits
    p_adts_header[1] |= 1;         // protection absent:1                     1bit

    p_adts_header[2] = (profile) << 6;  // profile:profile               2bits
    p_adts_header[2] |= (sampling_frequency_index & 0x0f)
                        << 2;                    // sampling frequency index:sampling_frequency_index  4bits
    p_adts_header[2] |= (0 << 1);                // private bit:0                   1bit
    p_adts_header[2] |= (channels & 0x04) >> 2;  // channel configuration:channels  高1bit

    p_adts_header[3] = (channels & 0x03) << 6;       // channel configuration:channels 低2bits
    p_adts_header[3] |= (0 << 5);                    // original：0                1bit
    p_adts_header[3] |= (0 << 4);                    // home：0                    1bit
    p_adts_header[3] |= (0 << 3);                    // copyright id bit：0        1bit
    p_adts_header[3] |= (0 << 2);                    // copyright id start：0      1bit
    p_adts_header[3] |= ((adtsLen & 0x1800) >> 11);  // frame length：value   高2bits

    p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);  // frame length:value    中间8bits
    p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5);    // frame length:value    低3bits
    p_adts_header[5] |= 0x1f;                              // buffer fullness:0x7ff 高5bits
    p_adts_header[6] = 0xfc;                               // ‭11111100‬       //buffer fullness:0x7ff 低6bits

    return 0;
}

static ES_S32 openInputAndFindStream(const ES_CHAR* pUrl, AVFormatContext** pContext, ES_S32* pVStreamIndex,
                                     ES_S32* pAStreamIndex) {
    if (!pUrl) {
        app_error("url is null.");
        return ES_FAILURE;
    }

    /* Try to open url. */
    AVFormatContext* pFmtCtx = NULL;
    AVDictionary* opts = NULL;
    if (1) {
        av_dict_set(&opts, "stimeout", "1000000", 0);
        av_dict_set(&opts, "max_delay", "0", 0);
        av_dict_set(&opts, "fflags", "nobuffer", 0);
        // av_dict_set(&opts, "probesize", "4096", 0);
        av_dict_set(&opts, "packet-buffering", "0", 0);
    }

    unsigned long long start = esclock();
    do {
        pFmtCtx = avformat_alloc_context();
        if (!pFmtCtx) {
            app_error("alloc context failed.");
            return ES_FAILURE;
        }
        if (1) {
            pFmtCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER | AV_CODEC_FLAG_LOW_DELAY | AVFMT_FLAG_NOBUFFER;
            av_opt_set(pFmtCtx->priv_data, "preset", "fast", 0);
            av_opt_set(pFmtCtx->priv_data, "tune", "zerolatency", 0);
        }
        /* Open input file, and allocate format context. */
        if (avformat_open_input(&pFmtCtx, pUrl, ES_NULL, &opts) < 0) {
            app_debug("Could not open source file '%s'.", pUrl);
            avformat_free_context(pFmtCtx);
            pFmtCtx = NULL;
            usleep(10 * 1000);
        } else {
            break;
        }
    } while (true);
    unsigned long long end = esclock();
    if (opts) {
        av_dict_free(&opts);
    }

    start = esclock();
    /* Retrieve stream information. */
    if (avformat_find_stream_info(pFmtCtx, ES_NULL) < 0) {
        app_error("Could not find stream information.");
        avformat_free_context(pFmtCtx);
        return ES_FAILURE;
    }
    end = esclock();

    start = esclock();
    /* Find best video stream. */
    int ret = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, ES_NULL, 0);
    if (ret < 0) {
        app_debug("Could not find video stream in input file %s.", pUrl);
    } else {
        *pVStreamIndex = ret;
    }

    /* Find best audio stream. */
    ret = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, ES_NULL, 0);
    if (ret < 0) {
        app_debug("Could not find audio stream in input file %s.\n", pUrl);
    } else {
        *pAStreamIndex = ret;
    }
    end = esclock();

    *pContext = pFmtCtx;
    return ES_SUCCESS;
}

ES_S32 DEMUXER_ProbeMeta(const ES_CHAR* pUrl, AVDEMUX_PARAM_S* pMeta) {
    if (!pMeta) {
        app_error("pMeta is null.");
        return ES_FAILURE;
    }
    pMeta->videotype = PT_BUTT;
    pMeta->height = -1;
    pMeta->width = -1;
    AVFormatContext* pFmtCtx = ES_NULL;
    ES_S32 vstreamIndex = -1;
    ES_S32 astreamIndex = -1;
    if (openInputAndFindStream(pUrl, &pFmtCtx, &vstreamIndex, &astreamIndex) == ES_SUCCESS) {
        if (vstreamIndex > -1) {
            pMeta->width = pFmtCtx->streams[vstreamIndex]->codecpar->width;
            pMeta->height = pFmtCtx->streams[vstreamIndex]->codecpar->height;
            enum AVCodecID id = pFmtCtx->streams[vstreamIndex]->codecpar->codec_id;
            ES_S32 format = pFmtCtx->streams[vstreamIndex]->codecpar->format;
            app_info(
                "Find %dx%d %s stream format %d with index %d in source file "
                "'%s'.",
                pMeta->width, pMeta->height, avcodec_get_name(id), format, vstreamIndex, pUrl);
            if (id == AV_CODEC_ID_H264) {
                pMeta->videotype = PT_H264;
            } else if (id == AV_CODEC_ID_HEVC || id == AV_CODEC_ID_H265) {
                pMeta->videotype = PT_H265;
            } else if (id == AV_CODEC_ID_MJPEG) {
                pMeta->videotype = PT_MJPEG;
            }
        }
        if (astreamIndex > -1) {
            pMeta->num_channels = pFmtCtx->streams[astreamIndex]->codecpar->ch_layout.nb_channels;
            pMeta->sample_rate = pFmtCtx->streams[astreamIndex]->codecpar->sample_rate;
            pMeta->frame_size = pFmtCtx->streams[astreamIndex]->codecpar->frame_size;
            pMeta->profile = pFmtCtx->streams[astreamIndex]->codecpar->profile;
            enum AVCodecID id = pFmtCtx->streams[astreamIndex]->codecpar->codec_id;
            ES_S32 format = pFmtCtx->streams[astreamIndex]->codecpar->format;
            app_info(
                "Find channel:%d samplerate:%d %s stream format %d with index "
                "%d in "
                "source file '%s'.",
                pMeta->num_channels, pMeta->sample_rate, avcodec_get_name(id), format, astreamIndex, pUrl);
            if (id == AV_CODEC_ID_AAC) {
                pMeta->audiotype = PT_AAC;
            }
        }

        avformat_close_input(&pFmtCtx);
        return ES_SUCCESS;
    }
    return ES_FAILURE;
}

static int CheckInterrupt(void* time) {
    unsigned long long start = *((unsigned long long*)time);
    printf("----------Interrupt start--------------%lld\n", start);
    unsigned long long now = esclock();
    printf("----------Interrupt now--------------%lld\n", now);
    return now - start >= 1000000 ? 1 : 0;  // 3秒超时
}

static AVFormatContext* get_stream(ES_CHAR* pUrl, AvDemuxElement* pAvDemuxElement, int& videostreamidx,
                                   int& audiostreamidx, int voutidx, int aoutidx) {
    AVFormatContext* pFmtCtx = NULL;
    do {
        pFmtCtx = avformat_alloc_context();
        AVDictionary* opts = NULL;

        if (1) {
            av_dict_set(&opts, "rtsp_transport", "tcp", 0);
            av_dict_set(&opts, "max_delay", "500", 0);
            av_dict_set(&opts, "stimeout", "10000000", 0);
        }

        int status = avformat_open_input(&pFmtCtx, pUrl, NULL, &opts);
        if (opts) {
            av_dict_free(&opts);
        }
        app_info("%s-%s-%d-%s status:%d\n", PLLOG_fileName(__FILE__), __func__, __LINE__, pAvDemuxElement->mName.c_str(),
                 status);
        if (status >= 0) {
            for (int i = 0; i < pFmtCtx->nb_streams; i++) {
                if (pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    videostreamidx = voutidx > -1 ? i : -1;
                    app_info("%s-%s-%d-%s videostreamidx:%d\n", PLLOG_fileName(__FILE__), __func__, __LINE__,
                             pAvDemuxElement->mName.c_str(), videostreamidx);
                } else if (pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                    audiostreamidx = aoutidx > -1 ? i : -1;
                    app_info("%s-%s-%d-%s audiostreamidx:%d\n", PLLOG_fileName(__FILE__), __func__, __LINE__,
                             pAvDemuxElement->mName.c_str(), audiostreamidx);
                }
            }
            break;
        } else {
            avformat_close_input(&pFmtCtx);
            pFmtCtx = NULL;
            app_info("%s-%s-%d-%s open file or url %s failed\n", PLLOG_fileName(__FILE__), __func__, __LINE__,
                     pAvDemuxElement->mName.c_str(), pUrl);
            usleep(10 * 1000);
        }
    } while (true);
    pFmtCtx->interrupt_callback.callback = CheckInterrupt;  // 超时回调
    return pFmtCtx;
}

static ES_VOID* plStartSendStream(ES_VOID* pArgs) {
    while (!isAllElementStart) {
        usleep(4 * 1000 * 1000);
    }

    AVDEMUX_PARAM_S* pAvDemuxParam = (AVDEMUX_PARAM_S*)pArgs;
    AvDemuxElement* pAvDemuxElement = (AvDemuxElement*)pAvDemuxParam->element;
    if (!pAvDemuxElement->m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(pAvDemuxElement->m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        set_thread_affinity(pAvDemuxElement->m_dieIndex);
        pAvDemuxElement->m_cpuSetFlag = true;
    }
    pAvDemuxElement->getCpuNumaID(__func__);

    app_info("%s-%s-%d-%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, pAvDemuxElement->mName.c_str());

    ES_S32 audioSendCount = 0;
    ES_S32 videoSendCount = 0;
    ES_S32 loopNum = pAvDemuxElement->mLoopNum;
    // ES_S32 ret = ES_SUCCESS;

    string threadName = pAvDemuxElement->mName + "_send_stream";
    prctl(PR_SET_NAME, (unsigned long)(threadName.c_str()));
    // demuxer
    unsigned long long frameInterval = 0;
    unsigned long long lastFrameTime = 0;
    if (pAvDemuxParam->outfps > 0) {
        frameInterval = 1000000 / pAvDemuxParam->outfps;  // us
    }

    ES_CHAR* pUrl = pAvDemuxParam->streamName;
    AVFormatContext* pFmtCtx = NULL;
    ES_S32 videostreamidx = -1;
    ES_S32 audiostreamidx = -1;
    int voutidx = pAvDemuxElement->voutidx;
    int aoutidx = pAvDemuxElement->aoutidx;

    pFmtCtx = get_stream(pUrl, pAvDemuxElement, videostreamidx, audiostreamidx, voutidx, aoutidx);

    AVPacket* pkt = av_packet_alloc();
    av_init_packet(pkt);

    // 1 获取相应的比特流过滤器
    // FLV/MP4/MKV等结构中，h264需要h264_mp4toannexb处理。添加SPS/PPS等信息。
    // FLV封装时，可以把多个NALU放在一个VIDEO TAG中,结构为4B NALU长度+NALU1+4B
    // NALU长度+NALU2+..., 需要做的处理把4B长度换成00000001或者000001
    const AVBitStreamFilter* bsfilter = NULL;
    AVBSFContext* bsf_ctx = NULL;
    if (videostreamidx >= 0) {
        if (pAvDemuxParam->videotype == PT_H264) {
            bsfilter = av_bsf_get_by_name("h264_mp4toannexb");
        } else if (pAvDemuxParam->videotype == PT_H265) {
            bsfilter = av_bsf_get_by_name("hevc_mp4toannexb");
        }
        if (bsfilter) {
            // 2 初始化过滤器上下文
            av_bsf_alloc(bsfilter, &bsf_ctx);  // AVBSFContext;
            // 3 添加解码器属性
            avcodec_parameters_copy(bsf_ctx->par_in, pFmtCtx->streams[videostreamidx]->codecpar);
            av_bsf_init(bsf_ctx);
        }
    }

    int64_t firstAudioPts = 0;
    int64_t firstVideoPts = 0;
    double audioTimebase = 0;
    double videoTimebase = 0;
    if (audiostreamidx > -1) {
        audioTimebase = av_q2d(pFmtCtx->streams[audiostreamidx]->time_base) * 1000;
    }
    if (videostreamidx > -1) {
        videoTimebase = av_q2d(pFmtCtx->streams[videostreamidx]->time_base) * 1000;
    }

    int leftFrame = -1;
    if (pAvDemuxParam->totalFrame > 0) {
        leftFrame = pAvDemuxParam->totalFrame;
        // printf("------------leftFrame:%d----------\n",leftFrame);
    }

    while (true) {
        pAvDemuxElement->sendStreamPerformance->performanceStaticStart();

        int status = 0;
        // for (int i = 0; i < pFmtCtx->nb_streams; i++)
        // {
        // app_warn("%s 4!\n", pAvDemuxElement->mName.c_str());
        unsigned long long start = esclock();
        fflush(stdout);
        pFmtCtx->interrupt_callback.opaque = (void*)(&start);
        status = av_read_frame(pFmtCtx, pkt);
        if (leftFrame > 0) leftFrame--;
        // printf("--------------leftFrame:%d------------------\n", leftFrame);
        // app_warn("%s 5!\n", pAvDemuxElement->mName.c_str());
        fflush(stdout);
        app_info("%s-%s-%d-%s av_read_frame ret:%d\n", PLLOG_fileName(__FILE__), __func__, __LINE__,
                 pAvDemuxElement->mName.c_str(), status);
        if (isVoEosFlag) {
            leftFrame = 0;
            loopNum = 1;
            printf("the isVoEosFlag is true\n");
            printf("the voutidx is %d\n", voutidx);
        }
        if (status < 0 || leftFrame == 0) {
            // printf("-----------------------status:%d----------------------\n",status);
            char buf[128];
            av_strerror(status, buf, sizeof(buf));
            // printf("av_read_frame : %s\n", buf);
            loopNum--;
            app_warn("%s end of stream , the loopNum is %d\n", pAvDemuxElement->mName.c_str(), loopNum);
            av_log(NULL, AV_LOG_DEBUG, "end of stream!\n");
            app_info("%s end of stream!\n", pAvDemuxElement->mName.c_str());

            // video
            if (loopNum > 0 && leftFrame != 0) {
                av_packet_unref(pkt);
                avformat_close_input(&pFmtCtx);
                pFmtCtx = get_stream(pUrl, pAvDemuxElement, videostreamidx, audiostreamidx, voutidx, aoutidx);
                continue;
            }
            if (voutidx >= 0) {
                CVideoPacketMeta* videoPacketMeta = pAvDemuxElement->vpacketPool->allocate();
                videoPacketMeta->pool = pAvDemuxElement->vpacketPool;
                videoPacketMeta->source = pAvDemuxElement->mName;
                videoPacketMeta->streamId = pAvDemuxParam->streamId;
                VDEC_STREAM_S* videoStream = (VDEC_STREAM_S*)malloc(sizeof(VDEC_STREAM_S));
                memset(videoStream, 0, sizeof(VDEC_STREAM_S));
                videoStream->bEndOfStream = ES_TRUE;
                videoPacketMeta->videoPkt = videoStream;
                videoPacketMeta->eosFlag = true;
                videoPacketMeta->isIpc = pAvDemuxParam->isIpc;
                videoPacketMeta->padIndex = pAvDemuxElement->mPadIndex;
                // app_error("the pipeline will exit !\n");
                // exit(0);
                printf("the pipeline will exit ! %d\n", videoPacketMeta->padIndex);
                app_ret ret = pAvDemuxElement->TransMitToNextToProcess((CBaseMeta*)videoPacketMeta, voutidx);
                if (pAvDemuxParam->isIpc == 1) {
                    gIpcDecCnt.ipcToDecodingCount[pAvDemuxElement->mPadIndex] += 1;
                }
                if (ret != APP_SUCCESS) {
                    app_error("video packet TransMitToNextToProcess error!\n");
                    // todo
                }
            }
            // audio
            if (aoutidx >= 0) {
                CAudioPacketMeta* audioPacketMeta = new CAudioPacketMeta;
                audioPacketMeta->source = pAvDemuxElement->mName;
                audioPacketMeta->bEndOfStream = ES_TRUE;
                audioPacketMeta->audioPkt = nullptr;
                app_ret ret = pAvDemuxElement->TransMitToNextToProcess((CBaseMeta*)audioPacketMeta, aoutidx);
                if (ret != APP_SUCCESS) {
                    app_error("audio packet TransMitToNextToProcess error!\n");
                    // todo
                }
            }

            break;
        } else if (pkt->stream_index == videostreamidx) {
            if (videoSendCount == 0) {
                firstVideoPts = pkt->pts;
            }

            if (bsf_ctx) {
                /**(5) 将输入packet提交到过滤器处理*/
                if (av_bsf_send_packet(bsf_ctx, pkt) < 0) {
                    av_packet_unref(pkt);
                    av_init_packet(pkt);
                    continue;
                }
                /**(6) 循环读取过滤器，直到返回0标明读取完毕*/
                for (;;) {
                    int flags = av_bsf_receive_packet(bsf_ctx, pkt);
                    if (flags == EAGAIN) {
                        continue;
                    } else {
                        break;
                    }
                }
            }

            CVideoPacketMeta* videoPacketMeta = pAvDemuxElement->vpacketPool->allocate();
            videoPacketMeta->pool = pAvDemuxElement->vpacketPool;
            videoPacketMeta->source = pAvDemuxElement->mName;
            videoPacketMeta->streamId = pAvDemuxParam->streamId;
            VDEC_STREAM_S* videoStream = (VDEC_STREAM_S*)malloc(sizeof(VDEC_STREAM_S));
            memset(videoStream, 0, sizeof(VDEC_STREAM_S));

            videoStream->PTS = (pkt->pts - firstVideoPts) * videoTimebase;
            videoStream->pAddr = pkt->data;
            videoStream->len = pkt->size;
            videoStream->bEndOfFrame = ES_TRUE;
            videoStream->bEndOfStream = ES_FALSE;

            videoPacketMeta->width = pAvDemuxParam->width;
            videoPacketMeta->height = pAvDemuxParam->height;
            videoPacketMeta->type = pAvDemuxParam->videotype;
            videoPacketMeta->index = videoSendCount;
            videoPacketMeta->pts = videoStream->PTS;
            videoPacketMeta->demuxPts = pkt->pts;
            videoPacketMeta->demuxDts = pkt->dts;
            videoPacketMeta->demuxDuration = pkt->duration;
            videoPacketMeta->timeBaseNum = pFmtCtx->streams[videostreamidx]->time_base.num;
            videoPacketMeta->timeBaseDen = pFmtCtx->streams[videostreamidx]->time_base.den;
            videoPacketMeta->keyFrame = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            videoPacketMeta->videoPkt = videoStream;
            videoPacketMeta->padIndex = pAvDemuxElement->mPadIndex;
            // app_ret ret =
            // pAvDemuxElement->TransMitToNextToProcess((CBaseMeta*)videoPacketMeta);

            // frameRate count
            if (frameInterval > 0 && pAvDemuxParam->isIpc != 1) {
                unsigned long long now = esclock();
                if (now < frameInterval + lastFrameTime) {
                    usleep(frameInterval + lastFrameTime - now);
                }
                lastFrameTime = esclock();
            }
            videoPacketMeta->isIpc = pAvDemuxParam->isIpc;
            app_ret ret = pAvDemuxElement->TransMitToNextToProcess((CBaseMeta*)videoPacketMeta, voutidx);
            if (pAvDemuxParam->isIpc == 1) {
                gIpcDecCnt.ipcToDecodingCount[pAvDemuxElement->mPadIndex] += 1;
            }
            if (ret != APP_SUCCESS) {
                app_error("video packet TransMitToNextToProcess error!\n");
                // todo
            }
            videoSendCount++;
            app_info(
                "SendVideoStream %s success, stream_index:%d, "
                "videoSendCount:%d.\n",
                pAvDemuxElement->mName.c_str(), pkt->stream_index, videoSendCount);
        } else if (pkt->stream_index == audiostreamidx) {
            if (audioSendCount == 0) {
                firstAudioPts = pkt->pts;
            }

            unsigned char* data = NULL;
            int data_len = 0;
            bool headerLessFlag = true;
            if (pkt->data[0] != 0xff | (pkt->data[0] & 0xf0) != 0xf0) {
                unsigned char adts_header[7];
                data_len = pkt->size;
                create_adts_header(adts_header, data_len, pAvDemuxParam->profile, pAvDemuxParam->sample_rate,
                                   pAvDemuxParam->num_channels);
                data_len += 7;
                data = (unsigned char*)malloc(data_len);
                memcpy(data, adts_header, 7);
                memcpy(data + 7, pkt->data, pkt->size);
                headerLessFlag = true;
            } else {
                data = pkt->data;
                data_len = pkt->size;
                headerLessFlag = false;
            }

            CAudioPacketMeta* audioPacketMeta = new CAudioPacketMeta;
            AUDIO_STREAM_S* audioStream = (AUDIO_STREAM_S*)malloc(sizeof(AUDIO_STREAM_S));
            memset(audioStream, 0, sizeof(AUDIO_STREAM_S));
            audioStream->TimeStamp = (pkt->pts - firstAudioPts) * audioTimebase;
            audioStream->Stream = data;
            audioStream->Len = data_len;
            // audioStream->Seq = 0;
            audioPacketMeta->source = pAvDemuxElement->mName;
            audioPacketMeta->pts = audioStream->TimeStamp;
            audioPacketMeta->bEndOfStream = ES_FALSE;
            audioPacketMeta->audioPkt = audioStream;
            audioPacketMeta->payloadType = PT_AAC;
            app_ret ret = pAvDemuxElement->TransMitToNextToProcess((CBaseMeta*)audioPacketMeta, aoutidx);
            if (ret != APP_SUCCESS) {
                app_error("audio packet TransMitToNextToProcess error!\n");
                // todo
            }
            if (headerLessFlag) {
                free(data);
            }
            audioSendCount++;
            app_info(
                "SendAudioStream %s success, stream_index:%d, "
                "audioSendCount:%d.\n",
                pAvDemuxElement->mName.c_str(), pkt->stream_index, audioSendCount);
        }
        av_packet_unref(pkt);
        //}
        pAvDemuxElement->sendStreamPerformance->performanceStaticEnd();
    }
    // pAvDemuxElement->sendStreamPerformance->performanceStaticReport();
    if (bsf_ctx) {
        av_bsf_free(&bsf_ctx);
    }
    av_packet_free(&pkt);
    avformat_close_input(&pFmtCtx);
    return (ES_VOID*)ES_SUCCESS;
}

app_ret AvDemuxElement::Init() {
    if (m_NextElementVec.size() < 1) {
        return APP_FAILURE;
    }
    // static int padIndex = 0;
    mPadIndex = getPadIndex();
    printf("%s mPadIndex: %d\n", mName.c_str(), mPadIndex);
    mElementType = AUDIO_DECODER;  // todo tmp modify
    memset(&avDemuxParam, 0, sizeof(AVDEMUX_PARAM_S));
    YAML::Node config = YAML::LoadFile(m_configFile);

    avDemuxParam.isIpc = 0;
    if (config["file"].IsDefined()) {
        string file = config["file"].template as<string>();
        memcpy(avDemuxParam.streamName, file.c_str(), file.size());
    } else if (config["url"].IsDefined()) {
        string url = config["url"].template as<string>();
        memcpy(avDemuxParam.streamName, url.c_str(), url.size());
        avDemuxParam.isIpc = 1;
        gIpcDecCnt.ipcIndexs[gIpcDecCnt.ipcCnt++] = mPadIndex;
    } else {
        // todo
    }

    const string streamId =
        config["stream-id"].IsDefined() ? config["stream-id"].template as<string>() : mName;
    snprintf(avDemuxParam.streamId, sizeof(avDemuxParam.streamId), "%s", streamId.c_str());

    if (config["outfps"].IsDefined()) {
        avDemuxParam.outfps = config["outfps"].template as<int>();
    } else {
        avDemuxParam.outfps = 0;
    }

    if (config["totalframe"].IsDefined()) {
        avDemuxParam.totalFrame = config["totalframe"].template as<int>();
    } else {
        avDemuxParam.totalFrame = 0;
    }

    YAML::Node debug = config["dump"];
    bool dumpFlag = debug["enable"].template as<bool>();
    if (dumpFlag) {
        string tmp = mName + "_dump.raw";
        dumpFp = fopen(tmp.c_str(), "wb");
    } else {
        dumpFp = NULL;
    }

    voutidx = -1;
    aoutidx = -1;
    DEMUXER_ProbeMeta(avDemuxParam.streamName, &avDemuxParam);
    avDemuxParam.element = this;

    vpacketPool = new MetaPool<CVideoPacketMeta>(100);

    sendStreamPerformance = new PerformanceStatic(mName, PERF_STATIC_SEGMENT);
    return APP_SUCCESS;
}

app_ret AvDemuxElement::Start() {
    int outputElementNum = m_NextElementVec.size();
    for (int i = 0; i < outputElementNum; i++) {
        if (m_NextElementVec[i]->mElementType == VIDEO_DECODER) {
            voutidx = i;
        } else if (m_NextElementVec[i]->mElementType == AUDIO_DECODER) {
            aoutidx = i;
        }
    }

    pthread_create(&sendDataPid, 0, plStartSendStream, (void*)(&avDemuxParam));
    app_info("demux %s thread created\n", mName.c_str());
    return APP_SUCCESS;
}

app_ret AvDemuxElement::Wait() {
    pthread_join(sendDataPid, ES_NULL);
    sendDataPid = 0;
    return APP_SUCCESS;
}

app_ret AvDemuxElement::ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) {
    app_ret ret = APP_SUCCESS;
    return ret;
}

app_ret AvDemuxElement::Finish() {
    if (dumpFp) {
        fclose(dumpFp);
    }
    delete vpacketPool;
    freeNumaNode(this, sizeof(AvDemuxElement));
    return APP_SUCCESS;
}

app_ret AvDemuxElement::TransMitToNextToProcess(CBaseMeta* baseMeta, int outChannelIdx) {
    app_ret ret = m_NextElementVec[outChannelIdx]->ProcessAndTransmit((CBaseMeta*)baseMeta, this);
    if (ret != APP_SUCCESS) {
        app_error("audio packet ProcessAndTransmit error!\n");
        // todo
    }
    return ret;
}

app_ret AvDemuxElement::perfStat() {
    sendStreamPerformance->performanceStaticReport();
    return APP_SUCCESS;
}

app_ret AvDemuxElement::InfoQuery(void* data, BASE_QUERY_TYPE type, BASE_QUERY_DIRECTION direction, int padIndex,
                                  CElement* inquirerElement) {
    app_ret ret = APP_SUCCESS;
    if (type == VIDEO_STREAM_INFO) {
        VideoStreamInfo* info = (VideoStreamInfo*)data;
        info->type = avDemuxParam.videotype;
        info->width = avDemuxParam.width;
        info->height = avDemuxParam.height;
    } else if (type == AUDIO_STREAM_INFO) {
        AudioStreamInfo* info = (AudioStreamInfo*)data;
        info->frame_size = avDemuxParam.frame_size;
        info->num_channels = avDemuxParam.num_channels;
        info->sample_rate = avDemuxParam.sample_rate;
    } else {
        ret = APP_FAILURE;
    }
    return ret;
}

extern "C" CElement* createEsAvDemuxElement(const char* name, const char* path, int loopNum, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(AvDemuxElement))) AvDemuxElement(name, path, loopNum, dieIndex);
}
