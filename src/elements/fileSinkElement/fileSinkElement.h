#ifndef _FILESINK_ELEMENT_H__
#define _FILESINK_ELEMENT_H__
#include "element.h"

#define FILE_NAME_LEN 128

class FileSinkElement : public CElement {
   public:
    FileSinkElement(const char *name = "filesinkelement", const char *config = "") : CElement(name, config){};
    ~FileSinkElement() = default;

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) override;
    app_ret startGetAudioFrame(string source, PAYLOAD_TYPE_E payloadType);
    app_ret Finish() override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self) override;
    app_ret perfStat() override;

   public:
    PerformanceStatic *saveStreamPerformance;

   private:
    bool chnStarted;
    string filePrefix;
    PAYLOAD_TYPE_E payloadType;
    // bool chnStarted;

    ES_CHAR saveFileName[FILE_NAME_LEN];
    FILE *outputFd;
    int frameCount;  // jpg _xxx.jpg
};
#endif  //_FILESINK_ELEMENT_H__
