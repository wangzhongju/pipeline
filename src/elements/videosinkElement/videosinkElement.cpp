#define PL_LOG_ID PL_LOG_VO
#include "videosinkElement.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <filesystem>

#include "../common/commyamlparser.h"
#include "es_vo.h"
#include "es_vps.h"

/* 按枚举值顺序填充，索引即 VO_INTF_SYNC_E 的值 */
static const struct voInterInfo VoDeviceInfo[] = {
    {720, 576, 50, VO_OUTPUT_PAL},             // 0
    {720, 480, 60, VO_OUTPUT_NTSC},            // 1
    {1920, 1080, 24, VO_OUTPUT_1080P24},       // 2
    {1920, 1080, 25, VO_OUTPUT_1080P25},       // 3
    {1920, 1080, 30, VO_OUTPUT_1080P30},       // 4
    {1920, 1080, 48, VO_OUTPUT_1080P48},       // 5
    {1280, 720, 25, VO_OUTPUT_720P25},         // 6
    {1280, 720, 30, VO_OUTPUT_720P30},         // 7
    {1280, 720, 50, VO_OUTPUT_720P50},         // 8
    {1280, 720, 60, VO_OUTPUT_720P60},         // 9
    {1280, 720, 100, VO_OUTPUT_720P100},       // 10
    {1280, 720, 120, VO_OUTPUT_720P120},       // 11
    {1920, 1080, 50, VO_OUTPUT_1080I50},       // 12
    {1920, 1080, 60, VO_OUTPUT_1080I60},       // 13
    {1920, 1080, 50, VO_OUTPUT_1080P50},       // 14
    {1920, 1080, 60, VO_OUTPUT_1080P60},       // 15
    {1920, 1080, 100, VO_OUTPUT_1080P100},     // 16
    {1920, 1080, 120, VO_OUTPUT_1080P120},     // 17
    {720, 576, 50, VO_OUTPUT_576P50},          // 18
    {720, 576, 100, VO_OUTPUT_576P100},        // 19
    {720, 576, 200, VO_OUTPUT_576P200},        // 20
    {720, 480, 60, VO_OUTPUT_480P60},          // 21
    {720, 480, 120, VO_OUTPUT_480P120},        // 22
    {720, 480, 240, VO_OUTPUT_480P240},        // 23
    {800, 600, 60, VO_OUTPUT_800x600_60},      // 24
    {1024, 768, 60, VO_OUTPUT_1024x768_60},    // 25
    {1152, 864, 75, VO_OUTPUT_1152x864_75},    // 26
    {1280, 1024, 60, VO_OUTPUT_1280x1024_60},  // 27
    {1366, 768, 60, VO_OUTPUT_1366x768_60},    // 28
    {1440, 900, 60, VO_OUTPUT_1440x900_60},    // 29
    {1440, 480, 60, VO_OUTPUT_1440x480_60},    // 30
    {1440, 576, 50, VO_OUTPUT_1440x576_50},    // 31
    {1280, 800, 60, VO_OUTPUT_1280x800_60},    // 32
    {1600, 1200, 60, VO_OUTPUT_1600x1200_60},  // 33
    {1600, 900, 60, VO_OUTPUT_1600x900_60},    // 34
    {1680, 1050, 60, VO_OUTPUT_1680x1050_60},  // 35
    {1920, 1200, 60, VO_OUTPUT_1920x1200_60},  // 36
    {640, 480, 60, VO_OUTPUT_640x480_60},      // 37
    {960, 576, 50, VO_OUTPUT_960H_PAL},        // 38
    {960, 480, 60, VO_OUTPUT_960H_NTSC},       // 39
    {1920, 2160, 30, VO_OUTPUT_1920x2160_30},  // 40
    {2560, 1440, 30, VO_OUTPUT_2560x1440_30},  // 41
    {2560, 1440, 60, VO_OUTPUT_2560x1440_60},  // 42
    {2560, 1600, 60, VO_OUTPUT_2560x1600_60},  // 43
    {2880, 240, 60, VO_OUTPUT_2880x240_60},    // 44
    {2880, 288, 50, VO_OUTPUT_2880x288_50},    // 45
    {2880, 480, 60, VO_OUTPUT_2880x480_60},    // 46
    {2880, 576, 50, VO_OUTPUT_2880x576_50},    // 47
    {3840, 2160, 24, VO_OUTPUT_3840x2160_24},  // 48
    {3840, 2160, 25, VO_OUTPUT_3840x2160_25},  // 49
    {3840, 2160, 30, VO_OUTPUT_3840x2160_30},  // 50
    {3840, 2160, 48, VO_OUTPUT_3840x2160_48},  // 51
    {3840, 2160, 50, VO_OUTPUT_3840x2160_50},  // 52
    {3840, 2160, 60, VO_OUTPUT_3840x2160_60},  // 53
    {4096, 2160, 24, VO_OUTPUT_4096x2160_24},  // 54
    {4096, 2160, 25, VO_OUTPUT_4096x2160_25},  // 55
    {4096, 2160, 30, VO_OUTPUT_4096x2160_30},  // 56
    {4096, 2160, 48, VO_OUTPUT_4096x2160_48},  // 57
    {4096, 2160, 50, VO_OUTPUT_4096x2160_50},  // 58
    {4096, 2160, 60, VO_OUTPUT_4096x2160_60},  // 59
    {320, 240, 60, VO_OUTPUT_320x240_60},      // 60
    {320, 240, 50, VO_OUTPUT_320x240_50},      // 61
    {240, 320, 50, VO_OUTPUT_240x320_50},      // 62
    {240, 320, 60, VO_OUTPUT_240x320_60},      // 63
    {800, 600, 50, VO_OUTPUT_800x600_50},      // 64
    {720, 1280, 60, VO_OUTPUT_720x1280_60},    // 65
    {1080, 1920, 60, VO_OUTPUT_1080x1920_60},  // 66
    {7680, 4320, 30, VO_OUTPUT_7680x4320_30},  // 67
    {0, 0, 0, VO_OUTPUT_USER},                 // 68
    {0, 0, 0, VO_OUTPUT_BUTT}                  // 69
};

int WH2Sync(int w, int h, int r) {
    for (int i = 0; i < sizeof(VoDeviceInfo) / sizeof(VoDeviceInfo[0]); i++) {
        const struct voInterInfo *p = &VoDeviceInfo[i];
        if (p->width == w && p->height == h && p->refresh == r) {
            return p->interfaceSync;
        }
    }
    return VO_OUTPUT_BUTT; /* 未找到 */
}

int Sync2WH(int *w, int *h, int sync) {
    for (int i = 0; i < sizeof(VoDeviceInfo) / sizeof(VoDeviceInfo[0]); i++) {
        const struct voInterInfo *p = &VoDeviceInfo[i];
        if (p->interfaceSync == sync) {
            *w = p->width;
            *h = p->height;
            return p->interfaceSync;
        }
    }
    return VO_OUTPUT_BUTT; /* 未找到 */
}

// PerformanceStatic *batchReleasePerformance = new PerformanceStatic("batchMetaRelease_Performance",
// PERF_STATIC_SEGMENT);

app_ret VideosinkElement::parseConfigFile(std::string _configFilePath) {
    if (_configFilePath.empty() || !std::filesystem::exists(_configFilePath)) {
        app_error(" %s %s\n", "Config File  doesn't exist, the file path is : ", _configFilePath.c_str());
        return APP_FAILURE;
    }

    YAML::Node configyml = YAML::LoadFile(_configFilePath);

    if (!(configyml.size() > 0)) {
        app_error(" %s %s\n", "Unable to parse config file, the file path is : ", _configFilePath.c_str());
        return APP_FAILURE;
    }

    m_initParams.outfps = 25;
    // Parse the config file here
    for (YAML::const_iterator itr = configyml.begin(); itr != configyml.end(); ++itr) {
        std::string paramKey = itr->first.as<std::string>();
        if (paramKey == "die-id") {
            int deviceID = itr->second.as<int>();
            m_initParams.dieID = deviceID;
            app_debug(" %s %d\n", "die-id is : ", m_initParams.dieID);
        } else if (paramKey == "interface-auto") {
            m_initParams.auto_inter = itr->second.as<bool>();
            app_debug(" m_initParams.auto_inter %d\n", m_initParams.auto_inter);
        } else if (paramKey == "dumpflag") {
            m_initParams.dumpflag = itr->second.as<int>();
            app_debug(" %s %d\n", "dumpflag is : ", m_initParams.dumpflag);
        } else if (paramKey == "interface-sync") {
            m_initParams.interfaceSync = itr->second.as<int>();
            app_debug(" %s %d\n", "interface-sync is : ", m_initParams.interfaceSync);
        } else if (paramKey == "interface-type") {
            m_initParams.interfaceType = itr->second.as<int>();
            app_debug(" %s %d\n", "interface-type is : ", m_initParams.interfaceType);
        } else if (paramKey == "image-size") {
            std::vector<int> tempImageSize = itr->second.as<std::vector<int>>();
            if (tempImageSize.size() != 2) {
                app_debug(" %s\n", "image-size size is not equal 2 ");
            }
            m_initParams.imageSize = tempImageSize;

            app_debug(" %s %d x %d\n", "image-size is : ", m_initParams.imageSize[0], m_initParams.imageSize[1]);
        } else if (paramKey == "disp-rect") {
            std::vector<int> tempRect = itr->second.as<std::vector<int>>();
            if (tempRect.size() != 4) {
                app_debug(" %s\n", "disp-rect size is not equal 4 ");
            }
            m_initParams.dispRect = tempRect;
            app_debug(" %s [ %d x %d ], [ %d x %d ]\n", "disp-rect is : ", m_initParams.dispRect[0],
                      m_initParams.dispRect[1], m_initParams.dispRect[2], m_initParams.dispRect[3]);
        } else if (paramKey == "channel-params") {
            for (auto it = itr->second.begin(); it != itr->second.end(); it++) {
                std::vector<int> tempRect = it->as<std::vector<int>>();
                if (tempRect.size() != 5) {
                    app_debug(" %s\n", "channel-rect size is not equal 5 ");
                    continue;
                }
                app_debug(" %s %d %s [ %d x %d ], [ %d x %d ]\n", "channel id is : ", tempRect[0],
                          "rect is : ", tempRect[1], tempRect[2], tempRect[3], tempRect[4]);
                m_initParams.channelRect.push_back(tempRect);
            }
        } else if (paramKey == "out-fps") {
            m_initParams.outfps = itr->second.as<int>();
            app_debug(" %s %d\n", "interface-sync is : ", m_initParams.interfaceSync);
        } else {
            app_debug(" %s %s\n", "Unknown parameter  : ", paramKey.c_str());
        }
    }

    string layerformat = GET_KEY_VALUE<string>(configyml, "layer-format", "nv12", false);  // default use nv12
    if (layerformat == "nv12") {
        m_initParams.pixelFormat = PIXEL_FORMAT_NV12;
    } else if (layerformat == "rgb") {
        m_initParams.pixelFormat = PIXEL_FORMAT_R8G8B8;
    } else if (layerformat == "bgra") {
        m_initParams.pixelFormat = PIXEL_FORMAT_B8G8R8A8;
    }

    return APP_SUCCESS;
}

app_ret VideosinkElement::Init() {
    app_ret ret = parseConfigFile(m_configFile);
    m_initParams.dieID = m_dieIndex;
    // outDelay = 1000 * 1000 / m_initParams.outfps;
    lastPts = 0;

    videoSinkPerformance = new PerformanceStatic(mName + "_EsVideoSink_Performance", PERF_STATIC_SEGMENT);
    // 1. 配置输出设备的公共属性并启用视频输出设备
    app_debug(" %s \n", " will ES_VPS_Init ");
    ret = ES_VPS_Init();
    if (ret != ES_SUCCESS) {
        app_error(" %s 0x%x\n", " ES_VPS_Init failure , the ret is ", ret);
        return ES_FAILURE;
    }

    // vo modules
    app_debug(" %s \n", " will ES_VO_Init ");
    ret = ES_VO_Init();
    if (ret != ES_SUCCESS) {
        app_error(" %s 0x%x \n", " ES_VO_Init failure, the ret is ", ret);
        return ES_FAILURE;
    }

    ret = ES_HDMI_Init();
    app_debug("the ES_HDMI_Init ret is 0x%x\n", ret);
    ES_HDMI_DISPLAY_MODE_S DispMode = {0};
    ret = ES_HDMI_GetDispMode(ES_HDMI_ID_0, &DispMode);
    app_debug("the ES_HDMI_GetDispMode ret is 0x%x \n", ret);
    app_debug("the vo hdmi num is %d\n", DispMode.modesNum);

    int max_width = 0, max_height = 0, max_refreh = 0, selecIndex = 0;
    ;
    for (int indexVo = 0; indexVo < DispMode.modesNum; indexVo++) {
        app_debug(" clock %d refresh %d type %d flags %d the h is %d %d %d %d , the v is %d %d %d %d ; \n",
                  DispMode.modes[indexVo].clock, DispMode.modes[indexVo].refresh, DispMode.modes[indexVo].type,
                  DispMode.modes[indexVo].flags, DispMode.modes[indexVo].hdisplay, DispMode.modes[indexVo].hsyncStart,
                  DispMode.modes[indexVo].hsyncEnd, DispMode.modes[indexVo].htotal, DispMode.modes[indexVo].vdisplay,
                  DispMode.modes[indexVo].vsyncStart, DispMode.modes[indexVo].vsyncEnd, DispMode.modes[indexVo].vtotal);
        app_debug(" \n");
        if (DispMode.modes[indexVo].hdisplay >= max_width && DispMode.modes[indexVo].vdisplay >= max_height &&
            DispMode.modes[indexVo].refresh >= max_refreh) {
            selecIndex = indexVo;
            max_width = DispMode.modes[indexVo].hdisplay;
            max_height = DispMode.modes[indexVo].vdisplay;
            max_refreh = DispMode.modes[indexVo].refresh;
            // break;
        }
    }

    if (m_initParams.auto_inter) {
        int sync = WH2Sync(max_width, max_height, max_refreh);
        if (VO_OUTPUT_BUTT == sync) {
            app_error(" nout find for { %d %d %d}\n", max_width, max_height, max_refreh);
        }
        m_initParams.interfaceSync = sync;
        m_initParams.imageSize[0] = max_width;
        m_initParams.imageSize[1] = max_height;
        m_initParams.channelRect[0][1] = m_initParams.dispRect[0] = 0;
        m_initParams.channelRect[0][2] = m_initParams.dispRect[1] = 0;
        m_initParams.channelRect[0][3] = m_initParams.dispRect[2] = max_width;
        m_initParams.channelRect[0][4] = m_initParams.dispRect[3] = max_height;
        app_debug("\n VO init auto by  %d  max [%d %d ]\n", sync, max_width, max_height);
    } else {
        int sync = Sync2WH(&max_width, &max_height, m_initParams.interfaceSync);
        if (VO_OUTPUT_BUTT == sync) {
            app_error(" nout find for { %d %d %d}\n", max_width, max_height, max_refreh);
        }
        if (m_initParams.imageSize[0] > max_width || m_initParams.imageSize[1] > max_height) {
            // m_initParams.imageSize[0] = max_width;
            // m_initParams.imageSize[1] = max_height;
            app_error("imageSize large than DEV display size, need check,now %d %d \n", max_width, max_height);
        }

        if (m_initParams.dispRect[2] > max_width || m_initParams.dispRect[3] > max_height) {
            // m_initParams.channelRect[0][1] = m_initParams.dispRect[0] = 0;
            // m_initParams.channelRect[0][2] = m_initParams.dispRect[1] = 0;
            // m_initParams.channelRect[0][3] = m_initParams.dispRect[2] = max_width;
            // m_initParams.channelRect[0][4] = m_initParams.dispRect[3] = max_height;
            app_error("channelRect large than DEV display size, use %d %d  \n", max_width, max_height);
        }
    }

    ES_HDMI_DeInit();

    app_debug(" %s \n", " will ES_VO_SetPubAttr ");
    VO_PUB_ATTR_S pstPubAttr = {0};
    app_debug(" %s %d\n", " the HDMI is ", int(VO_INTF_HDMI));
    pstPubAttr.intfType = (0x01L << (int)m_initParams.interfaceType);
    app_debug(" %s %d\n", " the infertace sync is ", int(m_initParams.interfaceSync));
    pstPubAttr.intfSync = (VO_INTF_SYNC_E)m_initParams.interfaceSync;
    ret = ES_VO_SetPubAttr(m_initParams.dieID, &pstPubAttr);
    app_debug(" %s %X\n", " the ES_VO_SetPubAttr ret is ", ret);
    if (APP_SUCCESS != ret) {
        app_error(" %s \n", "ES_VO_SetPubAttr  failure ");
        return APP_FAILURE;
    }

    app_debug(" %s \n", " will ES_VO_Enable ");
    ret = ES_VO_Enable(m_initParams.dieID, m_initParams.dieID);
    app_debug(" %s %X\n", " the ES_VO_Enable ret is ", ret);
    if (APP_SUCCESS != ret) {
        app_error(" %s \n", "ES_VO_Enable  failure ");
        return APP_FAILURE;
    }

    // 2. 设置视频层属性并使能视频层
    app_debug(" %s \n", " will ES_VO_SetVideoLayerAttr ");
    VO_VIDEO_LAYER_ATTR_S pLayerAttr = {0};
    pLayerAttr.pixFormat = m_initParams.pixelFormat;  // PIXEL_FORMAT_B8G8R8A8; //PIXEL_FORMAT_NV12;
    pLayerAttr.dispRect.x = m_initParams.dispRect[0];
    pLayerAttr.dispRect.y = m_initParams.dispRect[1];
    pLayerAttr.dispRect.width = m_initParams.dispRect[2];
    pLayerAttr.dispRect.height = m_initParams.dispRect[3];
    pLayerAttr.imageSize.width = m_initParams.imageSize[0];
    pLayerAttr.imageSize.height = m_initParams.imageSize[1];
    app_debug(" %s dispRect is [ %d, %d, %d, %d], imgSize is [%d , %d]\n",
              " the vo layer param is : ", pLayerAttr.dispRect.x, pLayerAttr.dispRect.y, pLayerAttr.dispRect.width,
              pLayerAttr.dispRect.height, pLayerAttr.imageSize.width, pLayerAttr.imageSize.height);
    ret = ES_VO_SetVideoLayerAttr(m_initParams.dieID, &pLayerAttr);
    app_debug(" %s %X\n", " the ES_VO_SetVideoLayerAttr ret is ", ret);
    if (APP_SUCCESS != ret) {
        app_error(" %s \n", "ES_VO_SetVideoLayerAttr  failure ");
        return APP_FAILURE;
    }

    app_debug(" %s \n", " will ES_VO_EnableVideoLayer ");
    ret = ES_VO_EnableVideoLayer(m_initParams.dieID);
    app_debug(" %s %X\n", " the ES_VO_EnableVideoLayer ret is ", ret);

    // 3. 配置输出通道的属性并使能通道
    for (int iChanelIndex = 0; iChanelIndex < m_initParams.channelRect.size(); iChanelIndex++) {
        VO_CHN_ATTR_S pstChnAttr = {0};
        int chanelID = m_initParams.channelRect[iChanelIndex][0];
        pstChnAttr.priority = 0;
        pstChnAttr.rect.x = m_initParams.channelRect[iChanelIndex][1];
        pstChnAttr.rect.y = m_initParams.channelRect[iChanelIndex][2];
        pstChnAttr.rect.width = m_initParams.channelRect[iChanelIndex][3];
        pstChnAttr.rect.height = m_initParams.channelRect[iChanelIndex][4];
        app_debug(" %s \n", " will ES_VO_SetChnAttr ");
        app_debug(" %s %d\n", " the channel priority is ", pstChnAttr.priority);
        app_debug(" %s [%d, %d, %d, %d, %d]\n", " the chanel params is ", chanelID, pstChnAttr.rect.x,
                  pstChnAttr.rect.y, pstChnAttr.rect.width, pstChnAttr.rect.height);
        ret = ES_VO_SetChnAttr(m_initParams.dieID, chanelID, &pstChnAttr);
        app_debug(" %s %X\n", " the ES_VO_SetChnAttr ret is ", ret);
        if (APP_SUCCESS != ret) {
            app_error(" %s \n", "ES_VO_SetChnAttr  failure ");
            return APP_FAILURE;
        }

        app_debug(" %s \n", " will ES_VO_EnableChn ");
        ret = ES_VO_EnableChn(m_initParams.dieID, chanelID);
        app_debug(" %s %X\n", " the ES_VO_EnableChn ret is ", ret);
        if (APP_SUCCESS != ret) {
            app_error(" %s \n", "ES_VO_EnableChn  failure ");
            return APP_FAILURE;
        }
    }

    return ret;
}

app_ret VideosinkElement::Finish() {
    app_debug(" %s %s\n", mName.c_str(), " VideosinkElement::Finish start ");
    app_ret ret;
    // 1. 禁用通道
    for (int iChanelIndex = 0; iChanelIndex < m_initParams.channelRect.size(); iChanelIndex++) {
        int chanelID = m_initParams.channelRect[iChanelIndex][0];
        ret = ES_VO_DisableChn(m_initParams.dieID, chanelID);
        app_debug(" %s %X\n", " the ES_VO_DisableChn ret is ", ret);
        if (APP_SUCCESS != ret) {
            app_error(" %s \n", "ES_VO_DisableChn  failure ");
            return APP_FAILURE;
        }
        app_debug(" %s \n", "ES_VO_DisableChn  success ");
    }
    // 2. 禁用视频层
    ret = ES_VO_DisableVideoLayer(m_initParams.dieID);
    app_debug(" %s %X\n", " the ES_VO_DisableVideoLayer ret is ", ret);
    if (APP_SUCCESS != ret) {
        app_error(" %s \n", "ES_VO_DisableVideoLayer  failure ");
        return APP_FAILURE;
    }
    app_debug(" %s \n", "ES_VO_DisableVideoLayer  success ");

    // 3. 禁用视频输出设备
    ret = ES_VO_Disable(m_initParams.dieID);
    app_debug(" %s %X\n", " the ES_VO_Disable ret is ", ret);
    if (APP_SUCCESS != ret) {
        app_error(" %s \n", "ES_VO_Disable  failure ");
        return APP_FAILURE;
    }
    app_debug(" %s \n", "ES_VO_Disable  success ");
    app_debug(" %s %s\n", mName.c_str(), " VideosinkElement::Finish end ");
    delete videoSinkPerformance;
    videoSinkPerformance = NULL;
    m_initParams.imageSize.clear();
    m_initParams.dispRect.clear();
    m_initParams.channelRect.clear();
    freeNumaNode(this, sizeof(VideosinkElement));
    return APP_SUCCESS;
}

ES_VOID CopyWBDateToFile(ES_S32 width, ES_S32 height, ES_S32 bit) {
    app_debug(" %s\n", " VideosinkElement::CopyWBDateToFile start ");
    static int frameIndex = 0;
    ES_S32 outFd = -1;
    ES_S32 inFd = -1;
    ES_U8 *data;
    ES_CHAR readFilePath[200] = {0};
    ES_CHAR writeFilePath[200] = {0};
    ES_S32 dataLen, readLen, writeLen;

    dataLen = width * height * 4;
    snprintf(readFilePath, sizeof(readFilePath), "/sys/kernel/debug/dri/0/Virtual-1/%dx%d-XRGB-%d.raw", width, height,
             bit);
    snprintf(writeFilePath, sizeof(readFilePath), "/root/%dx%d-XRGB-%d_%d.raw", width, height, bit, frameIndex);
    app_error(" %s %s\n", " the file path is : ", readFilePath);
    frameIndex++;
    inFd = open(readFilePath, O_RDONLY, 0);
    if (inFd < 0) {
        app_error(" %s \n", " open read file error ");
        return;
    } else {
        app_debug(" %s \n", " open read file success ");
    }
    data = (ES_U8 *)malloc(dataLen);
    if (!data) {
        app_error(" %s \n", " alloc data error ");
        close(inFd);
        return;
    }

    readLen = read(inFd, data, dataLen);
    if (readLen != dataLen) {
        app_error(" %s \n", " read file error ");
    }

    outFd = open(writeFilePath, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (outFd < 0) {
        app_error(" %s \n", " create write file error ");
        close(inFd);
        free(data);
        return;
    }

    writeLen = write(outFd, data, readLen);
    app_error(" dataLen: %d, readLen: %d, writeLen: %d\n", dataLen, readLen, writeLen);
    if (writeLen != readLen) {
        app_error(" %s \n", " writelen is not readlen ");
    }

    app_debug(" dataLen: %d, readLen: %d, writeLen: %d\n", dataLen, readLen, writeLen);
    close(inFd);
    close(outFd);
    free(data);
    app_debug(" %s\n", " VideosinkElement::CopyWBDateToFile end ");
    return;
}

app_ret VideosinkElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    app_ret ret;
    app_debug(" %s \n", " VideosinkElement::ProcessData enter ");
    VIDEO_FRAME_INFO_S *pFrameInfo = NULL;
    VIDEO_FRAME_INFO_S gridFrameInfo;
    gridFrameInfo.videoFrame.fd = 0;
    app_debug(" %s %d\n", " the meta type is ", (int)baseMeta->mMetaType);

    if (!m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        set_thread_affinity(m_dieIndex);
        getCpuNumaID(__func__);
        m_cpuSetFlag = true;
    }

    if (baseMeta->eosFlag) {
        app_debug(" %s \n", " get the eosflag frame ");
        if (m_initParams.dumpflag) {
            usleep(1000 * 200);
            CopyWBDateToFile(saveFrameWidth, saveFrameHeight, 8);
        }
        baseMeta->reduceUseCount();
        return APP_SUCCESS;
    }
    auto start = std::chrono::high_resolution_clock::now();

    if (FRAME_META == baseMeta->mMetaType) {
        // CFrameMeta *pFrameMeta = (CFrameMeta *)(baseMeta);
        // outDelay = pFrameMeta->smuxTimeout > 5 ? (batchMeta->smuxTimeout - 5) * 1000: (batchMeta->smuxTimeout ) *
        // 1000;
    } else if (BATCH_META == baseMeta->mMetaType) {
        CBatchMeta *batchMeta = (CBatchMeta *)(baseMeta);

        outDelay = batchMeta->smuxTimeout > 5 ? (batchMeta->smuxTimeout - 5) * 1000 : (batchMeta->smuxTimeout) * 1000;
    }
    unsigned long long time = esclock();
    if (lastPts > 0 && lastPts + outDelay > time) {
        usleep(lastPts + outDelay - time);
    }
    lastPts = esclock();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    app_info("the usleep cost all time is took %f ms\n", elapsed.count());

    if (FRAME_META == baseMeta->mMetaType) {
        CFrameMeta *pFrameMeta = (CFrameMeta *)(baseMeta);
        if (pFrameMeta->images.size() == 2) {
            pFrameInfo = pFrameMeta->images[1]->mPic;
        } else if (pFrameMeta->images.size() == 1) {
            pFrameInfo = pFrameMeta->images[0]->mPic;
        }
    } else if (BATCH_META == baseMeta->mMetaType) {
        CBatchMeta *batchMeta = (CBatchMeta *)(baseMeta);
        if (NULL != batchMeta->videoGrid) {
            app_debug(" %s \n", " will send the gridMeta to vo ");
            pFrameInfo = batchMeta->videoGrid->gridPic;
        } else {
            baseMeta->reduceUseCount();
            app_debug(" %s \n", " the batchMeta gridMeta is NULL ");
            return APP_SUCCESS;
        }
    }
    saveFrameWidth = pFrameInfo->videoFrame.width;
    saveFrameHeight = pFrameInfo->videoFrame.height;
    // else if(BATCH_META==baseMeta->mMetaType)
    // {
    //     CBatchMeta *pBatchMeta = (CBatchMeta *)(baseMeta);
    //     if(NULL != pBatchMeta->videoGrid)
    //     {
    //         pFrameInfo = &pBatchMeta->videoGrid->data_format;
    //     }
    // }

    videoSinkPerformance->performanceStaticStart();
    if (NULL != pFrameInfo) {
        app_debug(" %s \n", " start ES_VO_SendFrame ");
        app_debug(" %s %d\n", " m_initParams.dieID is  ", m_initParams.dieID);
        auto start = std::chrono::high_resolution_clock::now();
        ret = ES_VO_SendFrame(m_initParams.dieID, 0, pFrameInfo, 1000);  // playback
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        app_info("the ES_VO_SendFrame cost all time is took %f ms\n", elapsed.count());
        app_debug(" %s %X\n", " the ES_VO_SendFrame ret is ", ret);
        gVoCount++;
    }

    app_debug("%s %d\n", " the use count is: ", baseMeta->getUseCount());
    // auto start = std::chrono::high_resolution_clock::now();
    baseMeta->reduceUseCount();
    //     auto end = std::chrono::high_resolution_clock::now();
    //   std::chrono::duration<double, std::milli> elapsed = end - start;
    //   app_info("the ES_VO_SendFrame reduce release  cost all time is took %f ms\n", elapsed.count());
    videoSinkPerformance->performanceStaticEnd();
    app_debug(" %s \n", " VideosinkElement::ProcessData exit ");
    return APP_SUCCESS;
}

app_ret VideosinkElement::perfStat() {
    videoSinkPerformance->performanceStaticReport();
    frameReleasePerformance->performanceStaticReport();
    preMetaReleasePerformance->performanceStaticReport();
    npuMetaReleasePerformance->performanceStaticReport();
    rawImgMetaReleasePerformance->performanceStaticReport();
    objMetaReleasePerformance->performanceStaticReport();
    return APP_SUCCESS;
}

extern "C" CElement *createEsVideoSinkElement(const char *name, const char *configFile, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(VideosinkElement))) VideosinkElement(name, configFile, dieIndex);
}
