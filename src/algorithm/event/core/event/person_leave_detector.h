#ifndef _PERSON_LEAVE_DETECTOR_H__
#define _PERSON_LEAVE_DETECTOR_H__

#include "event_detector_base.h"

#include <map>

namespace algorithm::cdky {

/* 人员离岗事件配置。 */
struct PersonLeaveConfig : public BaseEventConfig {
    float leaveTimeThreshold = 10.0F;
    std::vector<TimeRange> activeTimeRanges;
    std::vector<std::string> targetLabels{"person"};
};

/* 人员离岗状态。 */
struct PersonLeaveState {
    bool personPresent = false;
    uint64_t lastPresentTimeMs = 0;
    uint64_t leaveStartTimeMs = 0;
    bool isLeaving = false;
};

class PersonLeaveEventDetector : public BaseEventDetector {
public:
    PersonLeaveEventDetector(const std::string& name,
                             const std::string& cameraId,
                             const PersonLeaveConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}

    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    bool isInActiveTimeRange();
    bool isPersonInRoi(const std::vector<CObjectMeta*>& objects);

private:
    PersonLeaveConfig m_config;
    PersonLeaveState m_state;
};

} // namespace algorithm::cdky

#endif // _PERSON_LEAVE_DETECTOR_H__
