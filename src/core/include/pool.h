#ifndef _ESSDK_PL_COMMON_POOL_H__
#define _ESSDK_PL_COMMON_POOL_H__
#include <numa.h>
#include <numaif.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>

// Base class for poolable objects
class Poolable {
   public:
    virtual ~Poolable() {}
};

// Memory pool class template
template <typename T>
class MetaPool {
   private:
    std::deque<T *> objects;
    std::vector<T *> m_allObjects;
    std::mutex mutex;
    std::condition_variable cv;
    int objSize;
    std::string mName;
    void *largeMemoryBlock;
    size_t largeMemoryBlockSize;
    int poolCnt;

   public:
    MetaPool(size_t poolSize, int dieID = 0, std::string name = "no name") {
        mName = name;
        objSize = sizeof(T);
        poolCnt = poolSize;
        size_t padding = 1024;  // Padding before and after each object
        size_t totalSize = poolSize * (objSize + 2 * padding);

        // Allocate a large memory block
        largeMemoryBlock = bindNumaNode(dieID, totalSize);
        largeMemoryBlockSize = totalSize;
        memset(largeMemoryBlock, 0x00, largeMemoryBlockSize);
        m_allObjects.reserve(poolSize);
        // Split the large memory block into smaller blocks
        for (size_t i = 0; i < poolSize; ++i) {
            char *start = static_cast<char *>(largeMemoryBlock) + i * (objSize + 2 * padding);
            T *element = new (start + padding) T();
            objects.push_back(element);
            m_allObjects.push_back(element);
        }
    }

    T *allocate() {
        std::unique_lock<std::mutex> lock(mutex);

        // Block indefinitely until an object becomes available
        // printf("%s allocate,pool size: %d\n", typeid(T).name(), objects.size());
        while (objects.empty()) {
            // printf("!!!!!!   attenting  metapool [ type:%s name:%s size:%d] allocate wait, may be need increase pool
            // size \n", typeid(T).name(),mName.c_str(),poolCnt);
            cv.wait(lock);
        }

        T *obj = objects.front();
        objects.pop_front();
        return obj;
    }

    T *allocate(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);

        // Block until an object becomes available or timeout occurs
        if (cv.wait_for(lock, timeout, [this] { return !objects.empty(); })) {
            T *obj = objects.front();
            objects.pop_front();
            return obj;
        } else {
            // printf("!!!!!!   attenting  metapool [ %s ] allocate wait  << timeout >>, need increase pool size
            // \n",mName.c_str());
            return nullptr;  // Timeout
        }
    }

    void deallocate(T *obj) {
        std::lock_guard<std::mutex> lock(mutex);
        // printf("%s deallocate,pool size: %d\n", typeid(T).name(), objects.size());
        // memset(obj,);
        objects.push_back(obj);
        cv.notify_one();  // Notify waiting threads
    }

    ~MetaPool() {
        for (T *obj : m_allObjects) {
            obj->~T();
        }
        freeNumaNode(largeMemoryBlock, largeMemoryBlockSize);
    }

    void *bindNumaNode(int nodeId, size_t size) {
        void *ptr = NULL;
        if (numa_num_configured_nodes() <= 1) {
            try {
                ptr = ::operator new(size);
            } catch (const std::bad_alloc &e) {
                std::cerr << "Failed to allocate memory using new: " << e.what() << std::endl;
                exit(1);
            }
        } else {
            ptr = numa_alloc_onnode(size, nodeId);
            if (ptr == nullptr) {
                std::cerr << "Failed to allocate memory on NUMA node " << nodeId << std::endl;
                exit(1);
            }
        }
        return ptr;
    }

    void freeNumaNode(void *ptr, size_t size) {
        if (ptr == nullptr) {
            return;  // 空指针无需处理
        }

        // 根据 NUMA 节点数量选择释放方式
        if (numa_num_configured_nodes() <= 1) {
            ::operator delete(ptr);  // 标准释放
        } else {
            numa_free(ptr, size);  // NUMA 释放（需传入大小）
        }

        ptr = nullptr;
    }
};

#endif