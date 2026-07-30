# Platform-managed pipeline

`platform_agent.sh` starts the single platform-facing control process. It
receives `AgentConfig`, decrypts every event model package, groups streams by
scenario and model, and runs the official pipeline inference plugins.

The runtime layout is:

- `pipeline_agent`: platform IPC, desired-state reconciliation, heartbeats.
- `espl_launch`: isolated inference worker for one scenario/model group.
- `EsEvidenceRecorder`: encoded GOP cache and event-triggered TS recording.
- `EsEvent`: tracking/event decision, JPEG snapshot, local alarm relay.

Start on the board:

```sh
cd /home/ubuntu/workspace/test/pipeline
./case/platform/platform_agent.sh
```

The platform starts and stops a device task through `StreamConfig.enabled`.
Changing its URL, event list, model package, or receiving a model-update
message restarts only the affected worker group.
