#ifndef _VIDEOSINK_ELEMENT_H__
#define _VIDEOSINK_ELEMENT_H__

#include "batch_meta.h"
#include "element.h"
#include "es_vo.h"

struct voInterInfo {
    int width = 0;
    int height = 0;
    int refresh = 0;
    int interfaceSync = VO_OUTPUT_BUTT;
};

struct VideosinkInitParams {
    VideosinkInitParams() {
        dieID = 0;
        interfaceType = VO_INTF_HDMI;
        interfaceSync = VO_OUTPUT_720P30;
        dumpflag = false;
        imageSize.resize(2);
        imageSize[0] = 0;
        imageSize[1] = 0;
        dispRect.resize(4);
        dispRect[0] = 0;
        dispRect[1] = 0;
        dispRect[2] = 0;
        dispRect[3] = 0;
    }
    uint dieID;
    uint interfaceType;
    uint interfaceSync;
    uint outfps;
    bool dumpflag;
    bool auto_inter = false;
    std::vector<int> imageSize;
    std::vector<int> dispRect;
    std::vector<std::vector<int>> channelRect;
    PIXEL_FORMAT_E pixelFormat;
};

struct LayerAttr {
    int layerAttrIndex;
    int width;
    int height;
    int fps;
};

class VideosinkElement : public CElement {
   public:
    VideosinkElement(const char *name = "", const char *configFile = "", int dieIndex = 0)
        : CElement(name, configFile, dieIndex) {};
    ~VideosinkElement() = default;
    app_ret Init() override;
    app_ret Finish() override;
    app_ret perfStat() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement = 0) override;

   private:
    app_ret parseConfigFile(std::string _configFilePath);
    VideosinkInitParams m_initParams;
    int saveFrameWidth;
    int saveFrameHeight;
    int outDelay;
    unsigned long long lastPts;
    PerformanceStatic *videoSinkPerformance;
};
#endif  //_VIDEOSINK_ELEMENT_H__