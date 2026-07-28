#ifndef _V4L2SRC_ELEMENT_H__
#define _V4L2SRC_ELEMENT_H__

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>  //共享内存

#include <iostream>
#include <thread>

#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "video.h"

struct V4l2InputDataInfo {
    std::string devpath;
    int height;
    int width;
    int fps;
    PIXEL_FORMAT_E pixelFormat;
    int buf_count;  // v4l2 dev buffer count

    VB_POOL pool;
    sem_t poolSem;

    int dateType;
    std::string memory_mode;  // dma, userptr, mmap
};

struct FrameBuffer {
    void *start;
    size_t length;  // buffer's length is different from cap_image_size
};

class CImageV4l2 : public CImage {
   public:
    CImageV4l2() {};
    CImageV4l2(VIDEO_FRAME_INFO_S *pic) : CImage(pic) {};
    virtual ~CImageV4l2() {};
    void release() override;
};

// 定义扩展帧元数据类
class V4l2FrameMeta : public CFrameMeta {
   public:
    int vbIndex;              // 对应v4l2缓冲区的索引
    int vfd;                  // v4l2 fd
    std::string memory_mode;  // dma,userptr,mmap
    unsigned int userptr_length;
    void *userptr_start;

    V4l2FrameMeta() : CFrameMeta(), vbIndex(-1), vfd(-1), userptr_length(-1), userptr_start(nullptr) {}
    virtual ~V4l2FrameMeta() {}

    virtual void release() override {
        if (eosFlag) {
            CFrameMeta::release();
            return;
        }

        // 重新入队缓冲区
        struct v4l2_buffer qbuf;
        memset(&qbuf, 0, sizeof(qbuf));
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.index = vbIndex;

        if (memory_mode == "userptr") {
            qbuf.memory = V4L2_MEMORY_USERPTR;
            qbuf.m.userptr = (unsigned long)userptr_start;
            qbuf.length = userptr_length;
        } else if (memory_mode == "dma") {
            qbuf.memory = V4L2_MEMORY_DMABUF;
            if (images.size() > 1 && images[1] && images[1]->mPic) {
                qbuf.m.fd = images[1]->mPic->videoFrame.fd;
            }
        } else if (memory_mode == "mmap") {
            qbuf.memory = V4L2_MEMORY_MMAP;
            // 对于MMAP模式，不需要设置额外的字段，驱动会根据index处理
        }
        // enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        // printf("33333333333333333333333333333333gggggggggggg %d,%d \n", vfd, type);
        // if (ioctl(vfd, VIDIOC_STREAMON, &type) < 0) {
        //     app_error("start fail 32323 \n");
        // }
        if (ioctl(vfd, VIDIOC_QBUF, &qbuf) < 0) {
            app_error("re-queue buffer failed for index %d: %s\n", vbIndex, strerror(errno));
        }
        CFrameMeta::release();
    }
};

class V4l2SrcElement : public CElement {
   public:
    V4l2SrcElement(const char *name = "v4l2src", const char *config = "", int dieIndex = 0)
        : CElement(name, config, dieIndex) {};
    ~V4l2SrcElement() = default;

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret Finish() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previou = 0) override;

   private:
    app_ret threadFunc();

   private:
    std::thread m_testsrcThread;
    double actual_fps;  // query from device
    int mPadIndex;

   private:
    int vfd;  // v4l2 fd
    uint16_t mFrameIndex;
    V4l2InputDataInfo v4l2InputDataInfo;
    FrameBuffer *frame_buffers = nullptr;
    bool dump_enable;

    MetaPool<V4l2FrameMeta> *v4l2FmetaPool;  // CFrameMeta对象池
    // MetaPool<CFrameMeta> *fmetaPool;
    MetaPool<CImageV4l2> *v4l2imagePool;
    std::vector<ES_U64> mV4l2FdArray;  // 保存每个缓冲区的fd
    int totalFrame;
};
#endif  //_V4L2SRC_ELEMENT_H__