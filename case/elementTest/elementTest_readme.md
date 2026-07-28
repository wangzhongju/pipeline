# elementTest_readme

本 README 面向 `pipeline/case/elementTest` 元素级自测（Element UT）演示用例，帮助你：
- 快速理解本 case 的整体功能与数据流拓扑；
- 参考现有实现，独立自测任一 Element（无需依赖上游已完成），加速开发联调；
- 正确配置与运行，快速体验运行效果。

关联文件位置：
- 自测脚本：`pipeline/case/elementTest/test_case.sh`
- 配置：`pipeline/case/elementTest/config/*.yaml`
- 简要说明：`pipeline/case/elementTest/readme.md`

---

## 案例整体功能描述

本案例通过内置的测试源 `EsTestSrc` 读取本地 YUV 数据并构造完整的视频帧/批次结构体，驱动下游 Element 完成“预处理 → 推理 → 后处理”的典型链路，或仅跑其中单个/部分环节。终端使用 `EsTestSink` 采集并验证输出。这样即便上游真实元素（如解码器/多路汇聚）尚未完成，也能独立开发和验证目标元素的功能与性能。

---

## 1. 功能与数据流拓扑

脚本 `test_case.sh` 提供 4 种常用自测链路（仅 Case01 默认开启）：

- Case01：预处理 + 推理 + 后处理
```
EsTestSrc -> EsPreProcess -> EsInfer -> EsPostProcess -> EsTestSink
```

- Case02：仅预处理
```
EsTestSrc -> EsPreProcess -> EsTestSink
```

- Case03：仅推理
```
EsTestSrc -> EsInfer -> EsTestSink
```

- Case04：仅后处理
```
EsTestSrc -> EsPostProcess -> EsTestSink
```

元素在本 case 中的职责：
- EsTestSrc：读取本地 YUV，并填充视频帧/Batch 结构，提供可控的上游输入；其配置在 `EsTestsrc.yaml`，多个模式互斥（详见下文“配置要点”）。
- EsPreProcess：将输入帧进行缩放/色彩空间/归一化等转换，使之满足模型输入；配置见 `EsPreProcess.yaml`。
- EsInfer：加载模型并进行 NPU 推理；配置见 `EsInfer.yaml`。
- EsPostProcess：对推理输出执行阈值/NMS/解析等，生成结构化结果；配置见 `EsPostProcess.yaml`。
- EsTestSink：用于校验/打印/统计下游输出，便于本地排错与验证。

---

## 2. 运行方法

方式 A：直接运行脚本（默认 Case01）
```bash
# 设备端 Linux shell
cd pipeline/case/elementTest
sh test_case.sh
```
脚本要点：
- 统一设置 PATH、PL_LOG_LEVEL 与 ulimit；
- 默认启用 Case01：
```bash
espl_launch config_path ./config/ \
  EsTestSrc -name src0 -path EsTestsrc.yaml -loopnum 10 -fps 1000 - \
  ! EsPreProcess -name preproc1 -path EsPreProcess.yaml - \
  ! EsInfer -name infer1 -path EsInfer.yaml - \
  ! EsPostProcess -name post1 -path EsPostProcess.yaml - \
  ! EsTestSink -
```

方式 B：切换到其它自测链路
- 打开 `test_case.sh`，注释/取消注释 Case02/Case03/Case04 对应的 `espl_launch` 片段，仅保留一段；
- 也可将 Case01 的中间环节注释掉，形成自定义链路；

方式 C：最小命令（以仅预处理为例）
```bash
espl_launch config_path ./config/ \
  EsTestSrc -name src0 -path EsTestsrc.yaml -loopnum 10 -fps 1000 - \
  ! EsPreProcess -name preproc1 -path EsPreProcess.yaml - \
  ! EsTestSink -
```

---

## 3. 配置文件总览与要点

- `EsTestsrc.yaml`（重要）：
  - 该文件内提供多种输入模式，配置项之间“互斥”，必须保证“仅有一个 enable:true”，其余均为 false；
  - 可配置 YUV 文件路径、分辨率、像素格式、循环次数等，模拟真实上游；
- `EsPreProcess.yaml`：
  - 输入/输出尺寸、色彩空间、归一化参数等应与模型预期一致；
- `EsInfer.yaml`：
  - 必填：`model-filepath`（模型路径）；
  - 可选：`unique-id`、`die-id`、`inferOutputPoolSize`、`dumpflag`、`isAsync`（注意：参数名为 `isAsync`，不是 `inferType`）；
- `EsPostProcess.yaml`：
  - 得分阈值、NMS、类别映射等需与模型匹配；

提示：请确保 `espl_launch` 在 PATH 中，依赖动态库已就绪；

---

## 4. 定制与扩展指南

- 自测目标切换：
  - 仅需替换链路中的目标 Element 与其对应的 YAML，即可在同一框架下自测新元素；
- 模型与性能：
  - 在 `EsInfer.yaml` 替换 `model-filepath`，并根据设备负载调节 `isAsync`、`inferOutputPoolSize`；
- 预处理对齐：
  - `EsPreProcess.yaml` 中的尺寸/像素/归一化必须与模型训练配置一致，避免推理偏差；
- 批量/帧率控制：
  - 通过 `EsTestSrc` 的 `-fps`、循环次数、输入尺寸调整数据压力，观察稳态吞吐；
- 日志与排错：
  - 适当提高 `PL_LOG_LEVEL`，并开启 `dumpflag` 以 dump 中间结果（注意磁盘空间）。

---

## 5. 常见问题与排错

- 无输出或报错：
  - 检查 `EsTestsrc.yaml` 是否存在多个同时 `enable:true` 的配置；
- 推理无结果/异常：
  - 核对 `EsPreProcess.yaml` 是否与模型预期对齐；
  - 检查 `EsInfer.yaml` 中 `model-filepath` 存在性与 `isAsync`/`inferOutputPoolSize` 设置；
- 结果异常：
  - 调整 `EsPostProcess.yaml` 的阈值/NMS；
- 性能不足：
  - 降低输入尺寸/帧率，或减少循环次数；

---

## 6. 参考
- `pipeline/case/elementTest/test_case.sh`
- `pipeline/src/pl_launch/pl_launch_readme.md`
- `pipeline/src/core/core_readme.md` 与各元素 README
