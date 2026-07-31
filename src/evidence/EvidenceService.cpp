#include "EvidenceService.h"

#include "SeiInjector.h"
#include "pipeline_agent/Logger.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

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
constexpr int64_t kOverlayWaitMs = 400;
constexpr size_t kMaxPendingPackets = 64;

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

void paintPixel(AVFrame* frame, int x, int y) {
    if (!frame || x < 0 || y < 0 || x >= frame->width || y >= frame->height) {
        return;
    }
    frame->data[0][y * frame->linesize[0] + x] = kBoxY;
    const int uv_x = x / 2;
    const int uv_y = y / 2;
    frame->data[1][uv_y * frame->linesize[1] + uv_x] = kBoxU;
    frame->data[2][uv_y * frame->linesize[2] + uv_x] = kBoxV;
}

const std::array<uint8_t, 7>& glyph(char value) {
    using Rows = std::array<uint8_t, 7>;
    static const std::unordered_map<char, Rows> table = {
        {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
        {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
        {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
        {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
        {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
        {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
        {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
        {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
        {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
        {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}},
        {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
        {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
        {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
        {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
        {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
        {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
        {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
        {'#',{10,31,10,10,31,10,10}}, {'.',{0,0,0,0,0,12,12}},
        {'-',{0,0,0,31,0,0,0}}, {'_',{0,0,0,0,0,0,31}},
        {' ',{0,0,0,0,0,0,0}}, {'?',{14,17,1,2,4,0,4}},
    };
    static const Rows unknown{14,17,1,2,4,0,4};
    const auto it = table.find(value);
    return it == table.end() ? unknown : it->second;
}

void fillRect(AVFrame* frame, int left, int top, int right, int bottom,
              uint8_t y_value, uint8_t u_value, uint8_t v_value) {
    left = std::clamp(left, 0, frame->width - 1);
    right = std::clamp(right, 0, frame->width - 1);
    top = std::clamp(top, 0, frame->height - 1);
    bottom = std::clamp(bottom, 0, frame->height - 1);
    for (int y = top; y <= bottom; ++y) {
        std::memset(frame->data[0] + y * frame->linesize[0] + left,
                    y_value, right - left + 1);
    }
    for (int y = top / 2; y <= bottom / 2; ++y) {
        for (int x = left / 2; x <= right / 2; ++x) {
            frame->data[1][y * frame->linesize[1] + x] = u_value;
            frame->data[2][y * frame->linesize[2] + x] = v_value;
        }
    }
}

void drawLabel(AVFrame* frame, int left, int top, const std::string& text) {
    const int scale = std::max(1, frame->width / 960);
    const int char_width = 6 * scale;
    const int label_width = std::min(
        frame->width - left,
        static_cast<int>(text.size()) * char_width + 4 * scale);
    const int label_height = 9 * scale;
    const int label_top = top >= label_height ? top - label_height : top;
    fillRect(frame, left, label_top, left + label_width - 1,
             label_top + label_height - 1, 32, 128, 128);
    int cursor = left + 2 * scale;
    for (unsigned char raw : text) {
        if (cursor + 5 * scale >= left + label_width) {
            break;
        }
        const char value = raw >= 'a' && raw <= 'z'
                               ? static_cast<char>(raw - 'a' + 'A')
                               : (raw >= 32 && raw <= 126
                                      ? static_cast<char>(raw) : '?');
        const auto& rows = glyph(value);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[row] & (1U << (4 - column))) == 0) {
                    continue;
                }
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        paintPixel(frame, cursor + column * scale + sx,
                                   label_top + scale + row * scale + sy);
                    }
                }
            }
        }
        cursor += char_width;
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
        std::ostringstream label;
        label << (object.class_name().empty() ? "unknown"
                                             : object.class_name());
        label << '#';
        if (object.track_id() > 0) {
            label << object.track_id();
        } else {
            label << "NA";
        }
        label << ' ' << std::fixed << std::setprecision(2)
              << object.confidence();
        drawLabel(frame, left, top, label.str());
    }
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

class EvidenceService::State {
public:
    struct Packet {
        EncodedVideoPacket metadata;
        std::vector<uint8_t> data;
        int64_t queued_at_ms = 0;
    };

    struct Overlay {
        int expected_groups = 1;
        std::unordered_map<std::string, std::vector<DetectionObject>> groups;
    };

    std::unordered_map<std::string, std::deque<Packet>> packets;
    std::unordered_map<std::string, std::map<int64_t, Overlay>> overlays;
};

namespace {

float intersectionOverUnion(const DetectionObject& first,
                            const DetectionObject& second) {
    if (!first.has_bbox() || !second.has_bbox()) {
        return 0.0F;
    }
    const auto& a = first.bbox();
    const auto& b = second.bbox();
    const float a_left = a.cx() - a.width() * 0.5F;
    const float a_top = a.cy() - a.height() * 0.5F;
    const float b_left = b.cx() - b.width() * 0.5F;
    const float b_top = b.cy() - b.height() * 0.5F;
    const float overlap_width = std::max(
        0.0F, std::min(a_left + a.width(), b_left + b.width()) -
                  std::max(a_left, b_left));
    const float overlap_height = std::max(
        0.0F, std::min(a_top + a.height(), b_top + b.height()) -
                  std::max(a_top, b_top));
    const float intersection = overlap_width * overlap_height;
    const float union_area = a.width() * a.height() +
                             b.width() * b.height() - intersection;
    return union_area > 0.0F ? intersection / union_area : 0.0F;
}

void mergeDetections(std::vector<DetectionObject>& destination,
                     const std::vector<DetectionObject>& source) {
    for (const auto& object : source) {
        auto duplicate = std::find_if(
            destination.begin(), destination.end(),
            [&object](const DetectionObject& existing) {
                return existing.class_id() == object.class_id() &&
                       existing.class_name() == object.class_name() &&
                       intersectionOverUnion(existing, object) >= 0.85F;
            });
        if (duplicate == destination.end()) {
            destination.push_back(object);
        } else if (object.confidence() > duplicate->confidence()) {
            *duplicate = object;
        }
    }
}

}  // namespace

class EvidenceService::Recorder {
public:
    ~Recorder() {
        close();
        clearCache();
    }

    void configure(std::string stream_id, std::string base_dir,
                   int duration_seconds) {
        stream_id_ = std::move(stream_id);
        base_dir_ = std::move(base_dir);
        duration_seconds_ =
            duration_seconds > 0 ? duration_seconds : kDefaultRecordSeconds;
    }

    bool append(const EncodedVideoPacket& source) {
        if (!source.data || source.size <= 0 || base_dir_.empty()) {
            return false;
        }
        closeExpired();

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

        if (source.key_frame) {
            clearCache();
        }
        if (!cache_.empty() || source.key_frame) {
            cache_.push_back(av_packet_clone(raw));
        }

        bool ok = true;
        if (format_) {
            ok = write(raw);
        }
        av_packet_free(&raw);
        return ok;
    }

    std::string trigger() {
        if (base_dir_.empty() || codec_id_ == AV_CODEC_ID_NONE) {
            return {};
        }
        deadline_ms_ = steadyNowMs() + duration_seconds_ * 1000LL;
        if (format_) {
            return file_name_;
        }

        file_name_ = relativeEvidenceName(stream_id_, "", "ts");
        const auto final_path = std::filesystem::path(base_dir_) / file_name_;
        output_path_ = final_path;
        std::error_code ec;
        std::filesystem::create_directories(output_path_.parent_path(), ec);
        if (ec || avformat_alloc_output_context2(
                      &format_, nullptr, "mpegts", output_path_.c_str()) < 0 ||
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
        if (avio_open(&format_->pb, output_path_.c_str(), AVIO_FLAG_WRITE) < 0 ||
            avformat_write_header(format_, nullptr) < 0) {
            close();
            return {};
        }
        start_timestamp_ = AV_NOPTS_VALUE;
        for (AVPacket* packet : cache_) {
            if (packet && !write(packet)) {
                close();
                return {};
            }
        }
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
            LOG_INFO("[Evidence] record saved stream={} file={}",
                     stream_id_, output_path_.string());
        }
        deadline_ms_ = 0;
        start_timestamp_ = AV_NOPTS_VALUE;
        file_name_.clear();
        output_path_.clear();
    }

private:
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
        if (format_->pb) {
            avio_flush(format_->pb);
        }
        return true;
    }

    void closeExpired() {
        if (format_ && deadline_ms_ > 0 && steadyNowMs() >= deadline_ms_) {
            close();
        }
    }

    void clearCache() {
        for (AVPacket* packet : cache_) {
            av_packet_free(&packet);
        }
        cache_.clear();
    }

    std::string stream_id_;
    std::string base_dir_;
    std::string file_name_;
    std::filesystem::path output_path_;
    int duration_seconds_ = kDefaultRecordSeconds;
    int64_t deadline_ms_ = 0;
    int64_t start_timestamp_ = AV_NOPTS_VALUE;
    AVCodecID codec_id_ = AV_CODEC_ID_NONE;
    int width_ = 0;
    int height_ = 0;
    AVRational time_base_{1, 1000};
    AVFormatContext* format_ = nullptr;
    AVStream* stream_ = nullptr;
    std::vector<AVPacket*> cache_;
};

EvidenceService& EvidenceService::instance() {
    static EvidenceService service;
    return service;
}

EvidenceService::EvidenceService() : state_(std::make_unique<State>()) {}

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
            drainPacketsLocked(it->first, true);
            it->second->close();
            state_->packets.erase(it->first);
            state_->overlays.erase(it->first);
            it = recorders_.erase(it);
        } else {
            ++it;
        }
    }
    configs_.swap(next);
}

bool EvidenceService::appendPacket(const std::string& stream_id,
                                   const EncodedVideoPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = recorders_.find(stream_id);
    if (it == recorders_.end()) {
        return true;
    }
    State::Packet owned;
    owned.metadata = packet;
    owned.data.assign(packet.data, packet.data + packet.size);
    owned.metadata.data = nullptr;
    owned.queued_at_ms = steadyNowMs();
    state_->packets[stream_id].push_back(std::move(owned));
    drainPacketsLocked(stream_id, false);
    return true;
}

void EvidenceService::updateDetections(
    const std::string& stream_id, int64_t media_pts_ms,
    const std::string& model_group_id, int expected_model_groups,
    const std::vector<DetectionObject>& objects) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recorders_.find(stream_id) == recorders_.end()) {
        return;
    }
    auto& overlay = state_->overlays[stream_id][media_pts_ms];
    overlay.expected_groups =
        std::max(overlay.expected_groups, std::max(1, expected_model_groups));
    overlay.groups[model_group_id] = objects;
    while (state_->overlays[stream_id].size() > kMaxPendingPackets * 2) {
        state_->overlays[stream_id].erase(
            state_->overlays[stream_id].begin());
    }
    drainPacketsLocked(stream_id, false);
}

void EvidenceService::drainPacketsLocked(const std::string& stream_id,
                                         bool force) {
    const auto recorder_it = recorders_.find(stream_id);
    if (recorder_it == recorders_.end()) {
        return;
    }
    auto& packets = state_->packets[stream_id];
    auto& overlays = state_->overlays[stream_id];
    while (!packets.empty()) {
        auto overlay_it = overlays.find(packets.front().metadata.media_pts_ms);
        const bool complete =
            overlay_it != overlays.end() &&
            static_cast<int>(overlay_it->second.groups.size()) >=
                overlay_it->second.expected_groups;
        const bool expired =
            steadyNowMs() - packets.front().queued_at_ms >= kOverlayWaitMs;
        if (!force && !complete && !expired &&
            packets.size() <= kMaxPendingPackets) {
            break;
        }

        State::Packet packet = std::move(packets.front());
        packets.pop_front();
        std::vector<DetectionObject> objects;
        if (overlay_it != overlays.end()) {
            for (const auto& group : overlay_it->second.groups) {
                mergeDetections(objects, group.second);
            }
            overlays.erase(overlay_it);
        }

        std::vector<uint8_t> annotated;
        if (injectMospSei(packet.data.data(), packet.data.size(),
                          packet.metadata.codec_id, objects,
                          static_cast<int>(kOverlayWaitMs * 2), annotated)) {
            packet.metadata.data = annotated.data();
            packet.metadata.size = static_cast<int>(annotated.size());
        } else {
            packet.metadata.data = packet.data.data();
            packet.metadata.size = static_cast<int>(packet.data.size());
        }
        recorder_it->second->append(packet.metadata);
    }
}

std::string EvidenceService::triggerRecording(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    drainPacketsLocked(stream_id, false);
    const auto it = recorders_.find(stream_id);
    return it == recorders_.end() ? std::string{} : it->second->trigger();
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
        drainPacketsLocked(entry.first, true);
        entry.second->close();
    }
    recorders_.clear();
    configs_.clear();
    state_->packets.clear();
    state_->overlays.clear();
}

}  // namespace pipeline::evidence
