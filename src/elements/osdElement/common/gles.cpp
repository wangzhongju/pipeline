#define PL_LOG_ID PL_LOG_OSD
#include "gles.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FBOSURFACE
#ifndef FBOSURFACE
#define PBUFFERSURFACE
#ifndef PBUFFERSURFACE
#define WINDOWSURFACE
#endif
#endif

PFNEGLCREATEIMAGEKHRPROC pfneglCreateImageKHR = NULL;
PFNEGLDESTROYIMAGEKHRPROC pfneglDestroyImageKHR = NULL;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pfnglEGLImageTargetTexture2DOES = NULL;
PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC
pfnglEGLImageTargetRenderbufferStorageOES = NULL;
PFNEGLQUERYDMABUFFORMATSEXTPROC pfneglQueryDmaBufFormatsEXT = NULL;
PFNEGLQUERYDMABUFMODIFIERSEXTPROC pfneglQueryDmaBufModifiersEXT = NULL;
PFNEGLCREATESYNCKHRPROC pfneglCreateSyncKHR = NULL;
PFNEGLDESTROYSYNCKHRPROC pfneglDestroySyncKHR = NULL;
PFNEGLCLIENTWAITSYNCKHRPROC pfneglClientWaitSyncKHR = NULL;

void egl_sync(EGLDisplay dpy) {
    EGLSyncKHR sync = pfneglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, NULL);
    pfneglClientWaitSyncKHR(dpy, sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 3000000000);
    pfneglDestroySyncKHR(dpy, sync);
}

const GLchar *v_shader_source =
    "#version 300 es\n"
    "out highp vec2 texcoord;\n"
    "in highp vec4 position;\n"
    "in highp vec2 inputtexcoord;\n"
    "void main(void)\n"
    "{\n"
    "	texcoord = inputtexcoord;\n"
    "	gl_Position = position;\n"
    "}\n";

const GLchar *f_shader_source_yuv =
    "#version 300 es\n"
    "#extension GL_EXT_YUV_target : enable\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "in highp vec2 texcoord;\n"
    "layout(yuv) out highp vec3 myFragColor;\n"
    "uniform highp __samplerExternal2DY2YEXT basetexture;\n"
    "uniform highp __samplerExternal2DY2YEXT blendtexture[8];\n"
    "uniform highp vec4 rectposition[8];\n"
    "uniform highp vec4 blendposition[8];\n"
    "uniform highp vec3 rectcolor;\n"
    "uniform highp vec3 blendcolor;\n"
    "uniform highp int rectnum;\n"
    "uniform highp int blendnum;\n"
    "bool inRect(float,float,float,float);\n"
    "vec4 getValueFromSamplerArray(int ndx, vec2 uv) {\n"
    "	 if (ndx == 0) {\n"
    "	  return texture(blendtexture[0], uv);\n"
    "	 } else if (ndx == 1) {\n"
    "	  return texture(blendtexture[1], uv);\n"
    "	 } else if (ndx == 2) {\n"
    "	  return texture(blendtexture[2], uv);\n"
    "	 } else if (ndx == 3) {\n"
    "	  return texture(blendtexture[3], uv);\n"
    "	 } else if (ndx == 4) {\n"
    "	  return texture(blendtexture[4], uv);\n"
    "	 } else if (ndx == 5) {\n"
    "	  return texture(blendtexture[5], uv);\n"
    "	 } else if (ndx == 6) {\n"
    "	  return texture(blendtexture[6], uv);\n"
    "	 } else {\n"
    "	  return texture(blendtexture[7], uv);\n"
    "	 }\n"
    "}\n"
    "void main(void)\n"
    "{\n"
    "	highp vec4 color = texture(basetexture, texcoord);\n"
    "	highp vec4 mask = vec4(0.0,0.0,0.0,0.0);\n"
    "	for(int i = 0; i < blendnum; i++){\n"
    "   	"
    "if(inRect(blendposition[i].x,blendposition[i].z,blendposition[i].y,"
    "blendposition[i].w))\n"
    "		{\n"
    "			highp vec2 tmp = "
    "vec2((texcoord.x-blendposition[i].x)/"
    "(blendposition[i].z-blendposition[i].x), "
    "(texcoord.y-blendposition[i].y)/"
    "(blendposition[i].w-blendposition[i].y));\n"
    "			mask = getValueFromSamplerArray(i,tmp);\n"
    //	"			mask = texture(blendtexture[0], tmp);\n"
    "			mask.a = mask.x;\n"
    "			mask.xyz = "
    "vec3(blendcolor.x,blendcolor.y,blendcolor.z);\n"
    "			break;\n"
    "		}\n"
    "	}\n"
    "	for(int i = 0; i < rectnum; i++){\n"
    "		"
    "if(inRect(rectposition[i].x,rectposition[i].z,rectposition[i].y,"
    "rectposition[i].w))\n"
    "		{\n"
    "			"
    "if(!inRect(rectposition[i].x+0.005,rectposition[i].z-0.005,rectposition[i]"
    ".y+0.005,rectposition[i].w-0.005))\n"
    "			{\n"
    "				color = "
    "vec4(rectcolor.x,rectcolor.y,rectcolor.z,1.0);\n"
    "				break;\n"
    "			}\n"
    "		}\n"
    "	}\n"
    "	highp vec4 myFragColorYUV = mix(color,mask,mask.a);\n"
    "	myFragColor = vec3(myFragColorYUV.x, myFragColorYUV.y, "
    "myFragColorYUV.z);\n"
    "}\n"
    "bool inRect(float x1,float x2, float y1, float y2){\n"
    "	if(texcoord.x<x1 || texcoord.x>x2 || texcoord.y<y1 || texcoord.y>y2)\n"
    "	{ return false; } else { return true; }\n"
    "}\n";

const GLchar *f_shader_source_rgb = "";

static void handle_egl_error(char *name) {
    EGLint error_code = eglGetError();
    ERROR("'%s' returned egl error  (0x%x)\n", name, error_code);
    exit(1);
}

static bool has_extension(const char *const extensions_list, const char *const extension_searched) {
    const char *extension = extensions_list;
    const size_t extension_searched_length = strlen(extension_searched);

    if (!extension) {
        return false;
    }

    if (!extension_searched_length) {
        return true;
    }

    while (true) {
        const size_t extension_length = strcspn(extension, " ");

        if (extension_length == extension_searched_length &&
            strncmp(extension, extension_searched, extension_length) == 0) {
            return true;
        }

        extension += extension_length;

        if (*extension == '\0') {
            return false;
        }

        extension += 1;
    }
}

static bool get_extension_funcs(EGLDisplay display) {
    const char *eglexts = eglQueryString(display, EGL_EXTENSIONS);
    const char *glexts = (const char *)glGetString(GL_EXTENSIONS);

    if (!has_extension(eglexts, "EGL_KHR_image_base")) {
        ERROR("No EGL_KHR_image_base extension\n");
        return false;
    }

    pfneglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    if (pfneglCreateImageKHR == NULL) {
        ERROR("eglGetProcAddress failed for eglCreateImageKHR\n");
        return false;
    }

    pfneglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (pfneglDestroyImageKHR == NULL) {
        ERROR("eglGetProcAddress failed for eglDestroyImageKHR\n");
        return false;
    }

    if (!has_extension(eglexts, "EGL_EXT_image_dma_buf_import_modifiers")) {
        ERROR("No EGL_KHR_image_base extension\n");
        return false;
    }

    pfneglQueryDmaBufFormatsEXT = (PFNEGLQUERYDMABUFFORMATSEXTPROC)eglGetProcAddress("eglQueryDmaBufFormatsEXT");
    if (!pfneglQueryDmaBufFormatsEXT) {
        ERROR("eglGetProcAddress failed for eglQueryDmaBufFormatsEXT\n");
        return false;
    }

    pfneglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
    if (!pfneglQueryDmaBufModifiersEXT) {
        ERROR("eglGetProcAddress failed for eglQueryDmaBufModifiersEXT\n");
        return false;
    }

    if (!has_extension(glexts, "GL_OES_EGL_image")) {
        ERROR("No GL_OES_EGL_image extension\n");
        return false;
    }

    pfnglEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (pfnglEGLImageTargetTexture2DOES == NULL) {
        ERROR("eglGetProcAddress failed for glEGLImageTargetTexture2DOES\n");
        return false;
    }

    if (!has_extension(glexts, "GL_OES_EGL_image_external")) {
        ERROR("No GL_OES_EGL_image_external extension\n");
        return false;
    }

    pfnglEGLImageTargetRenderbufferStorageOES =
        (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
    if (pfnglEGLImageTargetRenderbufferStorageOES == NULL) {
        ERROR(
            "eglGetProcAddress failed for "
            "glEGLImageTargetRenderbufferStorageOES\n");
        return false;
    }

    if (!has_extension(glexts, "GL_EXT_YUV_target")) {
        ERROR("No GL_EXT_YUV_target extension\n");
        return false;
    }

    pfneglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    if (pfneglCreateSyncKHR == NULL) {
        ERROR("eglGetProcAddress failed for eglCreateSyncKHR\n");
        return false;
    }

    pfneglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    if (pfneglDestroySyncKHR == NULL) {
        ERROR("eglGetProcAddress failed for eglDestroySyncKHR\n");
        return false;
    }

    pfneglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
    if (pfneglClientWaitSyncKHR == NULL) {
        ERROR("eglGetProcAddress failed for eglClientWaitSyncKHR\n");
        return false;
    }

    return true;
}

GLuint build_shader(const GLchar *shader_source, GLenum type) {
    GLuint shader = glCreateShader(type);
    if (!shader || !glIsShader(shader)) {
        return 0;
    }
    int len = strlen(shader_source);
    glShaderSource(shader, 1, &shader_source, &len);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char aszInfoLog[1024];
        ERROR("Error: Failed to compile GLSL shader\n");
        glGetShaderInfoLog(shader, 1024, &len, aszInfoLog);
        INFO("%s", aszInfoLog);
        return GL_FALSE;
    }

    return (status == GL_TRUE ? shader : 0);
}

int build_program(GLuint &program, int is_yuv) {
    GLuint v_shader, f_shader;

    v_shader = build_shader(v_shader_source, GL_VERTEX_SHADER);

    char *transition_source;

    if (is_yuv == 1) {
        transition_source = (char *)f_shader_source_yuv;
        INFO("%s\n", transition_source);
    } else {
        transition_source = (char *)f_shader_source_rgb;
        // sprintf(transition_source,f_shader_source_rgb);
    }

    GLchar *f_shader_source = (GLchar *)transition_source;
    if (!f_shader_source) {
        return -1;
    }

    f_shader = build_shader(f_shader_source, GL_FRAGMENT_SHADER);

    program = glCreateProgram();
    glAttachShader(program, v_shader);
    glAttachShader(program, f_shader);
    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    return status == GL_TRUE ? 0 : -1;
}

#ifdef PBUFFERSURFACE
void gl_init(int width, int height, EGLDisplay &dpy, EGLSurface &surface, EGLContext &context)
#else
void gl_init(EGLDisplay &dpy, EGLSurface &surface, EGLContext &context)
#endif
{
    EGLConfig configs[2];
    EGLBoolean eRetStatus;
    EGLint major, minor;
    EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 0, EGL_NONE};
    EGLint config_count;
    EGLint cfg_attribs[] = {EGL_BUFFER_SIZE,
                            EGL_DONT_CARE,
                            EGL_DEPTH_SIZE,
                            24,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES3_BIT_KHR,
                            EGL_NONE};
    EGLNativeDisplayType eglDisplay = EGL_DEFAULT_DISPLAY;

    dpy = eglGetDisplay(eglDisplay);

    eRetStatus = eglInitialize(dpy, &major, &minor);
    if (eRetStatus != EGL_TRUE) handle_egl_error((char *)"eglInitialize");

    eRetStatus = eglChooseConfig(dpy, cfg_attribs, configs, 2, &config_count);
    if (!eRetStatus) {
        handle_egl_error((char *)"eglChooseConfig");
    } else if (!config_count) {
        ERROR(
            "eglChooseConfig: no matching configs were returned by EGL "
            "(exiting).\n");
        exit(1);
    }

#ifdef WINDOWSURFACE
    EGLNativeWindowType eglWindow = 0;
    surface = eglCreateWindowSurface(dpy, configs[0], eglWindow, NULL);
    if (surface == EGL_NO_SURFACE) {
        handle_egl_error((char *)"eglCreateWindowSurface");
    }
#else
#ifdef PBUFFERSURFACE
    EGLint pbuf[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    surface = eglCreatePbufferSurface(dpy, configs[0], pbuf);
    if (surface == EGL_NO_SURFACE) {
        handle_egl_error((char *)"eglCreatePbufferSurface");
    }
#else
    surface = NULL;
#endif
#endif

    eRetStatus = eglBindAPI(EGL_OPENGL_ES_API);
    if (eRetStatus != EGL_TRUE) {
        handle_egl_error((char *)"eglBindAPI");
    }

    context = eglCreateContext(dpy, configs[0], EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        handle_egl_error((char *)"eglCreateContext");
    }

    eRetStatus = eglMakeCurrent(dpy, surface, surface, context);
    if (eRetStatus != EGL_TRUE) handle_egl_error((char *)"eglMakeCurrent");

    if (!get_extension_funcs(dpy)) {
        ERROR("get_extension_funcs: can't get all extension funcs (exiting).\n");
        exit(1);
    }
}

void vbo_init(GLuint &program) {
    int nShaderStatus, nInfoLogLength;
    char aszInfoLog[1024];
    static GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
    };

    static GLfloat colors[] = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    };

    static GLfloat texcoord[] = {
        0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0,
    };

    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "inputcolor");
    glBindAttribLocation(program, 2, "inputtexcoord");

    /* Link the program */
    glLinkProgram(program);
    /* Check it linked OK */
    glGetProgramiv(program, GL_LINK_STATUS, &nShaderStatus);
    if (nShaderStatus != GL_TRUE) {
        ERROR("Error: Failed to link GLSL program\n");
        glGetProgramInfoLog(program, 1024, &nInfoLogLength, aszInfoLog);
        INFO("%s", aszInfoLog);
        return;
    }

    glValidateProgram(program);
    glGetProgramiv(program, GL_VALIDATE_STATUS, &nShaderStatus);
    if (nShaderStatus != GL_TRUE) {
        ERROR("Error: Failed to validate GLSL program\n");
        glGetProgramInfoLog(program, 1024, &nInfoLogLength, aszInfoLog);
        INFO("%s", aszInfoLog);
        return;
    }
    glUseProgram(program);
    /* Enable vertex attribute arrays - disabled by default */
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    /* Associate vertex data with the shader attributes */
    glVertexAttribPointer(0, 4, GL_FLOAT, 0, 0, vertices);
    glVertexAttribPointer(1, 4, GL_FLOAT, 0, 0, colors);
    glVertexAttribPointer(2, 4, GL_FLOAT, 0, 0, texcoord);
}

EGLImageKHR create_eglimage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer,
                            const EGLint *attrib_list) {
    return pfneglCreateImageKHR(dpy, ctx, target, buffer, attrib_list);
}

EGLBoolean destroy_eglimage(EGLDisplay dpy, EGLImageKHR image) { return pfneglDestroyImageKHR(dpy, image); }

void eglimage_attrs_init(int *dmabuf_fds, int *offset, int format, int width, int height, int step,
                         EGLint *ext_image_attrs) {
    if (format == DRM_FORMAT_NV12 || format == DRM_FORMAT_YUV420) {
        uint32_t plane_count = 2;
        int stride[3];
        stride[0] = step;
        stride[1] = step;
        if (format == DRM_FORMAT_YUV420) {
            plane_count = 3;
            stride[1] = step / 2;
            stride[2] = step / 2;
        }
        unsigned index = 0;

        IMAGE_ATTRIBUTE(EGL_WIDTH, width, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_HEIGHT, height, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_LINUX_DRM_FOURCC_EXT, format, index, ext_image_attrs);

        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_FD_EXT, dmabuf_fds[0], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset[0], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_PITCH_EXT, stride[0], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, MOD_HI_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, MOD_LO_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);

        IMAGE_ATTRIBUTE(EGL_YUV_COLOR_SPACE_HINT_EXT, EGL_ITU_REC601_EXT, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_SAMPLE_RANGE_HINT_EXT, EGL_YUV_NARROW_RANGE_EXT, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_YUV_CHROMA_HORIZONTAL_SITING_HINT_EXT, EGL_YUV_CHROMA_SITING_0_EXT, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_YUV_CHROMA_VERTICAL_SITING_HINT_EXT, EGL_YUV_CHROMA_SITING_0_EXT, index, ext_image_attrs);

        /* Only one plane - set the "end of array" mark and return */
        if (plane_count < 2) {
            ext_image_attrs[index] = EGL_NONE;
            return;
        }

        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE1_FD_EXT, dmabuf_fds[1], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE1_OFFSET_EXT, offset[1], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE1_PITCH_EXT, stride[1], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, MOD_HI_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, MOD_LO_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);

        if (plane_count < 3) {
            ext_image_attrs[index] = EGL_NONE;
            return;
        }

        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE2_FD_EXT, dmabuf_fds[2], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE2_OFFSET_EXT, offset[2], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE2_PITCH_EXT, stride[2], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, MOD_HI_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, MOD_LO_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);
        /* Set the "end of array" mark */
        ext_image_attrs[index] = EGL_NONE;
    } else if (format == DRM_FORMAT_ARGB8888) {
        unsigned index = 0;

        IMAGE_ATTRIBUTE(EGL_WIDTH, width, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_HEIGHT, height, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_LINUX_DRM_FOURCC_EXT, format, index, ext_image_attrs);

        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_FD_EXT, dmabuf_fds[0], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset[0], index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_PITCH_EXT, step, index, ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, MOD_HI_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);
        IMAGE_ATTRIBUTE(EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, MOD_LO_FROM_FULL_MOD(DRM_FORMAT_MOD_LINEAR), index,
                        ext_image_attrs);

        /* Set the "end of array" mark */
        ext_image_attrs[index] = EGL_NONE;
    }
}

void eglimage2texture(GLenum target, GLeglImageOES image) {
    pfnglEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
}
