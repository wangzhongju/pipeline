#ifndef _ESSDKPL_GLES_H_
#define _ESSDKPL_GLES_H_
extern "C" {
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <assert.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#define INFO printf
#define ERROR printf

#define IMAGE_ATTRIBUTE(name, value, index, arr) \
    arr[index++] = name;                         \
    arr[index++] = value;

#define MOD_HI_FROM_FULL_MOD(modifier) ((uint32_t)((modifier >> 32) & UINT32_MAX))
#define MOD_LO_FROM_FULL_MOD(modifier) ((uint32_t)(modifier & UINT32_MAX))

#define PIXEL_FORMAT (GL_RGB)

GLuint build_shader(const GLchar *shader_source, GLenum type);
int build_program(GLuint &program, int is_yuv = 1);
void vbo_init(GLuint &program);
// void gl_init(int width, int height, EGLDisplay &dpy, EGLSurface &surface,
// EGLContext &context);
void gl_init(EGLDisplay &dpy, EGLSurface &surface, EGLContext &context);
EGLImageKHR create_eglimage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer,
                            const EGLint *attrib_list);
EGLBoolean destroy_eglimage(EGLDisplay dpy, EGLImageKHR image);
void eglimage_attrs_init(int *dmabuf_fds, int *offset, int format, int width, int height, int step,
                         EGLint *ext_image_attrs);
void eglimage2texture(GLenum target, GLeglImageOES image);
void egl_sync(EGLDisplay dpy);
#endif
