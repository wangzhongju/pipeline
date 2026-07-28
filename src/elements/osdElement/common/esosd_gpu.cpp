#define PL_LOG_ID PL_LOG_OSD
#include <cassert>
#include <map>

#include "esosd.h"
#include "freetype.h"
#include "gles.h"
using namespace std;

#define MAX_OVERLAY_BUF 8
#define MAX_BUF_SIZE 3840 * 2160 * 3

typedef map<int, EGLImageKHR> FD2EGLIMGMAP;

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
    // egl
    EGLDisplay dpy;
    EGLSurface surface;
    EGLContext context;
    EGLint ext_image_attrs_in1[50];
    EGLint ext_image_attrs_in2[50];
    EGLint ext_image_attrs_out[50];
    // gl
    GLuint program;
    GLuint background;
    GLuint overlay[MAX_OVERLAY_NUM];
    GLuint out_tex;
    GLuint fbo;

    int isyuv;
    // freetype
    EsFreeType2 ft;

    // dma buffer
    ES_S32 pool;
    ES_U64 dmafd[MAX_OVERLAY_BUF];
    ES_U64* virAddr[MAX_OVERLAY_BUF];

    FD2EGLIMGMAP* mPairSrc;
    FD2EGLIMGMAP* mPairDst;

    PerformanceStatic* glespart1;
    PerformanceStatic* glespart2;
    PerformanceStatic* glespart3;
    PerformanceStatic* glespart4;
    PerformanceStatic* glespart5;
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

OsdProcImpl::OsdProcImpl(int method, int is_yuv = 1)
    : flag(method), isyuv(is_yuv), background(0), out_tex(0), fbo(0), dpy(NULL), surface(NULL), context(NULL) {
    GLenum gl_error_code;
    // gl
    // gl_init(width, height, dpy, surface, context);
    gl_init(dpy, surface, context);
    // shader
    build_program(program, is_yuv);
    glUseProgram(program);
    // vbo
    vbo_init(program);
    // fbo
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_error_code = glGetError();
    assert(gl_error_code == GL_NO_ERROR);
    glClearColor(0.1, 0.1, 0.1, 1.0);
    // glViewport ( 0, 0, width, height );
    // create output texture
    const GLenum attachments[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
                                   GL_COLOR_ATTACHMENT3};
    glGenTextures(1, &out_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, out_tex);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachments[0], GL_TEXTURE_EXTERNAL_OES, out_tex, 0);
    gl_error_code = glGetError();
    assert(gl_error_code == GL_NO_ERROR);
    glDrawBuffers(1, attachments);

    // create input1 texture
    glGenTextures(1, &background);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, background);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_error_code = glGetError();
    assert(gl_error_code == GL_NO_ERROR);
    glUniform1i(glGetUniformLocation(program, "basetexture"), 1);
    // create input2 texture
    int overlayidx[MAX_OVERLAY_NUM];
    for (int i = 0; i < MAX_OVERLAY_NUM; i++) {
        glGenTextures(1, &overlay[i]);
        glActiveTexture(GL_TEXTURE2 + i);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, overlay[i]);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_error_code = glGetError();
        assert(gl_error_code == GL_NO_ERROR);
        overlayidx[i] = i + 2;
    }
    glUniform1iv(glGetUniformLocation(program, "blendtexture"), MAX_OVERLAY_NUM, overlayidx);

    // create dmabuf pool
    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 3;
    poolCfg.blkSize = MAX_BUF_SIZE;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    ES_S32 ret = ES_VB_CreatePool(&poolCfg, &pool);
    if (ES_SUCCESS != ret) {
        printf("%s create pool failed.", __FUNCTION__);
        return;
    }

    for (int i = 0; i < MAX_OVERLAY_BUF; i++) {
        dmafd[i] = -1;
        virAddr[i] = NULL;
    }
    if ((flag & OSD_DRAW_TEXT) || (flag & OSD_DRAW_SEGMENT)) {
        ret = ES_VB_GetBlock(pool, MAX_BUF_SIZE, ES_NULL, &dmafd[0]);
        if (ret) {
            printf("%s get a block from pool %d failed.", __FUNCTION__, pool);
            return;
        }

        virAddr[0] = (ES_U64*)ES_SYS_Mmap(dmafd[0], MAX_BUF_SIZE, SYS_CACHE_MODE_NOCACHE);
        if (NULL == virAddr[0]) {
            ES_VB_ReleaseBlock(dmafd[0]);
            return;
        }
    }

    mPairSrc = new FD2EGLIMGMAP;
    mPairDst = new FD2EGLIMGMAP;

    glespart1 = new PerformanceStatic("glespart1", PERF_STATIC_SEGMENT);
    glespart2 = new PerformanceStatic("glespart2", PERF_STATIC_SEGMENT);
    glespart3 = new PerformanceStatic("glespart3", PERF_STATIC_SEGMENT);
    glespart4 = new PerformanceStatic("glespart4", PERF_STATIC_SEGMENT);
    glespart5 = new PerformanceStatic("glespart5", PERF_STATIC_SEGMENT);
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
    if (textnum > MAX_OVERLAY_NUM) {
        return 0;
    }

    int text_width = 0, text_height = 0;
    int offset = 0;
    int dmabufidx = 0;
    EGLint egl_error_code;

    int offsets[4] = {0};
    int dmabuf_id[4] = {-1, -1, -1, -1};

    static EGLImageKHR egl_overlay_image = EGL_NO_IMAGE_KHR;

    for (int i = 0; i < textnum; i++) {
        string text = (*textvec)[i];
        Rect2f rect = (*rectvec)[i];
        // freetype
        glespart1->performanceStaticStart();
        {
            int baseline = 0;
            Size textSize = ft.getTextSize(text, fontHeight, thickness, &baseline);
            text_width = (textSize.width / 16 + 1) * 16;
            text_height = fontHeight * 3 / 2;
            if (text_width > rect.width * surf_width) {
                text_width = (int)(rect.width * surf_width);
            }
            if (text_height > rect.height * surf_height) {
                text_height = (int)(rect.height * surf_height);
            }

            if (offset + text_height * text_width * 3 / 2 > MAX_BUF_SIZE) {
                offset = 0;
                dmabufidx++;
                if (dmafd[dmabufidx] == -1) {
                    ES_S32 ret = ES_VB_GetBlock(pool, MAX_BUF_SIZE, ES_NULL, &dmafd[dmabufidx]);
                    if (ret) {
                        printf("%s get a block from pool %d failed.", __FUNCTION__, pool);
                        return -1;
                    }

                    virAddr[dmabufidx] = (ES_U64*)ES_SYS_Mmap(dmafd[dmabufidx], MAX_BUF_SIZE, SYS_CACHE_MODE_NOCACHE);
                    if (NULL == virAddr[dmabufidx]) {
                        ES_VB_ReleaseBlock(dmafd[dmabufidx]);
                        return -1;
                    }
                }
            }

            ES_U64* pVirAddr = virAddr[dmabufidx];

            BitMap dst;
            dst.data = (unsigned char*)pVirAddr + offset;
            dst.width = text_width;
            dst.height = text_height;
            dst.stride = text_width;
            ft.Text2Mask(dst, text, fontHeight, bottomLeftOrigin, FT_RENDER_MODE_NORMAL);
        }
        glespart1->performanceStaticEnd();

        // eglimage
        egl_overlay_image = EGL_NO_IMAGE_KHR;
        if (egl_overlay_image != EGL_NO_IMAGE_KHR) {
            destroy_eglimage(dpy, egl_overlay_image);
        }

        dmabuf_id[0] = dmafd[dmabufidx];
        dmabuf_id[1] = dmafd[dmabufidx];
        offsets[0] = offset;
        offsets[1] = offset + text_width * text_height / 2;
        eglimage_attrs_init(dmabuf_id, offsets, DRM_FORMAT_NV12, text_width, text_height, text_width,
                            ext_image_attrs_in2);
        glespart2->performanceStaticStart();
        egl_overlay_image = create_eglimage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ext_image_attrs_in2);
        glespart2->performanceStaticEnd();
        assert(egl_overlay_image != EGL_NO_IMAGE_KHR);

        /* Create an external texture backed by egl image */
        glespart3->performanceStaticStart();
        glActiveTexture(GL_TEXTURE2 + i);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, overlay[i]);
        eglimage2texture(GL_TEXTURE_EXTERNAL_OES, egl_overlay_image);
        glespart3->performanceStaticEnd();
        egl_error_code = eglGetError();
        assert(egl_error_code == EGL_SUCCESS);

        offset += text_width * text_height * 3 / 2;
    }

    for (int i = textnum; i < MAX_OVERLAY_NUM; i++) {
        // eglimage
        // static EGLImageKHR egl_overlay_image = EGL_NO_IMAGE_KHR;
        // if(egl_overlay_image != EGL_NO_IMAGE_KHR)
        // {
        // 	destroy_eglimage(dpy, egl_overlay_image);
        // }
        // egl_overlay_image = create_eglimage(dpy, EGL_NO_CONTEXT,
        // EGL_LINUX_DMA_BUF_EXT, NULL, ext_image_attrs_in2); assert(
        // egl_overlay_image != EGL_NO_IMAGE_KHR );

        /* Create an external texture backed by egl image */
        glActiveTexture(GL_TEXTURE2 + i);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, overlay[i]);
        eglimage2texture(GL_TEXTURE_EXTERNAL_OES, egl_overlay_image);
        egl_error_code = eglGetError();
        assert(egl_error_code == EGL_SUCCESS);
    }

    float* pos = (float*)malloc(textnum * 4 * sizeof(float));
    for (int i = 0; i < textnum; i++) {
        Rect2f rect = (*rectvec)[i];

        pos[4 * i] = rect.x;
        pos[4 * i + 1] = rect.y;
        pos[4 * i + 2] = rect.x + (float)text_width / surf_width;
        pos[4 * i + 3] = rect.y + (float)text_height / surf_height;
    }
    glespart4->performanceStaticStart();
    glUniform4fv(glGetUniformLocation(program, "blendposition"), textnum, pos);
    free(pos);
    if (isyuv == 1) {
        rgb2yuv(color);
    }
    glUniform3f(glGetUniformLocation(program, "blendcolor"), color.val[0], color.val[1], color.val[2]);
    glUniform1i(glGetUniformLocation(program, "blendnum"), textnum);
    glespart4->performanceStaticEnd();
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

    float* pos = (float*)malloc(rectnum * 4 * sizeof(float));

    for (int i = 0; i < rectnum; i++) {
        Rect2f rect = (*rectvec)[i];

        pos[4 * i] = rect.x;
        pos[4 * i + 1] = rect.y;
        pos[4 * i + 2] = rect.x + rect.width;
        pos[4 * i + 3] = rect.y + rect.height;
    }
    glUniform4fv(glGetUniformLocation(program, "rectposition"), rectnum, pos);
    free(pos);

    if (isyuv == 1) {
        rgb2yuv(color);
    }
    glUniform3f(glGetUniformLocation(program, "rectcolor"), color.val[0], color.val[1], color.val[2]);
    glUniform1i(glGetUniformLocation(program, "rectnum"), rectnum);
    return 1;
}

void OsdProcImpl::process(VIDEO_FRAME_S* src, VIDEO_FRAME_S* dst) {
    int offsets[4] = {0};
    int dmabuf_id[4] = {-1, -1, -1, -1};
    static EGLImageKHR egl_input_image = EGL_NO_IMAGE_KHR;
    static EGLImageKHR egl_output_image = EGL_NO_IMAGE_KHR;
    EGLint egl_error_code;

    glViewport(0, 0, src->width, src->height);

    // src frame
    // FD2EGLIMGMAP::iterator it =
    // mPairSrc->find(ES_SYS_Internal_GetFd(src->fd)); if(it != mPairSrc->end())
    // {
    // 	egl_input_image = it->second;
    // }
    // else
    {
        dmabuf_id[0] = ES_SYS_Internal_GetFd(src->fd);
        dmabuf_id[1] = ES_SYS_Internal_GetFd(src->fd);
        offsets[0] = 0;
        offsets[1] = src->stride[0] * src->height;
        eglimage_attrs_init(dmabuf_id, offsets, DRM_FORMAT_NV12, src->width, src->height, src->width,
                            ext_image_attrs_in1);
        glespart2->performanceStaticStart();
        egl_input_image = create_eglimage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ext_image_attrs_in1);
        glespart2->performanceStaticEnd();
        assert(egl_input_image != EGL_NO_IMAGE_KHR);
        //(*mPairSrc)[dmabuf_id[0]] = egl_input_image;
    }

    // dst frame
    // it = mPairDst->find(ES_SYS_Internal_GetFd(dst->fd));
    // if(it != mPairDst->end())
    // {
    // 	egl_output_image = it->second;
    // }
    // else
    {
        dmabuf_id[0] = ES_SYS_Internal_GetFd(dst->fd);
        dmabuf_id[1] = ES_SYS_Internal_GetFd(dst->fd);
        offsets[0] = 0;
        offsets[1] = dst->stride[0] * dst->height;
        eglimage_attrs_init(dmabuf_id, offsets, DRM_FORMAT_NV12, dst->width, dst->height, dst->width,
                            ext_image_attrs_out);
        glespart2->performanceStaticStart();
        egl_output_image = create_eglimage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ext_image_attrs_out);
        glespart2->performanceStaticEnd();
        assert(egl_output_image != EGL_NO_IMAGE_KHR);
        //(*mPairDst)[dmabuf_id[0]] = egl_output_image;
    }

    /* Create an external texture backed by output egl image */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, out_tex);
    eglimage2texture(GL_TEXTURE_EXTERNAL_OES, egl_output_image);
    egl_error_code = eglGetError();
    assert(egl_error_code == EGL_SUCCESS);
    assert(GL_FRAMEBUFFER_COMPLETE == glCheckFramebufferStatus(GL_FRAMEBUFFER));

    /* Create an external texture backed by input egl image */
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, background);
    eglimage2texture(GL_TEXTURE_EXTERNAL_OES, egl_input_image);
    egl_error_code = eglGetError();
    assert(egl_error_code == EGL_SUCCESS);

    glespart5->performanceStaticStart();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glUseProgram(program);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish();
    glespart5->performanceStaticEnd();
    destroy_eglimage(dpy, egl_input_image);
    destroy_eglimage(dpy, egl_output_image);
}

OsdProcImpl::~OsdProcImpl() {
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &out_tex);
    for (int i = 0; i < MAX_OVERLAY_NUM; i++) {
        glDeleteTextures(1, &overlay[i]);
    }
    glDeleteTextures(1, &background);
    eglDestroyContext(dpy, context);
    eglDestroySurface(dpy, surface);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    for (int i = 0; i < MAX_OVERLAY_BUF; i++) {
        if (virAddr[i] != NULL) {
            ES_SYS_Munmap(virAddr[i], MAX_BUF_SIZE);
        }
        if (dmafd[i] != -1) {
            ES_VB_ReleaseBlock(dmafd[i]);
        }
    }
    ES_VB_DestroyPool(pool);

    FD2EGLIMGMAP::iterator it;
    for (it = mPairSrc->begin(); it != mPairSrc->end(); it++) {
        destroy_eglimage(dpy, it->second);
    }
    mPairSrc->erase(mPairSrc->begin(), mPairSrc->end());
    delete mPairSrc;
    mPairSrc = nullptr;

    for (it = mPairDst->begin(); it != mPairDst->end(); it++) {
        destroy_eglimage(dpy, it->second);
    }
    mPairDst->erase(mPairDst->begin(), mPairDst->end());
    delete mPairDst;
    mPairDst = nullptr;
}

OsdProc* createOsdProc(int method, int is_yuv) { return (new OsdProcImpl(method, is_yuv)); }
