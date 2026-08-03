#include "EvidenceService.h"

#include "pipeline_agent/Logger.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include "es_sys_memory.h"
#include "es_comm_video.h"
}

namespace pipeline::evidence {
namespace {

constexpr int kDefaultRecordSeconds = 10;
constexpr uint8_t kBoxY = 135;
constexpr uint8_t kBoxU = 112;
constexpr uint8_t kBoxV = 194;
constexpr int kOverlayHoldFrames = 6;
constexpr int kMaxAlignmentDelayFrames = 250;
constexpr uint16_t kSeiItemDurationMs = 250;
constexpr std::array<uint8_t, 16> kSeiUuid{{
    0x4D, 0x45, 0x54, 0x41, 0x44, 0x41, 0x54, 0x41,
    0x53, 0x45, 0x49, 0x42, 0x59, 0x43, 0x48, 0x42,
}};

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string fileTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&value, &local);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::ostringstream output;
    output << std::put_time(&local, "%Y%m%d_%H%M%S_")
           << std::setw(3) << std::setfill('0') << millis;
    return output.str();
}

std::string safeName(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (const unsigned char value : input) {
        output.push_back(std::isalnum(value) || value == '-' || value == '_'
                             ? static_cast<char>(value)
                             : '_');
    }
    return output.empty() ? "unknown" : output;
}

std::string relativeEvidenceName(const std::string& stream_id,
                                 const std::string& suffix,
                                 const std::string& extension) {
    const std::string stamp = fileTimestamp();
    return stamp.substr(0, 8) + "/" + safeName(stream_id) + "_" + stamp +
           (suffix.empty() ? "" : "_" + safeName(suffix)) + "." + extension;
}

std::string ffmpegError(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}

AVCodecID toCodecId(int codec_id) {
    switch (codec_id) {
        case PT_H264:
            return AV_CODEC_ID_H264;
        case PT_H265:
            return AV_CODEC_ID_HEVC;
        default:
            return static_cast<AVCodecID>(codec_id);
    }
}

void paintPixel(AVFrame* frame, int x, int y, uint8_t value_y = kBoxY,
                uint8_t value_u = kBoxU, uint8_t value_v = kBoxV) {
    if (!frame || x < 0 || y < 0 || x >= frame->width || y >= frame->height) {
        return;
    }
    frame->data[0][y * frame->linesize[0] + x] = value_y;
    const int uv_x = x / 2;
    const int uv_y = y / 2;
    frame->data[1][uv_y * frame->linesize[1] + uv_x] = value_u;
    frame->data[2][uv_y * frame->linesize[2] + uv_x] = value_v;
}

std::array<uint8_t, 5> glyph(char value) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(value)))) {
        case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
        case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
        case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
        case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
        case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
        case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
        case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
        case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
        case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
        case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
        case 'W': return {0x3F, 0x40, 0x38, 0x40, 0x3F};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
        case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
        case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
        case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
        case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
        case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
        case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
        case '#': return {0x14, 0x7F, 0x14, 0x7F, 0x14};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
        case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
        default: return {0, 0, 0, 0, 0};
    }
}

void fillRect(AVFrame* frame, int left, int top, int right, int bottom,
              uint8_t value_y, uint8_t value_u, uint8_t value_v) {
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            paintPixel(frame, x, y, value_y, value_u, value_v);
        }
    }
}

void drawText(AVFrame* frame, int x, int y, const std::string& text,
              int scale) {
    const int width = static_cast<int>(text.size()) * 6 * scale;
    const int height = 7 * scale;
    fillRect(frame, std::max(0, x - scale), std::max(0, y - scale),
             std::min(frame->width - 1, x + width),
             std::min(frame->height - 1, y + height), 24, 128, 128);
    for (char value : text) {
        const auto bitmap = glyph(value);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((bitmap[column] & (1U << row)) == 0) {
                    continue;
                }
                for (int dx = 0; dx < scale; ++dx) {
                    for (int dy = 0; dy < scale; ++dy) {
                        paintPixel(frame, x + column * scale + dx,
                                   y + row * scale + dy);
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

void drawBoxes(AVFrame* frame, const std::vector<DetectionObject>& objects) {
    for (const auto& object : objects) {
        if (!object.has_bbox()) {
            continue;
        }
        const auto& box = object.bbox();
        const int left = std::clamp(
            static_cast<int>((box.cx() - box.width() * 0.5F) * frame->width),
            0, frame->width - 1);
        const int right = std::clamp(
            static_cast<int>((box.cx() + box.width() * 0.5F) * frame->width),
            0, frame->width - 1);
        const int top = std::clamp(
            static_cast<int>((box.cy() - box.height() * 0.5F) * frame->height),
            0, frame->height - 1);
        const int bottom = std::clamp(
            static_cast<int>((box.cy() + box.height() * 0.5F) * frame->height),
            0, frame->height - 1);
        const int thickness = std::max(2, frame->width / 640);
        for (int t = 0; t < thickness; ++t) {
            for (int x = left; x <= right; ++x) {
                paintPixel(frame, x, top + t);
                paintPixel(frame, x, bottom - t);
            }
            for (int y = top; y <= bottom; ++y) {
                paintPixel(frame, left + t, y);
                paintPixel(frame, right - t, y);
            }
        }
        char confidence[16] = {};
        std::snprintf(confidence, sizeof(confidence), "%.2f",
                      std::clamp(object.confidence(), 0.0F, 1.0F));
        std::ostringstream label;
        label << (object.class_name().empty() ? "OBJECT"
                                              : object.class_name());
        if (object.track_id() > 0) {
            label << "#" << object.track_id();
        }
        label << " " << confidence;
        const int text_scale = std::max(1, frame->width / 1280);
        const int text_height = 8 * text_scale;
        const int text_y = top >= text_height ? top - text_height : top + thickness;
        drawText(frame, left, text_y, label.str(), text_scale);
    }
}

void appendBeU16(std::vector<uint8_t>& output, uint16_t value) {
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

void appendBeU32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>(value >> 24));
    output.push_back(static_cast<uint8_t>(value >> 16));
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

uint16_t unitToU16(float value) {
    return static_cast<uint16_t>(
        std::lround(std::clamp(value, 0.0F, 1.0F) * 65535.0F));
}

uint8_t unitToU8(float value) {
    return static_cast<uint8_t>(
        std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

std::string overlayKey(const DetectionObject& object) {
    if (object.track_id() > 0) {
        return "track:" + std::to_string(object.track_id());
    }
    return "class:" + std::to_string(object.class_id()) + ":" +
           object.class_name();
}

std::vector<DetectionObject> interpolateDetections(
    int64_t packet_frame_index, int64_t previous_frame_index,
    const std::vector<DetectionObject>& previous_objects,
    int64_t next_frame_index,
    const std::vector<DetectionObject>& next_objects) {
    if (packet_frame_index < previous_frame_index) {
        return {};
    }
    if (next_frame_index <= previous_frame_index ||
        packet_frame_index >= next_frame_index) {
        return next_objects;
    }
    if (next_frame_index - previous_frame_index > kOverlayHoldFrames) {
        return packet_frame_index - previous_frame_index <=
                       kOverlayHoldFrames
                   ? previous_objects
                   : std::vector<DetectionObject>{};
    }

    const float alpha = std::clamp(
        static_cast<float>(packet_frame_index - previous_frame_index) /
            static_cast<float>(next_frame_index - previous_frame_index),
        0.0F, 1.0F);
    std::vector<DetectionObject> result;
    result.reserve(previous_objects.size() + next_objects.size());
    std::unordered_set<std::string> matched;
    for (const auto& previous : previous_objects) {
        const std::string key = overlayKey(previous);
        const auto next = std::find_if(
            next_objects.begin(), next_objects.end(),
            [&key](const DetectionObject& candidate) {
                return overlayKey(candidate) == key;
            });
        if (next == next_objects.end() || !previous.has_bbox() ||
            !next->has_bbox()) {
            if (packet_frame_index - previous_frame_index <=
                kOverlayHoldFrames) {
                result.push_back(previous);
            }
            continue;
        }

        matched.insert(key);
        DetectionObject object = *next;
        Box* box = object.mutable_bbox();
        const auto interpolate = [alpha](float from, float to) {
            return from + (to - from) * alpha;
        };
        box->set_cx(interpolate(previous.bbox().cx(), next->bbox().cx()));
        box->set_cy(interpolate(previous.bbox().cy(), next->bbox().cy()));
        box->set_width(
            interpolate(previous.bbox().width(), next->bbox().width()));
        box->set_height(
            interpolate(previous.bbox().height(), next->bbox().height()));
        object.set_confidence(interpolate(previous.confidence(),
                                          next->confidence()));
        result.push_back(std::move(object));
    }
    if (alpha >= 1.0F) {
        for (const auto& next : next_objects) {
            if (matched.find(overlayKey(next)) == matched.end()) {
                result.push_back(next);
            }
        }
    }
    return result;
}

void appendSeiLength(std::vector<uint8_t>& output, size_t value) {
    while (value >= 255) {
        output.push_back(255);
        value -= 255;
    }
    output.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t> escapeRbsp(const std::vector<uint8_t>& input) {
    std::vector<uint8_t> output;
    output.reserve(input.size() + 16);
    int zeros = 0;
    for (uint8_t value : input) {
        if (zeros >= 2 && value <= 3) {
            output.push_back(3);
            zeros = 0;
        }
        output.push_back(value);
        zeros = value == 0 ? zeros + 1 : 0;
    }
    return output;
}

std::vector<uint8_t> buildSeiNal(
    AVCodecID codec_id, const std::vector<DetectionObject>& objects) {
    if (objects.empty() ||
        (codec_id != AV_CODEC_ID_H264 && codec_id != AV_CODEC_ID_HEVC)) {
        return {};
    }
    std::vector<uint8_t> payload(kSeiUuid.begin(), kSeiUuid.end());
    payload.insert(payload.end(), {1, 0, 0, 0});
    const size_t count_index = payload.size() - 1;
    uint8_t count = 0;
    for (const auto& object : objects) {
        if (!object.has_bbox() ||
            count == std::numeric_limits<uint8_t>::max()) {
            continue;
        }
        payload.push_back(0);  // item id
        payload.push_back(1);  // bbox item
        appendBeU16(payload, kSeiItemDurationMs);
        appendBeU16(payload, static_cast<uint16_t>(std::min<uint32_t>(
                                 object.track_id(), 65535)));
        payload.push_back(unitToU8(object.confidence()));
        appendBeU16(payload, unitToU16(object.bbox().cx()));
        appendBeU16(payload, unitToU16(object.bbox().cy()));
        appendBeU16(payload, unitToU16(object.bbox().width()));
        appendBeU16(payload, unitToU16(object.bbox().height()));
        appendBeU16(payload, 0);  // angle
        appendBeU32(payload, 0);  // distance
        payload.push_back(static_cast<uint8_t>(
            std::clamp(object.bbox_style(), 0, 1)));
        appendBeU32(payload, object.bbox_style() == 1
                                ? 0xE35D6AFFU : 0x2FBF71FFU);
        const size_t name_size =
            std::min<size_t>(object.class_name().size(), 255);
        payload.push_back(static_cast<uint8_t>(name_size));
        payload.insert(payload.end(), object.class_name().begin(),
                       object.class_name().begin() + name_size);
        ++count;
    }
    payload[count_index] = count;
    if (count == 0) {
        return {};
    }

    std::vector<uint8_t> rbsp;
    appendSeiLength(rbsp, 5);  // user_data_unregistered
    appendSeiLength(rbsp, payload.size());
    rbsp.insert(rbsp.end(), payload.begin(), payload.end());
    rbsp.push_back(0x80);
    const auto escaped = escapeRbsp(rbsp);

    std::vector<uint8_t> nal{0, 0, 0, 1};
    if (codec_id == AV_CODEC_ID_HEVC) {
        nal.push_back(static_cast<uint8_t>(39 << 1));
        nal.push_back(1);
    } else {
        nal.push_back(0x06);
    }
    nal.insert(nal.end(), escaped.begin(), escaped.end());
    return nal;
}

AVPacket* prependSei(const AVPacket* source, AVCodecID codec_id,
                     const std::vector<DetectionObject>& objects) {
    const auto sei = buildSeiNal(codec_id, objects);
    const bool annex_b =
        source && source->size >= 3 && source->data[0] == 0 &&
        source->data[1] == 0 &&
        (source->data[2] == 1 ||
         (source->size >= 4 && source->data[2] == 0 &&
          source->data[3] == 1));
    if (!source || sei.empty() || !annex_b) {
        return source ? av_packet_clone(source) : nullptr;
    }
    if (std::search(source->data, source->data + source->size,
                    kSeiUuid.begin(), kSeiUuid.end()) !=
        source->data + source->size) {
        return av_packet_clone(source);
    }
    AVPacket* output = av_packet_alloc();
    if (!output ||
        av_new_packet(output, static_cast<int>(sei.size()) + source->size) < 0) {
        av_packet_free(&output);
        return nullptr;
    }
    std::memcpy(output->data, sei.data(), sei.size());
    std::memcpy(output->data + sei.size(), source->data, source->size);
    if (av_packet_copy_props(output, source) < 0) {
        av_packet_free(&output);
        return nullptr;
    }
    return output;
}

bool writeJpeg(const std::filesystem::path& output_path,
               const VIDEO_FRAME_INFO_S& source,
               const std::vector<DetectionObject>& objects) {
    const auto& video = source.videoFrame;
    if (video.fd <= 0 || video.width <= 0 || video.height <= 0 ||
        video.pixelFormat != PIXEL_FORMAT_NV12) {
        LOG_ERROR("[Evidence] invalid snapshot frame fd={} size={}x{} "
                  "format={} stride=[{},{}]",
                  video.fd, video.width, video.height, video.pixelFormat,
                  video.stride[0], video.stride[1]);
        return false;
    }

    const int y_stride = video.stride[0] > 0
                             ? static_cast<int>(video.stride[0])
                             : static_cast<int>(video.width);
    const int uv_stride = video.stride[1] > 0
                              ? static_cast<int>(video.stride[1])
                              : y_stride;
    if (y_stride < static_cast<int>(video.width) ||
        uv_stride < static_cast<int>(video.width)) {
        LOG_ERROR("[Evidence] invalid snapshot stride size={}x{} stride=[{},{}]",
                  video.width, video.height, y_stride, uv_stride);
        return false;
    }

    const int mapped_size =
        y_stride * static_cast<int>(video.height) +
        uv_stride * static_cast<int>(video.height) / 2;
    auto* mapped = static_cast<uint8_t*>(
        ES_SYS_Mmap(video.fd, mapped_size, SYS_CACHE_MODE_NOCACHE));
    if (!mapped) {
        LOG_ERROR("[Evidence] snapshot mmap failed fd={} bytes={}",
                  video.fd, mapped_size);
        return false;
    }

    // The EIC7700 hardware MJPEG encoder requires a thread-local ESCL context.
    // Snapshot calls run on pipeline worker threads, so use FFmpeg's software
    // encoder and keep evidence generation isolated from NPU/VENC contexts.
    const AVCodec* codec = avcodec_find_encoder_by_name("mjpeg");
    AVCodecContext* context = codec ? avcodec_alloc_context3(codec) : nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    bool ok = context && frame && packet;
    if (!ok) {
        LOG_ERROR("[Evidence] snapshot jpeg encoder allocation failed "
                  "codec={} context={} frame={} packet={}",
                  codec != nullptr, context != nullptr, frame != nullptr,
                  packet != nullptr);
    }
    if (ok) {
        context->width = video.width;
        context->height = video.height;
        context->pix_fmt = AV_PIX_FMT_YUVJ420P;
        context->color_range = AVCOL_RANGE_JPEG;
        context->time_base = AVRational{1, 25};
        const int result = avcodec_open2(context, codec, nullptr);
        ok = result >= 0;
        if (!ok) {
            LOG_ERROR("[Evidence] snapshot jpeg encoder open failed error={}",
                      ffmpegError(result));
        }
    }
    if (ok) {
        frame->format = context->pix_fmt;
        frame->width = context->width;
        frame->height = context->height;
        frame->color_range = context->color_range;
        int result = av_frame_get_buffer(frame, 32);
        if (result >= 0) {
            result = av_frame_make_writable(frame);
        }
        ok = result >= 0;
        if (!ok) {
            LOG_ERROR("[Evidence] snapshot frame allocation failed error={}",
                      ffmpegError(result));
        }
    }
    if (ok) {
        const uint8_t* source_data[4] = {
            mapped, mapped + y_stride * video.height, nullptr, nullptr};
        const int source_stride[4] = {y_stride, uv_stride, 0, 0};
        SwsContext* sws = sws_getContext(
            video.width, video.height, AV_PIX_FMT_NV12,
            video.width, video.height, context->pix_fmt,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        const int scaled = sws ? sws_scale(
                                     sws, source_data, source_stride, 0,
                                     video.height, frame->data, frame->linesize)
                               : 0;
        ok = sws && scaled == static_cast<int>(video.height);
        if (!ok) {
            LOG_ERROR("[Evidence] snapshot NV12 conversion failed "
                      "size={}x{} stride=[{},{}] scaled={}",
                      video.width, video.height, y_stride, uv_stride, scaled);
        }
        sws_freeContext(sws);
    }
    if (ok) {
        drawBoxes(frame, objects);
        frame->pts = 0;
        int result = avcodec_send_frame(context, frame);
        if (result >= 0) {
            // The board MJPEG encoder is asynchronous. A one-frame encode must
            // be drained explicitly, otherwise the first receive returns EAGAIN.
            const int flush_result = avcodec_send_frame(context, nullptr);
            if (flush_result < 0 && flush_result != AVERROR(EAGAIN) &&
                flush_result != AVERROR_EOF) {
                result = flush_result;
            } else {
                for (int attempt = 0; attempt < 200; ++attempt) {
                    result = avcodec_receive_packet(context, packet);
                    if (result != AVERROR(EAGAIN)) {
                        break;
                    }
                    av_usleep(1000);
                }
            }
        }
        ok = result >= 0;
        if (!ok) {
            LOG_ERROR("[Evidence] snapshot jpeg encode failed error={}",
                      ffmpegError(result));
        }
    }
    if (ok) {
        std::error_code ec;
        std::filesystem::create_directories(output_path.parent_path(), ec);
        FILE* output = ec ? nullptr : std::fopen(output_path.c_str(), "wb");
        ok = output && std::fwrite(packet->data, 1, packet->size, output) ==
                           static_cast<size_t>(packet->size);
        if (!ok) {
            LOG_ERROR("[Evidence] snapshot file write failed file={} "
                      "mkdir_error={} errno={}",
                      output_path.string(), ec.message(), errno);
        }
        if (output) {
            std::fclose(output);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&context);
    ES_SYS_Munmap(mapped, mapped_size);
    return ok;
}

}  // namespace

class EvidenceService::Recorder {
public:
    ~Recorder() {
        close();
        clearCache();
        clearPending();
    }

    void configure(std::string stream_id, std::string base_dir,
                   int duration_seconds) {
        if (stream_id_ != stream_id) {
            close();
            clearCache();
            clearPending();
            previous_detection_.reset();
            next_timestamp_ = AV_NOPTS_VALUE;
            last_duration_ = 0;
        }
        stream_id_ = std::move(stream_id);
        base_dir_ = std::move(base_dir);
        duration_seconds_ =
            duration_seconds > 0 ? duration_seconds : kDefaultRecordSeconds;
    }

    bool append(const EncodedVideoPacket& source) {
        if (!source.data || source.size <= 0 || base_dir_.empty()) {
            return false;
        }

        AVPacket* raw = av_packet_alloc();
        if (!raw || av_new_packet(raw, source.size) < 0) {
            av_packet_free(&raw);
            return false;
        }
        std::memcpy(raw->data, source.data, source.size);
        raw->pts = source.pts;
        raw->dts = source.dts;
        raw->duration = source.duration;
        raw->flags = source.key_frame ? AV_PKT_FLAG_KEY : 0;

        codec_id_ = toCodecId(source.codec_id);
        width_ = source.width;
        height_ = source.height;
        time_base_ = AVRational{
            source.time_base_num > 0 ? source.time_base_num : 1,
            source.time_base_den > 0 ? source.time_base_den : 1000};
        const int64_t default_duration = std::max<int64_t>(
            1, av_rescale_q(1, AVRational{1, 25}, time_base_));
        if (raw->duration <= 0) {
            raw->duration = last_duration_ > 0
                                ? last_duration_ : default_duration;
        }
        last_duration_ = raw->duration;
        if (raw->pts == AV_NOPTS_VALUE && raw->dts != AV_NOPTS_VALUE) {
            raw->pts = raw->dts;
        }
        if (raw->dts == AV_NOPTS_VALUE && raw->pts != AV_NOPTS_VALUE) {
            raw->dts = raw->pts;
        }
        if (raw->pts == AV_NOPTS_VALUE && raw->dts == AV_NOPTS_VALUE) {
            raw->pts = next_timestamp_ == AV_NOPTS_VALUE
                           ? 0 : next_timestamp_;
            raw->dts = raw->pts;
        }
        next_timestamp_ =
            std::max(raw->pts, raw->dts) + std::max<int64_t>(1, raw->duration);

        const int64_t frame_index = std::max<int64_t>(0, source.frame_index);
        pending_.push_back(PendingPacket{raw, frame_index});

        bool ok = flushOverdue(frame_index);
        if (ok) {
            ok = closeExpired();
        }
        return ok;
    }

    bool updateDetections(
        int64_t frame_index, const std::vector<DetectionObject>& objects) {
        if (frame_index < 0) {
            return false;
        }
        bool ok = true;
        DetectionSample next{frame_index, objects};
        if (!previous_detection_) {
            while (!pending_.empty() &&
                   pending_.front().frame_index < next.frame_index) {
                ok = commitPending({}) && ok;
            }
            if (!pending_.empty() &&
                pending_.front().frame_index == next.frame_index) {
                ok = commitPending(next.objects) && ok;
            }
            previous_detection_ = std::move(next);
        } else if (next.frame_index > previous_detection_->frame_index) {
            while (!pending_.empty() &&
                   pending_.front().frame_index <= next.frame_index) {
                const auto interpolated = interpolateDetections(
                    pending_.front().frame_index,
                    previous_detection_->frame_index,
                    previous_detection_->objects,
                    next.frame_index, next.objects);
                ok = commitPending(interpolated) && ok;
            }
            previous_detection_ = std::move(next);
        } else if (next.frame_index == previous_detection_->frame_index) {
            previous_detection_->objects = std::move(next.objects);
        }
        return closeExpired() && ok;
    }

    std::string trigger(
        const std::vector<DetectionObject>& /* alarm_objects */) {
        if (base_dir_.empty() || codec_id_ == AV_CODEC_ID_NONE) {
            return {};
        }
        if (!closeExpired()) {
            return {};
        }
        if (format_) {
            // Keep clips bounded. Repeated alarms during an active recording
            // share the same file instead of extending it forever.
            return file_name_;
        }

        file_name_ = relativeEvidenceName(stream_id_, "", "ts");
        const auto final_path = std::filesystem::path(base_dir_) / file_name_;
        temp_path_ = final_path.parent_path() /
                     ("." + final_path.filename().string());
        std::error_code ec;
        std::filesystem::create_directories(temp_path_.parent_path(), ec);
        if (ec || avformat_alloc_output_context2(
                      &format_, nullptr, "mpegts", temp_path_.c_str()) < 0 ||
            !format_) {
            close();
            return {};
        }

        stream_ = avformat_new_stream(format_, nullptr);
        if (!stream_) {
            close();
            return {};
        }
        stream_->time_base = time_base_;
        stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        stream_->codecpar->codec_id = codec_id_;
        stream_->codecpar->width = width_;
        stream_->codecpar->height = height_;
        if (avio_open(&format_->pb, temp_path_.c_str(), AVIO_FLAG_WRITE) < 0 ||
            avformat_write_header(format_, nullptr) < 0) {
            close();
            return {};
        }
        start_timestamp_ = AV_NOPTS_VALUE;
        for (const auto& cached : cache_) {
            if (!cached.packet || !write(cached.packet)) {
                close();
                return {};
            }
        }
        deadline_ms_ = steadyNowMs() + duration_seconds_ * 1000LL;
        return file_name_;
    }

    void close() {
        if (format_) {
            av_write_trailer(format_);
            if (format_->pb) {
                avio_closep(&format_->pb);
            }
            avformat_free_context(format_);
            format_ = nullptr;
            stream_ = nullptr;
            const auto final_path =
                std::filesystem::path(base_dir_) / file_name_;
            std::error_code ec;
            std::filesystem::rename(temp_path_, final_path, ec);
            if (ec) {
                LOG_ERROR("[Evidence] record finalize failed temp={} final={} error={}",
                          temp_path_.string(), final_path.string(), ec.message());
            } else {
                LOG_INFO("[Evidence] record saved stream={} file={}",
                         stream_id_, final_path.string());
            }
        }
        deadline_ms_ = 0;
        start_timestamp_ = AV_NOPTS_VALUE;
        file_name_.clear();
        temp_path_.clear();
    }

private:
    bool commitPending(const std::vector<DetectionObject>& objects) {
        if (pending_.empty()) {
            return true;
        }
        PendingPacket pending = pending_.front();
        pending_.pop_front();
        AVPacket* decorated = prependSei(pending.packet, codec_id_, objects);
        av_packet_free(&pending.packet);
        if (!decorated) {
            return false;
        }

        const bool key_frame = (decorated->flags & AV_PKT_FLAG_KEY) != 0;
        if (key_frame) {
            clearCache();
        }
        if (!cache_.empty() || key_frame) {
            AVPacket* cached = av_packet_clone(decorated);
            if (cached) {
                cache_.push_back(CachedPacket{cached});
            }
        }

        const bool ok = !format_ || write(decorated);
        av_packet_free(&decorated);
        return ok;
    }

    std::vector<DetectionObject> tailDetections(
        int64_t frame_index) const {
        if (!previous_detection_ ||
            frame_index < previous_detection_->frame_index ||
            frame_index - previous_detection_->frame_index >
                kOverlayHoldFrames) {
            return {};
        }
        return previous_detection_->objects;
    }

    bool flushOverdue(int64_t newest_frame_index) {
        bool ok = true;
        while (!pending_.empty() &&
               newest_frame_index - pending_.front().frame_index >
                   kMaxAlignmentDelayFrames) {
            ok = commitPending(
                     tailDetections(pending_.front().frame_index)) && ok;
        }
        return ok;
    }

    bool flushPendingTail() {
        bool ok = true;
        while (!pending_.empty()) {
            ok = commitPending(
                     tailDetections(pending_.front().frame_index)) && ok;
        }
        return ok;
    }

    bool write(const AVPacket* source) {
        AVPacket* packet = av_packet_clone(source);
        if (!packet) {
            return false;
        }
        const int64_t timestamp =
            packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
        if (start_timestamp_ == AV_NOPTS_VALUE &&
            timestamp != AV_NOPTS_VALUE) {
            start_timestamp_ = timestamp;
        }
        if (start_timestamp_ != AV_NOPTS_VALUE) {
            if (packet->pts != AV_NOPTS_VALUE) {
                packet->pts = std::max<int64_t>(0, packet->pts - start_timestamp_);
            }
            if (packet->dts != AV_NOPTS_VALUE) {
                packet->dts = std::max<int64_t>(0, packet->dts - start_timestamp_);
            }
        }
        packet->stream_index = stream_->index;
        packet->pos = -1;
        const int result = av_interleaved_write_frame(format_, packet);
        av_packet_free(&packet);
        if (result < 0) {
            LOG_ERROR("[Evidence] record mux failed stream={} error={}",
                      stream_id_, ffmpegError(result));
            return false;
        }
        return true;
    }

    bool closeExpired() {
        if (format_ && deadline_ms_ > 0 && steadyNowMs() >= deadline_ms_) {
            const bool ok = flushPendingTail();
            close();
            return ok;
        }
        return true;
    }

    void clearCache() {
        for (auto& cached : cache_) {
            av_packet_free(&cached.packet);
        }
        cache_.clear();
    }

    void clearPending() {
        for (auto& pending : pending_) {
            av_packet_free(&pending.packet);
        }
        pending_.clear();
    }

    struct CachedPacket {
        AVPacket* packet = nullptr;
    };

    struct PendingPacket {
        AVPacket* packet = nullptr;
        int64_t frame_index = 0;
    };

    struct DetectionSample {
        int64_t frame_index = 0;
        std::vector<DetectionObject> objects;
    };

    std::string stream_id_;
    std::string base_dir_;
    std::string file_name_;
    std::filesystem::path temp_path_;
    int duration_seconds_ = kDefaultRecordSeconds;
    int64_t deadline_ms_ = 0;
    int64_t start_timestamp_ = AV_NOPTS_VALUE;
    int64_t next_timestamp_ = AV_NOPTS_VALUE;
    int64_t last_duration_ = 0;
    AVCodecID codec_id_ = AV_CODEC_ID_NONE;
    int width_ = 0;
    int height_ = 0;
    AVRational time_base_{1, 1000};
    AVFormatContext* format_ = nullptr;
    AVStream* stream_ = nullptr;
    std::vector<CachedPacket> cache_;
    std::deque<PendingPacket> pending_;
    std::optional<DetectionSample> previous_detection_;
};

EvidenceService& EvidenceService::instance() {
    static EvidenceService service;
    return service;
}

EvidenceService::~EvidenceService() {
    close();
}

void EvidenceService::applyConfig(const AgentConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, StreamConfig> next;
    for (const auto& stream : config.streams()) {
        if (!stream.stream_id().empty()) {
            next.emplace(stream.stream_id(), stream);
            if (!stream.alarm_record_dir().empty()) {
                auto& recorder = recorders_[stream.stream_id()];
                if (!recorder) {
                    recorder = std::make_unique<Recorder>();
                }
                recorder->configure(stream.stream_id(),
                                    stream.alarm_record_dir(),
                                    stream.alarm_record_duration_s());
            }
        }
    }
    for (auto it = recorders_.begin(); it != recorders_.end();) {
        if (next.find(it->first) == next.end()) {
            it->second->close();
            it = recorders_.erase(it);
        } else {
            ++it;
        }
    }
    configs_.swap(next);
}

void EvidenceService::updateDetections(
    const std::string& stream_id,
    int64_t frame_index,
    const std::vector<DetectionObject>& objects) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = recorders_.find(stream_id);
    if (it != recorders_.end() &&
        !it->second->updateDetections(frame_index, objects)) {
        LOG_ERROR("[Evidence] detection alignment failed stream={} frame={}",
                  stream_id, frame_index);
    }
}

bool EvidenceService::appendPacket(const std::string& stream_id,
                                   const EncodedVideoPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = recorders_.find(stream_id);
    return it == recorders_.end() ||
           it->second->append(packet);
}

std::string EvidenceService::triggerRecording(
    const std::string& stream_id,
    const std::vector<DetectionObject>& alarm_objects) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = recorders_.find(stream_id);
    return it == recorders_.end()
               ? std::string{}
               : it->second->trigger(alarm_objects);
}

std::string EvidenceService::saveSnapshot(
    const std::string& stream_id, const VIDEO_FRAME_INFO_S& frame,
    const std::vector<DetectionObject>& objects, const std::string& scenario) {
    std::string base_dir;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = configs_.find(stream_id);
        if (it == configs_.end()) {
            return {};
        }
        base_dir = it->second.alarm_snapshot_dir();
    }
    if (base_dir.empty() || objects.empty()) {
        return {};
    }

    const std::string relative =
        relativeEvidenceName(stream_id, scenario, "jpg");
    const auto path = std::filesystem::path(base_dir) / relative;
    if (!writeJpeg(path, frame, objects)) {
        LOG_ERROR("[Evidence] snapshot failed stream={} file={}",
                  stream_id, path.string());
        return {};
    }
    LOG_INFO("[Evidence] snapshot saved stream={} file={}",
             stream_id, path.string());
    return relative;
}

void EvidenceService::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : recorders_) {
        entry.second->close();
    }
    recorders_.clear();
    configs_.clear();
}

}  // namespace pipeline::evidence
