# Hardhat platform alarm case

This case runs the 512x512 YOLOv8 hardhat model without OSD, video grid, or
display output. Class `head` means that the detected person is not wearing a
hardhat and drives the platform scenario `hardhat-detection`.

The model binary must exist at:

```text
models/260106_hardhat_cls2_512_b1_v1.model
```

Run one platform stream:

```sh
STREAM_ID=platform_stream_id ./case/hardhat/hardhat_pipeline.sh
```

The default URL is `rtsp://127.0.0.1:554/pull/$STREAM_ID`. An explicit URL can
be provided as the second argument. Multiple streams are supplied as repeated
ID and URL pairs.

An accepted alarm prints:

```text
alarm queued: stream=... scenario=hardhat-detection targets=...
```

This confirms that inference, event evaluation, protobuf mapping, and IPC queue
submission succeeded. Confirm final platform receipt in the platform service
log by matching the same stream ID and scenario.
