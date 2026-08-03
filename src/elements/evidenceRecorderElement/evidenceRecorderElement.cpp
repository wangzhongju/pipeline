#define PL_LOG_ID PL_LOG_OTHERS
#include "evidenceRecorderElement.h"

#include "EvidenceService.h"
#include "video.h"

app_ret EvidenceRecorderElement::ProcessData(
    CBaseMeta* base_meta, CElement const* previous_element) {
    (void)previous_element;
    if (!base_meta || base_meta->mMetaType != VIDEO_PACKET_META) {
        return APP_FAILURE;
    }

    auto* meta = static_cast<CVideoPacketMeta*>(base_meta);
    if (meta->eosFlag || !meta->videoPkt || !meta->videoPkt->pAddr ||
        meta->videoPkt->len <= 0) {
        return APP_SUCCESS;
    }

    pipeline::evidence::EncodedVideoPacket packet;
    packet.data = static_cast<const uint8_t*>(meta->videoPkt->pAddr);
    packet.size = meta->videoPkt->len;
    packet.codec_id = static_cast<int>(meta->type);
    packet.width = meta->width;
    packet.height = meta->height;
    packet.pts = meta->demuxPts;
    packet.dts = meta->demuxDts;
    packet.duration = meta->demuxDuration;
    packet.time_base_num = meta->timeBaseNum;
    packet.time_base_den = meta->timeBaseDen;
    packet.frame_index = meta->index;
    packet.key_frame = meta->keyFrame;
    pipeline::evidence::EvidenceService::instance().appendPacket(
        meta->streamId, packet);
    return APP_SUCCESS;
}

extern "C" CElement* createEsEvidenceRecorderElement(
    const char* name, int die_index) {
    return new EvidenceRecorderElement(name, die_index);
}
