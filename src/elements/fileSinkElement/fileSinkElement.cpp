#define PL_LOG_ID PL_LOG_OTHERS
#include "fileSinkElement.h"

#include <sys/prctl.h>
#include <yaml-cpp/yaml.h>

static string videoPt2Str(PAYLOAD_TYPE_E enType) {
    if (PT_H264 == enType) {
        return "h264";
    } else if (PT_H265 == enType) {
        return "h265";
    } else if (PT_JPEG == enType) {
        return "jpg";
    } else {
        return "data";
    }
}

app_ret FileSinkElement::Init() {
    /**
     * parse config.
     */
    // parse config
    YAML::Node config = YAML::LoadFile(m_configFile);
    // yaml:output
    YAML::Node yaml_dec = config["filesink"];
    filePrefix = yaml_dec["fileprefix"].template as<string>();
    payloadType = PT_BUTT;
    // outputFd=nullptr;
    // performance statics
    saveStreamPerformance = new PerformanceStatic(mName + "_FileSinkElement", PERF_STATIC_SEGMENT);
    return APP_SUCCESS;
}

app_ret FileSinkElement::Start() { return APP_SUCCESS; }

app_ret FileSinkElement::Wait() { return APP_SUCCESS; }

// return the position of the string, -1 means not find.
ES_S32 stringFindLastOf(const ES_CHAR *pString, const ES_CHAR tok) {
    if (pString == ES_NULL) {
        return -1;
    }
    ES_S32 len = strlen(pString);
    ES_S32 i;

    if (len <= 0) {
        return -1;
    }

    for (i = len - 1; i >= 0; i--) {
        if (pString[i] == tok) {
            return i;
        }
    }
    return -1;
}

ES_S32 saveStream(FILE *pFd, VENC_STREAM_S *pStream) {
    app_debug("saveStream packCount:%u, seq:%u! \n", pStream->packCount, pStream->seq);
    for (ES_U32 i = 0; i < pStream->packCount; i++) {
        ES_U64 dataSize = pStream->pPack[i].len - pStream->pPack[i].offset;
        ES_U64 *pVirAddr = (ES_U64 *)(pStream->pPack[i].pAddr + pStream->pPack[i].offset);

        if (ES_NULL == pFd) {
            app_error("%s \n", "FILESINK_SendStream: illegal params");
            return ES_FALSE;
        }

        if (dataSize > 0) {
            fwrite(pVirAddr, 1, dataSize, pFd);
            fflush(pFd);
        }
    }

    return ES_SUCCESS;
}

ES_S32 saveStreamJpeg(FILE *pFd, VENC_STREAM_S *pStream, char *filename) {
    app_debug("saveStream packCount:%u, seq:%u! \n", pStream->packCount, pStream->seq);
    for (ES_U32 i = 0; i < pStream->packCount; i++) {
        ES_U64 dataSize = pStream->pPack[i].len - pStream->pPack[i].offset;
        ES_U64 *pVirAddr = (ES_U64 *)(pStream->pPack[i].pAddr + pStream->pPack[i].offset);
        if (dataSize > 0) {
            FILE *fd = fopen(filename, "wb");
            fwrite(pVirAddr, 1, dataSize, fd);
            fflush(fd);
            fclose(fd);
        }
    }

    return ES_SUCCESS;
}

// CBaseMeta *baseMeta对当前接口而言，接收的应该是batchmeta
app_ret FileSinkElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    frameCount++;
    ES_S32 ret = ES_SUCCESS;
    app_debug("enter name:%s sinkname:%s  \n", mName.c_str(), mName.c_str());

    CVideoPacketMeta *pVideoPacketMeta = (CVideoPacketMeta *)baseMeta;

    // end(last) frame
    if (pVideoPacketMeta->eosFlag) {
        // end of filesink
        if (payloadType == PT_H264 || payloadType == PT_H265) {  // only video
            if (outputFd != 0) {
                fclose(outputFd);
            } else {
                app_error("FileSinkElement eos exit with no frame: sinkname:%s \n", mName.c_str());
            }
        }

        app_info("FileSinkElement eos exit: sinkname:%s \n", mName.c_str());
        return APP_SUCCESS;
    }

    if (!chnStarted) {
        payloadType = pVideoPacketMeta->type;
        assert(payloadType == PT_JPEG || payloadType == PT_H264 || payloadType == PT_H265);

        snprintf(saveFileName, FILE_NAME_LEN, "%s_%s.%s", filePrefix.c_str(), mName.c_str(),
                 videoPt2Str(payloadType).c_str());
        if (payloadType == PT_H264 || payloadType == PT_H265) {
            /* create file for save stream*/
            outputFd = fopen(saveFileName, "w+");
        }
        chnStarted = true;
        app_info("startChn startGetStream name:%s  \n", mName.c_str());
    }
    saveStreamPerformance->performanceStaticStart();

    VENC_STREAM_S *stream = pVideoPacketMeta->encVideoPkt;
    if (PT_JPEG == payloadType) {
        ES_CHAR jpeg_filename[FILE_NAME_LEN + 12 + 8] = {0};  // lenght("_chn127.h264")=12,length("_001.jpg")=8
        ES_CHAR purename[FILE_NAME_LEN] = {0};

        ES_S32 pos = stringFindLastOf(saveFileName, '.');
        memcpy(purename, saveFileName, pos);
        sprintf(jpeg_filename, "%s_%010d.jpg", purename, frameCount);

        ret = saveStreamJpeg(outputFd, stream, jpeg_filename);
    } else {
        assert(outputFd != nullptr);  // if send eos ,send normal frame assert
        ret = saveStream(outputFd, stream);
    }

    pVideoPacketMeta->reduceUseCount();

    saveStreamPerformance->performanceStaticEnd();
    app_debug("exit name:%s  \n", mName.c_str());
    return APP_SUCCESS;
}

app_ret FileSinkElement::perfStat() {
    saveStreamPerformance->performanceStaticReport();
    return APP_SUCCESS;
}

app_ret FileSinkElement::Finish() {
    // saveStreamPerformance->performanceStaticReport();
    delete saveStreamPerformance;
    return APP_SUCCESS;
}

app_ret FileSinkElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self) {
    // wait for start function finished
    if (!mRunningFlag) {
        sem_wait(&mStartFlag);
        mRunningFlag = true;
    }

    ProcessData(baseMeta, this);
    return APP_SUCCESS;
}

extern "C" CElement *createEsFileSinkElement(const char *name, const char *path) {
    return new FileSinkElement(name, path);
}
