# pl_launch_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 pl_launch 的核心代码逻辑与与 pipeline/core 的协作关系；
- 正确使用命令行参数组装并运行一条 Pipeline；
- 基于现有 pipeline/core/element 源码扩展自定义元素，并让 pl_launch 识别与装配。

目录结构（节选）：
- `pipeline/src/pl_launch/pl_launch.cpp`：入口 `main`，解析参数、动态创建元素、装配并运行管线
- `pipeline/src/core/include/pipeline.h`：`CPipeLine` 管线类（Add/Link/Init/Start/Wait/Finish）
- `pipeline/src/core/include/element.h`：`CElement` 元素基类接口
- `pipeline/case/*`：不同业务场景的脚本用法合集（espl_launch 参数示例）

---

## 1. 核心代码逻辑（pl_launch.cpp 概要）

pl_launch 的 `main` 负责“把命令行中的全局选项与元素令牌解析为真实的元素对象并装配为一条可运行的 Pipeline”。核心步骤如下：

1) 解析命令行参数（真实语法，来自 `option_parser` 与 `element_option_parser`）
- 全局选项（可出现在命令行任意位置，按需设置）：
  - `perfstat_interval <ms>`：设置性能统计周期（毫秒），调用 `CPipeLine::SetStatInterval()`
  - `config_path <DIR>`：设置配置根目录，后续元素的 `-path` 会拼接为 `CONFIG_PATH + path`
  - `lib_path <DIR>`：设置插件库搜索路径，默认 `/usr/local/lib/`
- 元素令牌（大小写与拼写需与源码一致）：每出现一次表示向管线追加一个元素；其后的若干子参数由 `element_option_parser` 解析，直到遇到分隔符 `!`（或 `-`）为止。
  - 支持的子参数（节选，见 `element_option_parser.cpp`）：
    - `-name <str>`、`-path <rel_yaml>`、`-timeout <ms>`、`-loopnum <int>`、`-fps <int>`、`-depth|-deepth <int>`、`-type <int>`、`-maxFrameRate <int>`、`-timeWnd <int>`、`-ipcCapacity <int>`、`-startPad <int>`、`-poolsize <int>`、`-elementType <int>`、`-die <0|1>`
  - 常见元素令牌与其动态库/工厂映射（来自源码）：
    - `EsAvDemux`  → `libes_plavdemux.so` / `createEsAvDemuxElement(name, path, loopnum, die)`
    - `EsVdec`     → `libes_plvdec.so`     / `createEsVdecElement(name, path, die)`
    - `EsMux`      → `libes_plmux.so`      / `createEsMuxElement(timeout, name, poolsize, die)`
    - `EsDualMux`  → `libes_pldualmux.so`  / `createEsDualMuxElement(timeout, name, die)`
    - `EsQueue`    → `libes_plqueue.so`    / `createEsQueueElement(depth, type, name, die)`
    - `EsPreProcess` → `libes_plpreprocess.so` / `createEsPreProcessElement(name, path, die)`
    - `EsInfer`    → `libes_plinfer.so`    / `createEsInferElement(name, path, die)`
    - `EsFaceSelect` → `libes_plfaceselect.so` / `createEsFaceSelectElement(name, path, die)`
    - `EsPostProcess` → `libes_plpostprocess.so` / `createEsPostProcessElement(name, path, die)`
    - `EsDemux`    → `libes_pldemux.so`    / `createEsDemuxElement(name, die)`
    - `EsTee`      → `libes_pltee.so`      / `createEsTeeElement(name, /*...*/)`

2) 创建 CPipeLine 实例并装配元素
- 每个元素令牌解析完成后（在内部已 `dlopen` 对应动态库并 `dlsym` 工厂），创建 `CElement*` 并调用 `pipeline.AddToPipeline(element, NULL)` 追加进管线序列。
- 链接规则：源码中在特定条件下调用 `pipeline->LinkMany(prev, current, NULL)` 建立前后相邻元素的链路；一般情况下，元素按出现顺序依次链接。

3) 初始化与启动
- `pipeline.Init()`：初始化日志/系统与视频缓冲（ES_SYS/ES_VB），并逐个 `element->Init()`。
- `pipeline.Start()`：依次 `element->Start()`，并开启性能统计定时器线程。

4) 运行、等待与清理
- `pipeline.WaitForFinish()`：等待退出事件（如 SIGINT 触发 `pipe->notifyExit()`）。
- `pipeline.Finish()`：逐个 `element->Finish()`，并退出系统层。

以上流程将 pl_launch 与 `pipeline/core` 打通：pl_launch 负责把“全局选项 + 元素令牌 → 元素对象 → 管线拓扑”，核心运行态与内存/线程模型由 `CPipeLine` 与 `CElement` 完成。

---

## 2. 命令行用法与实战示例（真实语法）

基本形态（元素间以 `!` 结束其子参数段）：
```bash
espl_launch \
  config_path ./pipeline/case \
  lib_path /usr/local/lib \
  perfstat_interval 1000 \
  EsAvDemux    -name demux0 -path avdemux.yml -loopnum 1 -die 0 ! \
  EsVdec       -name vdec0  -path h264_vdec.yml -die 0 ! \
  EsInfer      -name infer0 -path resnet50_infer.yml -die 0 ! \
  EsPostProcess -name post0 -path post.yml -die 0 ! \
  EsFaceSelect -name fs0    -path face_sel.yml -die 0 !
```

注意事项：
- `-path` 会与 `config_path` 拼接形成最终 YAML 路径。
- `-die` 的有效范围代码中限制为 0 或 1（超出会被夹紧为 [0,1]）。
- 不同元素支持的子参数略有差异，详见各元素 README 与其实现。

更多可参考：
  - `pipeline/case/od/od_pipeline.sh`
  - `pipeline/case/msi/multistageinfer.sh`
  - `pipeline/case/dualdie/*.sh`

Windows/路径注意：如需在 Windows/MSYS 环境下运行，请根据构建产物实际路径调整可执行与 YAML 的相对/绝对路径。

---

## 3. 与编码方式的对照（直接编码构建 Pipeline）

若不使用 pl_launch，也可以在 C++ 中手写装配流程，步骤与 pl_launch 保持一致：
```cpp
#include "pipeline.h"
#include "element.h"

int main() {
    CPipeLine pipeline;

    // 1) 创建元素实例（通常经由工厂函数）
    CElement* src   = createEsV4l2SrcElement("v4l2Src",   "./cam_src.yml", 0); // 伪代码
    CElement* vdec  = createEsVdecElement  ("vdec",       "./h264_vdec.yml", 0);
    CElement* infer = createEsInferElement ("infer",      "./resnet50.yml", 0);
    CElement* osd   = createEsOsdElement   ("osd",        "./osd.yml", 0);
    CElement* sink  = createEsVideoSinkElement("videosink","./hdmi_vo.yml", 0);

    // 2) 组装
    pipeline.AddToPipeline(src);
    pipeline.AddToPipeline(vdec);
    pipeline.AddToPipeline(infer);
    pipeline.AddToPipeline(osd);
    pipeline.AddToPipeline(sink);
    pipeline.LinkMany(src, vdec, infer, osd, sink, nullptr);

    // 3) 生命周期
    pipeline.Init();
    pipeline.Start();
    pipeline.WaitForFinish();
    pipeline.Finish();
    return 0;
}
```
核心与 pl_launch 相同：Add → Link → Init → Start → Wait → Finish。区别在于元素获取方式（编码中直接调用工厂函数；pl_launch 通过 `dlopen + dlsym` 根据令牌解析）。

---

## 4. 扩展自定义元素，让 pl_launch 识别并装配

要在 pl_launch 命令中使用自定义元素 `<YourElement>`，需要完成：

1) 实现元素类
- 继承 `CElement`，实现 `Init/Start/ProcessData/Finish` 等接口，详见 `src/core/include/element.h`
- 正确处理 `CBaseMeta` 引用计数与 `TransMitToNextToProcess()` 的转发

2) 提供工厂导出符号
- 约定一个 C 接口导出，用于 pl_launch 通过“元素名 → 工厂函数”创建实例：
```cpp
extern "C" CElement* createEsYourElement(const char* name, const char* configFile, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(YourElement))) YourElement(name, configFile, dieIndex);
}
```
- 元素名与工厂名的映射应在注册表或加载器里登记，确保 `-e YourElement` 可解析到 `createEsYourElement`

3) 构建和部署
- 将自定义元素编译为与主程序链接的静态库，或编译为共享库并被 pl_launch 动态加载
- 若为共享库方式：
  - 确认库文件位于运行时可搜索路径（如 `LD_LIBRARY_PATH`/`PATH`/程序内设定的插件目录）
  - 确保导出符号未被 C++ name mangling 影响（`extern "C"`）

4) YAML 配置与多 DIE
- 为元素准备 YAML 配置文件；在命令行中用 `-c` 传入
- 如需 NUMA/多 DIE 亲和，命令中紧随其后添加 `-d <die>`，或在元素内部读取 `dieIndex` 做线程/池绑定

完成上述后，你即可：
```bash
espl_launch -e YourElement -c ./your_element.yml -e ...
```

---

## 5. 常见排错与最佳实践

- 元素找不到/创建失败：
  - 检查元素名是否与注册表一致；检查工厂函数是否正确导出；动态库是否在搜索路径
- 运行后无数据流动：
  - 检查元素顺序与 LinkMany 的链路；核对上游输出/下游输入的 Meta 类型是否匹配
- 推理/解码等底层资源失败：
  - 查看元素的 `Init()` 日志；核对 YAML 参数；确认 ES_ SDK 初始化顺序是否正确（由 `CPipeLine::Init()` 统一处理）
- 性能问题：
  - 使用 `perfStat()` 输出；使用 `MetaPool`/`BlockQueue`；根据 `dieIndex` 绑定线程减少跨 NUMA 访问

---

## 6. 参考与关联文档
- `pipeline/src/pl_launch/pl_launch.cpp`
- `pipeline/src/core/core_readme.md`（系统/VB 初始化、Meta/队列/内存池/性能模型）
- `pipeline/src/core/include/pipeline.h` / `pipeline/src/core/src/pipeline.cpp`
- `pipeline/src/core/include/element.h`
- `pipeline/case/*` 用法示例

