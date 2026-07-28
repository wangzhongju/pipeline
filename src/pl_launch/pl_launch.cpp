#define PL_LOG_ID PL_LOG_LANUCH
#include <assert.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <numa.h>
#include <numaif.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "element_option_parser.h"
#include "pipeline.h"

using namespace std;

std::map<string, CElement *> gElementNameMap;

class PluginMgr {
   private:
    using soHandle = std::shared_ptr<void>;
    std::filesystem::path _lib_path = "/usr/local/lib/";
    std::unordered_map<std::string, soHandle> _so;
    PluginMgr() = default;
    PluginMgr(PluginMgr &&) = delete;
    PluginMgr(const PluginMgr &) = delete;
    PluginMgr &operator=(PluginMgr &&) = delete;
    PluginMgr &operator=(const PluginMgr &) = delete;

   public:
    static PluginMgr &Inst() {
        static PluginMgr mgr;
        return mgr;
    }
    /**
     * @brief Set the Lib Path object
     *
     * @param path
     */
    void SetLibPath(const char *path) { _lib_path = path; }
    /**
     * @brief load element plugin
     *
     * @tparam T create function prototype
     * @param file so file base name
     * @param mode dlopen mode
     * @param name function name
     * @return T* function pointer
     */
    template <class T>
    T *LoadPlugin(const char *file, int mode, const char *name) {
        std::string so_path = (_lib_path / file).string();
        auto it = _so.find(so_path);
        soHandle so;
        if (it == _so.end()) {
            void *handle = dlopen(so_path.c_str(), mode);
            if (handle) {
                so = soHandle(handle, dlclose);
                _so[so_path] = so;
            } else {
                std::cerr << "dlopen(" << file << ") failed: " << dlerror() << std::endl;
                return nullptr;
            }
        } else {
            so = it->second;
        }
        if (so) {
            T *func = reinterpret_cast<T *>(dlsym(so.get(), name));
            if (func) {
                return func;
            }
            std::cerr << "dlsym(" << name << ") failed!" << std::endl;
        }
        return nullptr;
    }
};

extern "C" {
bool __attribute__((visibility("default"))) gIsHaveVo = false;
}

static int fd_;

class SignalHandler {
   public:
    using Callback = std::function<void()>;

    static void registerExitSignals(const std::vector<int> &signals, SignalHandler::Callback callback) {
        for (int signum : signals) {
            registerHandler(signum, callback);
        }
    }

    static void registerHandler(int signum, Callback callback) {
        getCallback(signum) = callback;

        struct sigaction sa;
        sa.sa_handler = &SignalHandler::signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(signum, &sa, nullptr) == -1) {
            throw std::runtime_error("Failed to register signal handler");
        }
    }

    using Action = std::function<void(siginfo_t *, void *)>;

    static void registerAction(int signum, Action action) {
        getCallback(signum) = action;

        struct sigaction act;
        act.sa_sigaction = &SignalHandler::actionHandler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO;

        if (sigaction(signum, &act, nullptr) == -1) {
            throw std::runtime_error("Failed to register signal action");
        }
    }

   private:
    static void signalHandler(int signum) {
        std::lock_guard<std::mutex> lock(getMutex());
        if (auto fn = std::get_if<Callback>(&getCallback(signum))) {
            (*fn)();
        }
    }
    static void actionHandler(int signum, siginfo_t *info, void *ucontext) {
        std::lock_guard<std::mutex> lock(getMutex());
        if (auto fn = std::get_if<Action>(&getCallback(signum))) {
            (*fn)(info, ucontext);
        }
    }

    static std::variant<Callback, Action> &getCallback(int signum) {
        static std::map<int, std::variant<Callback, Action>> callback;
        return callback[signum];
    }

    static std::mutex &getMutex() {
        static std::mutex mutex;
        return mutex;
    }
};

static void my_backtrace() {
    const int N = 100;
    void *array[100];
    int size = backtrace(array, N);
    backtrace_symbols_fd(array, size, fd_);
}

int option_parser(int argc, char *argv[], CPipeLine *pipe) {
    CElement *prev = NULL;
    bool linkFlag = false;

    for (int argcIndex = 1; argcIndex < argc;) {
        if (!strcmp(argv[argcIndex], "perfstat_interval")) {
            argcIndex++;
            int perfstat_interval = atoi(argv[argcIndex++]);
            pipe->SetStatInterval(perfstat_interval);
        } else if (!strcmp(argv[argcIndex], "config_path")) {
            argcIndex++;
            set_config_path(argv[argcIndex++]);
        } else if (!strcmp(argv[argcIndex], "lib_path")) {
            argcIndex++;
            PluginMgr::Inst().SetLibPath(argv[argcIndex++]);
        } else if (!strcmp(argv[argcIndex], "EsAvDemux")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.loopnum = 1;
            param.name = "EsAvDemux" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int, int)>(
                "libes_plavdemux.so", RTLD_NOW, "createEsAvDemuxElement");
            if (create_func) {
                CElement *avdemux = NULL;
                std::thread([&pipe, &avdemux, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    avdemux = create_func(param.name.c_str(), param.path.c_str(), param.loopnum, param.dieIndex);
                    gElementNameMap[param.name] = avdemux;
                    pipe->AddToPipeline(avdemux, NULL);
                }).join();

                if (linkFlag) printf("EsAvDemux element cannot have prev element!!\n");
                prev = avdemux;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsVdec")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsVdec" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plvdec.so", RTLD_NOW, "createEsVdecElement");
            if (create_func) {
                CElement *decoder = NULL;
                std::thread([&pipe, &decoder, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    decoder = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = decoder;
                    pipe->AddToPipeline(decoder, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, decoder, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = decoder;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsMux")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.timeout = 40;  // ms
            param.name = "EsMux" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(int, const char *, int, int)>(
                "libes_plmux.so", RTLD_NOW, "createEsMuxElement");
            if (create_func) {
                CElement *EsMux = NULL;
                std::thread([&pipe, &EsMux, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsMux = create_func(param.timeout, param.name.c_str(), param.muxPoolSize, param.dieIndex);
                    gElementNameMap[param.name] = EsMux;
                    pipe->AddToPipeline(EsMux, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsMux, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsMux;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsDualMux")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.timeout = 100;
            param.name = "EsDualMux" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(int, const char *, int)>(
                "libes_pldualmux.so", RTLD_NOW, "createEsDualMuxElement");
            if (create_func) {
                CElement *EsDualMux = NULL;
                std::thread([&pipe, &EsDualMux, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsDualMux = create_func(param.timeout, param.name.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsDualMux;
                    pipe->AddToPipeline(EsDualMux, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsDualMux, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsDualMux;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsQueue")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsQueue" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(int, int, const char *, int)>(
                "libes_plqueue.so", RTLD_NOW, "createEsQueueElement");
            if (create_func) {
                CElement *EsQueue = NULL;
                std::thread([&pipe, &EsQueue, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsQueue = create_func(param.depth, param.type, param.name.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsQueue;
                    pipe->AddToPipeline(EsQueue, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsQueue, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsQueue;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsPreProcess")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsPreProcess" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plpreprocess.so", RTLD_NOW, "createEsPreProcessElement");
            if (create_func) {
                CElement *EsPreProcess = NULL;
                std::thread([&pipe, &EsPreProcess, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsPreProcess = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsPreProcess;
                    pipe->AddToPipeline(EsPreProcess, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsPreProcess, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsPreProcess;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsInfer")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsInfer" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plinfer.so", RTLD_NOW, "createEsInferElement");
            if (create_func) {
                CElement *EsInfer = NULL;
                std::thread([&pipe, &EsInfer, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsInfer = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsInfer;
                    pipe->AddToPipeline(EsInfer, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsInfer, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsInfer;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsFaceSelect")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsFaceSelect" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plfaceselect.so", RTLD_NOW, "createEsFaceSelectElement");
            if (create_func) {
                CElement *EsFaceSelect = NULL;
                std::thread([&pipe, &EsFaceSelect, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsFaceSelect = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsFaceSelect;
                    pipe->AddToPipeline(EsFaceSelect, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsFaceSelect, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsFaceSelect;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsPostProcess")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsPostProcess" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plpostprocess.so", RTLD_NOW, "createEsPostProcessElement");
            if (create_func) {
                CElement *EsPostProcess = NULL;
                std::thread([&pipe, &EsPostProcess, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsPostProcess = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsPostProcess;
                    pipe->AddToPipeline(EsPostProcess, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsPostProcess, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsPostProcess;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsDemux")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsDemux" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, int)>("libes_pldemux.so", RTLD_NOW,
                                                                                           "createEsDemuxElement");
            if (create_func) {
                CElement *EsDemux = NULL;
                std::thread([&pipe, &EsDemux, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsDemux = create_func(param.name.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsDemux;
                    pipe->AddToPipeline(EsDemux, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsDemux, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsDemux;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsTee")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsTee" + to_string(idx++);
            param.elementType = VIDEO_DECODER;
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, int, int)>(
                "libes_pltee.so", RTLD_NOW, "createEsTeeElement");
            if (create_func) {
                CElement *EsTee = NULL;
                std::thread([&pipe, &EsTee, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsTee = create_func(param.name.c_str(), param.elementType, param.dieIndex);
                    gElementNameMap[param.name] = EsTee;
                    pipe->AddToPipeline(EsTee, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsTee, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsTee;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsBufferSync")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsBufferSync" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, int)>(
                "libes_plbuffersync.so", RTLD_NOW, "createEsBufferSyncElement");
            if (create_func) {
                CElement *EsBufferSync = NULL;
                std::thread([&pipe, &EsBufferSync, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsBufferSync = create_func(param.name.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsBufferSync;
                    pipe->AddToPipeline(EsBufferSync, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsBufferSync, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsBufferSync;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsFrameRate")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.fps = -1;
            param.maxFrameRate = -1;
            param.timeWnd = 1000;
            param.name = "EsFrameRate" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(int, int, uint, const char *, int)>(
                "libes_plframerate.so", RTLD_NOW, "createEsFrameRateElement");
            if (create_func) {
                CElement *EsFrameRate = NULL;
                std::thread([&pipe, &EsFrameRate, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsFrameRate =
                        create_func(param.fps, param.maxFrameRate, param.timeWnd, param.name.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsFrameRate;
                    pipe->AddToPipeline(EsFrameRate, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsFrameRate, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsFrameRate;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsVenc")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsVenc" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plvenc.so", RTLD_NOW, "createEsVencElement");
            if (create_func) {
                CElement *EsVenc = NULL;
                std::thread([&pipe, &EsVenc, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsVenc = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsVenc;
                    pipe->AddToPipeline(EsVenc, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsVenc, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }

                prev = EsVenc;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsAdec")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsAdec" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_pladec.so", RTLD_NOW, "createEsAdecElement");
            if (create_func) {
                CElement *EsAdec = NULL;
                std::thread([&pipe, &EsAdec, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsAdec = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsAdec;
                    pipe->AddToPipeline(EsAdec, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsAdec, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }

                prev = EsAdec;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsVideoGrid")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsVideoGrid" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int, int)>(
                "libes_plvideogrid.so", RTLD_NOW, "createEsVideoGridElement");
            if (create_func) {
                CElement *EsVideoGrid = NULL;
                std::thread([&pipe, &EsVideoGrid, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsVideoGrid =
                        create_func(param.name.c_str(), param.path.c_str(), param.dieIndex, param.startPadIndex);
                    gElementNameMap[param.name] = EsVideoGrid;
                    pipe->AddToPipeline(EsVideoGrid, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsVideoGrid, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsVideoGrid;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsIpcGrid")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsIpcGrid" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int, int, int)>(
                "libes_plipcgrid.so", RTLD_NOW, "createEsIpcGridElement");
            if (create_func) {
                CElement *EsIpcGrid = NULL;
                std::thread([&pipe, &EsIpcGrid, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsIpcGrid = create_func(param.name.c_str(), param.path.c_str(), param.ipcCapacity,
                                            param.startPadIndex, param.dieIndex);
                    gElementNameMap[param.name] = EsIpcGrid;
                    pipe->AddToPipeline(EsIpcGrid, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsIpcGrid, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsIpcGrid;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsVideoDualGrid")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsVideoDualGrid" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plvideodualgrid.so", RTLD_NOW, "createEsVideoDualGridElement");
            if (create_func) {
                CElement *EsVideoDualGrid = NULL;
                std::thread([&pipe, &EsVideoDualGrid, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsVideoDualGrid = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsVideoDualGrid;
                    pipe->AddToPipeline(EsVideoDualGrid, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsVideoDualGrid, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsVideoDualGrid;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsOsd")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsOsd" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plosd.so", RTLD_NOW, "createEsOsdElement");
            if (create_func) {
                CElement *EsOsd = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                gElementNameMap[param.name] = EsOsd;
                pipe->AddToPipeline(EsOsd, NULL);
                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsOsd, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsOsd;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsTestSrc")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.fps = 30;
            param.loopnum = 100;
            param.name = "EsTestSrc" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int, int, int)>(
                "libes_pltestsrc.so", RTLD_NOW, "createEsTestSrcElement");
            if (create_func) {
                CElement *EsTestSrc = NULL;
                std::thread([&pipe, &EsTestSrc, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsTestSrc =
                        create_func(param.name.c_str(), param.path.c_str(), param.fps, param.loopnum, param.dieIndex);
                    gElementNameMap[param.name] = EsTestSrc;
                    pipe->AddToPipeline(EsTestSrc, NULL);
                }).join();

                if (linkFlag) printf("EsTestSrc element cannot have prev element!!\n");

                prev = EsTestSrc;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsTestSink")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsTestSink" + to_string(idx++);
            param.elementType = VIDEO_OUTPUT;
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, int, int)>(
                "libes_pltestsink.so", RTLD_NOW, "createEsTestSinkElement");
            if (create_func) {
                CElement *EsTestSink = NULL;
                std::thread([&pipe, &EsTestSink, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsTestSink = create_func(param.name.c_str(), param.elementType, param.dieIndex);
                    gElementNameMap[param.name] = EsTestSink;
                    pipe->AddToPipeline(EsTestSink, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsTestSink, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsTestSink;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsTracker")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsTracker" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_pltracker.so", RTLD_NOW, "createEsTrackerElement");
            if (create_func) {
                CElement *EsTracker = NULL;
                std::thread([&pipe, &EsTracker, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsTracker = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsTracker;
                    pipe->AddToPipeline(EsTracker, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsTracker, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }

                prev = EsTracker;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsCompare")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsCompare" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plcompare.so", RTLD_NOW, "createEsCompareElement");
            if (create_func) {
                CElement *EsCompare = NULL;
                std::thread([&pipe, &EsCompare, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsCompare = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsCompare;
                    pipe->AddToPipeline(EsCompare, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsCompare, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }

                prev = EsCompare;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsFileSink")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsFileSink" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plfilesink.so", RTLD_NOW, "createEsFileSinkElement");
            if (create_func) {
                CElement *EsFileSink = NULL;
                std::thread([&pipe, &EsFileSink, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsFileSink = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsFileSink;
                    pipe->AddToPipeline(EsFileSink, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsFileSink, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }

                prev = EsFileSink;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsVideoSink")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsVideoSink" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plvideosink.so", RTLD_NOW, "createEsVideoSinkElement");
            if (create_func) {
                CElement *EsVideoSink = NULL;
                std::thread([&pipe, &EsVideoSink, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsVideoSink = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsVideoSink;
                    pipe->AddToPipeline(EsVideoSink, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsVideoSink, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsVideoSink;
                linkFlag = false;
            }
            gIsHaveVo = true;
        } else if (!strcmp(argv[argcIndex], "EsOpenVo")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsOpenVo" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plopenvo.so", RTLD_NOW, "createEsOpenVoElement");
            if (create_func) {
                CElement *OpenVoElement = NULL;
                std::thread([&pipe, &OpenVoElement, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    OpenVoElement = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = OpenVoElement;
                    pipe->AddToPipeline(OpenVoElement, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, OpenVoElement, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = OpenVoElement;
                linkFlag = false;
            }
            gIsHaveVo = true;
        } else if (!strcmp(argv[argcIndex], "EsAudioSink")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsAudioSink" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plaudiosink.so", RTLD_NOW | RTLD_GLOBAL, "createEsAudioSinkElement");
            if (create_func) {
                CElement *EsAudioSink = NULL;
                std::thread([&pipe, &EsAudioSink, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsAudioSink = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsAudioSink;
                    pipe->AddToPipeline(EsAudioSink, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsAudioSink, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsAudioSink;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsAvSync")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsAvSync" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plavsync.so", RTLD_NOW | RTLD_GLOBAL, "createEsAvSyncElement");
            if (create_func) {
                CElement *EsAvSync = NULL;
                std::thread([&pipe, &EsAvSync, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsAvSync = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsAvSync;
                    pipe->AddToPipeline(EsAvSync, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsAvSync, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }
                prev = EsAvSync;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsAvMux")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsAvMux" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plavmux.so", RTLD_NOW, "createEsAvMuxElement");
            if (create_func) {
                CElement *EsAvMux = NULL;
                std::thread([&pipe, &EsAvMux, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsAvMux = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsAvMux;
                    pipe->AddToPipeline(EsAvMux, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsAvMux, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }

                prev = EsAvMux;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsTts")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsTts" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_pltts.so", RTLD_NOW, "createEsTtsElement");
            if (create_func) {
                CElement *EsTts = NULL;
                std::thread([&pipe, &EsTts, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    EsTts = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = EsTts;
                    pipe->AddToPipeline(EsTts, NULL);
                }).join();

                if (linkFlag) {
                    if (prev != NULL)
                        pipe->LinkMany(prev, EsTts, NULL);
                    else
                        printf("%s prev cannot be NULL\n", param.name.c_str());
                }

                prev = EsTts;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "EsV4l2Src")) {
            argcIndex++;
            static int idx = 0;
            ElementParam param;
            param.name = "EsV4l2Src" + to_string(idx++);
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            auto create_func = PluginMgr::Inst().LoadPlugin<CElement *(const char *, const char *, int)>(
                "libes_plv4l2src.so", RTLD_NOW, "createEsV4l2SrcElement");
            if (create_func) {
                CElement *esV4l2Src = NULL;
                std::thread([&pipe, &esV4l2Src, &create_func, &param]() {
                    pipe->setDieAffinety(param.dieIndex);
                    esV4l2Src = create_func(param.name.c_str(), param.path.c_str(), param.dieIndex);
                    gElementNameMap[param.name] = esV4l2Src;
                    pipe->AddToPipeline(esV4l2Src, NULL);
                }).join();
                if (linkFlag) printf("EsV4l2Src element cannot have prev element!!\n");

                prev = esV4l2Src;
                linkFlag = false;
            }
        } else if (!strcmp(argv[argcIndex], "element")) {
            argcIndex++;
            ElementParam param;
            CElement *present;
            argcIndex = element_option_parser(argv, argcIndex, argc, &param);
            numa_set_preferred(param.dieIndex);
            present = gElementNameMap[param.name];

            if (linkFlag) {
                if (prev != NULL)
                    pipe->LinkMany(prev, present, NULL);
                else
                    printf("%s prev cannot be NULL\n", param.name.c_str());
            }
            prev = gElementNameMap[param.name];
            linkFlag = false;
        } else if (!strcmp(argv[argcIndex], "!")) {
            argcIndex++;
            linkFlag = true;
        } else {
            static int idx = 0;
            std::string name = argv[argcIndex++];
            if (name.length() > 2) {
                ElementParam param;
                argcIndex = element_option_parser(argv, argcIndex, argc, &param);
                numa_set_preferred(param.dieIndex);
                while (param.name.empty()) {
                    param.name = "noname" + std::to_string(idx++);
                    if (gElementNameMap.find(param.name) != gElementNameMap.end()) {
                        param.name.clear();
                    }
                }
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
#if __cplusplus >= 202002L  // C++20
                if (name.starts_with("es"))
#else
                if (name.compare(0, 2, "es") == 0)
#endif
                {
                    name.replace(0, 2, "pl");
                }
                name = "libes_" + name + ".so";
                auto createElement = PluginMgr::Inst().LoadPlugin<CElement *(const ElementParam *)>(
                    name.c_str(), RTLD_NOW, "createElement");
                if (createElement) {
                    CElement *current = nullptr;
                    std::thread([&pipe, &current, createElement, &param]() {
                        pipe->setDieAffinety(param.dieIndex);
                        current = createElement(&param);
                        gElementNameMap[param.name] = current;
                        pipe->AddToPipeline(current, nullptr);
                    }).join();
                    if (linkFlag) {
                        if (prev != NULL) {
                            pipe->LinkMany(prev, current, NULL);
                        } else {
                            std::cout << param.name << " prev cannot be NULL" << std::endl;
                        }
                        linkFlag = false;
                    }
                    prev = current;
                }
            }
        }
    }
    return 1;
}

void release_globle_param() {
    map<string, CElement *>::iterator it;
    for (it = gElementNameMap.begin(); it != gElementNameMap.end(); it++) {
        delete it->second;
    }
    gElementNameMap.erase(gElementNameMap.begin(), gElementNameMap.end());
}

int main(int argc, char *argv[]) {
    // fd_ = open("espl_launch.trace", O_RDWR|O_CREAT, 777);
    auto pipe = std::make_unique<CPipeLine>();
#if 0
    SignalHandler::registerAction(SIGSEGV, [](siginfo_t *si, void *){
        uint64_t address = uint64_t(si->si_addr);
        my_backtrace();
        printf("si_addr:%lu\n", address);
        exit(1);
    });
#endif
    SignalHandler::registerExitSignals({SIGINT, SIGTERM}, [&pipe]() { pipe->notifyExit(); });

    option_parser(argc, argv, pipe.get());

    if (pipe->Init() != APP_SUCCESS) {
        std::cout << " pipeline init failed !!!!" << std::endl;
        pipe->Finish();
        return 0;
    }

    pipe->Start();
    std::cout << " will start WaitForFinish " << std::endl;
    pipe->WaitForFinish();
    std::cout << " will start Finish " << std::endl;
    pipe->Finish();
    std::cout << " will delete pipe " << std::endl;
    // release_globle_param();
    std::cout << "  pipe end " << std::endl;
    return 0;
}
