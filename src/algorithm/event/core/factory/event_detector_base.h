#ifndef _EVENT_DETECTOR_BASE_H__
#define _EVENT_DETECTOR_BASE_H__

#include "batch_meta.h"
#include "object_meta.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithm::cdky {

/* 事件输出信息。 */
struct EventInfo {
    uint64_t eventTimeMs = 0;
    uint64_t frameIndex = 0;
    std::string cameraId;
    int classId = 0;
    std::string eventType;
    std::string labelName;
    std::vector<CObjectMeta*> objects;
    std::map<std::string, std::string> extraInfo;
};

/* 归一化二维点。 */
struct RoiPoint2D {
    float x = 0.0F;
    float y = 0.0F;
};

/* 归一化矩形（角度保留但当前判定按轴对齐）。 */
struct RoiBox {
    float cx = 0.0F;
    float cy = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float angle = 0.0F;
};

/* 归一化多边形。 */
struct RoiPolygon {
    std::vector<RoiPoint2D> points;
};

enum class RoiMode {
    Unknown = 0,
    Include = 1,
    Exclude = 2,
};

/* ROI 定义：与 types.proto 的 RoiArea 语义对齐。 */
struct RoiArea {
    bool enabled = false;
    RoiMode mode = RoiMode::Unknown;
    float confidence = 0.0F;
    bool hasRect = false;
    bool hasPoly = false;
    RoiBox rect;
    RoiPolygon poly;
};

/* 时间段定义。 */
struct TimeRange {
    int startHour = 0;
    int startMinute = 0;
    int endHour = 0;
    int endMinute = 0;

    bool isInRange(int hour, int minute) const {
        const int startMin = startHour * 60 + startMinute;
        const int endMin = endHour * 60 + endMinute;
        const int currentMin = hour * 60 + minute;

        if (startMin <= endMin) {
            return currentMin >= startMin && currentMin <= endMin;
        }
        return currentMin >= startMin || currentMin <= endMin;
    }
};

inline float clampNormalized(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

inline bool labelMatches(const std::vector<std::string>& targetLabels, const std::string& labelName) {
    return targetLabels.empty() ||
           std::find(targetLabels.begin(), targetLabels.end(), labelName) != targetLabels.end();
}

inline std::string firstTargetLabel(const std::vector<std::string>& targetLabels) {
    return targetLabels.empty() ? std::string() : targetLabels.front();
}

inline bool pointInRect(const RoiBox& rect, float x, float y) {
    const float width = std::max(0.0F, clampNormalized(rect.width));
    const float height = std::max(0.0F, clampNormalized(rect.height));
    const float centerX = clampNormalized(rect.cx);
    const float centerY = clampNormalized(rect.cy);
    const float left = clampNormalized(centerX - width * 0.5F);
    const float top = clampNormalized(centerY - height * 0.5F);
    const float right = std::min(1.0F, left + width);
    const float bottom = std::min(1.0F, top + height);
    return x >= left && x <= right && y >= top && y <= bottom;
}

inline bool pointInPolygon(const RoiPolygon& polygon, float x, float y) {
    if (polygon.points.size() < 3) {
        return false;
    }

    bool inside = false;
    size_t j = polygon.points.size() - 1;
    for (size_t i = 0; i < polygon.points.size(); ++i) {
        const float xi = clampNormalized(polygon.points[i].x);
        const float yi = clampNormalized(polygon.points[i].y);
        const float xj = clampNormalized(polygon.points[j].x);
        const float yj = clampNormalized(polygon.points[j].y);
        const bool intersect = ((yi > y) != (yj > y)) &&
                               (x < (xj - xi) * (y - yi) / ((yj - yi) + 1e-6F) + xi);
        if (intersect) {
            inside = !inside;
        }
        j = i;
    }
    return inside;
}

inline bool pointInRoiArea(const RoiArea& area, float x, float y) {
    if (!area.enabled) {
        return false;
    }
    if (area.hasRect) {
        return pointInRect(area.rect, x, y);
    }
    if (area.hasPoly) {
        return pointInPolygon(area.poly, x, y);
    }
    return false;
}

inline bool passRoiAreas(const std::vector<RoiArea>& roiAreas,
                         float x,
                         float y,
                         float confidence,
                         float eventConfidenceThreshold) {
    if (roiAreas.empty()) {
        return confidence >= eventConfidenceThreshold;
    }

    bool hasInclude = false;
    bool includeMatched = false;
    for (const RoiArea& area : roiAreas) {
        if (!area.enabled) {
            continue;
        }

        if (area.mode == RoiMode::Include) {
            hasInclude = true;
        }

        const bool hit = pointInRoiArea(area, x, y);
        if (!hit) {
            continue;
        }

        if (area.mode == RoiMode::Exclude) {
            return false;
        }
        if (area.mode == RoiMode::Include) {
            const float roiConfidenceThreshold =
                area.confidence > 0.0F ? area.confidence : eventConfidenceThreshold;
            if (confidence >= roiConfidenceThreshold) {
                includeMatched = true;
            }
        }
    }

    if (hasInclude) {
        return includeMatched;
    }
    return confidence >= eventConfidenceThreshold;
}

/* 事件检测器基类配置。 */
struct BaseEventConfig {
    std::string eventType;
    bool enabled = true;
    int eventIntervalMs = 20000;
    float confidenceThreshold = 0.5F;
    int confidenceGraceMs = 300;
    std::vector<RoiArea> roiAreas;
    std::map<std::string, std::pair<std::string, std::string>> labelDescMap;
    virtual ~BaseEventConfig() = default;
};

class BaseEventDetector {
public:
    BaseEventDetector(const std::string& name, const std::string& cameraId)
        : m_name(name), m_cameraId(cameraId), m_lastEventTimeMs(0) {}

    virtual ~BaseEventDetector() = default;

    virtual bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) = 0;
    virtual BaseEventConfig* getConfig() = 0;
    virtual void reset() = 0;

    std::string getName() const { return m_name; }
    const std::string& getCameraId() const { return m_cameraId; }
    void setCameraId(const std::string& cameraId) { m_cameraId = cameraId; }
    void resetEventInterval() { m_lastEventTimeMs = 0; }

protected:
    bool checkEventInterval(uint64_t currentTimeMs) {
        BaseEventConfig* config = getConfig();
        if (currentTimeMs - m_lastEventTimeMs < static_cast<uint64_t>(config->eventIntervalMs)) {
            return false;
        }
        return true;
    }

    void updateLastEventTime(uint64_t currentTimeMs) { m_lastEventTimeMs = currentTimeMs; }

    uint64_t getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    uint64_t getEventTimestamp(const CFrameMeta* frameMeta) {
        if (frameMeta && frameMeta->timestampMs > 0) {
            return static_cast<uint64_t>(frameMeta->timestampMs);
        }
        return getCurrentTimestamp();
    }

    bool isWithinConfidenceGrace(uint64_t currentTimeMs, uint64_t lastValidTimeMs) {
        const BaseEventConfig* config = const_cast<BaseEventDetector*>(this)->getConfig();
        if (!config || config->confidenceGraceMs <= 0 || lastValidTimeMs == 0) {
            return false;
        }
        if (currentTimeMs < lastValidTimeMs) {
            return true;
        }
        return currentTimeMs - lastValidTimeMs <= static_cast<uint64_t>(config->confidenceGraceMs);
    }

    void fillBaseEventInfo(const CFrameMeta* frameMeta,
                           EventInfo& eventInfo,
                           uint64_t eventTimeMs,
                           int classId) const {
        eventInfo.eventTimeMs = eventTimeMs;
        eventInfo.frameIndex = frameMeta ? frameMeta->index : 0;
        eventInfo.cameraId = frameMeta ? frameMeta->cameraId : m_cameraId;
        eventInfo.classId = classId;

        const BaseEventConfig* config = const_cast<BaseEventDetector*>(this)->getConfig();
        if (config && !config->eventType.empty()) {
            eventInfo.eventType = config->eventType;
        } else {
            eventInfo.eventType = m_name;
        }
    }

    std::pair<std::string, std::string> resolveLabelDescription(const std::string& labelName) const {
        const BaseEventConfig* config = const_cast<BaseEventDetector*>(this)->getConfig();
        if (!config || config->labelDescMap.empty()) {
            return {};
        }

        if (!labelName.empty()) {
            const auto it = config->labelDescMap.find(labelName);
            if (it != config->labelDescMap.end()) {
                return it->second;
            }
        }

        if (config->labelDescMap.size() == 1) {
            return config->labelDescMap.begin()->second;
        }

        const auto defaultIt = config->labelDescMap.find("default");
        if (defaultIt != config->labelDescMap.end()) {
            return defaultIt->second;
        }

        return {};
    }

    void fillEventDescription(EventInfo& eventInfo, const std::string& labelName = "") const {
        const auto description = resolveLabelDescription(labelName);
        if (!description.first.empty()) {
            eventInfo.extraInfo["english_desc"] = description.first;
        }
        if (!description.second.empty()) {
            eventInfo.extraInfo["chinese_desc"] = description.second;
        }
    }

    bool objectPassesRoi(const CObjectMeta* obj) const {
        if (!obj) {
            return false;
        }
        const float centerX =
            clampNormalized(obj->detectorBboxInfo.left + obj->detectorBboxInfo.width * 0.5F);
        const float centerY =
            clampNormalized(obj->detectorBboxInfo.top + obj->detectorBboxInfo.height * 0.5F);
        const BaseEventConfig* config = const_cast<BaseEventDetector*>(this)->getConfig();
        if (!config) {
            return true;
        }
        return passRoiAreas(
            config->roiAreas, centerX, centerY, obj->detectorConfidence, config->confidenceThreshold);
    }

protected:
    std::string m_name;
    std::string m_cameraId;
    uint64_t m_lastEventTimeMs;
};

} // namespace algorithm::cdky

#endif // _EVENT_DETECTOR_BASE_H__
