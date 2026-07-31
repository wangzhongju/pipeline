#define PL_LOG_ID PL_LOG_OSD
#include "osdElement.h"

#include <yaml-cpp/yaml.h>

#include <ctime>  // 包含头文件
#include <iomanip>
#include <sstream>

#include "yaml_parser.h"

static void trim(string& s) {
    if (!s.empty()) {
        s.erase(0, s.find_first_not_of(" "));
        s.erase(s.find_last_not_of(" ") + 1);
    }
    return;
}

static void rect_adjust(Rect2f parentRect, Rect2f* childRect) {
    childRect->x = childRect->x * parentRect.width + parentRect.x;
    childRect->y = childRect->y * parentRect.height + parentRect.y;
    childRect->width = childRect->width * parentRect.width;
    childRect->height = childRect->height * parentRect.height;
    return;
}

app_ret OsdElement::Init() {
    if (m_NextElementVec.size() != 1 || m_PreviousElementVec.size() != 1) {
        return APP_FAILURE;
    }

    parse_config_file(&mOsdParam, m_configFile);
    mOsdProc = NULL;
    mOsdPerformance = new PerformanceStatic(mName + "_osd", PERF_STATIC_SEGMENT);
    time(&baseTime);  // 获取当前时间的time_t类型值
    return APP_SUCCESS;
}

app_ret OsdElement::Start() {
    printf("osd start succeed!\n");
    textColor.type = 2;
    textColor.val[0] = mOsdParam.textcolor.R;
    textColor.val[1] = mOsdParam.textcolor.G;
    textColor.val[2] = mOsdParam.textcolor.B;
    textColor.isDate = 0;
    rectColor.type = 2;
    rectColor.val[0] = mOsdParam.rectcolor.R;
    rectColor.val[1] = mOsdParam.rectcolor.G;
    rectColor.val[2] = mOsdParam.rectcolor.B;

    RtmOsdColor.type = 2;
    RtmOsdColor.val[0] = mOsdParam.rtmOsdParam.rectcolor.R;
    RtmOsdColor.val[1] = mOsdParam.rtmOsdParam.rectcolor.G;
    RtmOsdColor.val[2] = mOsdParam.rtmOsdParam.rectcolor.B;
    return APP_SUCCESS;
}

app_ret OsdElement::ProcessBatchMeta(CBatchMeta* batchMeta) {
    app_ret ret = APP_SUCCESS;
    app_info("%s-%s-%d-%s\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());
    int imgNum = batchMeta->getFrameMetaSize();
    app_debug("ProcessBatchMeta image number %d\n", imgNum);
    for (int i = 0; i < imgNum; i++) {
        CFrameMeta* frameMeta = batchMeta->getFrameMeta(i);
        ret = ProcessFrameMeta(frameMeta);
        if (ret != APP_SUCCESS) {
            app_error("ProcessFrameMeta %d failed!\n", i);
            return ret;
        }
    }
    return ret;
}

// 检查关键点是否在给定的向量中
static bool isKeypointInList(int keypointIndex, const std::vector<int>& list) {
    return std::find(list.begin(), list.end(), keypointIndex) != list.end();
}

app_ret OsdElement::ProcessFrameMeta(CFrameMeta* frameMeta) {
    CImage* img;
    ulong pts = frameMeta->pts;
    app_info("%s-%s-%d-%s\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());
    int channelID = mOsdParam.channelId;

    /*
    if (frameMeta->images.size() > 1) {
        img = frameMeta->images[1];
        app_debug("osd use pp1 to draw objs\n");
    } else {
        img = frameMeta->images[0];
        app_debug("osd use pp0 to draw objs\n");
    }
        */
    img = frameMeta->images[channelID];
    app_debug("osd use channel ID %d to draw objs src  \n", channelID);
    VIDEO_FRAME_S* videoFrame = &(img->mPic->videoFrame);

    if (mOsdParam.input_dump_enable) {
#ifdef HAVE_ESPL_DX
        bool ret = mDbg->save_video_frame(img->mPic, mInputDumpFile.c_str());
        if (0 != ret) {
            app_error(" OSD save error 0x%x\n", ret);
        }
#else
        app_error("OSD input dump requested, but espl_dx support is unavailable\n");
#endif
    }

    mOsdProc->prepareOsd(videoFrame, videoFrame->width, videoFrame->height);

    int objNum = frameMeta->objs.size();

    app_debug("ProcessFrameMeta object numbers %d\n", objNum);
    int fontHeight = videoFrame->height * mOsdParam.fontheight;
    if (objNum > 0) mOsdPerformance->performanceStaticStart();

    // process for yolo
    if (objNum) {
        int textNum = 0, rectNum = 0;
        vector<Rect2f> rectRects;
        vector<Rect2f> textRects;
        vector<string> strings;

        for (int i = 0; i < objNum; i++) {
            ES_ASSERT(i < MAX_OBJ_NUM, "the OSD size meta size is : %d  MAX_OBJ_NUM %d", i, MAX_OBJ_NUM);

            CObjectMeta* obj = frameMeta->objs[i];
#if 0
            if(PL_OBJ_YOLO != obj->objType){
                continue;
            }
#endif
            float x, y, width, height;
            x = obj->detectorBboxInfo.left;
            y = obj->detectorBboxInfo.top;
            width = obj->detectorBboxInfo.width;
            height = obj->detectorBboxInfo.height;
            if (obj->trackerBboxInfo.left >= 0 && obj->trackerBboxInfo.top >= 0 && obj->trackerBboxInfo.width > 0 &&
                obj->trackerBboxInfo.height > 0) {
                x = obj->trackerBboxInfo.left;
                y = obj->trackerBboxInfo.top;
                width = obj->trackerBboxInfo.width;
                height = obj->trackerBboxInfo.height;
            }
            x = x < 1 ? x : 1;
            y = y < 1 ? y : 1;
            width = (x + width < 1) ? width : 1 - x;
            height = (y + height < 1) ? height : 1 - y;

            string objectLabel = obj->objLable;
            trim(objectLabel);
            if (obj->trackerId >= 0) {
                objectLabel.append("#");
                objectLabel.append(std::to_string(obj->trackerId));
            }
            const float confidence = obj->trackerConfidence > 0.0F
                                         ? obj->trackerConfidence
                                         : obj->detectorConfidence;
            std::ostringstream labelStream;
            labelStream << objectLabel << " " << std::fixed
                        << std::setprecision(2) << confidence;
            objectLabel = labelStream.str();
            app_debug("object order %d, x %f, y %f, w %f, h %f, label %s\n",
                      i, x, y, width, height, objectLabel.c_str());

            if (x < 0 || y < 0) {
                Rect2f textRect = {0, 0, 1, mOsdParam.fontheight};
                textRects.push_back(textRect);
                strings.push_back(objectLabel);
                textNum++;
            } else {
                Rect2f rectRect = {x, y, width, height};
                x = rectRect.x;
                y = std::max(rectRect.y - mOsdParam.fontheight * 1.5, 0.0);  // 1.5倍行距
                width = rectRect.width;
                height = mOsdParam.fontheight;
                Rect2f textRect = {x, y, width, height};

                rectRects.push_back(rectRect);
                textRects.push_back(textRect);
                strings.push_back(objectLabel);
                textNum++;
                rectNum++;
            }
        }
        app_debug("textNum:%d,rectNum:%d\n", textNum, rectNum);
        // text
        if (textNum > 0 && mOsdParam.text_enable) {
            mOsdProc->settextparam(&strings, &textRects, textNum, videoFrame->width, videoFrame->height, fontHeight,
                                   textColor);
            mOsdProc->process(videoFrame, videoFrame);
        }

        // rect
        if (rectNum > 0 && mOsdParam.rect_enable) {
            mOsdProc->setrectparam(&rectRects, rectNum, rectColor, mOsdParam.pensize);
            mOsdProc->process(videoFrame, videoFrame);
        }
    }
    if (objNum > 0) mOsdPerformance->performanceStaticEnd(objNum);

    // process for rtmpose
    int rtmObjNum = frameMeta->rtmObjs.size();
    app_debug("OSD rtmObjNum %d index %d \n", rtmObjNum, frameMeta->index);
    if (rtmObjNum > 0) {
        int rectNum = 0;
        int pointNum = 0;
        vector<Rect2f> rectRects;
        Rect2f rectPose = {0};
        vector<Rect2f> rectPoints;

        int x, y;

        for (int i = 0; i < rtmObjNum; i++) {
            ES_ASSERT(i < MAX_OBJ_NUM, "the OSD size meta size is : %d  MAX_OBJ_NUM %d", i, MAX_OBJ_NUM);

            CObjectMeta* obj = frameMeta->rtmObjs[i];
#if 0
            if(PL_OBJ_RTMPOSE != obj->objType){
                continue;
            }
#endif

            app_debug(" osd  keyPoint size %d\n", obj->keyPoint.size());
            for (size_t j = 0; j < obj->keyPoint.size(); j++) {
                const auto& pose = obj->keyPoint[j];

                if ((isKeypointInList(j, mOsdParam.rtmOsdParam.body) ||
                     isKeypointInList(j, mOsdParam.rtmOsdParam.face) ||
                     isKeypointInList(j, mOsdParam.rtmOsdParam.left_hand) ||
                     isKeypointInList(j, mOsdParam.rtmOsdParam.right_hand) ||
                     isKeypointInList(j, mOsdParam.rtmOsdParam.left_foot) ||
                     isKeypointInList(j, mOsdParam.rtmOsdParam.right_foot))) {
                    rectPose.x = pose.x;
                    rectPose.y = pose.y;
                    x = x < 1 ? x : 1;
                    y = y < 1 ? y : 1;

                    rectNum++;
                    app_debug("OSD rtmpose i %d x %f y %f  \n", i, rectPose.x, rectPose.y);
                    rectRects.push_back(rectPose);
                }
            }

            // 遍历 keypointConnect 列表
            for (const auto& pair : mOsdParam.rtmOsdParam.keypointConnect) {
                if (pair.size() != 2) {
                    app_error("warning: keypointConnect pair size is not 2.\n");
                    continue;
                }

                int keypoint1 = pair[0];
                int keypoint2 = pair[1];

                app_debug("i %d keypoint1 %d %d \n", i, keypoint1, keypoint2);
                // 检查关键点是否在 keyPoint 范围内
                if (keypoint1 >= 0 && keypoint1 < obj->keyPoint.size() && keypoint2 >= 0 &&
                    keypoint2 < obj->keyPoint.size()) {
                    const auto& pose1 = obj->keyPoint[keypoint1];
                    const auto& pose2 = obj->keyPoint[keypoint2];

                    Rect2f rectPose1;
                    rectPose1.x = pose1.x;
                    rectPose1.y = pose1.y;

                    Rect2f rectPose2;
                    rectPose2.x = pose2.x;
                    rectPose2.y = pose2.y;

                    rectPose1.x = rectPose1.x < 1 ? rectPose1.x : 1;
                    rectPose1.y = rectPose1.y < 1 ? rectPose1.y : 1;
                    rectPose2.x = rectPose2.x < 1 ? rectPose2.x : 1;
                    rectPose2.y = rectPose2.y < 1 ? rectPose2.y : 1;

                    pointNum += 2;
                    app_debug("OSD rtmpose i %d x1 %f y1 %f  x2 %f y2 %f\n", i, pose1.x, pose1.y, pose2.x, pose2.y);
                    rectPoints.push_back(rectPose1);
                    rectPoints.push_back(rectPose2);
                } else {
                    app_debug("warning: keypoint index out of range: %d or %d\n", keypoint1, keypoint2);
                }
            }
        }

        app_debug("end rtmObj , mOsdParam.rtmOsdParam.enable %d rectNum:%d pointNum %d \n",
                  mOsdParam.rtmOsdParam.enable, rectNum, pointNum);
        if (rectNum > 0 && mOsdParam.rtmOsdParam.enable) {
            mOsdProc->setRtmOsdParam(&rectRects, rectNum, &rectPoints, pointNum, RtmOsdColor,
                                     mOsdParam.rtmOsdParam.pensize);
            mOsdProc->process(videoFrame, videoFrame);
        }
    }

    if (mOsdParam.time_enable) {
        vector<Rect2f> textRects;
        vector<string> strings;
        baseTime += pts / 1000;
        if (frameMeta->index % 25 == 0) {
            time(&baseTime);
        }
        char* dt = ctime(&baseTime);  // 将time_t转换为字符串形式表示的日期和时间
        string timestr = dt;
        Rect2f textRect = {0.7, 0, 0.5, mOsdParam.fontheight};
        strings.push_back(timestr);
        textRects.push_back(textRect);

        if (mOsdParam.time_enable) {
            textColor.isDate = true;
            // // 添加printf打印所有参数
            // printf("settextparam parameters:\n");
            // printf("  strings address: %p\n", &strings);
            // printf("  textRects address: %p\n", &textRects);
            // printf("  num: %d\n", 1);
            // printf("  width: %d\n", videoFrame->width);
            // printf("  height: %d\n", videoFrame->height);
            // printf("  fontHeight: %d\n", fontHeight);
            // printf("  textColor.isDate: %d\n", textColor.isDate);

            // // 如果需要打印vectors的内容，可以添加以下代码
            // printf("  strings size: %zu\n", strings.size());
            // for (size_t i = 0; i < strings.size(); ++i) {
            //     printf("    strings[%zu]: %s\n", i, strings[i].c_str());
            // }

            // printf("  textRects size: %zu\n", textRects.size());
            // for (size_t i = 0; i < textRects.size(); ++i) {
            //     printf("    textRects[%zu]: x=%.2f, y=%.2f, width=%.2f, height=%.2f\n", i, textRects[i].x,
            //            textRects[i].y, textRects[i].width, textRects[i].height);
            // }
            mOsdProc->settextparam(&strings, &textRects, 1, videoFrame->width, videoFrame->height, fontHeight,
                                   textColor);
            mOsdProc->process(videoFrame, videoFrame);
            textColor.isDate = false;
        }
    }

    if (mOsdParam.stable_enable) {
        vector<Rect2f> textRects;
        vector<string> strings;
        Rect2f textRect = {mOsdParam.pos[0], mOsdParam.pos[1], 1 - mOsdParam.pos[0], (float)(mOsdParam.fontheight)};
        string objLable = mOsdParam.stable_text;
        textRects.push_back(textRect);
        strings.push_back(objLable);

        if (mOsdParam.stable_enable) {
            mOsdProc->settextparam(&strings, &textRects, 1, videoFrame->width, videoFrame->height, fontHeight,
                                   textColor);
            mOsdProc->process(videoFrame, videoFrame);
        }
    }

    mOsdProc->unprepareOsd(videoFrame);

    if (mOsdParam.output_dump_enable) {
#ifdef HAVE_ESPL_DX
        mDbg->save_video_frame(img->mPic, mOutputDumpFile.c_str());
#else
        app_error("OSD output dump requested, but espl_dx support is unavailable\n");
#endif
    }

    return APP_SUCCESS;
}

app_ret OsdElement::ProcessGridMeta(CVideoGridMeta* gridMeta) {
    // preprare VIDEO_FRAME_S
    VIDEO_FRAME_INFO_S* videoFrameInfo = new VIDEO_FRAME_INFO_S;
    VIDEO_FRAME_S* videoFrame = &(videoFrameInfo->videoFrame);
    videoFrame->width = gridMeta->width;
    videoFrame->height = gridMeta->height;
    videoFrame->pixelFormat = gridMeta->data_format;
    for (int i = 0; i < 3; i++) {
        videoFrame->stride[i] = gridMeta->stride[i];
    }
    if (videoFrame->pixelFormat == PIXEL_FORMAT_NV12) {
        videoFrame->offset[0] = 0;
        videoFrame->offset[1] = videoFrame->stride[0] * videoFrame->height;
    }
    videoFrame->fd = gridMeta->memFd;

    // reconstruct CFrameMeta
    CFrameMeta* gridFrameMeta = new CFrameMeta;
    CImage* img = new CImage(videoFrameInfo);
    gridFrameMeta->images.push_back(img);
    // recaculate object rects
    float step_x = 1.0 / gridMeta->cols;
    float step_y = 1.0 / gridMeta->rows;
    int framenum = gridMeta->originFrameMetas.size();
    for (int i = 0; i < framenum; i++) {
        int r = i / gridMeta->cols;
        int c = i % gridMeta->cols;
        CFrameMeta* fmeta = gridMeta->originFrameMetas[i];
        Rect2f gridRect = {c * step_x, r * step_y, step_x, step_y};
        int objNum = fmeta->objs.size();
        for (int j = 0; j < objNum; j++) {
            CObjectMeta* obj = fmeta->objs[j];
            CObjectMeta* newObj = new CObjectMeta;
            newObj->objLable = obj->objLable;
            Rect2f rectRect = {obj->detectorBboxInfo.left, obj->detectorBboxInfo.top, obj->detectorBboxInfo.width,
                               obj->detectorBboxInfo.height};
            rect_adjust(gridRect, &rectRect);
            newObj->detectorBboxInfo.left = rectRect.x;
            newObj->detectorBboxInfo.top = rectRect.y;
            newObj->detectorBboxInfo.width = rectRect.width;
            newObj->detectorBboxInfo.height = rectRect.height;
            gridFrameMeta->objs.push_back(newObj);
        }
    }
    ProcessFrameMeta(gridFrameMeta);
    gridFrameMeta->reduceUseCount();
    delete videoFrame;
    return APP_SUCCESS;
}

app_ret OsdElement::ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) {
    app_info("enter %s OsdElement::ProcessData in !\n", mName.c_str());
    app_ret ret = APP_SUCCESS;
    if (baseMeta->eosFlag) {
        app_info("osd end of stream!\n");
        return ret;
    }
    if (!m_cpuSetFlag) {
        // int temp_cpuID = (m_cpuID++) % 4;
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(temp_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        // getCpuNumaID(__func__);
        set_thread_affinity(m_dieIndex);
        m_cpuSetFlag = true;
    }

    if (!mOsdProc) {
        mOsdProc = createOsdProc(OSD_DRAW_RECTANGLE | OSD_DRAW_TEXT);
        // mOsdProc->osdPerformance = mOsdPerformance;
        mOsdProc->loadFontData(mOsdParam.font_file_path, 0);
    }

    if (baseMeta->mMetaType == BATCH_META) {
        ret = ProcessBatchMeta(dynamic_cast<CBatchMeta*>(baseMeta));
    } else if (baseMeta->mMetaType == FRAME_META) {
        ret = ProcessFrameMeta(dynamic_cast<CFrameMeta*>(baseMeta));
    }
    app_info("exit %s OsdElement::ProcessData out !\n", mName.c_str());
    return ret;
}

app_ret OsdElement::perfStat() {
    mOsdPerformance->performanceStaticReport();
    return APP_SUCCESS;
}

app_ret OsdElement::Finish() {
    delete mOsdProc;
    delete mOsdPerformance;
    freeNumaNode(this, sizeof(OsdElement));
    return APP_SUCCESS;
}

extern "C" CElement* createEsOsdElement(const char* name, const char* path, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(OsdElement))) OsdElement(name, path, dieIndex);
}
