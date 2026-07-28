#define PL_LOG_ID PL_LOG_TESTSRC
#include "AdecDataGenerator.h"

#include "batch_meta.h"  // For CAudioPacketMeta

CAudioPacketMeta* AdecDataGenerator::prepareAudioPacketInputData(FILE* pfd) {
    CAudioPacketMeta* audioPakMeta = new CAudioPacketMeta();
    audioPakMeta->index = 0;
    audioPakMeta->source = m_adecDataInfo.source;

    ES_U32 u32Len = 640;
    ES_U32 u32ReadLen;

    AUDIO_STREAM_S* pAudioStream = new AUDIO_STREAM_S();
    ES_U8* pu8AudioStream = (ES_U8*)malloc(sizeof(ES_U8) * MAX_AUDIO_STREAM_LEN);
    if (NULL == pu8AudioStream) {
        printf("%s: malloc failed!\n", __FUNCTION__);
        delete pAudioStream;
        delete audioPakMeta;
        return NULL;
    }

    pAudioStream->Stream = pu8AudioStream;
    u32ReadLen = fread(pAudioStream->Stream, 1, u32Len, pfd);
    if (u32ReadLen <= 0) {
        free(pu8AudioStream);
        delete pAudioStream;
        delete audioPakMeta;
        return nullptr;  // End of audio file
    }

    pAudioStream->Len = u32ReadLen;
    audioPakMeta->payloadType = PT_AAC;
    audioPakMeta->audioPkt = pAudioStream;
    return audioPakMeta;
}

app_ret AdecDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto adecConfig = config["adecDataInput"];
    m_adecDataInfo.source = adecConfig["source"].as<std::string>();
    return APP_SUCCESS;
}

CBaseMeta* AdecDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CAudioPacketMeta* audioPakMeta;
    if (frameIndex < m_loopNum) {
        if (fp == NULL) {
            fp = fopen(m_adecDataInfo.source.c_str(), "rb");
            if (fp == NULL) {
                app_error("Failed to open audio file: %s", m_adecDataInfo.source.c_str());
                return nullptr;
            }
        }
        audioPakMeta = prepareAudioPacketInputData(fp);
        if (audioPakMeta == nullptr) {  // End of file reached
            audioPakMeta = new CAudioPacketMeta();
            audioPakMeta->eosFlag = true;
            audioPakMeta->bEndOfStream = ES_TRUE;
        }
    } else {  // frameIndex == m_loopNum
        audioPakMeta = new CAudioPacketMeta();
        audioPakMeta->eosFlag = true;
        audioPakMeta->bEndOfStream = ES_TRUE;
    }
    return static_cast<CBaseMeta*>(audioPakMeta);
}
