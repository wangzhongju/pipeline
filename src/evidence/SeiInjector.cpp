#include "SeiInjector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

extern "C" {
#include "es_comm_video.h"
}

namespace pipeline::evidence {
namespace {

constexpr std::array<uint8_t, 4> kStartCode{{0, 0, 0, 1}};
constexpr std::array<uint8_t, 16> kMospUuid{{
    0x4D, 0x45, 0x54, 0x41, 0x44, 0x41, 0x54, 0x41,
    0x53, 0x45, 0x49, 0x42, 0x59, 0x43, 0x48, 0x42,
}};

void appendU16(std::vector<uint8_t>& output, uint16_t value) {
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

void appendU32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>(value >> 24));
    output.push_back(static_cast<uint8_t>(value >> 16));
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

uint16_t unitU16(float value) {
    return static_cast<uint16_t>(
        std::round(std::clamp(value, 0.0F, 1.0F) * 65535.0F));
}

uint8_t unitU8(float value) {
    return static_cast<uint8_t>(
        std::round(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

uint16_t angleU16(float value) {
    float angle = std::fmod(value, 360.0F);
    if (angle < 0.0F) {
        angle += 360.0F;
    }
    return static_cast<uint16_t>(
        std::round(angle / 360.0F * 65535.0F));
}

void appendLength(std::vector<uint8_t>& output, size_t value) {
    while (value >= 255) {
        output.push_back(255);
        value -= 255;
    }
    output.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t> escapeRbsp(const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> output;
    output.reserve(rbsp.size() + 16);
    int zero_count = 0;
    for (uint8_t value : rbsp) {
        if (zero_count >= 2 && value <= 3) {
            output.push_back(3);
            zero_count = 0;
        }
        output.push_back(value);
        zero_count = value == 0 ? zero_count + 1 : 0;
    }
    return output;
}

bool annexB(const uint8_t* data, size_t size) {
    return data != nullptr &&
           ((size >= 4 && data[0] == 0 && data[1] == 0 &&
             data[2] == 0 && data[3] == 1) ||
            (size >= 3 && data[0] == 0 && data[1] == 0 &&
             data[2] == 1));
}

std::vector<uint8_t> buildPayload(
    const std::vector<DetectionObject>& objects,
    int display_duration_ms) {
    const size_t count = std::min<size_t>(objects.size(), 255);
    std::vector<uint8_t> payload;
    payload.reserve(kMospUuid.size() + 4 + count * 40);
    payload.insert(payload.end(), kMospUuid.begin(), kMospUuid.end());
    payload.push_back(1);
    payload.push_back(0);
    payload.push_back(0);
    payload.push_back(static_cast<uint8_t>(count));

    for (size_t index = 0; index < count; ++index) {
        const auto& object = objects[index];
        const auto& box = object.bbox();
        payload.push_back(static_cast<uint8_t>(index));
        payload.push_back(1);
        appendU16(payload, static_cast<uint16_t>(
            std::clamp(display_duration_ms, 0, 65535)));
        appendU16(payload, static_cast<uint16_t>(std::min<uint32_t>(
            object.track_id(), std::numeric_limits<uint16_t>::max())));
        payload.push_back(unitU8(object.confidence()));
        appendU16(payload, unitU16(box.cx()));
        appendU16(payload, unitU16(box.cy()));
        appendU16(payload, unitU16(box.width()));
        appendU16(payload, unitU16(box.height()));
        appendU16(payload, angleU16(box.angle()));
        appendU32(payload, 0);
        payload.push_back(object.bbox_style() == 0 ? 0 : 1);
        appendU32(payload, object.bbox_style() == 0
                               ? 0x2FBF71FFU
                               : 0xE35D6AFFU);
        const std::string label =
            object.class_name().empty() ? "unknown" : object.class_name();
        const size_t label_size = std::min<size_t>(label.size(), 255);
        payload.push_back(static_cast<uint8_t>(label_size));
        payload.insert(payload.end(), label.begin(),
                       label.begin() + static_cast<std::ptrdiff_t>(label_size));
    }
    return payload;
}

}  // namespace

bool injectMospSei(const uint8_t* source, size_t source_size,
                   int codec_id,
                   const std::vector<DetectionObject>& objects,
                   int display_duration_ms,
                   std::vector<uint8_t>& output) {
    output.clear();
    if (source == nullptr || source_size == 0 || objects.empty() ||
        (codec_id != PT_H264 && codec_id != PT_H265)) {
        return false;
    }

    const std::vector<uint8_t> payload =
        buildPayload(objects, display_duration_ms);
    std::vector<uint8_t> rbsp;
    appendLength(rbsp, 5);
    appendLength(rbsp, payload.size());
    rbsp.insert(rbsp.end(), payload.begin(), payload.end());
    rbsp.push_back(0x80);
    const std::vector<uint8_t> ebsp = escapeRbsp(rbsp);

    std::vector<uint8_t> nal;
    if (codec_id == PT_H265) {
        nal.push_back(static_cast<uint8_t>(39 << 1));
        nal.push_back(1);
    } else {
        nal.push_back(6);
    }
    nal.insert(nal.end(), ebsp.begin(), ebsp.end());

    output.reserve(source_size + nal.size() + 4);
    if (annexB(source, source_size)) {
        output.insert(output.end(), kStartCode.begin(), kStartCode.end());
    } else {
        appendU32(output, static_cast<uint32_t>(nal.size()));
    }
    output.insert(output.end(), nal.begin(), nal.end());
    output.insert(output.end(), source, source + source_size);
    return true;
}

}  // namespace pipeline::evidence
