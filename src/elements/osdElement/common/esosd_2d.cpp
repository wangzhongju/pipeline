#define PL_LOG_ID PL_LOG_OSD
#include <cassert>
#include <map>

#include "esosd.h"
#include "freetype.h"
extern "C" {
#include "es_vps.h"
}

using namespace std;

//#define MAX_BUF_SIZE 3840*2160*3
#define MAX_BITMAP_SIZE 1024 * 100
#define MAX_BITMAP_NUM 200

typedef map<string, VIDEO_FRAME_S*> STR2FRAME;
STR2FRAME str2frame;

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

   private:
    int flag;

    int isyuv;
    // freetype
    EsFreeType2 ft;

    // dma buffer
    VB_POOL pool;
    ES_U64 dmafd[MAX_BITMAP_NUM];
    unsigned char* virAddr[MAX_BITMAP_NUM];

    int mTextnum;
    int mRectnum;
    VIDEO_FRAME_S textMap[MAX_OVERLAY_NUM];
    int offset;
    Rect2i textRect[MAX_RECT_NUM];
    Rect2i rectRect[MAX_RECT_NUM];

    int mThickness;

    // Color textColor;
    ES_U32 rectColor;
    ES_U32 textColor;

    int surfWidth;
    int surfHeight;

    PerformanceStatic* osdPerformance;
};

static void rgb2yuv(Color& color) {
    float r = color.val[0];
    float g = color.val[1];
    float b = color.val[2];
    color.val[0] = 0.2256 * r + 0.5823 * g + 0.0509 * b + 0.0625;
    color.val[1] = -0.1227 * r - 0.3166 * g + 0.4392 * b + 0.5;
    color.val[2] = 0.4392 * r - 0.4039 * g - 0.0353 * b + 0.5;
    color.type = 1;
}

static bool inRect(int x, int y, Rect2i rect) {
    if (x > rect.x && x < (rect.x + rect.width) && y > rect.y && y < (rect.y + rect.height)) {
        return true;
    }
    return false;
}
void transrgb2argb(unsigned char* src, ES_U32 width, ES_U32 height, ES_U32 color) {
    // 获取 color 的各个通道值
    uint8_t alpha = (color >> 24) & 0xFF;
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = (color)&0xFF;

    // 计算目标图像的大小（字节）
    ES_U32 target_size = width * height * 4;

    // 创建临时缓冲区以存储转换后的图像数据
    unsigned char* dst = (unsigned char*)malloc(target_size);
    if (dst == NULL) {
        // 处理内存分配失败的情况
        printf(" transrgb2argb error ");
        return;
    }

    // 遍历源图像数据
    for (ES_U32 y = 0; y < height; y++) {
        for (ES_U32 x = 0; x < width; x++) {
            // 计算源图像中的像素位置
            ES_U32 src_idx = (y * width + x) * 3;

            // 获取源图像中的像素值
            uint8_t r = src[src_idx];
            uint8_t g = src[src_idx + 1];
            uint8_t b = src[src_idx + 2];

            // 计算目标图像中的像素位置
            ES_U32 dst_idx = (y * width + x) * 4;

            // 如果源像素是黑色 (0, 0, 0)，将像素值转换为 A8R8G8B8 格式，alpha 通道设为 0（透明）
            if (r == 0 && g == 0 && b == 0) {
                dst[dst_idx] = b;      // 0;
                dst[dst_idx + 1] = g;  // r;
                dst[dst_idx + 2] = r;  // g;
                dst[dst_idx + 3] = 0;  // b;
            } else {
                // 否则，将像素值则使用指定的颜色

                dst[dst_idx] = blue;       // alpha;
                dst[dst_idx + 1] = green;  // red;
                dst[dst_idx + 2] = red;    // green;
                dst[dst_idx + 3] = alpha;  // blue;
            }
        }
    }

    // 将转换后的图像数据复制回 src
    memcpy(src, dst, target_size);
#if 0
    static int cnt =5;
    if(cnt){
        char filename[40];
        snprintf(filename, sizeof(filename), "output_%d_%d_%d.argb", cnt,width,height);
      // 将转换后的图像数据写入文件
      FILE *file = fopen(filename, "wb");
      if (file == NULL) {
          fprintf(stderr, "Failed to open file for writing\n");
          //return;
      }

         // 写入像素数据
    if (fwrite(src, 1, target_size, file) != target_size) {
        fprintf(stderr, "Failed to write pixel data to file\n");
        fclose(file);
       // return;
    }

    cnt--;
    }
#endif

    // 释放临时缓冲区
    free(dst);
}

OsdProcImpl::OsdProcImpl(int method, int is_yuv = 1) : flag(method), isyuv(is_yuv) {
    // create dmabuf pool
    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = MAX_BITMAP_NUM;
    poolCfg.blkSize = MAX_BITMAP_SIZE;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    ES_S32 ret = ES_VB_CreatePool(&poolCfg, &pool);
    if (ES_SUCCESS != ret) {
        printf("%s create pool failed.", __FUNCTION__);
        return;
    }

    for (int i = 0; i < MAX_BITMAP_NUM; i++) {
        ret = ES_VB_GetBlock(pool, MAX_BITMAP_SIZE, ES_NULL, &dmafd[i]);
        if (ret) {
            printf("%s get a block from pool %d failed.", __FUNCTION__, pool);
            return;
        }
        virAddr[i] = NULL;

        //  virAddr[i] = (unsigned char*)ES_SYS_Mmap(dmafd[i], MAX_BITMAP_SIZE, SYS_CACHE_MODE_NOCACHE);
        //   if (NULL == virAddr[i]) {
        //      ES_VB_ReleaseBlock(dmafd[i]);
        //      return;
        //  }
    }

    // dmafd = -1;
    // virAddr = NULL;
    // if ((flag & OSD_DRAW_TEXT) || (flag & OSD_DRAW_SEGMENT))
    // {
    // 	ret = ES_VB_GetBlock(pool, MAX_BUF_SIZE, ES_NULL, &dmafd);
    // 	if (ret) {
    // 		printf("%s get a block from pool %d failed.", __FUNCTION__,
    // pool); 		return;
    // 	}

    // 	virAddr = (unsigned char*)ES_SYS_Mmap( dmafd, MAX_BUF_SIZE,
    // SYS_CACHE_MODE_NOCACHE); 	if (NULL == virAddr) {
    // 		ES_VB_ReleaseBlock(dmafd);
    // 		return;
    // 	}
    // }

    offset = 0;
    mTextnum = 0;
    mRectnum = 0;

    // /ES_VPS_Deinit();
    ret = ES_VPS_Init();
    if (ret != ES_SUCCESS) {
        app_error("ES_VPS_Init failed ret:%#X\n", ret);
        return;
    }
    /*
        VPS_PROPERTY_S prop = {};
        prop.type = VPS_PROPERTY_YUV_COLOR_MODE_SRC;
        prop.u.colorMode = testColorModes[i].colorMode;
        ret = ES_VPS_SetProperty(&prop);
        if (ret) {
            app_error("ES_VPS_SetProperty failed ret\n");
            return;
        }
    */
    osdPerformance = new PerformanceStatic("es_osd_hae", PERF_STATIC_SEGMENT);
}

void OsdProcImpl::loadFontData(string fontFileName, int id) {
    // freetype
    ft.loadFontData(fontFileName, id);
    return;
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

    unsigned char* pColor = (unsigned char*)(&textColor);
    pColor[3] = 255;
    pColor[2] = (uint8_t)color.val[0] * 255;
    pColor[1] = (uint8_t)color.val[1] * 255;
    pColor[0] = (uint8_t)color.val[2] * 255;

    osdPerformance->performanceStaticStart();
    for (int i = 0; i < textnum; i++) {
        string text = (*textvec)[i];
        Rect2f rect = (*rectvec)[i / 2];
        if (i % 2 == 1) {
            //
            rect.x += textMap[i - 1].width / (float)surf_width;
        }
        // freetype

        STR2FRAME::iterator it = str2frame.find(text);
        if (it != str2frame.end()) {
            app_debug("text is already exist in map\n");
            textMap[i] = (VIDEO_FRAME_S)(*(it->second));
            textRect[i].x = (int)(rect.x * surf_width) / 2 * 2;
            textRect[i].y = (int)(rect.y * surf_height) / 2 * 2;
            textRect[i].width = textMap[i].width;
            textRect[i].height = textMap[i].height;
            app_info("rectDst %d %d [%d]:%d %d %d %d\n", surf_width, surf_height, i, textRect[i].x, textRect[i].y,
                     textRect[i].width, textRect[i].height);
            if (textRect[i].width > surf_width - textRect[i].x) {
                textRect[i].x = (surf_width - textRect[i].width) > 0 ? (surf_width - textRect[i].width) : 0;
            }
            if (textRect[i].height > surf_height - textRect[i].y) {
                textRect[i].y = (surf_height - textRect[i].y) > 0 ? (surf_height - textRect[i].y) : 0;
            }
            app_info("rectDst %d %d [%d]:%d %d %d %d\n", surf_width, surf_height, i, textRect[i].x, textRect[i].y,
                     textRect[i].width, textRect[i].height);
        } else {
            app_debug("create a new text map\n");
            int text_width, text_height;
            int baseline = 0;
            Size textSize = ft.getTextSize(text, fontHeight, thickness, &baseline);
            text_width = ((textSize.width - 1) / 16 + 1) * 16;
            text_height = ((fontHeight * 3 / 2 - 1) / 2 + 1) * 2;  // textSize.height * 3 / 2;

            // ARGB
            if (text_width * text_height * 4 > MAX_BITMAP_SIZE) {
                app_error("%s bit map %dx%d is bigger than MAX_BITMAP_SIZE", text.c_str(), text_width, text_height);
                continue;
            }
            unsigned char* pVirAddr = NULL;
            ES_U64 dma_fd = 0;
            for (int m = 0; m < MAX_BITMAP_NUM; m++) {
                if (NULL == virAddr[m]) {
                    virAddr[m] = (unsigned char*)ES_SYS_Mmap(dmafd[m], MAX_BITMAP_SIZE, SYS_CACHE_MODE_NOCACHE);
                    if (NULL == virAddr[m]) {
                        ES_VB_ReleaseBlock(dmafd[m]);
                        app_error("%s ES_VB_ReleaseBlock failed", text.c_str());
                    }
                    // pVirAddr = virAddr[m];  // virAddr + offset;
                    pVirAddr = virAddr[m];
                    dma_fd = dmafd[m];
                    break;
                }
            }
            // unsigned char* pVirAddr = virAddr[i];  // virAddr + offset;
            ES_ASSERT(NULL == pVirAddr, "the 2d OSD :pVirAddr  %p", pVirAddr);
            BitMap dst;
            dst.data = (unsigned char*)pVirAddr;
            dst.width = text_width;
            dst.height = text_height;
            dst.stride = text_width * 3;
            app_debug("data:%p,text_width:%d,text_height:%d,text:%s\n", pVirAddr, text_width, text_height,
                      text.c_str());
            ft.Text2Mask(dst, text, fontHeight, bottomLeftOrigin, FT_RENDER_MODE_LCD);

            transrgb2argb(pVirAddr, text_width, text_height, textColor);

            textRect[i].x = (int)(rect.x * surf_width) / 2 * 2;
            textRect[i].y = (int)(rect.y * surf_height) / 2 * 2;
            textRect[i].width = text_width;
            textRect[i].height = text_height;

            app_info("rectDst %d %d [%d]:%d %d %d %d\n", surf_width, surf_height, i, textRect[i].x, textRect[i].y,
                     textRect[i].width, textRect[i].height);
            VIDEO_FRAME_S* pVideoFrame = (VIDEO_FRAME_S*)malloc(sizeof(VIDEO_FRAME_S));
            memset(pVideoFrame, 0, sizeof(VIDEO_FRAME_S));
            pVideoFrame->width = text_width;
            pVideoFrame->height = text_height;
            pVideoFrame->pixelFormat = PIXEL_FORMAT_B8G8R8A8;
            if (pVideoFrame->pixelFormat == PIXEL_FORMAT_B8G8R8A8) {
                pVideoFrame->stride[0] = text_width * 4;
                pVideoFrame->stride[1] = 0;
                pVideoFrame->stride[2] = 0;
                pVideoFrame->offset[0] = 0;
                // pVideoFrame->offset[1] = offset + pVideoFrame->stride[0] *
                // pVideoFrame->height;
            }
            pVideoFrame->fd = dma_fd;
            // offset += dst.stride * text_height;
            if (textRect[i].width > surf_width - textRect[i].x) {
                textRect[i].width = (surf_width - textRect[i].x) > 0 ? (surf_width - textRect[i].x) : 0;
            }
            if (textRect[i].height > surf_height - textRect[i].y) {
                textRect[i].height = (surf_height - textRect[i].y) > 0 ? (surf_height - textRect[i].y) : 0;
            }
            app_info("rectDst %d %d [%d]:%d %d %d %d\n", surf_width, surf_height, i, textRect[i].x, textRect[i].y,
                     textRect[i].width, textRect[i].height);
            textMap[i] = *pVideoFrame;
            str2frame[text] = pVideoFrame;
        }
    }
    osdPerformance->performanceStaticEnd();
    // rgb2yuv(color);
    return 1;
}

int OsdProcImpl::setrectparam(vector<Rect2f>* rectvec, int rectnum, Color color, int thickness, int line_type,
                              int shift) {
    if (flag & OSD_DRAW_RECTANGLE == 0) {
        return 0;
    }
    if (rectnum > MAX_RECT_NUM) {
        return 0;
    }
    mRectnum = rectnum;
    mThickness = thickness > 0 ? thickness : 4;
    for (int i = 0; i < rectnum; i++) {
        rectRect[i].x = (int)((*rectvec)[i].x * surfWidth) / 2 * 2;
        rectRect[i].y = (int)((*rectvec)[i].y * surfHeight) / 2 * 2;
        rectRect[i].width = (int)((*rectvec)[i].width * surfWidth) / 2 * 2;
        rectRect[i].height = (int)((*rectvec)[i].height * surfHeight) / 2 * 2;
    }

    // rgb2yuv(color);
    unsigned char* pColor = (unsigned char*)(&rectColor);
    pColor[3] = 255;
    pColor[2] = (uint8_t)color.val[0] * 255;
    pColor[1] = (uint8_t)color.val[1] * 255;
    pColor[0] = (uint8_t)color.val[2] * 255;
    return 1;
}

void OsdProcImpl::process(VIDEO_FRAME_S* src, VIDEO_FRAME_S* dst) {
    app_info("%s-%s-%d in pixel %d \n", PLLOG_fileName(__FILE__), __func__, __LINE__, src->pixelFormat);
// rect
#if 0
    if (mRectnum > 0) {
        POINT_S pStart[4 * MAX_OBJ_NUM*4];  // = (POINT_S*)malloc(4*mRectnum*sizeof(POINT_S));
        POINT_S pEnd[4 * MAX_OBJ_NUM*4];  // =
                                // (POINT_S*)malloc(4*mRectnum*sizeof(POINT_S));
      
        for (int n = 0; n < mRectnum; n++) {
            pStart[4 * n] = {rectRect[n].x, rectRect[n].y};
            pStart[4 * n + 1] = {rectRect[n].x + rectRect[n].width, rectRect[n].y};
            pStart[4 * n + 2] = {rectRect[n].x , rectRect[n].y + rectRect[n].height};
            pStart[4 * n + 3] = {rectRect[n].x, rectRect[n].y };
            pEnd[4 * n] = {rectRect[n].x + rectRect[n].width, rectRect[n].y};
            pEnd[4 * n + 1] = {rectRect[n].x + rectRect[n].width, rectRect[n].y + rectRect[n].height};
            pEnd[4 * n + 2] = {rectRect[n].x + rectRect[n].width, rectRect[n].y + rectRect[n].height};
            pEnd[4 * n + 3] = {rectRect[n].x, rectRect[n].y + rectRect[n].height};           

        }
        // code  fix ES_VPS_Line can't handle
#if 0
        for (int mthink = 1; mthink < mThickness / 2; mthink++) {
            for (int j = 0; j < mRectnum; j++) {
                // 处理 pStart
                pStart[4 * mRectnum * mthink + 4 * j] = {rectRect[j].x - mthink, rectRect[j].y - mthink};
                pStart[4 * mRectnum * mthink + 4 * j + 1] = {rectRect[j].x + rectRect[j].width + mthink, rectRect[j].y - mthink};
                pStart[4 * mRectnum * mthink + 4 * j + 2] = {rectRect[j].x - mthink, rectRect[j].y + rectRect[j].height + mthink};
                pStart[4 * mRectnum * mthink + 4 * j + 3] = {rectRect[j].x - mthink, rectRect[j].y - mthink};
        
                // 处理 pEnd
                pEnd[4 * mRectnum * mthink + 4 * j] = {rectRect[j].x + rectRect[j].width + mthink, rectRect[j].y - mthink};
                pEnd[4 * mRectnum * mthink + 4 * j + 1] = {rectRect[j].x + rectRect[j].width + mthink, rectRect[j].y + rectRect[j].height + mthink};
                pEnd[4 * mRectnum * mthink + 4 * j + 2] = {rectRect[j].x + rectRect[j].width + mthink, rectRect[j].y + rectRect[j].height + mthink};
                pEnd[4 * mRectnum * mthink + 4 * j + 3] = {rectRect[j].x - mthink, rectRect[j].y + rectRect[j].height + mthink};
            }
        }
#endif

//#ifdef __RISCV__
         ES_S32 ret = ES_VPS_Line( src, rectColor, pStart, pEnd, mRectnum , HW_TYPE_HAE); 
         if(ret != ES_SUCCESS)
         {
            printf("\n error ret 0x%x\n",ret);
         	app_error("ES_VPS_Line failed\n");
         }
//#endif
        // free(pStart);
        // free(pEnd);
    }
#endif

    if (mRectnum > 0) {
        ES_U32 colour[MAX_OBJ_NUM * 4];
        RECT_S rect[MAX_OBJ_NUM * 4];
        for (int n = 0; n < mRectnum; n++) {
            rect[4 * n].x = (ES_S32)rectRect[n].x;
            rect[4 * n].y = (ES_S32)rectRect[n].y;
            rect[4 * n].width = (ES_U32)rectRect[n].width;
            rect[4 * n].height = mThickness / 2 * 2;  //(ES_U32)rectRect[n].height ;
            if (rect[4 * n].x + rect[4 * n].width > surfWidth) {
                rect[4 * n].x = surfWidth - rect[4 * n].width;
            }
            if (rect[4 * n].y + rect[4 * n].height > surfHeight) {
                rect[4 * n].y = surfHeight - rect[4 * n].height;
            }

            colour[4 * n] = rectColor;

            rect[4 * n + 1].x = (ES_S32)rectRect[n].x + (ES_U32)rectRect[n].width;
            rect[4 * n + 1].y = (ES_S32)rectRect[n].y;
            rect[4 * n + 1].width = mThickness / 2 * 2;
            rect[4 * n + 1].height = (ES_U32)rectRect[n].height;
            if (rect[4 * n + 1].x + rect[4 * n + 1].width > surfWidth) {
                rect[4 * n + 1].x = surfWidth - rect[4 * n + 1].width;
            }
            if (rect[4 * n + 1].y + rect[4 * n + 1].height > surfHeight) {
                rect[4 * n + 1].y = surfHeight - rect[4 * n + 1].height;
            }
            colour[4 * n + 1] = rectColor;

            rect[4 * n + 2].x = (ES_S32)rectRect[n].x;
            rect[4 * n + 2].y = (ES_S32)rectRect[n].y + (ES_U32)rectRect[n].height;
            rect[4 * n + 2].width = (ES_U32)rectRect[n].width;
            rect[4 * n + 2].height = mThickness / 2 * 2;
            if (rect[4 * n + 2].x + rect[4 * n + 2].width > surfWidth) {
                rect[4 * n + 2].x = surfWidth - rect[4 * n + 2].width;
            }
            if (rect[4 * n + 2].y + rect[4 * n + 2].height > surfHeight) {
                rect[4 * n + 2].y = surfHeight - rect[4 * n + 2].height;
            }
            colour[4 * n + 2] = rectColor;

            rect[4 * n + 3].x = (ES_S32)rectRect[n].x;
            rect[4 * n + 3].y = (ES_S32)rectRect[n].y;
            rect[4 * n + 3].width = mThickness / 2 * 2;
            rect[4 * n + 3].height = (ES_U32)rectRect[n].height;
            if (rect[4 * n + 3].x + rect[4 * n + 3].width > surfWidth) {
                rect[4 * n + 3].x = surfWidth - rect[4 * n + 3].width;
            }
            if (rect[4 * n + 3].y + rect[4 * n + 3].height > surfHeight) {
                rect[4 * n + 3].y = surfHeight - rect[4 * n + 3].height;
            }
            colour[4 * n + 3] = rectColor;
        }
        ES_S32 ret = ES_VPS_Fill(src, colour, mRectnum * 4, rect, mRectnum * 4, HW_TYPE_HAE);
        if (ret != ES_SUCCESS) {
            app_error("\n error ret 0x%x\n", ret);
        }
    }

    app_debug("finish draw rect\n");
    // text

    if (mTextnum > 0) {
        RECT_S rectSrc[MAX_OBJ_NUM], rectDst[MAX_OBJ_NUM];
        ROTATION_E rotate[MAX_OBJ_NUM];
        VIDEO_FRAME_S textMap__[MAX_OVERLAY_NUM];

        // rectSrc = (RECT_S *)malloc(mTextnum*sizeof(RECT_S));
        // rectDst = (RECT_S *)malloc(mTextnum*sizeof(RECT_S));
        memset(rectSrc, 0, mTextnum * sizeof(RECT_S));
        memset(rectDst, 0, mTextnum * sizeof(RECT_S));
        // memset(rotate, 0, mTextnum * sizeof(RECT_S));
        int j = 0;
        for (int n = 0; n < mTextnum; n++) {
            if (n % 2 == 1) {
                continue;  // skill score
            }
            if (textRect[n].width == 0 || textRect[n].height == 0) {
                continue;
            }
            rectDst[j].x = textRect[n].x;
            rectDst[j].y = textRect[n].y;
            rectDst[j].width = textRect[n].width;
            rectDst[j].height = textRect[n].height;
            rectSrc[j].x = 0;
            rectSrc[j].y = 0;
            rectSrc[j].width = textMap[n].width;
            rectSrc[j].height = textMap[n].height;
            rotate[j] = ROTATION_0;
            textMap__[j] = textMap[n];
            j++;
            app_info("rectSrc[%d] :%d %d %d %d\n", n, rectSrc[n].x, rectSrc[n].y, rectSrc[n].width, rectSrc[n].height);
            app_info("rectDst[%d]:%d %d %d %d\n", n, rectDst[n].x, rectDst[n].y, rectDst[n].width, rectDst[n].height);
        }
        //#ifdef __RISCV__
        ES_S32 ret = ES_VPS_MultiSourcesBlit(textMap__, j, rectSrc, rotate, rectDst, dst, HW_TYPE_HAE);
        if (ret != ES_SUCCESS) {
            app_error("ES_VPS_MultiSourcesBlit failed 0x %x\n", ret);
        }
        //#endif

        // free(rectSrc);
        // free(rectDst);
    }

#if 0
    if (mTextnum > 0) {
        RECT_S rectSrc[MAX_OBJ_NUM], rectDst[MAX_OBJ_NUM];
        ROTATION_E rotate[MAX_OBJ_NUM];

        VIDEO_FRAME_S textMap__[MAX_OVERLAY_NUM];
        VIDEO_FRAME_S textDst__[MAX_OVERLAY_NUM];

        // rectSrc = (RECT_S *)malloc(mTextnum*sizeof(RECT_S));
        // rectDst = (RECT_S *)malloc(mTextnum*sizeof(RECT_S));
        memset(rectSrc, 0, mTextnum * sizeof(RECT_S));
        memset(rectDst, 0, mTextnum * sizeof(RECT_S));
       // memset(rotate, 0, mTextnum * sizeof(RECT_S));
        int j =0;
        for (int n = 0; n < mTextnum; n++) {
            if(n %2 == 1){
                continue;// skill score
            }
            if( textRect[n].width ==0 || textRect[n].height == 0){
                continue;
            }
            rectDst[j].x = textRect[n].x;
            rectDst[j].y = textRect[n].y;
            rectDst[j].width = textRect[n].width;
            rectDst[j].height = textRect[n].height;
            rectSrc[j].x = 0;
            rectSrc[j].y = 0;
            rectSrc[j].width = textMap[n].width;
            rectSrc[j].height = textMap[n].height;
            rotate[j] = ROTATION_0 ;
            textMap__[j]= textMap[n];
            textDst__[j]= *dst;
            j++;
            app_info("rectSrc[%d] :%d %d %d %d\n", n, rectSrc[n].x, rectSrc[n].y, rectSrc[n].width, rectSrc[n].height);
            app_info("rectDst[%d]:%d %d %d %d\n", n, rectDst[n].x, rectDst[n].y, rectDst[n].width, rectDst[n].height);
        }
        ES_S32 ret = ES_VPS_AlphaBlending(textMap__, textDst__,VPS_BLEND_SRC_OVER_DST, 0,0,HW_TYPE_HAE);
        if (ret != ES_SUCCESS) {
            app_error("ES_VPS_AlphaBlending failed 0x %x\n",ret);
        }
    }
#endif

    app_debug("finish draw text\n");

    app_info("%s-%s-%d out\n", PLLOG_fileName(__FILE__), __func__, __LINE__);
    return;
}

OsdProcImpl::~OsdProcImpl() {
    for (int i = 0; i < MAX_BITMAP_NUM; i++) {
        if (virAddr[i] != NULL) {
            ES_SYS_Munmap(virAddr[i], MAX_BITMAP_SIZE);
            virAddr[i] = NULL;
        }
        if (dmafd[i] != -1) {
            ES_VB_ReleaseBlock(dmafd[i]);
        }
    }
    ES_VB_DestroyPool(pool);
    for (STR2FRAME::iterator it = str2frame.begin(); it != str2frame.end(); ++it) {
        delete it->second;
    }
    str2frame.clear();

    ES_VPS_Deinit();

    osdPerformance->performanceStaticReport();
    delete osdPerformance;
}

OsdProc* createOsdProc(int method, int is_yuv) { return (new OsdProcImpl(method, is_yuv)); }
