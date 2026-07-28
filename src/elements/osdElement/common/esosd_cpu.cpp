#define PL_LOG_ID PL_LOG_OSD
#include <cassert>
#include <map>

#include "esosd.h"
#include "freetype.h"

using namespace std;

// #define MAX_OVERLAY_BUF 8
// #define MAX_BUF_SIZE 3840*2160*3

typedef map<string, BitMap> STR2BITMAP;
STR2BITMAP str2bitmap;
STR2BITMAP confidence2bitmap;

class OsdProcImpl : public OsdProc {
   public:
    OsdProcImpl(int method, int is_yuv);
    ~OsdProcImpl();

    void loadFontData(string fontFileName, int id) override;

    void process(VIDEO_FRAME_S* src, VIDEO_FRAME_S* dest) override;

    int settextparam(vector<string>* textvec, vector<Rect2f>* rectvec, int textnum, int surf_width, int surf_height,
                     int fontHeight, Color color, int thickness, int line_type, bool bottomLeftOrigin) override;

    int setrectparam(vector<Rect2f>* rectvec, int rectnum, Color color, int thickness, int line_type,
                     int shift) override;

    int setRtmOsdParam(vector<Rect2f>* rectvec, int rectnum, vector<Rect2f>* pointvec, int pointNum, Color color,
                       int thickness, int line_type, int shift) override;
    int prepareOsd(VIDEO_FRAME_S* src, int surf_width, int surf_height) override;
    int unprepareOsd(VIDEO_FRAME_S* src) override;

   private:
    int flag;

    int isyuv;
    // freetype
    EsFreeType2 ft;

    // dma buffer
    // ES_S32 pool;
    // ES_U64 dmafd[MAX_OVERLAY_BUF];
    // unsigned char* virAddr[2048];

    int mTextnum = 0;
    int mRectnum = 0;
    BitMap textMap[MAX_OVERLAY_NUM] = {0};
    // int offset;
    Rect2i textRect[MAX_RECT_NUM] = {0};
    Rect2i rectRect[MAX_RECT_NUM] = {0};

    int mThickness;

    Color textColor;
    Color rectColor;

    int surfWidth;
    int surfHeight;

    // rtmpose
    int mRtmRectNum = 0;
    int mRtmPointNum = 0;
    ;
    int mRtmThickness;
    Color mRtmrectColor;
    Rect2i mRtmtRect[MAX_RECT_NUM] = {0};
    Rect2i mRtmtPoint[MAX_RECT_NUM * 2] = {0};
    unsigned char* pSrc = NULL;
    unsigned char* pSrcTime = NULL;
    size_t currentTimeBufSize = 0;
};

static void rgb2yuv(Color& color) {
    if (1 == color.type) {
        return;
    }
    float r = color.val[0];
    float g = color.val[1];
    float b = color.val[2];
    color.val[0] = 0.2256 * r + 0.5823 * g + 0.0509 * b + 0.0625;
    color.val[1] = -0.1227 * r - 0.3166 * g + 0.4392 * b + 0.5;
    color.val[2] = 0.4392 * r - 0.4039 * g - 0.0353 * b + 0.5;
    color.type = 1;

    color.fval2_225[0] = (unsigned char)(color.val[0] * 255);
    color.fval2_225[1] = (unsigned char)(color.val[1] * 255);
    color.fval2_225[2] = (unsigned char)(color.val[2] * 255);
    unsigned char uvPairTmp[2] = {color.fval2_225[1], color.fval2_225[2]};
    // 预处理UV对数组（栈上分配，避免动态内存）
    for (int w = 0; w < sizeof(color.uvPair); w += 2) {
        memcpy(color.uvPair + w, uvPairTmp, 2);  // 批量写入UV对
    }
}

static bool inRect(int x, int y, Rect2i rect) {
    if (x > rect.x && x < (rect.x + rect.width) && y > rect.y && y < (rect.y + rect.height)) {
        return true;
    }
    return false;
}

OsdProcImpl::OsdProcImpl(int method, int is_yuv = 1) : flag(method), isyuv(is_yuv) {
    // create dmabuf pool
    // VB_POOL_CONFIG_S poolCfg = {0};
    // poolCfg.blkCnt = 3;
    // poolCfg.blkSize = MAX_BUF_SIZE;
    // poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    // ES_S32 ret = ES_VB_CreatePool(&poolCfg,&pool);
    // if (ES_SUCCESS != ret) {
    //     printf("%s create pool failed.", __FUNCTION__);
    //     return;
    // }

    // for(int i = 0; i < MAX_OVERLAY_BUF; i++)
    // {
    // 	dmafd[i] = -1;
    // 	virAddr[i] = NULL;
    // }
    // if ((flag & OSD_DRAW_TEXT) || (flag & OSD_DRAW_SEGMENT))
    // {
    // 	ret = ES_VB_GetBlock(pool, MAX_BUF_SIZE, ES_NULL, &dmafd[0]);
    // 	if (ret) {
    // 		printf("%s get a block from pool %d failed.", __FUNCTION__,
    // pool); 		return;
    // 	}

    // 	virAddr[0] = (ES_U64*)ES_SYS_Mmap( dmafd[0], MAX_BUF_SIZE,
    // SYS_CACHE_MODE_NOCACHE); 	if (NULL == virAddr[0]) {
    // 		ES_VB_ReleaseBlock(dmafd[0]);
    // 		return;
    // 	}
    // }

    // offset = 0;
    mTextnum = 0;
    mRectnum = 0;
    mRtmRectNum = 0;
    mRtmPointNum = 0;
}

void OsdProcImpl::loadFontData(string fontFileName, int id) {
    // freetype
    ft.loadFontData(fontFileName, id);
    return;
}

void drawline(unsigned char* pSrc, int imgW, int imgH, int pointcnt, Rect2i* points, uint8_t yValue, uint8_t uValue,
              uint8_t vValue, int lineWidth, int step, int uvStride) {
    if (pointcnt < 2) {
        // 如果点的数量小于2，则无法绘制线段
        return;
    }

    unsigned char* yPlane = pSrc;                 // Y 平面的起始地址
    unsigned char* uvPlane = pSrc + imgW * imgH;  // UV 平面的起始地址

    for (int i = 0; i < pointcnt - 1; i = i + 2) {
        int x1 = points[i].x;
        int y1 = points[i].y;
        int x2 = points[i + 1].x;
        int y2 = points[i + 1].y;

        // 使用Bresenham算法绘制线段
        int dx = x2 - x1;
        int dy = y2 - y1;
        int sx = (dx >= 0) ? 1 : -1;
        int sy = (dy >= 0) ? 1 : -1;
        dx = abs(dx);
        dy = abs(dy);

        int err = dx - dy;
        int e2;

        int x = x1;
        int y = y1;

        while (1) {
            // 绘制线宽范围内的像素
            for (int wx = -lineWidth / 2; wx <= lineWidth / 2; wx++) {
                for (int wy = -lineWidth / 2; wy <= lineWidth / 2; wy++) {
                    int px = x + wx;
                    int py = y + wy;

                    if (px >= 0 && px < imgW && py >= 0 && py < imgH) {
                        // 设置Y平面的像素值
                        yPlane[py * imgW + px] = yValue;

                        // 设置UV平面的像素值
                        // UV平面是交错存储的，每2x2像素块共享一个UV值
                        int uvX = (px / 2);
                        int uvY = (py / 2);
                        int uvIndex = uvY * (imgW / 2) + uvX;

                        // 确保uvIndex在有效范围内
                        if (uvIndex < (imgW * imgH / 2)) {
                            uvPlane[uvIndex * 2] = uValue;
                            uvPlane[uvIndex * 2 + 1] = vValue;
                        }
                    }
                }
            }

            if (x == x2 && y == y2) {
                break;
            }

            e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x += sx;
            }
            if (e2 < dx) {
                err += dx;
                y += sy;
            }

            // 应用步长
            for (int s = 1; s < step; s++) {
                x += sx;
                y += sy;

                if (x == x2 && y == y2) {
                    break;
                }
            }
        }
    }
}

int OsdProcImpl::settextparam(vector<string>* textvec, vector<Rect2f>* rectvec, int textnum, int surf_width,
                              int surf_height, int fontHeight, Color color, int thickness, int line_type,
                              bool bottomLeftOrigin) {
    if (flag & OSD_DRAW_TEXT == 0) {
        return 0;
    }

    // int dmabufidx = 0;

    surfWidth = surf_width;
    surfHeight = surf_height;
    mTextnum = textnum;

    for (int i = 0; i < textnum; i++) {
        STR2BITMAP* mapptr = NULL;
        string text = (*textvec)[i];
        Rect2f rect = (*rectvec)[i / 2];
        app_debug("I %d string %s\n", i, text.c_str());
        if (i % 2 == 0) {
            mapptr = &str2bitmap;
        } else {
            // continue;
            mapptr = &confidence2bitmap;
            rect.x += textMap[i - 1].width / (float)surf_width;
        }
        // freetype
        STR2BITMAP::iterator it = mapptr->find(text);
        if (it != mapptr->end()) {
            app_debug("text is already exist in map\n");
            textMap[i] = (BitMap)(it->second);
            textRect[i].x = rect.x * surf_width;
            textRect[i].y = rect.y * surf_height;
            textRect[i].width = textMap[i].width;
            textRect[i].height = textMap[i].height;
            if (textRect[i].width > surf_width - textRect[i].x) {
                textRect[i].width = (surf_width - textRect[i].x) > 0 ? (surf_width - textRect[i].x) : 0;
            }
            if (textRect[i].height > surf_height - textRect[i].y) {
                textRect[i].height = (surf_height - textRect[i].y) > 0 ? (surf_height - textRect[i].y) : 0;
            }
        } else {
            app_debug("create a new text map\n");
            int text_width, text_height;
            int baseline = 0;
            Size textSize = ft.getTextSize(text, fontHeight, thickness, &baseline);
            text_width = (textSize.width / 16 + 1) * 16;
            text_height = fontHeight * 3 / 2;  // textSize.height * 3 / 2;
            // text_height = fontHeight * 3 / 2;
            // if(text_width > rect.width*surf_width)
            // {
            // 	text_width = (int)(rect.width*surf_width);
            // }
            // if(text_height > rect.height*surf_height)
            // {
            // 	text_height = (int)(rect.height*surf_height);
            // }

            // if(offset + text_height*text_width > MAX_BUF_SIZE)
            // {
            // 	offset = 0;
            // 	dmabufidx++;
            // 	if(dmafd[dmabufidx] == -1)
            // 	{
            // 		ES_S32 ret = ES_VB_GetBlock(pool, MAX_BUF_SIZE, ES_NULL,
            // &dmafd[dmabufidx]); 		if (ret) {
            // app_error("%s get a block from pool %d failed.", __FUNCTION__,
            // pool); return -1;
            // 		}

            // 		virAddr[dmabufidx] = (ES_U64*)ES_SYS_Mmap(
            // dmafd[dmabufidx], MAX_BUF_SIZE, SYS_CACHE_MODE_NOCACHE);
            // if (NULL == virAddr[dmabufidx]) {
            // ES_VB_ReleaseBlock(dmafd[dmabufidx]); 			return
            // -1;
            // 		}
            // 	}
            // }

            unsigned char* pVirAddr = NULL;
            if (!color.isDate) {
                pVirAddr = (unsigned char*)malloc(text_width * text_height);  // virAddr[currentId++];
            } else {
                // 检查是否需要重新分配缓冲区
                if (pSrcTime && (text_width * text_height > currentTimeBufSize)) {
                    free(pSrcTime);
                    pSrcTime = NULL;
                }

                if (!pSrcTime) {
                    currentTimeBufSize = text_width * text_height;
                    pSrcTime = (unsigned char*)malloc(currentTimeBufSize);
                }
                pVirAddr = pSrcTime;
            }
            assert(pVirAddr != NULL);

            BitMap dst;
            dst.data = (unsigned char*)pVirAddr;
            dst.width = text_width;
            dst.height = text_height;
            dst.stride = text_width;
            app_debug("data:%p,text_width:%d,text_height:%d,text:%s\n", pVirAddr, text_width, text_height,
                      text.c_str());
            ft.Text2Mask(dst, text, fontHeight, bottomLeftOrigin, FT_RENDER_MODE_NORMAL);

            textRect[i].x = rect.x * surf_width;
            textRect[i].y = rect.y * surf_height;
            textRect[i].width = text_width;
            textRect[i].height = text_height;
            textMap[i] = dst;

            if (textRect[i].width > surf_width - textRect[i].x) {
                textRect[i].width = (surf_width - textRect[i].x) > 0 ? (surf_width - textRect[i].x) : 0;
            }
            if (textRect[i].height > surf_height - textRect[i].y) {
                textRect[i].height = (surf_height - textRect[i].y) > 0 ? (surf_height - textRect[i].y) : 0;
            }

            // offset += text_width * text_height;

            // solve memory leak when display date, need refactor later
            if (!color.isDate) {
                (*mapptr)[text] = dst;
            }
        }
    }

    rgb2yuv(color);
    textColor = color;
    return 1;
}

int OsdProcImpl::prepareOsd(VIDEO_FRAME_S* src, int surf_width, int surf_height) {
    int ret = 0;
#if 0

    ret = ES_SYS_MemFlushCache(src->fd);
    if(ret != 0 ){
        app_error("\n ES_SYS_MemFlushCache error 0x%x\n",ret);
    }
#endif

    surfWidth = surf_width;
    surfHeight = surf_height;

    // ES_S32 poolID = ES_VB_Handle2PoolId(src->fd);
    ES_U64 getsize = 0;
    ES_SYS_GetMemSize(src->fd, &getsize);
    // printf("\n poolID %d size %d %llu\n",poolID,size,getsize);

    if (NULL == pSrc) {
        pSrc = (unsigned char*)ES_SYS_Mmap(src->fd, getsize, SYS_CACHE_MODE_NOCACHE);
        if (!pSrc) {
            printf("ES_SYS_Mmap failed in OsdProcImpl::process\n");
            return ret;
        }
    }

    return ret;
}

int OsdProcImpl::unprepareOsd(VIDEO_FRAME_S* src) {
    int ret = 0;
#if 0
    ret = ES_SYS_MemFlushCache(src->fd);
    if(ret != 0 ){
        app_error("\n ES_SYS_MemFlushCache error 0x%x\n",ret);
    }
#endif
    ES_U64 getsize = 0;
    ES_SYS_GetMemSize(src->fd, &getsize);
    // printf("\n unprepare size %d %llu\n",size,getsize);

    if (NULL != pSrc) {
        ret = ES_SYS_Munmap((void*)pSrc, getsize);
        if (ret != 0) {
            app_error(" ES_SYS_Munmap failed 0x%x\n", ret);
            return ret;
        }
        pSrc = NULL;
    }

    return ret;
}

int OsdProcImpl::setRtmOsdParam(vector<Rect2f>* rectvec, int rectnum, vector<Rect2f>* pointvec, int pointNum,
                                Color color, int thickness, int line_type, int shift) {
    if (rectnum > MAX_RECT_NUM) {
        app_error("\n rectnum  %d > MAX_RECT_NUM %d \n", rectnum, MAX_RECT_NUM);
        return 0;
    }
    mRtmRectNum = rectnum;
    mRtmPointNum = pointNum;
    mRtmThickness = thickness > 0 ? thickness : 2;

    for (int i = 0; i < rectnum; i++) {
        mRtmtRect[i].x = (*rectvec)[i].x * surfWidth;
        mRtmtRect[i].y = (*rectvec)[i].y * surfHeight;
        mRtmtRect[i].width = (*rectvec)[i].width * surfWidth;
        mRtmtRect[i].height = (*rectvec)[i].height * surfHeight;

        mRtmtRect[i].x = mRtmtRect[i].x / 2 * 2;
        mRtmtRect[i].y = mRtmtRect[i].y / 2 * 2;
        mRtmtRect[i].width = mRtmtRect[i].width / 2 * 2;
        mRtmtRect[i].height = mRtmtRect[i].height / 2 * 2;

        if (mRtmtRect[i].x + mRtmThickness > surfWidth) {
            mRtmtRect[i].x = (surfWidth - mRtmThickness);
        };
        if (mRtmtRect[i].y + mRtmThickness > surfHeight) {
            mRtmtRect[i].y = (surfHeight - mRtmThickness);
        };
    }

    for (int i = 0; i < pointNum; i++) {
        mRtmtPoint[i].x = (*pointvec)[i].x * surfWidth;
        mRtmtPoint[i].y = (*pointvec)[i].y * surfHeight;
        mRtmtPoint[i].width = (*pointvec)[i].width * surfWidth;
        mRtmtPoint[i].height = (*pointvec)[i].height * surfHeight;

        mRtmtPoint[i].x = mRtmtPoint[i].x / 2 * 2;
        mRtmtPoint[i].y = mRtmtPoint[i].y / 2 * 2;
        mRtmtPoint[i].width = mRtmtPoint[i].width / 2 * 2;
        mRtmtPoint[i].height = mRtmtPoint[i].height / 2 * 2;

        if (mRtmtPoint[i].x + mRtmThickness > surfWidth) {
            mRtmtPoint[i].x = (surfWidth - mRtmThickness);
        };
        if (mRtmtPoint[i].y + mRtmThickness > surfHeight) {
            mRtmtPoint[i].y = (surfHeight - mRtmThickness);
        };
    }

    rgb2yuv(color);
    mRtmrectColor = color;
    return 1;
}

int OsdProcImpl::setrectparam(vector<Rect2f>* rectvec, int rectnum, Color color, int thickness, int line_type,
                              int shift) {
    if (flag & OSD_DRAW_RECTANGLE == 0) {
        return 0;
    }
    if (rectnum > MAX_RECT_NUM) {
        app_error("\n rectnum  %d > MAX_RECT_NUM %d \n", rectnum, MAX_RECT_NUM);
        return 0;
    }
    mRectnum = rectnum;
    mThickness = thickness > 0 ? thickness / 2 * 2 : 4;
    for (int i = 0; i < rectnum; i++) {
        rectRect[i].x = (*rectvec)[i].x * surfWidth;
        rectRect[i].y = (*rectvec)[i].y * surfHeight;
        rectRect[i].width = (*rectvec)[i].width * surfWidth;
        rectRect[i].height = (*rectvec)[i].height * surfHeight;

        rectRect[i].x = rectRect[i].x / 2 * 2;
        rectRect[i].y = rectRect[i].y / 2 * 2;
        rectRect[i].width = rectRect[i].width / 2 * 2;
        rectRect[i].height = rectRect[i].height / 2 * 2;
    }

    rgb2yuv(color);
    rectColor = color;
    return 1;
}

void OsdProcImpl::process(VIDEO_FRAME_S* src, VIDEO_FRAME_S* dst) {
    app_info("%s-%s-%d in\n", PLLOG_fileName(__FILE__), __func__, __LINE__);
    // osdPerformance->performanceStaticStart();
    if (src->pixelFormat != PIXEL_FORMAT_NV12) {
        printf("pixel format must be nv12 now\n");
        return;
    }
    int step = src->stride[0];

    app_debug("w %d h %d retnum num %d mRtmRectNum%d \n", step, src->height, mRectnum, mRtmRectNum);
    int planeOffset = src->height * src->stride[0];
    // rect
    for (int n = 0; n < mRectnum; n++) {
        int rectWidth = rectRect[n].width;
        int rectHeight = rectRect[n].height;
        if (rectWidth < 2 * mThickness || rectHeight < 2 * mThickness) {
            continue;
        }

        // 提前计算一些常用的值，避免重复计算
        int rectTop = rectRect[n].y;
        int rectLeft = rectRect[n].x;
        int rectBottom = rectRect[n].y + rectRect[n].height;
        int rectRight = rectRect[n].x + rectRect[n].width;
        int mHalfThickness = mThickness / 2;

        int uvStride = rectWidth / 2 * 2;  // 每行UV数据长度
        unsigned char uValue = (unsigned char)(rectColor.fval2_225[1]);
        unsigned char vValue = (unsigned char)(rectColor.fval2_225[2]);
        unsigned char yValue = (unsigned char)(rectColor.fval2_225[0]);

        // Y平面处理：顶边和底边
        unsigned char* yPlaneTop = pSrc + rectTop * step + rectLeft;
        unsigned char* yPlaneBottom = pSrc + (rectBottom - mThickness) * step + rectLeft;

        // 绘制上边框
        for (int h = 0; h < mThickness; h++) {
            memset(yPlaneTop + h * step, yValue, rectRect[n].width);
        }

        // 绘制下边框
        for (int h = 0; h < mThickness; h++) {
            memset(yPlaneBottom + h * step, yValue, rectRect[n].width);
        }

        // Y平面处理：左边和右边
        unsigned char* yPlaneLeft = pSrc + (rectTop + mThickness) * step + rectLeft;
        unsigned char* yPlaneRight = pSrc + (rectTop + mThickness) * step + (rectRight - mThickness);

        // 绘制左边框
        for (int h = 0; h < rectRect[n].height - 2 * mThickness; h++) {
            int h_step = h * step;
            for (int w = 0; w < mThickness; w += 2) {
                yPlaneLeft[h_step + w] = yValue;
                yPlaneLeft[h_step + w + 1] = yValue;
            }
        }

        // 绘制右边框
        for (int h = 0; h < rectRect[n].height - 2 * mThickness; h++) {
            int h_step = h * step;
            for (int w = 0; w < mThickness; w += 2) {
                yPlaneRight[h_step + w] = yValue;
                yPlaneRight[h_step + w + 1] = yValue;
            }
        }

        // UV平面处理：顶边和底边（UV平面是交错存储）
        unsigned char* uvPlaneTop = pSrc + planeOffset + (rectTop / 2) * step + (rectLeft / 2) * 2;
        unsigned char* uvPlaneBottom =
            pSrc + planeOffset + (rectBottom / 2 - mHalfThickness) * step + (rectLeft / 2) * 2;

        // 绘制上边框的UV
        for (int h = 0; h < mHalfThickness; h++) {
            //  for (int w = 0; w < rectRect[n].width / 2; w++) {
            // uvPlaneTop[h * step + w * 2] = uValue;  // U值
            // uvPlaneTop[h * step + w * 2 + 1] = vValue; // V值
            memcpy(uvPlaneTop + h * step, rectColor.uvPair, uvStride);
            //  }
        }
        // 绘制下边框的UV
        for (int h = 0; h < mHalfThickness; h++) {
            // for (int w = 0; w < rectRect[n].width / 2; w++) {
            // uvPlaneBottom[h * step + w * 2] = uValue;  // U值
            // uvPlaneBottom[h * step + w * 2 + 1] = vValue; // V值
            memcpy(uvPlaneBottom + h * step, rectColor.uvPair, uvStride);
            // }
        }

        // UV平面处理：左边和右边
        unsigned char* uvPlaneLeft = pSrc + planeOffset + (rectTop / 2 + mHalfThickness) * step + (rectLeft / 2) * 2;
        unsigned char* uvPlaneRight =
            pSrc + planeOffset + (rectTop / 2 + mHalfThickness) * step + (rectRight / 2 - mHalfThickness) * 2;

        // 绘制左边框的UV
        for (int h = 0; h < rectRect[n].height / 2 - mHalfThickness; h++) {
            int h_step = h * step;
            for (int w = 0; w < mHalfThickness; w++) {
                uvPlaneLeft[h_step + w * 2] = uValue;      // U值
                uvPlaneLeft[h_step + w * 2 + 1] = vValue;  // V值
            }
        }

        // 绘制右边框的UV
        for (int h = 0; h < rectRect[n].height / 2 - mHalfThickness; h++) {
            int h_step = h * step;
            for (int w = 0; w < mHalfThickness; w++) {
                uvPlaneRight[h_step + w * 2] = uValue;      // U值
                uvPlaneRight[h_step + w * 2 + 1] = vValue;  // V值
            }
        }
    }
    app_debug("finish draw rect\n");
    // text

    const unsigned char textColor0 = textColor.fval2_225[0];
    const unsigned char textColor1 = textColor.fval2_225[1];
    const unsigned char textColor2 = textColor.fval2_225[2];

    for (int n = 0; n < mTextnum; n++) {
        if (n % 2 == 1) {
            continue;
        }
        int startX = textRect[n].x;
        int startY = textRect[n].y;
        int stopX = textRect[n].x + textRect[n].width;
        int stopY = textRect[n].y + textRect[n].height;
        unsigned char* data = textMap[n].data;
        int mapWidth = textMap[n].width;
        // int mapHeight = textMap[n].height;

        // 处理 Y 平面
        for (int h = startY; h < stopY; h++) {
            unsigned int tmphw = h * step;
            int row_offset = (h - startY) * mapWidth;
            for (int w = startX; w < stopX; w++) {
                unsigned char tmp = data[row_offset + (w - startX)];
                if (tmp > 64) {
                    pSrc[tmphw + w] = textColor0;
                }
            }
        }

        // 处理 UV 平面（仅偶数行和偶数列）
        for (int h = startY; h < stopY; h += 2) {
            int uv_h = h / 2;

            int offsetUV = planeOffset + uv_h * step;
            int row_offset = (h - startY) * mapWidth;
            for (int w = startX; w < stopX; w += 2) {
                int uv_w = w;
                int p_offset = offsetUV + uv_w;
                unsigned char tmp = data[row_offset + (w - startX)];
                if (tmp > 64) {
                    pSrc[p_offset] = textColor1;
                    pSrc[p_offset + 1] = textColor2;
                    // memcpy(pSrc + offsetUV + uv_w,textColor.uvPair,2);
                }
            }
        }
    }

    app_debug("finish draw text %d\n", mTextnum);

    for (int n = 0; n < mRtmRectNum; n++) {
        int centerX = mRtmtRect[n].x;
        int centerY = mRtmtRect[n].y;
        int width = mRtmThickness * 2;   // 宽度为 mRtmThickness 的两倍
        int height = mRtmThickness * 2;  // 高度为 mRtmThickness 的两倍

        // 计算矩形的左上角坐标
        int rectTop = centerY - mRtmThickness;
        int rectLeft = centerX - mRtmThickness;
        int rectBottom = rectTop + height;
        int rectRight = rectLeft + width;

        if (rectTop < 0) {
            rectTop = 0;
        };
        if (rectLeft < 0) {
            rectLeft = 0;
        };
        if (rectBottom > src->height) {
            rectBottom = src->height;
            rectTop = src->height - height;
        };
        if (rectRight > src->width) {
            rectRight = src->width;
            rectLeft = src->width - width;
        };

        if (width < 2 * mRtmThickness || height < 2 * mRtmThickness) {
            app_error("\n width < 2 * mRtmThickness  w h {%d %d} mRtmThickness %d\n", width, height, mRtmThickness);
            continue;
        }

        unsigned char yValue = (unsigned char)(mRtmrectColor.fval2_225[0]);

        // 填充矩形内部的 Y 平面
        for (int h = rectTop; h < rectBottom; h++) {
            unsigned char* yPlane = pSrc + h * step + rectLeft;
            memset(yPlane, yValue, width);
        }

        // 填充矩形内部的 UV 平面
        for (int h = rectTop / 2; h < rectBottom / 2; h++) {
            unsigned char* uvPlane = pSrc + planeOffset + h * step + (rectLeft / 2) * 2;
            memcpy(uvPlane, mRtmrectColor.uvPair, width);
        }
    }

    app_debug("finish mRtmRectNum %d  \n", mRtmRectNum);
    // draw point connect
    if (1) {
        unsigned char uValue = (unsigned char)(mRtmrectColor.fval2_225[1]);
        unsigned char vValue = (unsigned char)(mRtmrectColor.fval2_225[2]);
        unsigned char yValue = (unsigned char)(mRtmrectColor.fval2_225[0]);

        drawline(pSrc, surfWidth, surfHeight, mRtmPointNum, mRtmtPoint, yValue, uValue, vValue, 2, 1, step);
        app_debug("finish   mRtmPointNum %d\n", mRtmPointNum);
    }

    app_debug("finish draw rtmpose\n");
    // ES_S32 ret = ES_SYS_MemFlushCache(src->fd);
    // if (ES_SUCCESS != ret) {
    // 	printf("ES_SYS_MemFlushCache failed, ret %x!\n", ret);
    // }

    // ES_SYS_Munmap((void*)pSrc, size);
    //  osdPerformance->performanceStaticEnd(mRectnum);
    mRectnum = 0;
    mTextnum = 0;
    mRtmRectNum = 0;
    mRtmPointNum = 0;
    app_info("%s-%s-%d out\n", PLLOG_fileName(__FILE__), __func__, __LINE__);
    return;
}

OsdProcImpl::~OsdProcImpl() {
    // for(int i = 0; i < MAX_OVERLAY_BUF; i++)
    // {
    // 	if(virAddr[i] != NULL)
    // 	{
    // 		//ES_SYS_Munmap( virAddr[i], MAX_BUF_SIZE);
    // 		//free(virAddr[i]);
    // 	}
    // 	// if(dmafd[i] != -1)
    // 	// {
    // 	// 	ES_VB_ReleaseBlock(dmafd[i]);
    // 	// }
    // }
    // ES_VB_DestroyPool(pool);
    STR2BITMAP::iterator it;
    for (it = str2bitmap.begin(); it != str2bitmap.end(); it++) {
        BitMap tmp = (BitMap)(it->second);
        free(tmp.data);
    }
    str2bitmap.erase(str2bitmap.begin(), str2bitmap.end());

    for (it = confidence2bitmap.begin(); it != confidence2bitmap.end(); it++) {
        BitMap tmp = (BitMap)(it->second);
        free(tmp.data);
    }
    confidence2bitmap.erase(confidence2bitmap.begin(), confidence2bitmap.end());

    // osdPerformance->performanceStaticReport();
    // delete osdPerformance;
}

OsdProc* createOsdProc(int method, int is_yuv) { return (new OsdProcImpl(method, is_yuv)); }
