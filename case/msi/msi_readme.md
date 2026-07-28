# msi_readme

本 README 面向 `pipeline/case/msi` 多阶段推理（Multi-Stage Inference）演示用例，帮助你：
- 快速理解本 case 的整体功能与数据流拓扑；
- 参考现有实现，按需定制为你的业务场景；
- 正确配置与运行，复现运行效果。

关联文件位置：
- 脚本：`pipeline/case/msi/multiStageInfer.sh`
- 配置：`pipeline/case/msi/config/*.yaml`

---

## 案例整体功能描述

本案例展示在单设备上以多路输入（脚本示例为 4 路）运行“检测 → 姿态（或第二阶段）”的多阶段 AI 推理流水线。第一阶段通过 `EsPreProcess`+`EsInfer`+`EsPostProcess` 完成通用目标检测；第二阶段对检测结果的感兴趣区域（ROI）再次进行 `EsPreProcess_rtmpose`+`EsInfer_rtmpose`+`EsPostProcess_rtmpose` 的精细化推理（例如关键点/姿态估计）。随后在 `EsOsd` 叠加检测框/关键点/标签，`EsVideoGrid` 进行多画面拼接输出，`EsVideoSink` 将结果送至显示设备，实现端到端的多路多阶段实时分析。

---

## 1. 功能与数据流拓扑

数据链路概览（以 `multiStageInfer.sh` 为准）：

```
EsAvDemux x4 -> EsVdec x4 ---> EsMux("mux1") -> EsQueue -> EsPreProcess -> EsInfer -> EsQueue -> EsPostProcess
                                                    -> EsQueue -> EsPreProcess_rtmpose -> EsInfer_rtmpose -> EsQueue -> EsPostProcess_rtmpose
                                                                                                              -> EsOsd -> EsQueue -> EsVideoGrid -> EsQueue -> EsVideoSink
```

元素在本 case 中的职责：
- EsAvDemux：读取多路输入（文件/IPC 等），输出压缩帧。脚本示例使用 4 路文件输入（`EsAvDemux_22/19/7/21.yaml`）。
- EsVdec：解码压缩帧为原始视频帧（如 NV12）。
- EsMux：汇聚多路解码帧，统一输出到后续流水线。
- EsQueue：上下游解耦的缓冲队列，用于抗抖与稳态吞吐。
- EsPreProcess：第一阶段的图像预处理（缩放/色彩/归一化）以适配检测模型输入。
- EsInfer：第一阶段推理（通用检测模型）。
- EsPostProcess：第一阶段后处理（得出目标类别、框、置信度等）。
- EsPreProcess_rtmpose：第二阶段预处理，通常对第一阶段输出的 ROI 进行裁剪/缩放以适配姿态模型输入。
- EsInfer_rtmpose：第二阶段推理（例如 RTMPose/关键点模型）。
- EsPostProcess_rtmpose：第二阶段后处理（解析关键点/骨架等结果）。
- EsOsd：叠加检测与姿态关键点等可视化信息。
- EsVideoGrid：多路画面网格拼接（例如 2×2 或 4×4，具体以配置为准）。
- EsVideoSink：视频输出（VO/HDMI），将拼接结果送显。

---

## 2. 运行方法

推荐直接使用脚本（设备端 Linux shell）：

```bash
sh pipeline/case/msi/multiStageInfer.sh
```

脚本内关键环境与参数（节选）：
- `PATH`：包含可执行 `espl_launch`
- `LD_LIBRARY_PATH`：包含运行期依赖的库路径
- `PL_LOG_LEVEL=4`：日志级别（INFO）
- `echo 1 > /proc/eswin/vb`：启用视频缓冲 VB
- `ulimit`：放开 core/文件句柄/锁内存/栈等限制
- `case_path=/opt/demo/pipeline/case/msi`：配置根目录
- `cloopnum=200000000`：循环读帧次数（近似“无穷”）

脚本核心调用（与源码一致的真实语法）：

```bash
espl_launch \
  perfstat_interval 10000000 \
  config_path $case_path/config/ \
  EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum - ! EsVdec -name decoder1  -path EsVdec.yaml - ! EsMux    -name mux1 -timeout 40 -poolsize 16 - \
  EsAvDemux -path EsAvDemux_19.yaml -loopnum $cloopnum - ! EsVdec -name decoder19 -path EsVdec.yaml - ! element -name mux1 - \
  EsAvDemux -path EsAvDemux_7.yaml  -loopnum $cloopnum - ! EsVdec -name decoder7  -path EsVdec.yaml - ! element -name mux1 - \
  EsAvDemux -path EsAvDemux_21.yaml -loopnum $cloopnum - ! EsVdec -name decoder21 -path EsVdec.yaml - ! element -name mux1 - \
  ! EsQueue      -name queuepreproc1 -type 0 -deepth 5 - \
  ! EsPreProcess -name preproc1      -path EsPreProcess.yaml - \
  ! EsInfer      -name infer1        -path EsInfer.yaml - \
  ! EsQueue      -name queuepost1    -type 0 -deepth 5 - ! EsPostProcess -name post1 -path EsPostProcess.yaml - \
  ! EsQueue      -name queuepreproc2 -type 0 -deepth 5 - \
  ! EsPreProcess -name preproc2      -path EsPreProcess_rtmpose.yaml - \
  ! EsInfer      -name infer2        -path EsInfer_rtmpose.yaml - \
  ! EsQueue      -name queuepost2    -type 0 -deepth 5 - ! EsPostProcess -name post2 -path EsPostProcess_rtmpose.yaml - \
  ! EsOsd        -name osd1          -path EsOsd.yaml - \
  ! EsQueue      -name queuegrid1    -type 0 -deepth 5 - \
  ! EsVideoGrid  -name videogrid1    -path EsVideoGrid.yaml - \
  ! EsQueue      -name queuevo1      -type 0 -deepth 5 - \
  ! EsVideoSink  -name vo1           -path EsVideoSink.yaml -
```

说明：
- `config_path` 为全局配置根目录，元素 `-path` 均为相对路径；
- 第一路创建 `EsMux -name mux1`，后续路用 `element -name mux1` 并入；
- `!` 结束一个元素的子参数段，`-` 为继续标记（与脚本风格相关）。

---

## 3. 配置文件总览与要点

配置目录：`pipeline/case/msi/config/`

输入与解码：
- `EsAvDemux_*.yaml`：指定输入源（文件/IPC），脚本示例为 4 路文件输入。
- `EsVdec.yaml`：解码输出格式/像素/色彩空间等。

第一阶段（检测）：
- `EsPreProcess.yaml`：输入尺寸、像素格式、归一化等；
- `EsInfer.yaml`：模型路径、异步设置（建议使用 `isAsync` 而不是 `inferType`）、输出池大小等；
- `EsPostProcess.yaml`：阈值、类别、NMS 等。

第二阶段（姿态/关键点等）：
- `EsPreProcess_rtmpose.yaml`：针对 ROI 的预处理参数；
- `EsInfer_rtmpose.yaml`：第二阶段模型路径与运行参数；
- `EsPostProcess_rtmpose.yaml`：解析关键点、骨架连线等。

可视化/拼接/显示与导出：
- `EsOsd.yaml`：框、关键点、骨架样式；
- `EsVideoGrid.yaml`：网格布局（如 2×2、4×4），输出分辨率；
- `EsVideoSink.yaml`/`EsVideoSink_4k.yaml`：显示模式与分辨率；
- 如需编码/落盘可参考 `EsVenc.yaml`、`EsVenc_jpeg.yaml`、`EsFileSink.yaml` 等。

其他：
- `labels.txt`：类别标签；
- `result.md5`：样例结果校验（如提供）。

---

## 4. 定制与扩展指南

- 调整路数与布局：
  - 增减 `EsAvDemux_X.yaml`，在脚本中对应添加/删除 `EsAvDemux + EsVdec` 组合；
  - 修改 `EsVideoGrid.yaml` 网格为 2×2/3×3/4×4 等，并匹配显示分辨率；
- 替换模型：
  - 第一阶段在 `EsInfer.yaml` 替换 `model-filepath` 并调整 `EsPreProcess.yaml` 与 `EsPostProcess.yaml`；
  - 第二阶段在 `EsInfer_rtmpose.yaml` 替换模型并同步 `EsPreProcess_rtmpose.yaml` 与 `EsPostProcess_rtmpose.yaml`；
- 性能与稳态：
  - 合理设置 `EsMux -poolsize`、各 `EsQueue -deepth`、`perfstat_interval`；
  - 在多 DIE/NUMA 设备上可按需设置元素 `-die` 亲和（若平台支持）；
- 可视化与导出：
  - 在 `EsOsd.yaml` 调整样式，必要时启用编码/文件落盘以便离线分析（`EsVenc*`、`EsFileSink.yaml`）。
- 插件化扩展：
  - 按 `pipeline/src/elements/*` 模式实现自定义元素并导出工厂；
  - 通过 pl_launch 令牌机制插入到脚本（详见 `pipeline/src/pl_launch/pl_launch_readme.md`）。

---

## 5. 常见问题与排错

- 无输出/黑屏：
  - 检查 `EsVideoSink.yaml` 与显示设备是否匹配；确认连接无误；
  - `EsVideoGrid.yaml` 的网格与输入路数不一致会导致空画面；
- 第二阶段无结果：
  - 核对第一阶段检测阈值是否过高导致无 ROI；
  - 检查 `EsPreProcess_rtmpose.yaml` 尺寸是否与第二阶段模型一致；
- 性能不足/卡顿：
  - 增大 `EsQueue -deepth`、`EsMux -poolsize`，降低统计频率；
  - 关闭不必要的 dump/日志；确认输入码率与存储/网络是否瓶颈；
- 动态库/元素找不到：
  - 确保 `espl_launch` 在 `PATH`，相关动态库在 `LD_LIBRARY_PATH`；
  - 元素令牌大小写与脚本一致，`-path` 相对 `config_path`。

---

## 6. 参考
- `pipeline/case/msi/multiStageInfer.sh`
- `pipeline/src/pl_launch/pl_launch_readme.md`
- `pipeline/src/core/core_readme.md` 与各元素 README
