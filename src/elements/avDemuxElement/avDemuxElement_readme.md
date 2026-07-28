# avDemuxElement_readme

本说明面向使用与二次开发者，帮助你：
- 快速理解 `avDemuxElement` 的核心代码逻辑与在 Pipeline 中的角色；
- 熟悉与本 element 相关联的 SDK API 调用方式；
- 两种使用方式：通过编码方式构建 pipeline、通过 `espl_launch` 命令参数构建 pipeline；
- 基于源码定制自己的 element 插件。

---

## 1. 功能与定位
`avDemuxElement` 负责从本地文件或网络流（如 RTSP）中读入多媒体封装数据，并进行“解复用”（Demux）：
- 识别并分离视频码流（H.264/H.265/MJPEG 等）与音频码流（如 AAC）；
- 对 H.264/H.265 容器内码流进行必要的 bitstream filter（如 mp4/FLV/mkv 容器转 Annex-B）；
- 以 `CVideoPacketMeta`（以及音频数据）形式向下游 element 发送数据包，供解码、显示或编码等环节继续处理。

注意：本 element 使用 FFmpeg 完成网络/文件 IO 与容器解析，不直接调用多媒体 SDK 的解码接口（这一步通常在下游 `EsVdec` 等 element 中完成）。

---

## 2. 核心代码逻辑
源码位置：`pipeline/src/elements/avDemuxElement/`
- 头文件：`avDemuxElement.h`
- 源文件：`avDemuxElement.cpp`
- 配置范例：`avDemuxElement_config.yml`、`avdemux.yaml`

关键结构：`AVDEMUX_PARAM_S`（`avDemuxElement.h`）
```c++
typedef struct {
    char            streamName[MAX_STREAM_NAME_LEN]; // 输入源（文件路径或 URL）
    // video
    PAYLOAD_TYPE_E  videotype;                       // 视频编码类型（PT_H264/PT_H265/PT_MJPEG/...）
    ES_S32          width, height;                   // 视频宽高（探测得出）
    // audio
    PAYLOAD_TYPE_E  audiotype;                       // 音频编码类型（如 PT_AAC）
    ADEC_MODE_E     Mode;                            // 音频解码模式（若涉及）
    ES_U32          frame_size, num_channels, sample_rate; // 音频参数
    int             profile;                         // AAC profile 等
    int             outfps;                          // 输出帧率整形（可选）
    int             totalFrame;                      // 总帧数上限（可选）
    int             isIpc;                           // 是否 IPC 模式（可选）
    CElement*       element;                         // 回指本 element
} AVDEMUX_PARAM_S;
```

执行主线：`plStartSendStream()` 线程函数（`avDemuxElement.cpp`）
- 打开输入：`get_stream()` 内部通过 FFmpeg（`avformat_alloc_context/avformat_open_input`）打开 `file/url`，并寻找音视频流下标；
- 可选 Bsf：根据 `videotype` 选择 `h264_mp4toannexb / hevc_mp4toannexb` 做 Annex-B 码流整形；
- 帧循环：`av_read_frame()` 拉取音/视频包；
  - 当命中视频流：
    - 若限制总帧数 `totalFrame`，在达到阈值后退出；
    - 根据 `outfps` 进行简单节流；
    - 申请 `CVideoPacketMeta`（来自 `vpacketPool`），填充码流地址、大小、时间戳等，调用 `TransMitToNextToProcess()` 发往下游；
  - 当命中音频流：可按需封装为音频 meta 并发送（具体以工程版本为准）。
- 循环/重连：源读完或异常时，按 `mLoopNum`/`leftFrame` 逻辑控制是否重连与循环播放。

辅助能力：
- `DEMUXER_ProbeMeta()`：打开输入并探测分辨率/编码类型/音频信息；
- 日志与性能：使用 `app_info/app_warn/app_error` 以及 `PerformanceStatic` 记录吞吐耗时；
- CPU/NUMA：在启动时绑定线程至指定 DIE（`set_thread_affinity()`）。

---

## 3. 与 SDK API 的关系（仅列出以 ES_ 开头的真实 SDK API）
本 element 本身不直接调用解码、显示等多媒体 SDK 接口，主要使用的是 FFmpeg API；但在整个 Pipeline 生命周期中，会由 core 层完成 SDK 初始化与公共内存（VB）管理。为方便你在阅读/扩展时快速对齐 SDK 触点，下面按“Pipeline 生命周期”与“常见下游”两类列出相关的真实 SDK API（全部以 `ES_` 开头）。

3.1 Pipeline 生命周期相关（来自 `pipeline/src/core/src/pipeline.cpp` 等）
- 系统初始化/退出：
  - `ES_SYS_SetLogCfgPath`
  - `ES_SYS_Init`
  - `ES_SYS_Exit`
- 视频缓冲（VB）配置与初始化：
  - `ES_VB_GetConfig`, `ES_VB_SetConfig`, `ES_VB_Init`
- VB 资源申请与释放（内存池/块）：
  - `ES_VB_CreatePool`, `ES_VB_GetBlock`, `ES_VB_ReleaseBlock`, `ES_VB_DestroyPool`
  - 可选：`ES_VB_AllocIOVA`（需要 IOVA 时）

说明：在 `pipeline/src/core/src/pl_mem_wrap.cpp` 中存在 `PL_ES_VB_*` 封装，但本 README 不展示 `PL_ES_` 名称；其底层即映射到上述 `ES_VB_*` 真实 SDK API。

3.2 常见下游 element 所涉及的 SDK（供你串联整条链路时参考）
- 视频解码（在 `EsVdec` 等 element 中）：
  - 示例 API：`ES_VDEC_SendStream`, `ES_VDEC_GetFrame`, `ES_VDEC_ReleaseFrame`
- 编码/显示/图像处理等模块：根据你的实际下游 element 查阅对应 README 与源码。

小结：`avDemuxElement` 与 SDK 的“直接接触面”较少，更多是通过 core 完成系统/VB 初始化，并把码流元数据交给下游由其调用 `ES_` 解码/处理 API。

---

## 4. 配置与参数
参考 `avDemuxElement_config.yml`：
```yaml
# 输入源：二选一（file 或 url）。
file: ""          # 例如：/data/test.mp4 或 /data/test.h264
# url: ""         # 例如：rtsp://user:pass@192.168.1.10/stream1

# 输出帧率整形（可选），不设置按源保持
outfps: 25

# 读取总帧数上限（可选）
totalframe: 0

# 调试转储（可选）
dump:
  enable: 0
```
重要：代码中还会在运行期填充 `AVDEMUX_PARAM_S` 的探测结果（如 `videotype/width/height/audiotype/sample_rate/...`）。

---

## 5. 两种使用方式

5.1 通过编码方式构建 Pipeline（参考 `src/pl_launch/pl_launch.cpp` 的 `main()` 与 `option_parser`）
- `pl_launch` 负责：
  - 解析命令行参数（如 `config_path`、各 element 的 `-path`、`-die`、`-loopnum` 等）；
  - 动态装载各 element 的 so（如 `libes_plavdemux.so`），调用工厂方法创建 element；
  - 调用 `CPipeLine::AddToPipeline` 加入流水线，并按 `LinkMany` 将各 element 串接；
  - 启动、等待与关闭。
- 若你需要直接在代码中组装链路，可参考 `pl_launch.cpp` 中 `EsAvDemux` 分支：
  ```text
  // 伪代码流程
  void* handle = dlopen("/usr/local/lib/libes_plavdemux.so", RTLD_NOW);
  auto create = dlsym(createEsAvDemuxElement);
  CElement* demux = create(name, (CONFIG_PATH + path).c_str(), loopnum, dieIndex);
  pipe->AddToPipeline(demux, NULL);
  // ... 再创建下游（如 EsVdec/EsMux/EsQueue ...）并 LinkMany 串接
  ```

5.2 通过 `espl_launch` 命令参数构建 Pipeline（参考 `pipeline/case` 下各脚本与 `codec/config/EsAvDemux_*.yaml`）
- 常见参数：
  - `config_path <dir>`：配置文件根目录（便于通过相对路径传递 `-path`）
  - `lib_path <dir>`：element 动态库目录（默认 `/usr/local/lib/`）
  - 逐段声明 element 与其参数，例如：
    ```bash
    espl_launch \
      config_path /path/to/pipeline/case/codec/config \
      EsAvDemux -path EsAvDemux_1.yaml -loopnum 1 -die 0 ! \
      EsVdec   -path EsVdec.yaml       -die 0 ! \
      EsMux    -timeout 40 -poolsize 16 -die 0 !
    ```
  - 多路/多段链路可按脚本样例扩展。
- 示例配置文件可参考：`pipeline/case/codec/config/EsAvDemux_*.yaml`。

---

## 6. 二次开发：如何基于源码定制自己的 element 插件
1) 工程结构与工厂
- 新建目录 `pipeline/src/elements/<YourElement>`；
- 以 `CElement` 为基类，实现 `Init/Start/ProcessData/TransMitToNextToProcess/Finish/InfoQuery` 等；
- 暴露工厂函数，例如：
  ```c++
  extern "C" CElement* createEsYourElement(const char* name, const char* path, int dieIndex);
  ```
- 确保生成动态库并被 `pl_launch` 通过 `dlopen` 正确装载。

2) 配置管理
- 在 `Init()` 中解析 YAML（可参考本 element 的 `avDemuxElement_config.yml` 与读取逻辑）；
- 尽量把外部可调参数放入 YAML，结构与命名遵循现有风格（小写、下划线/驼峰统一）。

3) 与 SDK 的交互规范
- README 中仅暴露真实的 `ES_` SDK API 名称，不暴露 `PL_ES_`；
- 若需要公共内存池/块，直接调用：
  - `ES_VB_CreatePool/ES_VB_GetBlock/ES_VB_ReleaseBlock/ES_VB_DestroyPool`（必要时 `ES_VB_AllocIOVA`）；
- 若涉及解码/编码/显示，请查阅对应模块的 `ES_` API（如 `ES_VDEC_*`），并在你的 element 中进行封装调用；
- 遵循 pipeline 的 `Meta` 传递约定（如使用 `CVideoPacketMeta`/`CFrameMeta` 等），并在合适时机 `TransMitToNextToProcess()`。

4) 性能与健壮性
- 使用 `PerformanceStatic` 记录时延、吞吐；
- 线程 CPU 亲和与 NUMA：按 `pl_launch` 传入的 `-die` 参数进行亲和设置；
- 网络流重连与循环播放：参考 `plStartSendStream()` 逻辑；
- 日志等级与路径通过 core 的系统初始化及 log 配置控制（参见 3.1 中的 `ES_SYS_*`）。

---

## 7. 常见问题（FAQ）
- 为什么 README 没有列出 `PL_ES_...` API？
  - 因为这些是工程内部封装名。为便于你在独立工程或别的环境中复用，本 README 仅列出真实 SDK 的 `ES_` API，并在第 3 节给出其所在位置与调用时机。
- `avDemuxElement` 是否依赖硬件解码？
  - 不依赖。它只做容器解复用与码流整形；硬件/软件解码在下游。
- Windows 与 Linux 路径差异？
  - YAML 与命令行中的路径请按你的运行环境填写。`pipeline/case` 下脚本以 Linux shell 为主，你也可以在 Windows 上用等价命令行参数调用 `espl_launch`。

---

## 8. 参考文件
- `pipeline/src/elements/avDemuxElement/avDemuxElement.h/.cpp`
- `pipeline/src/pl_launch/pl_launch.cpp`, `src/pl_launch/element_option_parser.cpp`
- `pipeline/src/core/src/pipeline.cpp`, `src/core/src/pl_mem_wrap.cpp`
- `pipeline/case/codec/config/EsAvDemux_*.yaml`
