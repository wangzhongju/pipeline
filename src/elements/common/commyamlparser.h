#ifndef COMM_YAML_PARSER_H_
#define COMM_YAML_PARSER_H_
#include <yaml-cpp/yaml.h>

#include "batch_meta.h"
#define DPRINT printf

#define RGB 0
#define RGBA 1
#define ARGB 2
#define BGR 3
#define BGRA 4
#define ABGR 5
#define GRAY 6

template <typename T>
inline T GET_KEY_VALUE(YAML::Node config, std::string key, T defaultvalue, bool required = false) {
    if (config[key].IsDefined()) {
        return config[key].template as<T>();
    } else if (!required) {
        // DPRINT("warning: %s is not defined.\n", key.c_str());
        return defaultvalue;
    } else if (required) {
        DPRINT("error: %s is not defined.\n", key.c_str());
        exit(1);
    }
    return defaultvalue;
}

template <typename T>
inline T GET_LIST_VALUE(YAML::Node config, std::string key, int idx, T defaultvalue, bool required = false) {
    if (config[key][idx].IsDefined()) {
        return config[key][idx].template as<T>();
    } else if (!required) {
        // DPRINT("warning: %s[%d] is not defined.\n", key.c_str(), idx);
        return defaultvalue;
    } else if (required) {
        DPRINT("error: %s[%d] is not defined.\n", key.c_str(), idx);
        exit(1);
    }
    return defaultvalue;
}

inline PIXEL_FORMAT_E get_base_pixel_format(int format, CDataType data_type, CDataFormat data_order) {
    PIXEL_FORMAT_E ret;

    if (data_type == DATA_U8) {
        switch (format) {
            case ARGB:
                ret = PIXEL_FORMAT_A8R8G8B8;
                break;
            case ABGR:
                ret = PIXEL_FORMAT_A8B8G8R8;
                break;
            case RGB:
                ret = PIXEL_FORMAT_R8G8B8;
                break;
            default:
                ret = PIXEL_FORMAT_UNKNOWN;
        }
    } else if (data_type == DATA_S8) {
        switch (format) {
            case RGB:
                ret = PIXEL_FORMAT_R8G8B8I;
                break;
            case BGR:
                ret = PIXEL_FORMAT_B8G8R8I;
                break;
            default:
                ret = PIXEL_FORMAT_UNKNOWN;
        }
    } else if (data_type == DATA_S16) {
        switch (format) {
            case RGB:
                ret = PIXEL_FORMAT_R16G16B16I;
                break;
            case BGR:
                ret = PIXEL_FORMAT_B16G16R16I;
                break;
            default:
                ret = PIXEL_FORMAT_UNKNOWN;
        }
    } else if (data_type == DATA_F16) {
        switch (format) {
            case RGB:
                ret = PIXEL_FORMAT_R16G16B16F;
                break;
            case BGR:
                ret = PIXEL_FORMAT_B16G16R16F;
                break;
            case GRAY:
                ret = PIXEL_FORMAT_GRAY16F;
                break;
            default:
                ret = PIXEL_FORMAT_UNKNOWN;
        }
    } else if (data_type == DATA_F32) {
        switch (format) {
            case RGB:
                ret = PIXEL_FORMAT_R32G32B32F;
                break;
            case GRAY:
                ret = PIXEL_FORMAT_GRAY32F;
                break;
            default:
                ret = PIXEL_FORMAT_UNKNOWN;
        }
    } else {
        ret = PIXEL_FORMAT_UNKNOWN;
    }

    if (data_order == NCHW) {
        switch (ret) {
            case PIXEL_FORMAT_R8G8B8:
                ret = PIXEL_FORMAT_R8G8B8_PLANAR;
                break;
            case PIXEL_FORMAT_R8G8B8I:
                ret = PIXEL_FORMAT_R8G8B8I_PLANAR;
                break;
            case PIXEL_FORMAT_B8G8R8I:
                ret = PIXEL_FORMAT_B8G8R8I_PLANAR;
                break;
            case PIXEL_FORMAT_R16G16B16I:
                ret = PIXEL_FORMAT_R16G16B16I_PLANAR;
                break;
            case PIXEL_FORMAT_R32G32B32F:
                ret = PIXEL_FORMAT_R32G32B32F_PLANAR;
                break;
            case PIXEL_FORMAT_R16G16B16F:
                ret = PIXEL_FORMAT_R16G16B16F_PLANAR;
                break;
            default:
                ret = PIXEL_FORMAT_UNKNOWN;
        }
    }
    return ret;
}

inline bool esquerybpp(PIXEL_FORMAT_E Format, int *Bpp) {
    switch (Format) {
        case PIXEL_FORMAT_GRAY8:
            *Bpp = 1;
            break;

        case PIXEL_FORMAT_YUY2:
        case PIXEL_FORMAT_UYVY:
        case PIXEL_FORMAT_YVYU:
        case PIXEL_FORMAT_VYUY:
        case PIXEL_FORMAT_I420:
        case PIXEL_FORMAT_YV12:
        case PIXEL_FORMAT_NV12:
        case PIXEL_FORMAT_NV21:
        case PIXEL_FORMAT_GRAY16F:
            *Bpp = 2;
            break;

        case PIXEL_FORMAT_R8G8B8_PLANAR:
        case PIXEL_FORMAT_R8G8B8:
        case PIXEL_FORMAT_R8G8B8I:
        case PIXEL_FORMAT_B8G8R8I:
        case PIXEL_FORMAT_R8G8B8I_PLANAR:
        case PIXEL_FORMAT_B8G8R8I_PLANAR:
            *Bpp = 3;
            break;

        case PIXEL_FORMAT_A8R8G8B8:
        case PIXEL_FORMAT_A8B8G8R8:
        case PIXEL_FORMAT_GRAY32F:
            *Bpp = 4;
            break;

        case PIXEL_FORMAT_R16G16B16F:
        case PIXEL_FORMAT_R16G16B16I:
        case PIXEL_FORMAT_R16G16B16I_PLANAR:
        case PIXEL_FORMAT_R16G16B16F_PLANAR:
            *Bpp = 6;
            break;

        case PIXEL_FORMAT_R32G32B32F:
        case PIXEL_FORMAT_R32G32B32F_PLANAR:
            *Bpp = 12;
            break;

        default:
            return false;
    }

    return true;
}

#endif