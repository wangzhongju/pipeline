#ifndef _ESSDKPL_OSD_H_
#define _ESSDKPL_OSD_H_
#ifdef __cplusplus
#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "log.h"
#include "sw_performance.h"

extern "C" {
#include "es_comm_video.h"
#include "es_sys.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
}
using namespace std;

#if defined(OSD_HW_WITH_CPU)
#define MAX_OBJ_NUM 2048
#elif defined(OSD_HW_WITH_2D)
#define MAX_OBJ_NUM 128
#elif defined(OSD_HW_WITH_3D)
#define MAX_OBJ_NUM 8
#endif
#define MAX_RECT_NUM MAX_OBJ_NUM
#define MAX_OVERLAY_NUM MAX_OBJ_NUM

typedef struct {
    float x;
    float y;
    float width;
    float height;
} Rect2f;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} Rect2i;

typedef struct {
    int type;  // yuv:1,rgb:2
    float val[3];
    bool isDate;
    unsigned char fval2_225[3];
    unsigned char uvPair[4096];
} Color;

enum Method { OSD_DRAW_RECTANGLE = (1 << 0), OSD_DRAW_TEXT = (1 << 1), OSD_DRAW_SEGMENT = (1 << 2) };

class OsdProc {
   public:
    virtual ~OsdProc(){};
    virtual void loadFontData(string fontFileName, int id) = 0;

    virtual void process(VIDEO_FRAME_S* src, VIDEO_FRAME_S* dst) = 0;

    virtual int settextparam(vector<string>* textvec, vector<Rect2f>* rectvec, int textnum, int surf_width,
                             int surf_height, int fontHeight, Color color, int thickness = -1, int line_type = 16,
                             bool bottomLeftOrigin = false) = 0;

    virtual int setrectparam(vector<Rect2f>* rectvec, int rectnum, Color color, int thickness = -1, int line_type = 16,
                             int shift = 0) = 0;

    virtual int setRtmOsdParam(vector<Rect2f>* rectvec, int rectnum, vector<Rect2f>* pointvec, int pointNum,
                               Color color, int thickness = -1, int line_type = 16, int shift = 0) = 0;
    virtual int prepareOsd(VIDEO_FRAME_S* src, int surf_width, int surf_height) = 0;
    virtual int unprepareOsd(VIDEO_FRAME_S* src) = 0;

    PerformanceStatic* osdPerformance;
};

OsdProc* createOsdProc(int method, int is_yuv = 1);

#endif
#endif  //_ESSDKPL_OSD_H_
