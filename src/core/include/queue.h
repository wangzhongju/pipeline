#ifndef _ESSDK_PL_QUEUE_H__
#define _ESSDK_PL_QUEUE_H__

#include <stdio.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "base_meta.h"

using namespace std;

template <class T>
class BlockQueue {
   public:
    typedef std::unique_lock<std::mutex> TLock;

    explicit BlockQueue(const long long int maxCapacity = -1, const string name = "not named")
        : mMaxCapacity(maxCapacity), mName(name) {};

    size_t size() {
        TLock lock(mMutex);
        return mList.size();
    }

    size_t capacity() { return mMaxCapacity; }

    string name() { return mName; }

    void push_back(const T &item) {
        TLock lock(mMutex);
        if (hasCapacity()) {
            while (mList.size() >= mMaxCapacity) {
                // importanted !!!!!!!!!!!!!!!!!!
                // When this log appears, it means the queue is full and the size needs to be increased.
                // if (mName.find("PoolID") != std::string::npos) {
                //   std::cout << "!!!!! attenting !!!  push_back: Queue " << mName << " is full, waiting... ,!!!! the
                //   queue size needs to be increased" << std::endl;
                // }
                // std::cout << "push_back: Queue " << mName << " is full, waiting... ,!!!! the quene size needs to be
                // increased" << std::endl;

                mNotFull.wait(lock);
            }
        }

        mList.push_back(item);
        mNotEmpty.notify_all();
    }

    void push_front(const T &item) {
        TLock lock(mMutex);

        if (hasCapacity()) {
            while (mList.size() >= mMaxCapacity) {
                // 如需调试日志可在此处打开
                mNotFull.wait(lock);
            }
        }

        mList.push_front(item);   // 关键区别：改为前插
        mNotEmpty.notify_all();   // 与 push_back 保持一致
    }
    
    T pop() {
        TLock lock(mMutex);
        while (mList.empty()) {
            // std::cout << " >>> attenting pop: Queue " << mName << " is empty, waiting... " << std::endl;
            mNotEmpty.wait(lock);
        }

        T temp = *mList.begin();
        mList.pop_front();

        mNotFull.notify_all();
        return temp;
    }

    T pop_back() {
        TLock lock(mMutex);
        while (mList.empty()) {
            // std::cout << " >>> attenting pop: Queue " << mName << " is empty, waiting... " << std::endl;
            mNotEmpty.wait(lock);
        }

        T temp = mList.back();
        mList.pop_back();

        mNotFull.notify_all();
        return temp;
    }

    bool empty() {
        TLock lock(mMutex);
        return mList.empty();
    }

    bool full() {
        if (!hasCapacity()) {
            return false;
        }

        TLock lock(mMutex);
        return mList.size() >= mMaxCapacity;
    }

    void remove(const T &item) {
        TLock lock(mMutex);
        auto newEnd = std::remove_if(mList.begin(), mList.end(), [&](const T &elem) { return elem == item; });
        mList.erase(newEnd, mList.end());
        mNotFull.notify_all();
    }

   private:
    bool hasCapacity() const { return mMaxCapacity > 0; }

    typedef std::deque<T> TList;
    TList mList;

    const int mMaxCapacity;
    const string mName;

    std::mutex mMutex;
    std::condition_variable mNotEmpty;
    std::condition_variable mNotFull;
};

typedef BlockQueue<CBaseMeta *> BaseMetaQueue;

typedef struct _plVBStruct_ {
    VB_POOL poolId;
    ES_U64 blkSize;
    ES_U64 memFd;
    ES_CHAR strZone[128];
    char name[128];
    ES_VOID *pIOVA = ES_NULL;
} plVBstr;

// 使用 std::unordered_map 存储多个 BlockQueue
using BlockQueueMap = std::unordered_map<VB_POOL, BlockQueue<plVBstr *> *>;
using memFdMap = std::unordered_map<VB_POOL, plVBstr *>;
class BlockQueueManager {
   public:
    // 插入数据到指定的 BlockQueue
    void insert(VB_POOL poolId, plVBstr *item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queueMap_.find(poolId) == queueMap_.end()) {
            // 如果对应的 BlockQueue 不存在，则创建一个新的
            // queueMap_.emplace(std::make_pair(poolId, BlockQueue<plVBstr*>()));
            // new BlockQueue<plVBstr*> queue;
            std::string queueName = "PoolID:" + std::to_string(poolId);
            queueMap_.emplace(poolId, new BlockQueue<plVBstr *>(-1, queueName.c_str()));
        }

        memFdMap_.emplace(item->memFd, item);

        // 释放锁，然后调用 BlockQueue 的 push_back 方法
        lock.unlock();
        queueMap_[poolId]->push_back(item);
        // 同时将 memFd 和 item 插入到哈希表中
    }

    // 从指定的 BlockQueue 中取出数据
    plVBstr *pop(VB_POOL poolId) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queueMap_.find(poolId) != queueMap_.end()) {
            // 释放锁，然后调用 BlockQueue 的 pop 方法
            lock.unlock();
            plVBstr *temp = queueMap_[poolId]->pop_back();

            lock.lock();
            memFdMapRelease_.emplace(temp->memFd, temp);
            return temp;
        }
        return nullptr;  // 如果 BlockQueue 不存在，返回 nullptr
    }

    // 检查指定的 BlockQueue 是否存在
    bool contains(VB_POOL poolId) {
        std::unique_lock<std::mutex> lock(mutex_);
        return queueMap_.find(poolId) != queueMap_.end();
    }

    // 根据 memFd 查找元素
    plVBstr *find_by_memFd(ES_U64 memFd) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = memFdMap_.find(memFd);
        if (it != memFdMap_.end()) {
            return it->second;  // 找到对应的 plVBstr*
        }
        return nullptr;  // 没有找到
    }

    // release,fd 对应的plVBstr 放回queueMap_， 需要规避重复release 的情况，并且在release 失败 返回nullptr
    plVBstr *release(ES_U64 memFd) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = memFdMapRelease_.find(memFd);
        if (it != memFdMapRelease_.end()) {
            plVBstr *item = it->second;
            memFdMapRelease_.erase(memFd);

            if (queueMap_.find(item->poolId) != queueMap_.end()) {
                lock.unlock();
                queueMap_[item->poolId]->push_back(item);
                return item;
            }
        }
        return nullptr;  // 没有找到
    }

    // 从指定的 BlockQueue 中删除元素
    void remove(VB_POOL poolId, plVBstr *item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queueMap_.find(poolId) != queueMap_.end()) {
            lock.unlock();
            queueMap_[poolId]->remove(item);

            lock.lock();
            memFdMap_.erase(item->memFd);
            delete item;
        }
    }
    size_t size(VB_POOL poolId) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queueMap_.find(poolId) != queueMap_.end()) {
            lock.unlock();
            return queueMap_[poolId]->size();
        }
    }

    // 清理资源
    ~BlockQueueManager() {
        for (auto &pair : queueMap_) {
            while (!pair.second->empty()) {
                plVBstr *item = pair.second->pop();
                delete item;
            }
            delete pair.second;
        }
        queueMap_.clear();
        memFdMap_.clear();  // 清空哈希表
        memFdMapRelease_.clear();
    }

   private:
    BlockQueueMap queueMap_;  // 存储多个 BlockQueue
    memFdMap memFdMap_;
    memFdMap memFdMapRelease_;
    std::mutex mutex_;  // 线程安全的互斥锁
};

#endif  //_ESSDK_PL_QUEUE_H__