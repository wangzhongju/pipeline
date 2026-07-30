# Media-Agent Replacement Architecture

## 1. Purpose

This document describes the current architecture of the EIC7700 Pipeline
service that replaces `media-agent`.

The implementation preserves the official hardware data path for RTSP ingest,
decode, preprocessing, inference, and DSP postprocessing. Only the required
platform-facing business capabilities are implemented in this repository:

- platform configuration receive and ACK;
- heartbeat and stream counts;
- task start, stop, and model reload;
- package decryption and runtime configuration generation;
- tracking and event decisions;
- snapshot and video evidence;
- alarm relay and platform reporting.

The replacement does not link to the `media-agent` source tree or import its
decoder, inference scheduler, CPU postprocessor, or RTSP recorder.

## 2. Deployment and Process Model

Platform mode deploys one executable:

```text
pipeline_agent
```

The same executable has two roles:

```text
pipeline_agent
├── manager process
│   ├── platform protobuf/UDS client
│   ├── incremental desired-state store
│   ├── worker reconciliation
│   ├── heartbeat
│   └── alarm forwarding
└── pipeline_agent --worker
    ├── one isolated stream/scenario/package execution unit
    └── official Pipeline plugin graph
```

The manager owns all child processes. A worker is started with `fork + exec`,
and it is stopped with `SIGTERM`, a bounded wait, then `SIGKILL` only after the
grace period expires.

`espl_launch` remains available for legacy static cases but is not required by
platform mode.

## 3. High-Level Data Flow

```text
smart-guard-edge
       |
       | framed protobuf over Unix stream socket
       v
+--------------------------+
| pipeline_agent manager   |
| IpcClient / TaskManager  |
+-----+--------------------+
      | fork/exec worker
      v
+------------------------------------------------------------------+
| EsAvDemux                                                       |
|    | compressed packet                                           |
|    +--> EsEvidenceRecorder --> per-stream GOP/record state        |
|    |                                                             |
|    v                                                             |
| EsVdec -> EsMux -> EsQueue -> EsPreProcess -> EsInfer            |
|                                      -> EsPostProcess(DSP)        |
|                                      -> EsTrackerLite             |
|                                      -> EsEvent -> EsTestSink     |
+------------------------------------------------+-----------------+
                                                 |
                                                 | AlarmInfo datagram
                                                 v
                                   manager AlarmRelayServer
                                                 |
                                                 v
                                        platform send queue
```

Display output is intentionally absent. `EsVideoSink` is not part of the
platform graph, so a monitor or DRM output is not a runtime dependency.

## 4. Platform Protocol

The protocol is defined in:

```text
src/business/proto/media-agent.proto
```

Transport framing:

```text
4-byte magic 0xDEADBEEF
4-byte big-endian protobuf length
serialized Envelope
```

Supported inbound messages:

- `MSG_CONFIG`: incremental task start, update, or stop;
- `MSG_ALG_MODEL_UPDATE`: reload all or selected scenarios.

Supported outbound messages:

- `MSG_ACK`;
- `MSG_HEARTBEAT`;
- `MSG_ALARM`.

`SocketSender` reconnects to the platform socket and owns independent send and
receive threads. Pipeline callbacks never write directly to the platform
stream socket.

## 5. Incremental Desired State

The platform normally sends one device task per configuration message. A
message is therefore treated as an incremental update, not a complete global
snapshot.

The manager maintains:

```text
active_streams: map<stream_id, StreamConfig>
```

Merge semantics:

```text
enabled=true  -> insert or replace this stream_id
enabled=false -> erase this stream_id
absent stream -> preserve its previous state
```

Rapid updates are debounced:

- apply after `config_debounce_ms` of inactivity; or
- apply when `config_max_wait_ms` is reached.

This prevents a burst of platform messages from repeatedly rebuilding worker
state.

## 6. Worker Identity and Reconciliation

The execution key is:

```text
stream_id | model_scenario_code | package_path
```

This key deliberately prevents cross-device algorithm aggregation. A device
failure, task stop, URL update, or package update must not restart unrelated
devices.

For each desired worker, the manager also stores the serialized `StreamConfig`
as a signature. Reconciliation follows these rules:

1. stop workers whose keys are no longer desired;
2. retain workers with an unchanged key, signature, and live PID;
3. restart only workers whose configuration changed;
4. start only new workers;
5. reload only workers selected by a model-update message.

A platform task contains one stream and may contain several algorithms. If the
algorithms use different scenario/package pairs, the current implementation
creates one isolated worker per pair. They are still controlled by the single
manager process.

## 7. VDEC Group Allocation

Each worker receives a non-overlapping VDEC group range. Existing workers keep
their offsets when another task starts or stops.

The allocator scans the range `[0, 128)` and chooses the first available
contiguous region. A region is returned when its worker exits.

Stable VDEC allocation is essential for:

- branch-local task updates;
- correlating `/proc/esmap/dec` with a worker;
- preventing group collisions;
- avoiding global decode restart.

## 8. Package and Model Lifecycle

`AlgorithmConfig.model_config_name` is an absolute package path.

Worker startup:

1. decrypt and validate the package;
2. extract package files into a worker-specific runtime directory;
3. read the package root JSON;
4. locate the package-declared `.model`;
5. read preprocessing, classes, thresholds, and output scales;
6. generate Pipeline YAML files;
7. start the worker graph.

The package is the only source of model data. There is no board-side model
fallback or hard-coded replacement.

An EIC7700 YOLOv8 detection package contains:

- runtime JSON;
- compiled `.model`;
- `esquant/table.json`;
- output order file;
- package manifest.

The current DSP `ES_AK_DSP_DetectionOut` contract is:

- exactly three NCHW outputs;
- stride order 8, 16, and 32;
- `64 + class_count` channels;
- signed 16-bit output tensors;
- one `int16_step` per output;
- raw DFL box logits and class logits;
- sigmoid performed by the DSP postprocessor.

The old six-output box/class split is not supported by this graph.

## 9. Generated Worker Configuration

Each worker owns a directory under the manager runtime directory:

```text
<runtime>/<scenario>_<stable-id>/
├── model/
├── labels.txt
├── agent-config.pb
├── EsAvDemux_1.yaml
├── EsVdec.yaml
├── EsPreProcess.yaml
├── EsInfer.yaml
├── EsPostProcess.yaml
├── EsTrackerLite.yaml
└── EsEvent.yaml
```

The directory is removed and recreated only when that worker starts.

Important mappings:

| Platform/package field | Worker configuration |
| --- | --- |
| `rtsp_url`, `stream_id` | `EsAvDemux_*.yaml` |
| model input size | VDEC scale and PreProcess output |
| preprocess padding | PreProcess letterbox padding |
| package `.model` | `EsInfer.yaml` |
| class names | `labels.txt` |
| threshold and output scales | `EsPostProcess.yaml` |
| full stream algorithms | `agent-config.pb` |
| snapshot/record directories | Evidence service configuration |

The current preprocessing sampling interval is one inference frame for every
three decoded frames.

## 10. Worker Plugin Graph

For each stream/scenario/package execution unit:

```text
EsAvDemux
  -> EsEvidenceRecorder
  -> EsVdec
  -> EsMux
  -> EsQueue
  -> EsPreProcess
  -> EsInfer
  -> EsQueue
  -> EsPostProcess
  -> EsTrackerLite
  -> EsEvent
  -> EsTestSink
```

### 10.1 EsAvDemux

Pulls RTSP and attaches stable stream identity, keyframe state, and packet
timing metadata.

### 10.2 EsEvidenceRecorder

Receives compressed packets before decode and maintains recording state. It
creates event clips without opening a second RTSP session and without
re-encoding the video.

### 10.3 EsVdec, EsPreProcess, EsInfer, EsPostProcess

Use the EIC7700 media, NPU, and DSP path. Preprocessing performs letterbox and
normalization. Detection postprocessing uses the vendor DSP operator.

### 10.4 EsTrackerLite

Adapts postprocessed objects to the self-contained ByteTrack implementation.
Tracker state is isolated by stream and worker lifetime.

### 10.5 EsEvent

Applies threshold, ROI, scheduling, temporal, and dedup rules from the fixed
worker configuration. It creates an `AlarmInfo` only after an event is
triggered.

### 10.6 EsTestSink

Terminates the service graph without display output.

## 11. Evidence Architecture

### 11.1 Video

Video evidence uses compressed packets from `EsAvDemux`:

- preserve keyframe and timing metadata;
- cache the required pre-event GOP window;
- append post-event packets;
- remux to MPEG-TS;
- avoid decode and re-encode;
- use per-stream recording state.

This path minimizes CPU, NPU, VENC, and memory-bandwidth cost.

### 11.2 Snapshot

The event element selects an original-resolution frame rather than the
512x512 inference tensor. Detection coordinates are mapped back from
letterbox space before evidence is generated.

The current `EvidenceService` maps NV12 and uses FFmpeg software JPEG encoding
on an evidence path. It does not encode every frame, but alarm bursts can still
create CPU and memory-bandwidth spikes.

An on-demand hardware JPEG worker remains a planned optimization.

## 12. Alarm Path

```text
EsEvent
  -> build AlarmInfo
  -> AlarmRelayClient (Unix datagram)
  -> manager AlarmRelayServer
  -> IpcClient bounded send queue
  -> platform protobuf socket
  -> platform persistence and publication
```

The local datagram separates worker lifetime from the manager's platform
connection. A worker cannot write directly to the shared platform stream.

Evidence filenames are attached to `AlarmInfo` before it enters the relay.

## 13. Threading and Backpressure

The control and data planes are separated:

- platform receive thread parses and enqueues configuration;
- manager loop reconciles desired state;
- worker Pipeline threads execute media callbacks;
- evidence work runs outside NPU/DSP callbacks where possible;
- alarm relay uses a Unix datagram;
- platform sending uses a bounded queue.

No inference callback may block waiting for a platform reconnect.

Current bounded resources:

- platform send queue;
- Pipeline queue depths;
- VDEC, Mux, PreProcess, and Infer output pools;
- per-worker process and VDEC group;
- evidence packet/record state.

## 14. MMZ and Pool Model

MMZ/VB memory is not represented by worker RSS. Operational monitoring must
use `/proc/eswin/vb`.

Default pool settings:

| Pool | Default |
| --- | ---: |
| VDEC | 2 |
| Mux | 8 |
| PreProcess | 12 |
| Infer output | 8 |

These are per worker. Increasing a value must be evaluated against the maximum
task count.

Resource lifecycle:

1. manager starts a worker;
2. worker creates media pools;
3. worker receives `SIGTERM`;
4. Pipeline elements stop and release VB blocks;
5. manager waits for process exit;
6. VDEC offset becomes reusable.

Creating a replacement before the old worker exits can temporarily double
MMZ usage and is intentionally avoided.

## 15. Performance Model

At 25 FPS input and preprocessing interval 3:

```text
VDEC rate per stream: approximately 25 FPS
NPU input per stream: approximately 8.33 FPS
13-stream nominal VDEC rate: 325 FPS
13-stream nominal NPU input: 108.3 FPS
```

NPU and DSP use asynchronous submissions. Short `es_hw_watcher` samples and
per-process `top` samples are expected to vary. Long-window counters are the
correct throughput measurement.

Production mode keeps worker logging low. Per-element performance output must
be explicitly enabled for diagnostics.

## 16. Failure Isolation

The manager remains alive when a worker exits. It reaps the child and runs
reconciliation again.

Isolation guarantees currently provided:

- one task update does not rewrite other active stream state;
- one worker stop does not signal other workers;
- VDEC groups do not overlap;
- model extraction directories do not overlap;
- tracker and event state do not cross workers;
- platform reconnect does not stop inference workers.

Failures still local to a worker include RTSP connection, package validation,
model initialization, pool allocation, and plugin startup.

## 17. Observability

Manager log events:

- platform connect/reconnect;
- configuration receive and ACK;
- incremental merge and active stream count;
- worker start, stop, timeout, and exit;
- desired/actual reconciliation;
- model reload;
- heartbeat and alarm send failures.

System sources:

- `/proc/eswin/vb`: MMZ/VB pools and free memory;
- `/proc/esmap/dec`: VDEC cumulative counters;
- `/opt/eswin/bin/es_hw_watcher`: VDEC/NPU/DSP utilization;
- `pidstat`: long-window process CPU;
- platform service journal: alarm receive, persist, and publish.

## 18. Security and Filesystem Requirements

The Pipeline service should run as the unprivileged board user.

Required permissions:

- platform UDS writable by the Pipeline user;
- model package readable;
- runtime directory writable;
- snapshot and record directories writable;
- Pipeline libraries and executable readable/executable.

Running the whole process as root is not a replacement for correct socket and
storage permissions.

Decrypted models exist only in worker runtime directories and are deleted when
that worker directory is recreated or the runtime tree is cleaned.

## 19. Current Limitations

- a changed task restarts its affected worker rather than hot-swapping a graph
  branch in place;
- multiple events on one stream may load separate model contexts;
- snapshots currently use software JPEG;
- alarm delivery has a bounded in-memory queue but no durable disk spool;
- recording timestamps still depend on complete upstream PTS/DTS propagation;
- storage quota and startup recovery require further production hardening;
- heartbeat currently reports active desired streams as successful and does
  not yet expose detailed per-worker health.

## 20. Acceptance Tests

Before production release:

1. start and stop one task through the platform;
2. add a second task without changing the first worker PID;
3. stop and restore one task while other VDEC counters continue;
4. run the maximum target channel count and monitor MMZ;
5. stop all tasks and verify VB pool release;
6. verify three-output S16 model ABI;
7. verify original-resolution snapshot;
8. verify playable event clip;
9. verify alarm receive, persistence, and publication;
10. verify platform socket reconnect;
11. verify worker crash isolation;
12. verify selected model reload;
13. run long-duration RTSP stability testing;
14. inspect all queues and logs for sustained backpressure.

Operational commands and the local platform simulator are documented in
`case/platform/run.md`.
