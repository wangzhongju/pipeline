#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 事件模块句柄。 */
typedef struct event_handle_t event_handle_t;

/* ROI 语义模式。 */
typedef enum event_roi_mode_t {
    EVENT_ROI_MODE_UNKNOWN = 0, /* 未指定，按默认逻辑处理。 */
    EVENT_ROI_INCLUDE = 1,      /* 仅在 ROI 内生效。 */
    EVENT_ROI_EXCLUDE = 2,      /* ROI 内忽略（黑名单区域）。 */
} event_roi_mode_t;

/* ROI 形状类型。 */
typedef enum event_roi_shape_t {
    EVENT_ROI_SHAPE_UNKNOWN = 0,
    EVENT_ROI_SHAPE_RECT = 1,
    EVENT_ROI_SHAPE_POLY = 2,
} event_roi_shape_t;

/* 矩形 ROI（归一化坐标，范围 [0,1]）。 */
typedef struct event_box_t {
    float cx;     /* 中心点 X（归一化）。 */
    float cy;     /* 中心点 Y（归一化）。 */
    float width;  /* 宽度（归一化）。 */
    float height; /* 高度（归一化）。 */
    float angle;  /* 旋转角度（当前几何判定暂未使用）。 */
} event_box_t;

/* 多边形顶点（归一化坐标，范围 [0,1]）。 */
typedef struct event_point2d_t {
    float x; /* 顶点 X（归一化）。 */
    float y; /* 顶点 Y（归一化）。 */
} event_point2d_t;

/* 多边形 ROI。 */
typedef struct event_polygon_t {
    const event_point2d_t* points; /* 顶点数组指针。 */
    size_t point_count;             /* 顶点数量，>=3 才有效。 */
} event_polygon_t;

/* ROI 区域定义。 */
typedef struct event_roi_area_t {
    int enabled;                  /* 0=禁用，非0=启用。 */
    event_roi_mode_t mode;        /* ROI 语义模式。 */
    event_roi_shape_t shape_type; /* 形状类型。 */
    event_box_t rect;             /* 矩形 ROI（shape_type=RECT 时有效）。 */
    event_polygon_t poly;         /* 多边形 ROI（shape_type=POLY 时有效）。 */
    float confidence;             /* ROI 区域级置信度阈值，范围建议 [0,1]；0 表示不限制。 */
} event_roi_area_t;

/* 目标对象（输入检测与输出告警共用）。 */
typedef struct event_object_t {
    const char* class_name; /* 类别名称，例如 "person"。 */
    int class_id;           /* 类别 ID。 */
    int tracker_id;         /* 跟踪 ID，<=0 表示无有效跟踪 ID。 */
    float confidence;       /* 置信度，范围建议 [0,1]。 */
    float x;                /* 框中心点 X（归一化）。 */
    float y;                /* 框中心点 Y（归一化）。 */
    float width;            /* 框宽度（归一化）。 */
    float height;           /* 框高度（归一化）。 */
} event_object_t;

/* 事件请求参数。 */
typedef struct event_request_t {
    const char* event_name;            /* 事件名称，需与 Event.yaml 标签一致。 */
    int has_roi_override;              /* 0=使用配置 ROI；非0=使用本次请求 ROI。 */
    const event_roi_area_t* roi_areas; /* 请求级 ROI 数组。 */
    size_t roi_area_count;             /* 请求级 ROI 数量；配合 has_roi_override 使用。 */
    float confidence_threshold;         /* 请求级目标置信度阈值；>0 时生效，<=0 使用配置阈值。 */
    const char* config_path;            /* 请求级模型配置文件路径，事件模块会解析其中的 target_detection 覆盖参数。 */
    int event_interval_ms;              /* 请求级报警时间间隔，单位毫秒；>0 时生效，<=0 使用配置间隔。 */
} event_request_t;

/* 帧描述（事件判定必需字段）。 */
typedef struct event_frame_desc_t {
    const char* camera_id; /* 相机/通道 ID，用于区分各自事件状态。 */
    int64_t timestamp_ms; /* Frame timestamp in ms; <=0 falls back to system clock. */
} event_frame_desc_t;

/* 事件告警输出。 */
typedef struct event_alarm_t {
    const char* event_name;        /* 事件名称。 */
    const char* description;       /* 事件描述文案。 */
    const event_object_t* objects; /* 告警关联目标数组。 */
    size_t object_count;           /* 告警关联目标数量。 */
} event_alarm_t;

/* 事件模块创建参数。 */
typedef struct event_config_t {
    const char* config_path; /* 配置文件路径，空则使用默认路径。 */
} event_config_t;

/**
 * @brief 创建事件模块实例。
 * @param config 配置参数。
 * @param out_handle 输出句柄。
 * @return 0 成功，非 0 失败。
 */
int event_create(const event_config_t* config, event_handle_t** out_handle);

/**
 * @brief 销毁事件模块实例。
 * @param handle 事件句柄。
 */
void event_destroy(event_handle_t* handle);

/**
 * @brief 重置事件模块内部状态。
 * @param handle 事件句柄。
 * @return 0 成功，非 0 失败。
 */
int event_reset(event_handle_t* handle);

/**
 * @brief 获取当前支持的事件名称列表。
 * @param handle 事件句柄。
 * @param out_names 输出名称数组指针。
 * @param out_count 输出名称数量。
 * @return 0 成功，非 0 失败。
 */
int event_list_supported_names(event_handle_t* handle, const char*** out_names, size_t* out_count);

/**
 * @brief 执行单帧事件判定。
 * @param handle 事件句柄。
 * @param frame_desc 帧描述（当前仅需 camera_id）。
 * @param objects 输入目标数组（中心点坐标+宽高，均为归一化坐标）。
 * @param object_count 输入目标数量。
 * @param requests 事件请求数组。
 * @param request_count 事件请求数量。
 * @param out_alarms 输出告警数组指针。
 * @param out_alarm_count 输出告警数量。
 * @return 0 成功，非 0 失败。
 */
int event_process(event_handle_t* handle,
                  const event_frame_desc_t* frame_desc,
                  const event_object_t* objects,
                  size_t object_count,
                  const event_request_t* requests,
                  size_t request_count,
                  const event_alarm_t** out_alarms,
                  size_t* out_alarm_count);

#ifdef __cplusplus
}
#endif
