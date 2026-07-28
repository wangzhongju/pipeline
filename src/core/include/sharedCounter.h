#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <stdexcept>
#include <thread>

/**
 * @class SharedCounter
 * @brief A thread-safe, process-shared counter manager using POSIX shared memory.
 *
 * This class provides:
 * 1. Singleton access to shared counters (`vdecCounter`, `vencCounter`, `vencOffset`)
 * 2. Process-shared memory management with reference counting
 * 3. Thread-safe operations via atomic variables and file locks
 * 4. Automatic cleanup when the last reference is destroyed
 *
 * @note Copy constructor and assignment operator are deleted to enforce uniqueness.
 * @note Uses `std::atomic` for lock-free thread-safe counter operations.
 * @note Uses POSIX shared memory (`shm_open`, `mmap`) for inter-process communication.
 */
class SharedCounter {
   public:
    // Disable copy construction and assignment to maintain singleton property
    SharedCounter(const SharedCounter &) = delete;
    SharedCounter &operator=(const SharedCounter &) = delete;

    /**
     * @brief Singleton accessor for the global SharedCounter instance.
     * @return Reference to the singleton instance.
     * @note Thread-safe due to C++11's local static variable initialization guarantees.
     */
    static SharedCounter &sharedCounter() {
        static SharedCounter instance;
        return instance;
    }

    int vdecCounterIncrement() { return shared_data_->vdecCounter.fetch_add(1, std::memory_order_relaxed) + 1; }

    int vdecCounterDecrement() { return shared_data_->vdecCounter.fetch_sub(1, std::memory_order_relaxed) - 1; }

    int vdecCounterGet() const { return shared_data_->vdecCounter.load(std::memory_order_relaxed); }

    int vencCounterIncrement() { return shared_data_->vencCounter.fetch_add(1, std::memory_order_relaxed) + 1; }

    int vencCounterDecrement() { return shared_data_->vencCounter.fetch_sub(1, std::memory_order_relaxed) - 1; }

    int vencCounterGet() const { return shared_data_->vencCounter.load(std::memory_order_relaxed); }

    int vencOffsetIncrement(int value) {
        return shared_data_->vencOffset.fetch_add(value, std::memory_order_relaxed) + value;
    }

    int vencOffsetDecrement(int value) {
        return shared_data_->vencOffset.fetch_sub(value, std::memory_order_relaxed) - value;
    }

    int vencOffsetGet() const { return shared_data_->vencOffset.load(std::memory_order_relaxed); }

    /**
     * @brief Acquires an exclusive lock on the shared memory segment.
     * @return 0 on success, -1 on failure (sets errno).
     * @note Uses `flock` with `LOCK_EX` for process-level mutual exclusion.
     */
    int lockSharedMemory() { return flock(shm_fd_, LOCK_EX); }

    /**
     * @brief Releases the exclusive lock on the shared memory segment.
     * @return 0 on success, -1 on failure (sets errno).
     */
    int unlockSharedMemory() { return flock(shm_fd_, LOCK_UN); }

   private:
    /**
     * @struct SharedData
     * @brief Layout of the shared memory segment.
     *
     * All members are `std::atomic` to ensure cross-process consistency.
     * Uses `memory_order_acquire`/`memory_order_release` for proper synchronization.
     */
    struct SharedData {
        std::atomic<bool> initialized{false};  // Initialize flag
        std::atomic<int> ref_count{0};         // Map reference count
        std::atomic<int> vdecCounter{0};       // Vdec elements shared counter
        std::atomic<int> vencCounter{0};       // Venc elements shared counter
        std::atomic<int> vencOffset{0};        // Venc elements shared Offset
    };

    int shm_fd_ = -1;                    // File descriptor for shared memory object
    SharedData *shared_data_ = nullptr;  // Pointer to mapped shared memory

    /**
     * @brief Private constructor for singleton pattern.
     * @note Initializes shared memory and handles potential failures.
     * @note If initialization fails, cleans up resources and rethrows exceptions.
     */
    SharedCounter() {
        try {
            app_debug("SharedCounter (PID: %d, TID: %d), init shared memory... \n", getpid(),
                      std::this_thread::get_id());
            shmInitialize();
        } catch (...) {
            app_error("SharedCounter (PID: %d, TID: %d), init shared memory failed... \n", getpid(),
                      std::this_thread::get_id());
            shmCleanup();
            throw;
        }
    }

    /**
     * @brief Destructor that cleans up shared memory resources.
     * @note Decrements reference count and unlinks shared memory when last reference.
     */
    ~SharedCounter() {
        app_debug("SharedCounter destroyed (PID: %d, TID: %d), cleaning up... \n", getpid(),
                  std::this_thread::get_id());
        shmCleanup();
    }

    /**
     * @brief Initializes or maps the shared memory segment.
     *
     * Steps:
     * 1. Creates/opens shared memory object
     * 2. Sets its size
     * 3. Maps it into process memory
     * 4. Initializes data structures if first user
     * 5. Increments reference count if already initialized
     *
     * @note Uses file locking for thread safety during initialization.
     * @note Uses placement new to construct atomics in shared memory.
     */
    void shmInitialize() {
        shm_fd_ = shm_open("/essdkpl_shared_memory", O_CREAT | O_RDWR, 0666);
        if (shm_fd_ == -1) {
            app_error("ShareCounter shm_open failed");
            return;
        }

        if (ftruncate(shm_fd_, sizeof(SharedData)) == -1) {
            close(shm_fd_);
            app_error("ShareCounter ftruncate failed");
            return;
        }

        void *addr = mmap(nullptr, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (addr == MAP_FAILED) {
            close(shm_fd_);
            app_error("ShareCounter mmap failed");
            return;
        }
        shared_data_ = static_cast<SharedData *>(addr);

        if (lockSharedMemory() == -1) {
            munmap(shared_data_, sizeof(SharedData));
            close(shm_fd_);
            app_error("ShareCounter initialize get lock failed \n");
            return;
        }

        try {
            if (!shared_data_->initialized.load(std::memory_order_acquire)) {
                // First-time initialization (owner)
                new (&shared_data_->vdecCounter) std::atomic<int>(0);
                new (&shared_data_->vencCounter) std::atomic<int>(0);
                new (&shared_data_->vencOffset) std::atomic<int>(0);
                new (&shared_data_->ref_count) std::atomic<int>(1);
                shared_data_->initialized.store(true, std::memory_order_release);
                app_debug("SharedCounter initialized shared memory (owner)\n");
            } else {
                // Existing shared memory (client)
                shared_data_->ref_count.fetch_add(1, std::memory_order_relaxed);
                app_debug("ShareCounter mapped existing shared memory\n");
            }
        } catch (...) {
            unlockSharedMemory();
            munmap(shared_data_, sizeof(SharedData));
            close(shm_fd_);
            throw;
        }

        unlockSharedMemory();
    }

    /**
     * @brief Cleans up shared memory resources.
     *
     * Steps:
     * 1. Acquires lock for thread safety
     * 2. Decrements reference count
     * 3. If last reference, unmaps memory and unlinks shared object
     * 4. Otherwise just unmaps memory
     *
     * @note Ensures proper cleanup even if exceptions occur.
     */
    void shmCleanup() {
        if (!shared_data_) return;

        if (lockSharedMemory() == -1) {
            munmap(shared_data_, sizeof(SharedData));
            close(shm_fd_);
            shared_data_ = nullptr;
            shm_fd_ = -1;
            app_error("SharedCounter cleanup get lock failed \n");
            return;
        }

        try {
            int ref_count = shared_data_->ref_count.fetch_sub(1, std::memory_order_acq_rel);

            if (ref_count == 1) {
                // Last reference - full cleanup
                munmap(shared_data_, sizeof(SharedData));
                close(shm_fd_);
                shm_unlink("/essdkpl_shared_memory");
                shared_data_ = nullptr;
                shm_fd_ = -1;
                return;
            }
        } catch (...) {
            app_error("SharedCounter cleanup failed \n");
        }

        unlockSharedMemory();
        munmap(shared_data_, sizeof(SharedData));
        close(shm_fd_);
        shared_data_ = nullptr;
        shm_fd_ = -1;
    }
};
