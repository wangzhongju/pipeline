#define PL_LOG_ID PL_LOG_OTHERS

#define MEDIA_AGENT_LOGGER_NAME "cdky.event"
#include "common/Logger.h"
#include "vehicle_parking_detector.h"

#include <algorithm>
#include <ctime>

namespace algorithm::cdky {

bool VehicleParkingEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) {
        LOG_DEBUG("vehicle_parking disabled detector={}", m_name.c_str());
        return false;
    }
    if (!isInActiveTimeRange()) {
        LOG_TRACE("vehicle_parking outside active time detector={} camera_id={}",
                  m_name.c_str(),
                  m_cameraId);
        return false;
    }

    const uint64_t currentTimeMs = getEventTimestamp(frameMeta);
    std::string vehicleLabel;
    const bool vehicleInRoi = isVehicleInRoi(frameMeta->objs, vehicleLabel);

    if (vehicleInRoi) {
        m_state.lastVehicleTimeMs = currentTimeMs;
        if (!m_state.vehiclePresent) {
            m_state.vehiclePresent = true;
            m_state.parkingStartTimeMs = currentTimeMs;
            m_state.isParking = true;
            m_state.currentVehicleLabel = vehicleLabel;
        }

        const uint64_t parkingTimeMs = currentTimeMs - m_state.parkingStartTimeMs;
        const uint64_t thresholdMs = static_cast<uint64_t>(m_config.parkingTimeThreshold * 1000);
        if (parkingTimeMs >= thresholdMs && checkEventInterval(currentTimeMs)) {
            fillBaseEventInfo(frameMeta, eventInfo, currentTimeMs, 0);
            fillEventDescription(eventInfo, vehicleLabel);
            eventInfo.extraInfo["vehicle_type"] = vehicleLabel;
            eventInfo.extraInfo["parking_duration"] = std::to_string(parkingTimeMs / 1000) + "s";

            updateLastEventTime(currentTimeMs);
            LOG_INFO("vehicle_parking triggered detector={} camera_id={} vehicle={} parking_ms={}",
                     m_name.c_str(),
                     m_cameraId,
                     vehicleLabel.c_str(),
                     parkingTimeMs);
            return true;
        }
    } else {
        if (m_state.vehiclePresent &&
            isWithinConfidenceGrace(currentTimeMs, m_state.lastVehicleTimeMs)) {
            return false;
        }
        m_state.vehiclePresent = false;
        m_state.parkingStartTimeMs = 0;
        m_state.lastVehicleTimeMs = 0;
        m_state.isParking = false;
        m_state.currentVehicleLabel.clear();
    }

    return false;
}

bool VehicleParkingEventDetector::isInActiveTimeRange() {
    if (m_config.activeTimeRanges.empty()) {
        return true;
    }

    const auto now = std::chrono::system_clock::now();
    const auto timeValue = std::chrono::system_clock::to_time_t(now);
    const std::tm localTm = *std::localtime(&timeValue);

    for (const auto& range : m_config.activeTimeRanges) {
        if (range.isInRange(localTm.tm_hour, localTm.tm_min)) {
            return true;
        }
    }
    return false;
}

bool VehicleParkingEventDetector::isVehicleInRoi(const std::vector<CObjectMeta*>& objects,
                                                 std::string& vehicleLabel) {
    for (CObjectMeta* obj : objects) {
        if (!obj) {
            continue;
        }

        bool isTargetVehicle = false;
        for (const std::string& label : m_config.targetLabels) {
            if (obj->objLable == label) {
                isTargetVehicle = true;
                vehicleLabel = label;
                break;
            }
        }
        if (!isTargetVehicle) {
            continue;
        }
        if (!objectPassesRoi(obj)) {
            continue;
        }
        return true;
    }
    return false;
}

void VehicleParkingEventDetector::reset() {
    m_state.vehiclePresent = false;
    m_state.parkingStartTimeMs = 0;
    m_state.lastVehicleTimeMs = 0;
    m_state.isParking = false;
    m_state.currentVehicleLabel.clear();
    LOG_DEBUG("vehicle_parking reset detector={} camera_id={}", m_name.c_str(), m_cameraId);
}

} // namespace algorithm::cdky
