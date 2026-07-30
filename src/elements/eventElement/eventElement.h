#ifndef _EVENT_ELEMENT_H_
#define _EVENT_ELEMENT_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "batch_meta.h"
#include "element.h"
#include "pipeline_event.h"
#include "pipeline_agent/AlarmRelay.h"
#include "pipeline_agent/IpcClient.h"

class EventElement : public CElement {
   public:
    EventElement(const char *name, const char *config, int dieIndex)
        : CElement(name, config, dieIndex) {}
    ~EventElement() override;

    app_ret Init() override;
    app_ret Start() override;
    app_ret ProcessData(CBaseMeta *baseMeta,
                        CElement const *previousElement = nullptr) override;
    app_ret Finish() override;

   private:
    struct StreamEventState {
        event_handle_t *handle = nullptr;
        std::unordered_map<std::string, int64_t> lastAlarmMs;
    };

    void applyConfig(const AgentConfig &config);
    bool loadFixedConfig(const std::string &path);
    bool sendAlarm(AlarmInfo alarm);
    void heartbeatLoop();
    StreamEventState *getOrCreateState(const std::string &streamId);
    void destroyStates();

    std::string socketPath_;
    std::string agentId_;
    std::string eventConfigPath_;
    std::string fixedConfigPath_;
    std::string alarmRelayPath_;
    std::string scenarioFilter_;
    int sendQueueSize_ = 100;
    int heartbeatIntervalMs_ = 10000;

    std::unique_ptr<pipeline::agent::IpcClient> ipc_;
    std::unique_ptr<pipeline::agent::AlarmRelayClient> alarmRelay_;
    std::atomic<bool> stop_{false};
    std::thread heartbeatThread_;

    std::mutex configMutex_;
    std::unordered_map<std::string, StreamConfig> streamConfigs_;

    std::mutex eventMutex_;
    std::unordered_map<std::string, StreamEventState> eventStates_;
};

#endif
