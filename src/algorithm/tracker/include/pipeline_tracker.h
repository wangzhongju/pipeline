#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 跟踪模块句柄。 */
typedef struct tracker_handle_t tracker_handle_t;

/* 跟踪模块配置。 */
typedef struct tracker_config_t {
    int enabled;                  /* 0=禁用；非0=启用。 */
    const char* tracker_type;     /* 跟踪器类型字符串，当前默认 "bytetrack"。 */
    float min_thresh;             /* 低阈值。 */
    float high_thresh;            /* 高阈值。 */
    float max_iou_distance;       /* 匹配 IOU 距离阈值。 */
    float high_thresh_person;     /* 行人高阈值。 */
    float high_thresh_motorbike;  /* 摩托/非机动车高阈值。 */
    int max_age;                  /* 最大存活帧数。 */
    int n_init;                   /* 新轨迹确认所需最小命中次数。 */
} tracker_config_t;

/* 单帧描述。 */
typedef struct tracker_frame_desc_t {
    int width;          /* 图像宽度（像素）。 */
    int height;         /* 图像高度（像素）。 */
    int64_t timestamp_ms; /* 时间戳（毫秒）。 */
} tracker_frame_desc_t;

/* 输入检测框。 */
typedef struct tracker_detection_t {
    float x;          /* 框中心点 X（归一化，范围建议 [0,1]）。 */
    float y;          /* 框中心点 Y（归一化，范围建议 [0,1]）。 */
    float width;      /* 框宽度（归一化）。 */
    float height;     /* 框高度（归一化）。 */
    float confidence; /* 置信度，范围建议 [0,1]。 */
    int class_id;     /* 类别 ID。 */
} tracker_detection_t;

/* 输出跟踪结果。 */
typedef struct tracker_output_t {
    float x;          /* 框中心点 X（归一化）。 */
    float y;          /* 框中心点 Y（归一化）。 */
    float width;      /* 框宽度（归一化）。 */
    float height;     /* 框高度（归一化）。 */
    float confidence; /* 置信度。 */
    int class_id;     /* 类别 ID。 */
    int track_id;     /* 跟踪 ID，-1 表示未匹配。 */
    int matched;      /* 是否匹配成功：0=否，1=是。 */
} tracker_output_t;

/**
 * @brief 创建跟踪模块实例。
 * @param config 配置参数。
 * @param out_handle 输出句柄。
 * @return 0 成功，非 0 失败。
 */
int tracker_create(const tracker_config_t* config, tracker_handle_t** out_handle);

/**
 * @brief 销毁跟踪模块实例。
 * @param handle 跟踪句柄。
 */
void tracker_destroy(tracker_handle_t* handle);

/**
 * @brief 重置跟踪状态。
 * @param handle 跟踪句柄。
 * @return 0 成功，非 0 失败。
 */
int tracker_reset(tracker_handle_t* handle);

/**
 * @brief 执行单帧跟踪。
 * @param handle 跟踪句柄。
 * @param frame_desc 当前帧描述。
 * @param detections 输入检测框数组（中心点坐标+宽高，归一化）。
 * @param detection_count 输入检测框数量。
 * @param outputs 输出结果数组，长度应不小于 detection_count。
 * @return 0 成功，非 0 失败。
 */
int tracker_process(tracker_handle_t* handle,
                    const tracker_frame_desc_t* frame_desc,
                    const tracker_detection_t* detections,
                    size_t detection_count,
                    tracker_output_t* outputs);

#ifdef __cplusplus
}
#endif
