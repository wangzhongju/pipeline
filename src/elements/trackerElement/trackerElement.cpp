#define PL_LOG_ID PL_LOG_TRACKER
#include "trackerElement.h"

#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <iostream>

#include "es_sys.h"
#include "impl_EsTracker.hpp"

extern "C" {
#include "BoxType.h"
#include "es_tracker_api.h"
}

int TrackerElement::parseLabelsFile(const std::string &labelsFilePath) {
    app_debug("%s-%d: %s\n", __func__, __LINE__, "start parseLabelsFile");
    std::ifstream labels_file(labelsFilePath);
    std::string delim{':'};
    std::string strlim{','};
    if (!labels_file.is_open()) {
        app_error("%s-%d: %s\n", __func__, __LINE__, "labels file open error");
        return -1;
    }
    while (labels_file.good() && !labels_file.eof()) {
        std::string line, word;

        std::vector<std::string> l;
        size_t pos = 0, oldpos = 0;
        int index = 0;

        std::getline(labels_file, line, '\n');
        if (line.empty()) continue;

        while ((pos = line.find(delim, oldpos)) != std::string::npos) {
            word = line.substr(oldpos, pos - oldpos);
            l.push_back(word);
            oldpos = pos + delim.length();
            index = std::stoi(word.c_str());
        }
        size_t pos2 = 0;
        if ((pos2 = line.find(strlim, oldpos)) != std::string::npos) {
            word = line.substr(oldpos, pos2 - oldpos);
            l.push_back(word);
            oldpos = pos2 + delim.length();
            m_postLabels.push_back(word);
        } else {
            l.push_back(line.substr(oldpos));
            m_postLabels.push_back(std::string(line.substr(oldpos)));
        }
    }

    if (labels_file.bad()) {
        app_error("%s-%d: %s\n", __func__, __LINE__, "labels file is bad");
        return -1;
    }
    app_debug("%s-%d: %s\n", __func__, __LINE__, "parseLabelsFile end");
    return 0;
}

app_ret TrackerElement::Init() {
    mFps = 25;
    mDumpPath = "./";

    // get params from configure file
    YAML::Node config = YAML::LoadFile(m_configFile);

    // settings.fps = 20;
    // settings.trackerPredictMethod=ES_TRACK_PREV;
    // settings.initialTrackLen=1;
    // settings.l1GradientThresh = 0.0;
    // settings.checkDetScore = 0.0;
    // settings.abandonThresh = 0.0;
    // settings.initTrackAreaThresh = 0.0;

    // settings.normalIouThresh = 0.6;
    // settings.kalmanIouThresh = 0.6;
    // settings.optiflowIouThresh = 0.9;

    // settings.emitQualityThresh = 20.0;
    // settings.initTrackAreaThresh = 3025;
    // settings.orientationDecayClipAreaThresh = 10000;

    // settings.l1GradientThresh = 0.23;
    // settings.checkDetScore = 0.18;
    // settings.abandonThresh = 0.6;
    // settings.disableOrientationOffsetRatio = 0.125;

    if (config["initialTrackLen"].IsDefined()) {
        settings.initialTrackLen = config["initialTrackLen"].template as<int>();
    }

    if (config["fps"].IsDefined()) {
        settings.fps = config["fps"].template as<int>();
    }

    if (config["trackerPredictMethod"].IsDefined()) {
        settings.trackerPredictMethod = EsTrackerPredictMethod(config["trackerPredictMethod"].template as<int>());
    }

    if (config["normalIouThresh"].IsDefined()) {
        settings.normalIouThresh = config["normalIouThresh"].template as<float>();
    }

    if (config["kalmanIouThresh"].IsDefined()) {
        settings.kalmanIouThresh = config["kalmanIouThresh"].template as<float>();
    }

    if (config["optiflowIouThresh"].IsDefined()) {
        settings.optiflowIouThresh = config["optiflowIouThresh"].template as<float>();
    }

    if (config["emitQualityThresh"].IsDefined()) {
        settings.emitQualityThresh = config["emitQualityThresh"].template as<int>();
    }
    if (config["initTrackAreaThresh"].IsDefined()) {
        settings.initTrackAreaThresh = config["initTrackAreaThresh"].template as<int>();
    }

    if (config["orientationDecayClipAreaThresh"].IsDefined()) {
        settings.orientationDecayClipAreaThresh = config["orientationDecayClipAreaThresh"].template as<int>();
    }

    if (config["l1GradientThresh"].IsDefined()) {
        settings.l1GradientThresh = config["l1GradientThresh"].template as<float>();
    }

    if (config["checkDetScore"].IsDefined()) {
        settings.checkDetScore = config["checkDetScore"].template as<float>();
    }

    if (config["abandonThresh"].IsDefined()) {
        settings.abandonThresh = config["abandonThresh"].template as<float>();
    }

    if (config["prevDetectUsedMax"].IsDefined()) {
        settings.prevDetectUsedMax = config["prevDetectUsedMax"].template as<int>();
    }

    if (config["disableOrientationOffsetRatio"].IsDefined()) {
        settings.disableOrientationOffsetRatio = config["disableOrientationOffsetRatio"].template as<float>();
    }

    if (config["dump-flag"].IsDefined()) {
        mDumpFlag = config["dump-flag"].template as<bool>();
    }

    if (config["labelfilePath"].IsDefined()) {
        labelfilePath = config["labelfilePath"].template as<string>();

        app_debug(" %s\n", "will parse label file ");
        int labelLength = parseLabelsFile(labelfilePath);  // m_postLabels
        assert(0 == labelLength);
    }

    processDataCount = 0;
    faceTensorC = 7;  // pFace[0]->pFace[5]
    trackPerformance = new PerformanceStatic(mName + "_trackPerformance", PERF_STATIC_SEGMENT);
    // trackRgbPerformance = new PerformanceStatic(mName +
    // "_trackRgbPerformance", PERF_STATIC_SEGMENT);
    trackProcPerformance = new PerformanceStatic(mName + "_trackProcPerformance", PERF_STATIC_SEGMENT);

    // todo parse lable text

    return APP_SUCCESS;
}
app_ret TrackerElement::Start() { return APP_SUCCESS; }
app_ret TrackerElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    CBatchMeta *batchMeta = (CBatchMeta *)(baseMeta);
    app_debug("%s %s\n", "Enter: ", mName.c_str());

    trackPerformance->performanceStaticStart();
    for (int index = 0; index < batchMeta->getFrameMetaSize(); index++) {
        CFrameMeta *frameMeta = batchMeta->getFrameMeta(index);
        if (frameMeta->eosFlag) {
            continue;
        }
        int pad_index = frameMeta->padIndex;

        if (!trackerSettingsMap.count(pad_index)) {
            EsTrackerMgr::Instance(pad_index).Init(settings);
            trackerSettingsMap[pad_index] = &settings;
        }

        // ###### trans frameMeta.images(nv12) -> cvmat(bgr) -> tensor ######
        // get frame info from pp1
        CImage *frame_image = frameMeta->images[1];  // todo pp1
        assert(frame_image != nullptr);
        VIDEO_FRAME_INFO_S *video_frame_info = frame_image->mPic;
        VIDEO_FRAME_S frame_info = video_frame_info->videoFrame;

        ES_TENSOR_S imageTensor;
        // ES_TENSOR_SHAPE_S shape0{0,0,0,0,0,0};
        // imageTensor.shape=shape0;
        // imageTensor.dataType = ES_DATA_PRECISION_E::ES_PRECISION_FP32;
        // imageTensor.pData.hostBuffer = nullptr;

        ES_U32 imageShape[] = {0, 0, 0, 0, 0, 0};
        imageTensor.shapeDim = sizeof(imageShape) / sizeof(imageShape[0]);
        // assert(imageTensor.shapeDim == faceTensorC);//todo for sure
        // printf("#################!!!!!!!!!!!!!!!!!!!! %d \n",
        // sizeof(imageTensor.shape));
        memcpy(imageTensor.shape, imageShape, sizeof(imageTensor.shape));
        // printf("imageTensor.shape:## ## ## %d %d %d %d %d %d %d
        // \n",imageTensor.shape[0],imageTensor.shape[1],imageTensor.shape[2],imageTensor.shape[3],imageTensor.shape[4],imageTensor.shape[5],imageTensor.shape[6]);
        imageTensor.dataType = ES_DATA_PRECISION_E::ES_PRECISION_FP32;
        imageTensor.hostBuf = nullptr;

        // ###### trans frameMeta->objs -> faceTensor ######
        // std::vector<SingleBoxS>  -> ES_TENSOR_S
        ES_TENSOR_S faceTensor;
        int faceNum = frameMeta->objs.size();

        // ES_TENSOR_SHAPE_S shape{1,faceTensorC,1,faceNum,1,4};
        // faceTensor.shape=shape;
        // faceTensor.dataType=ES_DATA_PRECISION_E::ES_PRECISION_FP32;
        // faceTensor.pData.hostBuffer = malloc(sizeof(float) * faceTensorC *
        // faceNum);

        ES_U32 faceShape[] = {1, static_cast<ES_U32>(faceTensorC), 1, static_cast<ES_U32>(faceNum), 1, 4};
        faceTensor.shapeDim = sizeof(faceShape) / sizeof(faceShape[0]);
        // assert(faceTensor.shapeDim==faceTensorC);
        // printf("#################!!!!!!!!!!!!!!!!!!!! %d \n",
        // sizeof(faceTensor.shape));
        memcpy(faceTensor.shape, faceShape, sizeof(faceTensor.shape));
        // printf("faceTensor.shape:## ## ## %d %d %d %d %d %d %d
        // \n",faceTensor.shape[0],faceTensor.shape[1],faceTensor.shape[2],faceTensor.shape[3],faceTensor.shape[4],faceTensor.shape[5],faceTensor.shape[6]);
        faceTensor.dataType = ES_DATA_PRECISION_E::ES_PRECISION_FP32;
        faceTensor.hostBuf = malloc(sizeof(float) * faceTensorC * faceNum);

        // get objects list
        // map<int,CObjectMeta *> dectInfoCache;
        app_debug("faceTensor.faceNum: %d \n", faceNum);

        for (int idx = 0; idx < frameMeta->objs.size(); idx++) {
            // dectInfoCache[idx]=frameMeta->objs[idx];
            assert(6 < faceTensorC);  // pFace[5]
            float *pFace = (float *)faceTensor.hostBuf + idx * faceTensorC;
            pFace[0] = (frameMeta->objs[idx]->detectorBboxInfo.left) * frame_info.width;
            pFace[0] = pFace[0] < frame_info.width ? pFace[0] : frame_info.width;
            pFace[1] = (frameMeta->objs[idx]->detectorBboxInfo.top) * frame_info.height;
            pFace[1] = pFace[1] < frame_info.height ? pFace[1] : frame_info.height;
            pFace[2] = (frameMeta->objs[idx]->detectorBboxInfo.width + frameMeta->objs[idx]->detectorBboxInfo.left) *
                       frame_info.width;  // 2 point location
            pFace[2] = pFace[2] < frame_info.width ? pFace[2] : frame_info.width;
            pFace[3] = (frameMeta->objs[idx]->detectorBboxInfo.height + frameMeta->objs[idx]->detectorBboxInfo.top) *
                       frame_info.height;
            pFace[3] = pFace[3] < frame_info.height ? pFace[3] : frame_info.height;
            pFace[4] = frameMeta->objs[idx]->detectorConfidence;
            pFace[5] = idx;                            // outTensor->boxIndex match inputTensor->outputTensor
            pFace[6] = frameMeta->objs[idx]->classId;  // todo float?
            app_debug("frameMeta->objs index:%d ,pFace0->5: %f %f %f %f %f %f %f \n", idx, pFace[0], pFace[1], pFace[2],
                      pFace[3], pFace[4], pFace[5], pFace[6]);
        }
        std::vector<EsTrackBoxS> trackBoxes;
        trackProcPerformance->performanceStaticStart();
        EsTrackerErrCode code = EsTrackerMgr::Instance(pad_index).Process(imageTensor, faceTensor, trackBoxes);
        trackProcPerformance->performanceStaticEnd();
        free(faceTensor.hostBuf);
        if (processDataCount % 10 == 0) {
            EsTrackerMgr::Instance(pad_index).mEsSeqLinesMgr.GetFinished();
        }

        if (code > 0) {
            app_error("TrackerProcess ret %d \n", code);
        }
        // write track_id to objects list
        WriteTrackIDPre(frameMeta->objs, trackBoxes, frame_info.width, frame_info.height);
    }
    app_debug("%s %s\n", "Exit: ", mName.c_str());
    processDataCount++;
    trackPerformance->performanceStaticEnd();
    return APP_SUCCESS;
}

// app_ret TrackerElement::WriteTrackID(vector<CObjectMeta *, StackAllocator<CObjectMeta *, MAX_OBJMETA_PER_FRAME>>
// &pl_objects, std::vector<EsTrackBoxS> &tracked_objects, int width, int height) {
//     // 注：pl_objects和tracked_objects，互不隶属(都可能包含独有元素)
//     // 目前讨论：tracker调整后的objs，只增不减，比如进来objs5个元素，tracker出4个,经过tracker后objs长度也保持5个。
//     vector<CObjectMeta *> pl_track_objects;  // tracker append
//     for (int i = 0; i < tracked_objects.size(); i++) {
//         EsTrackBoxS trackBox = tracked_objects[i];
//         int boxIndex = trackBox.boxIndex;
//         int trackerId = trackBox.trackID;
//         assert(trackerId >= 0);

//         // find person info by boxIndex
//         CObjectMeta *objMeta = nullptr;
//         if (trackBox.isPredictedBox) {  // exist in trackInfoCache,but can't
//                                         // match dect
//             objMeta = new CObjectMeta;
//             objMeta->trackerId = trackBox.trackID;

//             cv::Rect optical_det_rect = EsTensor2Rect(trackBox.opticalDetRect);
//             objMeta->trackerBboxInfo.left = (float)optical_det_rect.x / width;
//             objMeta->trackerBboxInfo.top = (float)optical_det_rect.y / height;
//             objMeta->trackerBboxInfo.width = (float)optical_det_rect.width / width;
//             objMeta->trackerBboxInfo.height = (float)optical_det_rect.height / height;
//             objMeta->trackerConfidence = trackBox.detScore;
//             objMeta->classId = trackBox.classId;
//             app_debug("not match dect:%d %f [%f %f %f %f]\n", trackerId, trackBox.detScore,
//             objMeta->trackerBboxInfo.left, objMeta->trackerBboxInfo.top, objMeta->trackerBboxInfo.width,
//                       objMeta->trackerBboxInfo.height);

//             assert(trackInfoCache.count(trackerId) > 0);
//             // add cache info
//             CObjectMeta *cacheObjMeta = trackInfoCache[trackerId];
//             objMeta->classId = cacheObjMeta->classId;
//             objMeta->objLable = cacheObjMeta->objLable;

//             // use tracker info fill detectorBboxInfo
//             objMeta->detectorBboxInfo.left = objMeta->trackerBboxInfo.left;
//             objMeta->detectorBboxInfo.top = objMeta->trackerBboxInfo.top;
//             objMeta->detectorBboxInfo.width = objMeta->trackerBboxInfo.width;
//             objMeta->detectorBboxInfo.height = objMeta->trackerBboxInfo.height;
//             objMeta->detectorConfidence = objMeta->trackerConfidence;

//             pl_track_objects.push_back(objMeta);
//         } else {  // may be not exist trackInfoCache,but match dect
//             cv::Rect det_rect = EsTensor2Rect(trackBox.detRect);
//             assert(boxIndex >= 0 && boxIndex < pl_objects.size());  // todo assert

//             objMeta = pl_objects[boxIndex];
//             objMeta->trackerId = trackBox.trackID;

//             objMeta->trackerBboxInfo.left = (float)det_rect.x / width;
//             objMeta->trackerBboxInfo.top = (float)det_rect.y / height;
//             objMeta->trackerBboxInfo.width = (float)det_rect.width / width;
//             objMeta->trackerBboxInfo.height = (float)det_rect.height / height;
//             objMeta->trackerConfidence = trackBox.detScore;
//             app_debug("match dect:%d %f [%f %f %f %f]\n", trackerId, trackBox.detScore,
//             objMeta->trackerBboxInfo.left, objMeta->trackerBboxInfo.top, objMeta->trackerBboxInfo.width,
//                       objMeta->trackerBboxInfo.height);

//             // add to trackInfoCache
//             if (trackInfoCache.count(trackerId) == 0) {
//                 CObjectMeta *cacheObjMeta = new CObjectMeta;
//                 cacheObjMeta->classId = objMeta->classId;
//                 cacheObjMeta->objLable = objMeta->objLable;

//                 cacheObjMeta->detectorBboxInfo.left = objMeta->detectorBboxInfo.left;
//                 cacheObjMeta->detectorBboxInfo.top = objMeta->detectorBboxInfo.top;
//                 cacheObjMeta->detectorBboxInfo.width = objMeta->detectorBboxInfo.width;
//                 cacheObjMeta->detectorBboxInfo.height = objMeta->detectorBboxInfo.height;

//                 cacheObjMeta->parentFrameMeta = objMeta->parentFrameMeta;

//                 trackInfoCache[trackerId] = cacheObjMeta;
//             }
//         }
//     }
//     if (pl_track_objects.size() > 0) {
//         pl_objects.insert(pl_objects.end(), pl_track_objects.begin(), pl_track_objects.end());
//     }

//     return APP_SUCCESS;
// }

app_ret TrackerElement::WriteTrackIDPre(vector<CObjectMeta *> &pl_objects, std::vector<EsTrackBoxS> &tracked_objects,
                                        int width, int height) {
    // 注：pl_objects和tracked_objects，互不隶属(都可能包含独有元素)
    // 目前讨论：tracker调整后的objs，只增不减，比如进来objs5个元素，tracker出4个,经过tracker后objs长度也保持5个。
    vector<CObjectMeta *> pl_track_objects;  // tracker append
    app_debug("tracked_objects.size():%d %d \n", pl_objects.size(), tracked_objects.size());
    if (pl_objects.size() > 0) {
        return APP_SUCCESS;
    }

    for (int i = 0; i < tracked_objects.size(); i++) {
        EsTrackBoxS trackBox = tracked_objects[i];

        // find person info by boxIndex
        CObjectMeta *objMeta = nullptr;

        objMeta = new CObjectMeta;
        objMeta->trackerId = trackBox.trackID;

        objMeta->trackerBboxInfo.left = (float)(*(int *)trackBox.detRect.hostBuf) / width;
        objMeta->trackerBboxInfo.top = (float)(((int *)trackBox.detRect.hostBuf)[1]) / height;
        objMeta->trackerBboxInfo.width = (float)(((int *)trackBox.detRect.hostBuf)[2]) / width;
        objMeta->trackerBboxInfo.height = (float)(((int *)trackBox.detRect.hostBuf)[3]) / height;
        // printf("####### trackBox.classId:%d \n",trackBox.classId);

        assert(trackBox.classId >= 0 && trackBox.classId < m_postLabels.size());
        objMeta->classId = trackBox.classId;
        // printf("#### %d \n",trackBox.classId);
        // printf("######### %d \n",m_postLabels.size());
        objMeta->objLable = m_postLabels[objMeta->classId];

        objMeta->trackerConfidence = trackBox.detScore;
        app_debug("not match dect:%d %f [%f %f %f %f] %s\n", 0, trackBox.detScore, objMeta->trackerBboxInfo.left,
                  objMeta->trackerBboxInfo.top, objMeta->trackerBboxInfo.width, objMeta->trackerBboxInfo.height,
                  objMeta->objLable.c_str());

        // use tracker info fill detectorBboxInfo
        objMeta->detectorBboxInfo.left = objMeta->trackerBboxInfo.left;
        objMeta->detectorBboxInfo.top = objMeta->trackerBboxInfo.top;
        objMeta->detectorBboxInfo.width = objMeta->trackerBboxInfo.width;
        objMeta->detectorBboxInfo.height = objMeta->trackerBboxInfo.height;
        objMeta->detectorConfidence = objMeta->trackerConfidence;

        pl_track_objects.push_back(objMeta);
    }
    if (pl_track_objects.size() > 0) {
        pl_objects.insert(pl_objects.end(), pl_track_objects.begin(), pl_track_objects.end());
    }

    return APP_SUCCESS;
}
app_ret TrackerElement::perfStat() {
    trackPerformance->performanceStaticReport();
    // trackRgbPerformance->performanceStaticReport();
    trackProcPerformance->performanceStaticReport();
    return APP_SUCCESS;
}

app_ret TrackerElement::Finish() {
    delete trackPerformance;
    //  delete trackRgbPerformance;
    delete trackProcPerformance;

    // delete settings;
    trackerSettingsMap.clear();
    trackInfoCache.clear();

    return APP_SUCCESS;
}

extern "C" CElement *createEsTrackerElement(const char *name, const char *path, int dieIndex) {
    return new TrackerElement(name, path, dieIndex);
}