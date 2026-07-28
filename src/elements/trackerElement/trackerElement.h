#ifndef _TRACKER_ELEMENT_H__
#define _TRACKER_ELEMENT_H__

#include "BoxType.h"
#include "batch_meta.h"
#include "element.h"
#include "es_tracker_api.h"
#include "stdio.h"

class TrackerElement : public CElement {
   public:
    TrackerElement(const char *name = "tracker", const char *config = "", int dieIndex = 0)
        : CElement(name, config, dieIndex){};
    ~TrackerElement() = default;

    app_ret Init() override;
    int parseLabelsFile(const std::string &labelsFilePath);
    app_ret Finish() override;
    app_ret Start() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement = 0) override;
    // app_ret WriteTrackID(vector<CObjectMeta *, StackAllocator<CObjectMeta *, MAX_OBJMETA_PER_FRAME>> &pl_objects,
    // std::vector<EsTrackBoxS> &tracked_objects, int width, int height);
    app_ret WriteTrackIDPre(vector<CObjectMeta *> &pl_objects, std::vector<EsTrackBoxS> &tracked_objects, int width,
                            int height);
    app_ret perfStat() override;

    int mFps;
    int mWidth;
    int mHight;
    int mTrackMethod;
    int mIdResetMode;
    int mPredictFrameNum;
    int mFrameIntervalNum;
    bool mDumpFlag;
    std::string mDumpPath;
    // TrackerSettings mSettings;
    int processDataCount;
    int faceTensorC;

    string labelfilePath;
    PerformanceStatic *trackPerformance;
    // PerformanceStatic* trackRgbPerformance;
    PerformanceStatic *trackProcPerformance;
    EsTrackerSettings settings;
    // set<int> initedIdSet;
    map<int, EsTrackerSettings *> trackerSettingsMap;
    map<int, CObjectMeta *> trackInfoCache;  // trackId->objMeta
    std::vector<string> m_postLabels;
};
#endif  // _TRACKER_ELEMENT_H__