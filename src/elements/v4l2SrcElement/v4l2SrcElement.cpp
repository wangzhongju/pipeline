#define PL_LOG_ID PL_LOG_V4L2
#include "v4l2SrcElement.h"

#include <ctype.h>  // 用于字符可打印性检查
#include <stdio.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <cassert>
#include <random>

#include "es_sys.h"
#include "es_vb_memory.h"

/**
 * @brief 打印指定内存地址中的字符
 * @param base_addr 基础内存地址 (即 buf.m.userptr)
 */
void print_address_characters(unsigned long base_addr) {
    // 将地址转为 char* 指针便于逐字节访问
    char *ptr = (char *)base_addr;

    // 定义需要打印的偏移量数组
    int offsets[] = {0, 1, 2, 100, 101, 102, 1000, 1001, 1002};
    int offset_count = sizeof(offsets) / sizeof(offsets[0]);

    printf("Address    Offset  Char (Hex)\n");
    printf("=============================\n");

    for (int i = 0; i < offset_count; i++) {
        int offset = offsets[i];
        char current_char = *(ptr + offset);

        // 检查字符是否可打印（ASCII 32-126）
        char display_char =
            isprint((unsigned char)current_char) ? current_char : '.';  // 不可打印字符用'.'代替[7](@ref)

        // 打印地址、偏移量和字符信息
        printf("%-10p %-7d %c (0x%02X)\n", (ptr + offset), offset, display_char, (unsigned char)current_char);
    }
}
void CImageV4l2::release() {
    free(mPic);
    mPic = nullptr;
    if (pool) {
        pool->deallocate((CImageV4l2 *)this);
    } else {
        delete this;
    }
}

bool dumpFile(std::string fileName, char *buf, uint64_t bufSize) {
    FILE *saveFile = fopen(fileName.c_str(), "wb");
    if (NULL == saveFile) {
        app_debug("%s-%d: %s \n", __func__, __LINE__, "fopen error");
        return false;
    }

    int count = fwrite(buf, sizeof(char), bufSize, saveFile);
    fclose(saveFile);

    if (count != bufSize) {
        return false;
    }

    return true;
};

// 辅助函数：将像素格式FourCC码转为可读字符串
std::string fourccToString(uint32_t fourcc) {
    // 按字节转换为字符表示
    char buf[5] = {0};
    buf[0] = (fourcc & 0xff);
    buf[1] = (fourcc >> 8) & 0xff;
    buf[2] = (fourcc >> 16) & 0xff;
    buf[3] = (fourcc >> 24) & 0xff;

    // 检查是否为可打印字符
    bool printable = true;
    for (int i = 0; i < 4; i++) {
        if (!isprint(static_cast<unsigned char>(buf[i]))) {
            printable = false;
            break;
        }
    }

    // 返回适当的字符串表示
    if (printable) {
        return std::string(buf);
    }
    // 处理常见的非打印格式
    else if (fourcc == V4L2_PIX_FMT_YUYV)
        return "YUYV/422";
    else if (fourcc == V4L2_PIX_FMT_MJPEG)
        return "MJPEG";
    else if (fourcc == V4L2_PIX_FMT_H264)
        return "H264";
    else if (fourcc == V4L2_PIX_FMT_YUV420)
        return "YUV420";
    else if (fourcc == V4L2_PIX_FMT_NV12)
        return "NV12";
    else {
        app_debug("not support type fourcc:%d \n", fourcc);  // 单行打印语句
        assert("not support type");

        return NULL;
    }
}

VIDEO_FRAME_INFO_S *createVideoFrame(ES_U32 count, ES_U32 width = 1280, ES_U32 height = 720,
                                     PIXEL_FORMAT_E srcParam_pixelFormat = PIXEL_FORMAT_NV12, ES_U32 fps = 30) {
    // ES_BOOL bBitWidth8 = ES_TRUE;
    // PIXEL_FORMAT_E srcParam_pixelFormat = PIXEL_FORMAT_NV12;
    VIDEO_FRAME_INFO_S *videoFrameInfo = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
    memset(videoFrameInfo, 0, sizeof(VIDEO_FRAME_INFO_S));
    videoFrameInfo->videoFrame.fd = 0;  // sourceData.fd;
    // videoFrameInfo->videoFrame.virAddr[0] = 0;
    // //(ES_U64)(ES_UL)sourceData.pVirAddr;
    videoFrameInfo->poolId = 0;  // sourceData.vbPoolId;//
    videoFrameInfo->videoFrame.width = width;
    videoFrameInfo->videoFrame.height = height;

    switch (srcParam_pixelFormat) {
        case PIXEL_FORMAT_NV12: {
            videoFrameInfo->videoFrame.stride[0] = width;
            videoFrameInfo->videoFrame.stride[1] = width;
            videoFrameInfo->videoFrame.stride[2] = width;
            videoFrameInfo->videoFrame.offset[0] = 0;
            videoFrameInfo->videoFrame.offset[1] = width * height;
            videoFrameInfo->videoFrame.offset[2] = width * height + ((width * height) >> 2);

            videoFrameInfo->videoFrame.pixelFormat = PIXEL_FORMAT_NV12;
            videoFrameInfo->videoFrame.field = VIDEO_FIELD_FRAME;
            // videoFrameInfo->videoFrame.compressMode = COMPRESS_MODE_NONE;
            // videoFrameInfo->videoFrame.videoFormat = VIDEO_FORMAT_LINEAR;

            videoFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;  // ignore
            videoFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;     // ignore

            videoFrameInfo->videoFrame.PTS = count * (1000000 / fps);
            // 1000000us
            //    videoFrameInfo->videoFrame.timeRef = count * 2;
            break;
        }
        case PIXEL_FORMAT_YUY2: {
            // YUY2 格式特性: 打包格式(Packed), 单平面
            // 计算跨度和偏移
            int pitch = width * 2;  // YUY2 每像素占用 2 字节
            // 设置平面参数
            videoFrameInfo->videoFrame.stride[0] = pitch;  // 主平面跨度为 width * 2
            videoFrameInfo->videoFrame.stride[1] = 0;      // 没有第二个平面
            videoFrameInfo->videoFrame.stride[2] = 0;      // 没有第三个平面

            videoFrameInfo->videoFrame.offset[0] = 0;  // 数据从缓冲区起始位置开始
            videoFrameInfo->videoFrame.offset[1] = 0;  // 无第二平面
            videoFrameInfo->videoFrame.offset[2] = 0;  // 无第三平面

            // 设置帧格式属性
            videoFrameInfo->videoFrame.pixelFormat = PIXEL_FORMAT_YUY2;
            videoFrameInfo->videoFrame.field = VIDEO_FIELD_FRAME;

            // 色域属性设置
            videoFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;
            videoFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;

            // 时间标记处理
            videoFrameInfo->videoFrame.PTS = count * (1000000 / fps);

            break;
        }
        default:
            app_error("Unsupported pixel format: %d \n", srcParam_pixelFormat);
            break;
    }
    return videoFrameInfo;
}

CFrameMeta *transV4l2ToVideoFrame(V4l2InputDataInfo &v4l2InputDataInfo, uint padIndex, int frameIndex,
                                  FrameBuffer &fbuf, std::string m_VBName, ES_U32 fps = 30) {
    ES_U32 width = v4l2InputDataInfo.width;
    ES_U32 height = v4l2InputDataInfo.height;

    VIDEO_FRAME_INFO_S *videoFrameInfo =
        createVideoFrame(frameIndex, width, height, v4l2InputDataInfo.pixelFormat, fps);  // count -> pts
    ES_U32 size = 0;
    switch (v4l2InputDataInfo.pixelFormat) {
        case PIXEL_FORMAT_NV12:
            size =
                (videoFrameInfo->videoFrame.stride[0] * height + videoFrameInfo->videoFrame.stride[1] * (height >> 1));
            break;
        case PIXEL_FORMAT_YUY2:
            size = (videoFrameInfo->videoFrame.stride[0] * height + videoFrameInfo->videoFrame.stride[1] * height);
            break;
    }
    assert(size > 0 && "videoframe size illagel!");
    assert(size <= fbuf.length && "videoframe fd buffer size should <= v4l2 frame buffer size");

    ES_S32 ret =
        PL_ES_VB_GetBlock(v4l2InputDataInfo.pool, size, m_VBName.c_str(), &videoFrameInfo->videoFrame.fd, "v4l2src");
    // videoFrameInfo->poolId =
    // ES_VB_Handle2PoolId(videoFrameInfo->videoFrame.fd);
    while (ret || videoFrameInfo->poolId < 0) {
        app_debug("%s get a block from poolid %d  ret %d failed. \n", __FUNCTION__, v4l2InputDataInfo.pool, ret);
        // free(videoFrameInfo);
        ret = PL_ES_VB_GetBlock(v4l2InputDataInfo.pool, size, m_VBName.c_str(), &videoFrameInfo->videoFrame.fd);
        // videoFrameInfo->poolId =
        // ES_VB_Handle2PoolId(videoFrameInfo->videoFrame.fd);
        usleep(1000000);
        // return ES_NULL;
    }

    ES_U64 fd = videoFrameInfo->videoFrame.fd;
    ES_U64 *pVirAddr = ES_NULL;
    pVirAddr = (ES_U64 *)ES_SYS_Mmap(fd, size, SYS_CACHE_MODE_NOCACHE);
    if (NULL == pVirAddr) {
        free(videoFrameInfo);
        return ES_NULL;
    }

    // int tempCount = fread(pVirAddr, 1, size, fp);
    // if (size != tempCount) {
    //     app_debug("%s \n", "read file size not match");
    // }

    memcpy(pVirAddr, fbuf.start, fbuf.length);
    // frame->length = readbuf.length;

    ret = ES_SYS_Munmap(pVirAddr, size);
    if (ret != ES_SUCCESS) {
        free(videoFrameInfo);
        return ES_NULL;
    }

    CFrameMeta *frameMeta = nullptr;

    CImage *cimage = new CImage;
    cimage->mPic = videoFrameInfo;

    frameMeta = new CFrameMeta;
    frameMeta->images.push_back(nullptr);  // pp0
    frameMeta->images.push_back(cimage);   // pp1
    frameMeta->mMetaType = FRAME_META;
    frameMeta->index = frameIndex;
    frameMeta->padIndex = padIndex;
    return frameMeta;
}

app_ret V4l2SrcElement::threadFunc() {
    // 启动摄像头数据采集
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(vfd, VIDIOC_STREAMON, &type) < 0) {
        app_error("start fail \n");
        return ES_FALSE;
    }
    struct v4l2_buffer buf;
    // frame rate
    unsigned long long frameInterval = 0;
    unsigned long long lastFrameTime = 0;
    if (actual_fps > 0) {
        frameInterval = 1000000 / actual_fps;  // us
    }

    // 出队视频缓冲区
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2InputDataInfo.memory_mode == "userptr") {
        buf.memory = V4L2_MEMORY_USERPTR;
    } else if (v4l2InputDataInfo.memory_mode == "dma") {
        buf.memory = V4L2_MEMORY_DMABUF;
    } else if (v4l2InputDataInfo.memory_mode == "mmap") {
        buf.memory = V4L2_MEMORY_MMAP;
    }

    while (true) {
        if (totalFrame > 0 && mFrameIndex >= totalFrame) {
            app_info("V4l2SrcElement reached totalframe limit (%d), sending EOS.", totalFrame);
            V4l2FrameMeta *eosMeta = v4l2FmetaPool->allocate();
            eosMeta->eosFlag = true;
            eosMeta->memory_mode = v4l2InputDataInfo.memory_mode;
            TransMitToNextToProcess(static_cast<CFrameMeta *>(eosMeta));
            break;
        }

        // CBaseMeta *baseMeta = NULL;

        // frameRate count
        if (frameInterval > 0) {
            unsigned long long now = esclock();
            if (now < frameInterval + lastFrameTime) {
                usleep(frameInterval + lastFrameTime - now);
            }
            lastFrameTime = esclock();
        }

        if (ioctl(vfd, VIDIOC_DQBUF, &buf) == -1) {
            app_error("无法出队视频缓冲区 \n");
            close(vfd);
            return ES_FALSE;
        }
        // 注意：这里分配的是V4l2FrameMeta对象
        V4l2FrameMeta *frameMeta = v4l2FmetaPool->allocate();
        frameMeta->memory_mode = v4l2InputDataInfo.memory_mode;

        VIDEO_FRAME_INFO_S *vf = createVideoFrame(0, v4l2InputDataInfo.width, v4l2InputDataInfo.height,
                                                  v4l2InputDataInfo.pixelFormat, v4l2InputDataInfo.fps);

        // 对于MMAP模式，需要将数据从mmap缓冲区拷贝到dma缓冲区
        if (v4l2InputDataInfo.memory_mode == "mmap") {
            // 映射dma缓冲区到虚拟地址
            ES_U64 *pVirAddr = ES_NULL;
            pVirAddr =
                (ES_U64 *)ES_SYS_Mmap(mV4l2FdArray[buf.index], frame_buffers[buf.index].length, SYS_CACHE_MODE_NOCACHE);
            if (NULL == pVirAddr) {
                app_error("ES_SYS_Mmap failed for MMAP mode\n");
                free(vf);
                v4l2FmetaPool->deallocate(frameMeta);
                continue;
            }

            // 从mmap缓冲区拷贝数据到dma缓冲区
            memcpy(pVirAddr, frame_buffers[buf.index].start, frame_buffers[buf.index].length);

            // 解除映射
            int ret = ES_SYS_Munmap(pVirAddr, frame_buffers[buf.index].length);
            if (ret != ES_SUCCESS) {
                app_error("ES_SYS_Munmap failed for MMAP mode\n");
            }

            vf->videoFrame.fd = mV4l2FdArray[buf.index];
        } else {
            vf->videoFrame.fd = mV4l2FdArray[buf.index];
        }

        if (frameMeta->memory_mode == "userptr") {
            int ret = ES_SYS_MemFlushCache(vf->videoFrame.fd);
            if (0 != ret) {
                app_error("ES_SYS_MemFlushCache failed, ret %x!\n", ret);
            }

            frameMeta->userptr_length = frame_buffers[buf.index].length;
            frameMeta->userptr_start = frame_buffers[buf.index].start;
        }

        vf->videoFrame.PTS = mFrameIndex * (1000000 / actual_fps);

        CImageV4l2 *cimage = v4l2imagePool->allocate();
        cimage->pool = (MetaPool<CImage> *)v4l2imagePool;
        cimage->mPic = vf;

        assert(frameMeta->images.size() == 0 && "make sure");
        frameMeta->images.push_back(nullptr);
        frameMeta->images.push_back(cimage);

        // 关键操作：建立对象与实际数据的关联
        frameMeta->mMetaType = FRAME_META;
        frameMeta->index = mFrameIndex;
        frameMeta->padIndex = mPadIndex;

        frameMeta->vbIndex = buf.index;  // 记录缓冲区索引
        frameMeta->pool = (MetaPool<CFrameMeta> *)v4l2FmetaPool;
        frameMeta->vfd = vfd;

        if (dump_enable) {
            // CFrameMeta *frameMeta=(CFrameMeta*)baseMeta;
            app_debug("v4l2 sendframe 0x%08x   write framemeta:%d \n", &frameMeta->images, mFrameIndex);

            string fileName = "v4l2_sendframe_" + mName + "_" + to_string(mFrameIndex) + ".yuv";

            ES_U64 *pVirAddr = ES_NULL;
            ES_U32 size = 0;
            switch (frameMeta->images[1]->mPic->videoFrame.pixelFormat) {
                case PIXEL_FORMAT_NV12:
                    size = (frameMeta->images[1]->mPic->videoFrame.width *
                            frameMeta->images[1]->mPic->videoFrame.height * 3 / 2);
                    break;
                case PIXEL_FORMAT_YUY2:
                    size = (frameMeta->images[1]->mPic->videoFrame.width * frameMeta->images[1]->mPic->videoFrame.height
                            << 1);
                    break;
            }
            assert(size > 0 && "videoframe size illagel!");
            pVirAddr = (ES_U64 *)ES_SYS_Mmap(frameMeta->images[1]->mPic->videoFrame.fd, size, SYS_CACHE_MODE_NOCACHE);

            if (NULL != pVirAddr) {
                dumpFile(fileName, (char *)pVirAddr, size);
                int ret = ES_SYS_Munmap(pVirAddr, size);
                if (ret > 0) {
                    app_error("frameDumpflag name:%s  mFrameIndex:%d %d unmap error\n", mName.c_str(), mFrameIndex,
                              ret);
                }
            } else {
                app_error("frameDumpflag name:%s  mFrameIndex:%d pVirAddr is null\n", mName.c_str(), mFrameIndex);
            }
        }

        // 发送给后续节点
        TransMitToNextToProcess(static_cast<CFrameMeta *>(frameMeta));
        mFrameIndex++;
    }
    // 停止视频流采集
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(vfd, VIDIOC_STREAMOFF, &type) == -1) {
        app_error("无法停止视频流采集 \n");
        close(vfd);
        return ES_FALSE;
    }

    return APP_SUCCESS;
}

/**
 * @brief 检查 V4L2 设备支持的内存模式（DMA > USERPTR 优先级）
 * @param device_path V4L2 设备路径（如 "/dev/video0"）
 * @return 支持的优选内存模式："dma"（DMA）、"userptr"（USERPTR）或 "mmap"
 */
std::string check_v4l2_memory_mode(const std::string &device_path) {
    int fd = open(device_path.c_str(), O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return "";
    }

    // 优先检查 DMA（V4L2_MEMORY_MMAP）
    v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        if (cap.capabilities & V4L2_CAP_STREAMING) {
            v4l2_requestbuffers reqbuf = {0};
            reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            reqbuf.memory = V4L2_MEMORY_MMAP;  // DMA 模式
            reqbuf.count = 1;                  // 仅检测，不实际分配

            if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) == 0) {
                close(fd);
                return "dma";  // 支持 DMA
            }
        }
    }

    // 其次检查 USERPTR（V4L2_MEMORY_USERPTR）
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        if (cap.capabilities & V4L2_CAP_STREAMING) {
            v4l2_requestbuffers reqbuf = {0};
            reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            reqbuf.memory = V4L2_MEMORY_USERPTR;  // USERPTR 模式
            reqbuf.count = 1;

            if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) == 0) {
                close(fd);
                return "userptr";  // 支持 USERPTR
            }
        }
    }

    close(fd);
    return "mmap";  // 默认mmap
}

app_ret V4l2SrcElement::Init() {
    if (m_NextElementVec.size() != 1) {
        return APP_FAILURE;
    }
    app_debug("%s %s\n", " Init : ", m_configFile.c_str());
    YAML::Node config = YAML::LoadFile(m_configFile);

    // config parse
    v4l2InputDataInfo.devpath = config["devpath"].template as<string>();
    if (config["memory_mode"].IsDefined()) {
        v4l2InputDataInfo.memory_mode = config["memory_mode"].template as<string>();
    } else {
        v4l2InputDataInfo.memory_mode = check_v4l2_memory_mode(v4l2InputDataInfo.devpath);
        app_debug("memory_mode is not configured auto detect:%s %s\n", v4l2InputDataInfo.devpath,
                  v4l2InputDataInfo.memory_mode);
        assert(v4l2InputDataInfo.memory_mode.size() > 0 &&
               "memory_mode auto detect failt ,please set value in configure file");
    }
    v4l2InputDataInfo.width = config["width"].template as<int>();
    v4l2InputDataInfo.height = config["height"].template as<int>();
    v4l2InputDataInfo.fps = config["fps"].template as<int>();
    dump_enable = config["dump"]["enable"].template as<bool>();
    string format = config["pixel_format"].template as<string>();
    int framesize = 0;
    if (format == "nv12") {
        v4l2InputDataInfo.pixelFormat = PIXEL_FORMAT_NV12;
        framesize = v4l2InputDataInfo.width * v4l2InputDataInfo.height * 3 / 2;
    } else if (format == "yuy2" || format == "yuyv") {
        v4l2InputDataInfo.pixelFormat = PIXEL_FORMAT_YUY2;
        framesize = v4l2InputDataInfo.width * v4l2InputDataInfo.height * 2;
    } else {
        assert("illegal pixel_format");
    }
    assert(framesize > 0 && "framesize illagel");
    v4l2InputDataInfo.buf_count = config["buf_count"].template as<int>();
    if (config["totalframe"].IsDefined()) {
        totalFrame = config["totalframe"].template as<int>();
    } else {
        totalFrame = 0;
    }

    // videoframe mem pool, needed for both modes (for userptr copy)
    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 32;
    poolCfg.blkSize = framesize;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &v4l2InputDataInfo.pool);
    app_debug("%s %d\n", "create pool ret is :", (int)ret);

    mPadIndex = getPadIndex();
    printf("%s mPadIndex: %d\n", mName.c_str(), mPadIndex);

    size_t frameSize = 0;
    switch (v4l2InputDataInfo.pixelFormat) {
        case PIXEL_FORMAT_NV12:
            frameSize = (v4l2InputDataInfo.width * v4l2InputDataInfo.height * 3 / 2);
            break;
        case PIXEL_FORMAT_YUY2:
            frameSize = (v4l2InputDataInfo.width * v4l2InputDataInfo.height << 1);
            break;
    }
    assert(frameSize > 0 && "videoframe size illagel!");

    // 打开摄像头设备
    vfd = open(v4l2InputDataInfo.devpath.c_str(), O_RDWR);
    if (vfd == -1) {
        app_error("无法打开摄像头设备 \n");
        return ES_FALSE;
    }

    // 创建对象池
    v4l2FmetaPool = new MetaPool<V4l2FrameMeta>(v4l2InputDataInfo.buf_count);
    v4l2imagePool = new MetaPool<CImageV4l2>(v4l2InputDataInfo.buf_count);
    mV4l2FdArray.resize(v4l2InputDataInfo.buf_count);
    if (v4l2InputDataInfo.memory_mode == "dma") {
        assert(v4l2InputDataInfo.buf_count <= poolCfg.blkCnt && "v4l2InputDataInfo.buf_count should <= poolCfg.blkCnt");
        // DMA mode: only allocate dma-buf pool

        for (int i = 0; i < v4l2InputDataInfo.buf_count; i++) {
            ES_S32 ret = PL_ES_VB_GetBlock(v4l2InputDataInfo.pool, frameSize, m_VBName.c_str(), &mV4l2FdArray[i]);
            if (ret) {
                app_error("VB_GetBlock failed for buffer %d\n", i);
                return APP_FAILURE;
            }
        }
    } else if (v4l2InputDataInfo.memory_mode == "userptr") {
        // USERPTR mode: use malloc memory and convert to fd via ES_USER_Malloc_To_MemFd
        frame_buffers = new FrameBuffer[v4l2InputDataInfo.buf_count];
        for (int i = 0; i < v4l2InputDataInfo.buf_count; i++) {
            // 1. Allocate memory using malloc
            // frame_buffers[i].start = malloc(frameSize);

            long page_size = sysconf(_SC_PAGESIZE);
            if (page_size == -1) {
                perror("sysconf failed");
                return 1;
            }
            // 为帧缓冲区分配内存，确保内存起始地址按页对齐（page-aligned）
            // - frame_buffers[i].start: 指向第i个帧缓冲区的起始地址
            // - aligned_alloc(): 分配对齐的内存块
            // - page_size: 系统内存页大小（如4096字节）
            // - frameSize: 需要分配的帧缓冲区大小（单位：字节）
            frame_buffers[i].start = aligned_alloc(page_size, frameSize);

            if (!frame_buffers[i].start) {
                app_error("malloc for userptr mode failed for buffer %d\n", i);
                return APP_FAILURE;
            }

            // 2. Convert malloc memory to fd using ES_USER_Malloc_To_MemFd
            ES_U64 dma_fd = 0;
            ES_S32 ret = ES_USER_Malloc_To_MemFd(frame_buffers[i].start, frameSize, &dma_fd, SYS_CACHE_MODE_NOCACHE);
            if (ret != ES_SUCCESS) {
                app_error("ES_USER_Malloc_To_MemFd failed for buffer %d, ret=%d\n", i, ret);
                free(frame_buffers[i].start);
                return APP_FAILURE;
            }

            mV4l2FdArray[i] = dma_fd;
            frame_buffers[i].length = frameSize;
        }
    } else if (v4l2InputDataInfo.memory_mode == "mmap") {
        assert(v4l2InputDataInfo.buf_count <= poolCfg.blkCnt && "v4l2InputDataInfo.buf_count should <= poolCfg.blkCnt");
        // MMAP mode: use v4l2 mmap buffers
        frame_buffers = new FrameBuffer[v4l2InputDataInfo.buf_count];
        // 不需要预先分配 mV4l2FdArray，因为缓冲区由V4L2驱动分配
        // 但我们需要为后续处理分配dma缓冲区
        for (int i = 0; i < v4l2InputDataInfo.buf_count; i++) {
            ES_S32 ret = PL_ES_VB_GetBlock(v4l2InputDataInfo.pool, frameSize, m_VBName.c_str(), &mV4l2FdArray[i]);
            if (ret) {
                app_error("VB_GetBlock failed for buffer %d\n", i);
                return APP_FAILURE;
            }
        }
    } else {
        app_error("Invalid memory mode: %s", v4l2InputDataInfo.memory_mode.c_str());
        return APP_FAILURE;
    }

    // v4l2初始化（修改为DMABUF模式）
    // v4l2 init
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;

    // 查询摄像头能力
    if (ioctl(vfd, VIDIOC_QUERYCAP, &cap) == -1) {
        app_error("无法查询摄像头能力 \n");
        close(vfd);
        return ES_FALSE;
    }

    // 设置视频格式
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = v4l2InputDataInfo.width;
    fmt.fmt.pix.height = v4l2InputDataInfo.height;
    fmt.fmt.pix.pixelformat =
        v4l2InputDataInfo.pixelFormat == PIXEL_FORMAT_YUY2 ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_NV12;  // YUV格式
    if (ioctl(vfd, VIDIOC_S_FMT, &fmt) == -1) {
        app_error("无法设置视频格式");
        close(vfd);
        return ES_FALSE;
    }

    // 验证实际设置结果
    struct v4l2_format actualFmt;
    memset(&actualFmt, 0, sizeof(actualFmt));
    actualFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(vfd, VIDIOC_G_FMT, &actualFmt) == 0) {
        // 获取像素格式的字符串表示
        std::string actualPixfmt = fourccToString(actualFmt.fmt.pix.pixelformat);
        if (actualPixfmt.empty()) {
            app_error("not support type fourcc:%p \n", actualFmt.fmt.pix.pixelformat);
            return ES_FALSE;
        }
        // 在VIDIOC_G_FMT后获取实际sizeimage
        assert(frameSize == actualFmt.fmt.pix.sizeimage && "framasize not match(user calc vs 4l2dev)");

        // 获取内存模式的字符串描述
        std::string memoryModeStr;
        if (v4l2InputDataInfo.memory_mode == "dma") {
            memoryModeStr = "DMA (DMABUF)";
        } else if (v4l2InputDataInfo.memory_mode == "userptr") {
            memoryModeStr = "User Pointer";
        } else if (v4l2InputDataInfo.memory_mode == "mmap") {
            memoryModeStr = "Memory Mapped";
        } else {
            memoryModeStr = "Unknown (" + v4l2InputDataInfo.memory_mode + ")";
        }

        app_info(
            "实际设置参数:\n"
            "  宽度: %u\n"
            "  高度: %u\n"
            "  像素格式: %s\n"
            "  实际FourCC: 0x%08x\n"
            "  内存模式: %s\n"
            "图像字节大小: %u 字节\n"
            "每行字节数: %u 字节\n",
            actualFmt.fmt.pix.width, actualFmt.fmt.pix.height, actualPixfmt.c_str(), actualFmt.fmt.pix.pixelformat,
            memoryModeStr.c_str(),  // 添加内存模式信息
            actualFmt.fmt.pix.sizeimage, actualFmt.fmt.pix.bytesperline);
    }

    // 设置帧率为30fps
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;                        // 分子
    parm.parm.capture.timeperframe.denominator = v4l2InputDataInfo.fps;  // 分母（30帧/秒）
    if (ioctl(vfd, VIDIOC_S_PARM, &parm) == -1) {
        app_error("警告：设置帧率失败，将继续使用默认帧率。");
        // 注意：这里不退出，因为不是所有设备都支持设置帧率
    }

    // 检查实际设置的帧率
    if (ioctl(vfd, VIDIOC_G_PARM, &parm) == 0) {
        actual_fps = static_cast<double>(parm.parm.capture.timeperframe.denominator) /
                     static_cast<double>(parm.parm.capture.timeperframe.numerator);
        app_info("实际帧率:%d fps ", actual_fps);
    }

    // 请求视频缓冲区
    memset(&req, 0, sizeof(req));
    req.count = v4l2InputDataInfo.buf_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2InputDataInfo.memory_mode == "userptr") {
        req.memory = V4L2_MEMORY_USERPTR;
    } else if (v4l2InputDataInfo.memory_mode == "dma") {
        req.memory = V4L2_MEMORY_DMABUF;
    } else if (v4l2InputDataInfo.memory_mode == "mmap") {
        req.memory = V4L2_MEMORY_MMAP;
    }
    if (ioctl(vfd, VIDIOC_REQBUFS, &req) == -1) {
        app_error("VIDIOC_REQBUFS failed: %s (可能不支持当前内存模式)", strerror(errno));
        close(vfd);
        return ES_FALSE;
    }

    // 实际分配到的缓冲区可能少于请求的数量
    assert(v4l2InputDataInfo.buf_count == req.count && "v4l2 buf req failt");

    // 对于MMAP模式，需要映射缓冲区
    if (v4l2InputDataInfo.memory_mode == "mmap") {
        for (int i = 0; i < req.count; i++) {
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.index = i;
            buf.memory = V4L2_MEMORY_MMAP;

            if (ioctl(vfd, VIDIOC_QUERYBUF, &buf) == -1) {
                app_error("VIDIOC_QUERYBUF failed for buffer %d: %s", i, strerror(errno));
                close(vfd);
                return APP_FAILURE;
            }

            frame_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, vfd, buf.m.offset);
            if (frame_buffers[i].start == MAP_FAILED) {
                app_error("mmap failed for buffer %d: %s", i, strerror(errno));
                close(vfd);
                return APP_FAILURE;
            }
            frame_buffers[i].length = buf.length;
        }
    }

    // 入队缓冲区
    for (int i = 0; i < req.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.index = i;

        if (v4l2InputDataInfo.memory_mode == "userptr") {
            buf.memory = V4L2_MEMORY_USERPTR;
            buf.m.userptr = (unsigned long)frame_buffers[i].start;

            // // 改为普通内存分配测试
            // size_t page_size = sysconf(_SC_PAGESIZE);
            // buf.m.userptr = (unsigned long)aligned_alloc(page_size, frameSize) ;

            buf.length = frame_buffers[i].length;
        } else if (v4l2InputDataInfo.memory_mode == "dma") {
            buf.memory = V4L2_MEMORY_DMABUF;
            buf.m.fd = mV4l2FdArray[i];
        } else if (v4l2InputDataInfo.memory_mode == "mmap") {
            buf.memory = V4L2_MEMORY_MMAP;
            // 对于MMAP模式，不需要设置额外的字段，驱动会根据index处理
        }

        if (ioctl(vfd, VIDIOC_QBUF, &buf) == -1) {
            app_error("Queue buffer %d failed: %s", i, strerror(errno));
            close(vfd);
            return APP_FAILURE;
        }
    }

    mFrameIndex = 0;
    return APP_SUCCESS;
}

app_ret V4l2SrcElement::Start() {
    app_debug("testsrc element will start\n");
    m_testsrcThread = std::thread(std::mem_fn(&V4l2SrcElement::threadFunc), this);
    return APP_SUCCESS;
}

app_ret V4l2SrcElement::Wait() {
    m_testsrcThread.join();
    return APP_SUCCESS;
}
app_ret V4l2SrcElement::Finish() {
    // 销毁对象池
    if (v4l2FmetaPool) {
        delete v4l2FmetaPool;
        v4l2FmetaPool = nullptr;
    }
    if (v4l2imagePool) {
        delete v4l2imagePool;
        v4l2imagePool = nullptr;
    }

    if (v4l2InputDataInfo.memory_mode == "dma") {
        // 释放dma-buf池
        for (int fd : mV4l2FdArray) {
            if (fd > 0) PL_ES_VB_ReleaseBlock(fd);
        }
    } else if (v4l2InputDataInfo.memory_mode == "userptr") {
        // 释放userptr内存
        if (frame_buffers) {
            for (int i = 0; i < v4l2InputDataInfo.buf_count; i++) {
                if (frame_buffers[i].start) {
                    free(frame_buffers[i].start);
                }
            }
            delete[] frame_buffers;
            frame_buffers = nullptr;
        }
    } else if (v4l2InputDataInfo.memory_mode == "mmap") {
        // 释放mmap内存
        if (frame_buffers) {
            for (int i = 0; i < v4l2InputDataInfo.buf_count; i++) {
                if (frame_buffers[i].start && frame_buffers[i].start != MAP_FAILED) {
                    munmap(frame_buffers[i].start, frame_buffers[i].length);
                }
            }
            delete[] frame_buffers;
            frame_buffers = nullptr;
        }
        // 释放dma缓冲区
        for (int fd : mV4l2FdArray) {
            if (fd > 0) PL_ES_VB_ReleaseBlock(fd);
        }
    }

    mV4l2FdArray.clear();
    mV4l2FdArray.shrink_to_fit();

    PL_ES_VB_DestroyPool(v4l2InputDataInfo.pool);
    // 关闭摄像头设备
    close(vfd);

    freeNumaNode(this, sizeof(V4l2SrcElement));
    return APP_SUCCESS;
}

app_ret V4l2SrcElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) { return APP_SUCCESS; }

app_ret V4l2SrcElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previousElement) {
    ProcessData(baseMeta, this);
    return APP_SUCCESS;
}

extern "C" CElement *createEsV4l2SrcElement(const char *name, const char *path, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(V4l2SrcElement))) V4l2SrcElement(name, path, dieIndex);
}
