# Pipeline Replacement Architecture

## 1. Goal

Use the official EIC7700 `pipeline` implementation for RTSP ingest, hardware
decode, preprocessing, inference and postprocessing. Migrate the required
business capabilities into this repository:

- platform task/configuration receive and ACK;
- heartbeat and runtime health reporting;
- tracking and event decisions;
- alarm deduplication and alarm reporting;
- alarm snapshot and video evidence lifecycle.

The custom inference, decode, infer scheduler, publish buffer and SEI path from
`media-agent` must not be linked into the new data plane.

## 2. Target Process Model

The replacement is one deployable service with two internal planes:

```text
                         platform local service
                                  |
                           protobuf over UDS
                                  |
                    +-------------v-------------+
                    | PipelineAgent control     |
                    | config/ACK/heartbeat      |
                    +------+------+-------------+
                           |      |
                 RCU config|      |bounded alarm queue
                           |      |
  RTSP compressed packets  |      v
       |                   |  AlarmDispatcher
       v                   |      |
  EsAvDemux ---> EsVdec ---> EsMux ---> Pre ---> Infer ---> Post
       |                                              |
       | encoded packet tap                           v
       +-----------------------> EvidenceStore <--- EsEvent
                                                         ^
                                                     EsTrackerLite
```

`PipelineAgent` is the executable and lifecycle owner. Pipeline elements remain
data-plane plugins. Network and disk I/O never run in an inference callback.

## 3. Modules

### 3.1 PipelineAgent

Owns the pipeline-local implementation migrated from these legacy modules:

- `src/ipc/IpcClient.*`;
- `src/ipc/SocketSender.*`;
- `src/protocol/MessageMapper.*`;
- the config diff, heartbeat and alarm dedup business logic from
  `src/pipeline/Pipeline.*`.

It does not own a decoder or inference implementation.

Responsibilities:

- receive `AgentConfig` and return an ACK;
- validate stream IDs, URLs, model mappings and evidence directories;
- publish immutable configuration generations to plugins;
- create, stop or replace stream branches;
- report total/success/failed stream counts;
- accept alarm triggers from `EsEvent`;
- wait for evidence results with a bounded timeout and report `AlarmInfo`;
- persist unsent alarms to a small disk spool for retry.

The EIC7700 hardware pipeline must run as the unprivileged `ubuntu` user.
The platform UDS must therefore grant that user write permission. Production
deployment should create
`/opt/smart-guard/run/media-agent/media_agent.sock` as `0660` with a shared
service group (preferred), or apply an equivalent ACL after each socket
creation. Running the complete pipeline as root is not a substitute because
the vendor VPS initialization uses user-scoped IPC resources.

An initial configuration creates the graph. A transport change (`rtsp_url`,
codec or enabled state) replaces only the affected input branch. Event
threshold, ROI, alarm level, dedup interval and evidence duration are hot
updates and must not restart decode or inference.

If the current pipeline core cannot safely remove a running element, generation
1 may rebuild the whole graph on transport changes. The final implementation
must add branch-scoped stop/remove before claiming zero-interruption hot update.

### 3.2 EsTrackerLite

Input: `CBatchMeta` after `EsPostProcess`.

Output: the same `CBatchMeta`, with `CObjectMeta::trackerId`,
`trackerBboxInfo` and `trackerConfidence` populated.

Implementation:

- adapt `CObjectMeta` to `tracker_detection_t`;
- use the self-contained ByteTrack source in `src/algorithm/tracker`;
- keep one tracker handle per `streamId`;
- process frames of the same stream serially;
- reset state when a stream is disabled, reconnects or changes generation.

The existing official `EsTracker` reads image data and uses the legacy tracker
API. It should not be enabled at the same time as `EsTrackerLite`.

### 3.3 EsEvent

Input: tracked `CBatchMeta`.

Output: lightweight `AlarmTrigger` objects to a bounded non-blocking queue.

Implementation:

- adapt `CObjectMeta` to `event_object_t`;
- use the self-contained event engine in `src/algorithm/event`;
- read a shared immutable `StreamRuntimeConfig`;
- own one event handle/state set per stream;
- perform ROI, threshold and temporal event decisions;
- never create JPEG/video files and never send protobuf on the pipeline thread;
- drop or coalesce duplicate queue entries when downstream is overloaded.

Tracking and event decision are separate plugins because they have different
state reset rules and can be tested independently. They may share an adapter
library for normalized boxes and stream identity.

### 3.4 EvidenceStore

Evidence handling is an asynchronous service, not a synchronous inference
plugin. It has two inputs:

1. a compressed packet tap immediately after `EsAvDemux`;
2. an alarm frame reference and `AlarmTrigger` from `EsEvent`.

Per stream it owns:

- a bounded encoded GOP ring;
- active post-alarm recording sessions;
- a bounded snapshot worker queue;
- storage quotas and atomic temporary-file rename.

No additional RTSP connection is allowed.

## 4. Media Strategy

### 4.1 Video clips

Use the `media-agent::Recorder` design principle, but consume packets from
`EsAvDemux`:

- retain the latest complete GOP per stream;
- on alarm, start from the cached keyframe;
- append post-alarm packets until the requested duration;
- remux without decoding or re-encoding;
- write to a hidden temporary file, close the muxer, then atomically rename.

Preferred container on this device is MPEG-TS for the write path:

- no final MP4 index rewrite;
- survives abrupt power loss better;
- accepts H.264/H.265 Annex-B naturally;
- low CPU and no VENC/VB load.

If the platform requires MP4, remux the completed TS asynchronously or use
fragmented MP4. Do not use `faststart` on the real-time path because it rewrites
the file at close.

The packet metadata must preserve stream ID, keyframe flag, original PTS/DTS,
duration and time base. The current foundational fields are in
`core/include/video.h`. Before enqueueing, packet bytes must be copied or
reference-counted because the FFmpeg `AVPacket` storage is invalid after the
demux callback returns.

### 4.2 Alarm snapshots

Do not reuse `media-agent::Snapshotter` as the primary EIC7700 path. It converts
NV12 through `libswscale` and performs software MJPEG encoding, which adds CPU
and memory bandwidth pressure.

Preferred path:

- retain the alarm `CFrameMeta`/VB block using reference counting;
- submit only alarm frames to an on-demand hardware JPEG encoder worker;
- optionally draw alarm boxes in a dedicated evidence surface;
- release the VB reference immediately after JPEG completion;
- fall back to software JPEG only when the hardware encoder is unavailable.

Continuous `EsVenc` JPEG encoding is also rejected because it encodes every
frame while only a tiny fraction are alarm evidence.

## 5. Threading And Backpressure

Pipeline callbacks have strict bounded work:

- tracker and event processing run synchronously but perform no I/O;
- packet tap copies into a bounded per-stream GOP ring;
- alarm enqueue is non-blocking;
- snapshot, mux close/rename, protobuf send and retry run on workers.

Required queue policies:

| Queue | Full policy |
| --- | --- |
| alarm triggers | coalesce same stream/scenario/track, then drop oldest |
| snapshot requests | keep first and latest per alarm window |
| encoded GOP ring | evict oldest complete GOP |
| platform send queue | drop stale heartbeat first; persist alarms |

Disk I/O errors disable evidence for that stream but must not block inference.

## 6. Stable Identity And Timestamps

`streamId` is the business primary key. Element names and `padIndex` are runtime
implementation details and must never be sent to the platform.

Each demuxed packet and decoded frame carries:

- `streamId`;
- monotonically increasing frame/packet index;
- media PTS;
- wall-clock timestamp captured at ingest;
- pipeline configuration generation.

Alarm, snapshot and recording requests use the same stream ID and generation.
Results from an older generation are discarded after a stream restart.

## 7. Configuration Mapping

Platform `AlgorithmConfig.model_scenario_code` is mapped to a local model
profile. Multiple event scenarios may share one inference profile. Therefore:

- model loading is keyed by model profile/version, not event name;
- event rules remain per stream and per scenario;
- ROI, threshold, alarm level and schedule remain business configuration;
- a model update creates a new profile generation, warms it, then switches.

Invalid model paths or unsupported scenarios produce a negative ACK with a
specific reason. They must not terminate the running generation.

## 8. Delivery Phases

### Phase A: data contract and static graph

- propagate stable stream IDs and original packet timing;
- add `EsTrackerLite` and `EsEvent`;
- reuse platform protobuf IPC and asynchronous alarm dispatch;
- run a statically declared multi-stream graph;
- report alarms without evidence, then enable snapshots and TS clips.

Acceptance: same platform task and alarm protocol as `media-agent`; no
additional RTSP sessions; pipeline throughput remains within 5% of the OD
baseline.

### Phase B: runtime graph control

- create `PipelineAgent` graph ownership;
- apply runtime-only configuration without restart;
- replace one stream branch on URL/codec changes;
- add reconnect and branch health state.

Acceptance: changing one stream does not interrupt other streams.

### Phase C: production evidence and recovery

- hardware JPEG snapshots;
- GOP pre-record and post-record TS;
- storage quota, temporary files and startup recovery;
- durable alarm spool and retry;
- process/service packaging.

Acceptance: power interruption leaves no visible partial evidence file and
alarms are retried after reconnect.

## 9. Performance Gates

Use the existing 26-channel one-hour baseline and compare:

- VDEC and NPU throughput must not regress by more than 5%;
- no pipeline callback may block on socket or disk I/O;
- alarm-disabled memory must remain flat after warmup;
- encoded GOP cache has a configured hard byte limit;
- all VB blocks return after stream stop;
- queue depth, dropped/coalesced alarm count, evidence latency and IPC retry
  count are observable;
- run 26 channels for at least 8 hours after evidence is enabled because the
  current one-hour test showed slow RSS growth that needs longer observation.

## 10. Non-Goals

- importing `media-agent` RTSP puller, decoder, infer scheduler or detector;
- opening a second RTSP session for recording;
- re-encoding alarm video;
- doing filesystem or platform I/O inside `EsPostProcess`, tracker or event
  callbacks;
- treating a process-wide pipeline restart as the final hot-update design.

## 11. Current Implementation Status

Phase A foundations implemented in this tree:

- `streamId`, keyframe and original packet timing propagation from
  `EsAvDemux` through decoded frames;
- `EsTrackerLite`, backed only by the ByteTrack C API;
- `EsEvent`, backed only by eventEdge plus the existing protobuf/UDS platform
  protocol;
- platform configuration receive, ACK, heartbeat, event deduplication and
  alarm enqueue;
- optional activation from `od_pipeline_rtsp_stress.sh` with
  `ENABLE_BUSINESS=1`;
- RISC-V cross-build and install rules for the two plugins and their business
  libraries.

The following items are intentionally not claimed as complete replacement yet:

- platform-driven creation/removal of RTSP graph branches;
- stream health-aware heartbeat counts;
- model-update generation switching;
- encoded GOP evidence tap and TS recorder;
- on-demand hardware JPEG snapshot worker;
- durable alarm spool and storage quota/recovery.

Until these items are implemented and tested on the board, Phase A is suitable
for protocol/event integration tests, not final production replacement of
`media-agent`.
