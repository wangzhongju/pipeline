#define PL_LOG_ID PL_LOG_TESTSRC
#include "FaceSelectDataGenerator.h"

#include <random>

CBatchMeta *FaceSelectDataGenerator::prepareFaceselectTestData(int frameIndex, FILE *&fp) {
    // This function is almost identical to the original global function
    string file_path = m_encodeInputDataInfo.filepath;
    // The original code re-opened the file pointer, but we will reuse the one passed in.
    CFrameMeta *frameMeta = (CFrameMeta *)prepareEncodeInputData(m_encodeInputDataInfo, m_fps, frameIndex, fp);
    if (!frameMeta) {
        return nullptr;
    }

    frameMeta->index = frameIndex;
    frameMeta->pts = frameIndex * (1000 / m_fps);

    std::default_random_engine e;
    std::uniform_int_distribution<int> u(1, 10);  // 左闭右闭区间
    e.seed(time(0));

    // 0: 该trackID的objMeta评分大于阈值,会一直存在；
    CObjectMeta *objMeta0 = new CObjectMeta();
    objMeta0->objLable = "face";
    objMeta0->detectorConfidence = 10;
    objMeta0->trackerId = 0;
    frameMeta->objs.push_back(objMeta0);

    // 1: 该trackID的objMeta评分大于阈值,只存在一半的时间；
    if (frameIndex < m_loopNum / 2) {
        CObjectMeta *objMeta1 = new CObjectMeta();
        objMeta1->objLable = "face";
        objMeta1->detectorConfidence = 10;
        objMeta1->trackerId = 1;
        objMeta1->detectorBboxInfo.left = 0.1;
        objMeta1->detectorBboxInfo.top = 0.1;
        objMeta1->detectorBboxInfo.width = 0.1;
        objMeta1->detectorBboxInfo.height = 0.1;
        frameMeta->objs.push_back(objMeta1);
    }

    // 2: 该trackID的objMeta评分小于阈值,会一直存在；
    CObjectMeta *objMeta2 = new CObjectMeta();
    objMeta2->objLable = "face";
    objMeta2->detectorConfidence = 0.0;
    objMeta2->trackerId = 2;
    objMeta2->detectorBboxInfo.left = 0.1;
    objMeta2->detectorBboxInfo.top = 0.2;
    objMeta2->detectorBboxInfo.width = 0.2;
    objMeta2->detectorBboxInfo.height = 0.2;
    frameMeta->objs.push_back(objMeta2);

    // 3: 该trackID的objMeta评分小于阈值,只存在一半的时间；
    if (frameIndex < m_loopNum / 2) {
        CObjectMeta *objMeta3 = new CObjectMeta();
        objMeta3->objLable = "face";
        objMeta3->detectorConfidence = 0.0;
        objMeta3->trackerId = 3;
        objMeta3->detectorBboxInfo.left = 0.1;
        objMeta3->detectorBboxInfo.top = 0.2;
        objMeta3->detectorBboxInfo.width = 0.2;
        objMeta3->detectorBboxInfo.height = 0.4;
        frameMeta->objs.push_back(objMeta3);
    }

    // 4: 该trackID的objMeta评分随机生成，存在时间为半秒；
    if (m_fps + m_fps / 2 <= frameIndex && frameIndex <= 2 * m_fps) {
        CObjectMeta *objMeta4 = new CObjectMeta();
        objMeta4->objLable = "face";
        objMeta4->detectorConfidence = 0.5 + u(e) * 0.1;
        objMeta4->trackerId = 4;
        objMeta4->detectorBboxInfo.left = 0.3;
        objMeta4->detectorBboxInfo.top = 0.4;
        objMeta4->detectorBboxInfo.width = 0.3;
        objMeta4->detectorBboxInfo.height = 0.5;
        frameMeta->objs.push_back(objMeta4);
    }

    // 5: 该trackID的objMeta评分随机生成，存在时间为1秒；
    if (m_fps <= frameIndex && frameIndex <= 2 * m_fps) {
        CObjectMeta *objMeta5 = new CObjectMeta();
        objMeta5->objLable = "face";
        objMeta5->detectorConfidence = 0.5 + u(e) * 0.1;
        objMeta5->trackerId = 5;
        objMeta5->detectorBboxInfo.left = 0.3;
        objMeta5->detectorBboxInfo.top = 0.1;
        objMeta5->detectorBboxInfo.width = 0.4;
        objMeta5->detectorBboxInfo.height = 0.5;
        frameMeta->objs.push_back(objMeta5);
    }

    // 6: 该trackID的objMeta评分随机生成，存在时间为2秒；
    if (m_fps <= frameIndex && frameIndex <= 3 * m_fps) {
        CObjectMeta *objMeta6 = new CObjectMeta();
        objMeta6->objLable = "face";
        objMeta6->detectorConfidence = 0.5 + u(e) * 0.1;
        objMeta6->trackerId = 6;
        objMeta6->detectorBboxInfo.left = 0.2;
        objMeta6->detectorBboxInfo.top = 0.4;
        objMeta6->detectorBboxInfo.width = 0.1;
        objMeta6->detectorBboxInfo.height = 0.2;
        frameMeta->objs.push_back(objMeta6);
    }

    // 7: 该trackID的objMeta评分随机生成，存在时间为3秒；
    if (m_fps <= frameIndex && frameIndex <= 4 * m_fps) {
        CObjectMeta *objMeta7 = new CObjectMeta();
        objMeta7->objLable = "face";
        objMeta7->detectorConfidence = 0.5 + u(e) * 0.1;
        objMeta7->trackerId = 7;
        objMeta7->detectorBboxInfo.left = 0.1;
        objMeta7->detectorBboxInfo.top = 0.3;
        objMeta7->detectorBboxInfo.width = 0.2;
        objMeta7->detectorBboxInfo.height = 0.5;
        frameMeta->objs.push_back(objMeta7);
    }

    // 8: 该trackID的objMeta评分随机生成，存在时间随机为2秒,
    // 每60帧数据更新一次trackID;；
    if (frameIndex - m_fps * (frameIndex / 60) <= frameIndex &&
        frameIndex <= frameIndex + m_fps * (frameIndex / 60) - 10) {
        CObjectMeta *objMeta8 = new CObjectMeta();
        objMeta8->objLable = "face";
        objMeta8->detectorConfidence = 0.5 + u(e) * 0.1;
        objMeta8->trackerId = 8 + (frameIndex / 60);
        objMeta8->detectorBboxInfo.left = 0.4;
        objMeta8->detectorBboxInfo.top = 0.1;
        objMeta8->detectorBboxInfo.width = 0.2;
        objMeta8->detectorBboxInfo.height = 0.5;
        frameMeta->objs.push_back(objMeta8);
    }

    app_debug("%s %ld \n", " the frameMeta has objMeta size is : ", frameMeta->objs.size());

    CBatchMeta *batchMeta = new CBatchMeta();
    batchMeta->addFrameMeta(frameMeta);

    return batchMeta;
}

app_ret FaceSelectDataGenerator::Init(const YAML::Node &config, const std::string &vbName, int fps, uint16_t loopNum) {
    m_fps = fps;
    m_loopNum = loopNum;

    auto faceSelectConfig = config["faceSelectData"];
    m_encodeInputDataInfo.filepath = faceSelectConfig["filepath"].as<std::string>();
    m_encodeInputDataInfo.width = faceSelectConfig["width"].as<int>();
    m_encodeInputDataInfo.height = faceSelectConfig["height"].as<int>();
    m_encodeInputDataInfo.dateType = faceSelectConfig["datatype"].as<int>();

    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 10;
    poolCfg.blkSize = m_encodeInputDataInfo.width * m_encodeInputDataInfo.height * 3 / 2;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &m_encodeInputDataInfo.pool);
    app_debug("%s %d\n", "create pool ret is :", (int)ret);
    if (ret != ES_SUCCESS) {
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

CBaseMeta *FaceSelectDataGenerator::GenerateData(int frameIndex, FILE *&fp) {
    if (fp == NULL) {
        fp = fopen(m_encodeInputDataInfo.filepath.c_str(), "r");
        if (fp == NULL) {
            app_error("Failed to open file: %s", m_encodeInputDataInfo.filepath.c_str());
            return nullptr;
        }
    }

    CBaseMeta *baseMeta = prepareFaceselectTestData(frameIndex, fp);
    if (frameIndex == m_loopNum) {
        baseMeta->eosFlag = true;
    }
    return baseMeta;
}
