#ifndef _TRACKER_LITE_ELEMENT_H_
#define _TRACKER_LITE_ELEMENT_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "batch_meta.h"
#include "element.h"
#include "pipeline_tracker.h"

class TrackerLiteElement : public CElement {
   public:
    TrackerLiteElement(const char *name, const char *config, int dieIndex)
        : CElement(name, config, dieIndex) {}
    ~TrackerLiteElement() override;

    app_ret Init() override;
    app_ret ProcessData(CBaseMeta *baseMeta,
                        CElement const *previousElement = nullptr) override;
    app_ret Finish() override;

   private:
    tracker_handle_t *getOrCreateTracker(const std::string &streamId);
    void destroyTrackers();

    tracker_config_t config_{};
    std::mutex mutex_;
    std::unordered_map<std::string, tracker_handle_t *> trackers_;
};

#endif
