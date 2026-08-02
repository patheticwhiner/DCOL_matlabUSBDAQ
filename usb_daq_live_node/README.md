# USB DAQ Live Node

面向恒凯科技 USB-DAQ V5.2L 的跨平台 C++ 实时采集程序，不依赖 MATLAB。Windows
动态加载仓库中的 `Usb_Daq_V52_Dll.dll`；Linux 在同一可执行程序中直接使用 libusb
后端，不需要另建 `linux_usb_daq` 应用。

设计参考 `DCOL-ANC-Tools/audio_udp_node`：采集线程只负责 USB 读取和落盘，GUI
线程只负责控制和波形显示，两者通过线程安全的滚动历史缓冲协作。

## 图形界面

GUI 与参考工程 `audio_udp_node` 使用相同的 SDL2 + Dear ImGui + ImPlot 技术栈。
程序默认采用 Light 主题，并提供右上角 `Dark mode` 按钮用于即时切换；界面使用
系统字体并支持高 DPI 显示。启动时程序读取鼠标所在显示器的物理分辨率、
可用工作区和 DPI，默认窗口约占工作区的 82%，不会自动最大化；字体、控件和侧栏
仍按 `DPI/96` 同步缩放。检测结果会显示在顶部状态栏和控制台。采集参数、通道
选择、运行状态和实时波形分区显示，其中通道始终按物理（软件）形式标注，例如
`AIN1 (AD0)`。

时间窗口提供三种模式：

- `Fit all (full history)`：通过固定内存的多分辨率概览，从 0 秒显示到当前采集时间；
  不再受 `historySeconds` 限制。概览只影响显示，原始落盘数据不会被抽取或平均。
- `Follow (fixed window)`：始终跟踪最新数据，窗口秒数由 `Follow window` 控制。
- `Free pan / zoom`：停止自动跟踪，可用鼠标自由平移、缩放；在其他模式下手动操作
  X 轴也会自动切换到 Free。最近 `historySeconds` 范围使用高分辨率环形缓存，更早
  的会话数据使用多分辨率概览。

波形显示可在 `Adaptive smooth line` 与 `Min/max envelope` 间切换。平滑只作用于
GUI 预览，不修改采集值或落盘数据。实测当前板卡在 ±5 V 量程下，DLL 输出的相邻
离散电平约相差 305.2 µV，因此自动放大 Y 轴时仍可能看见真实的量化台阶。

Y 轴默认使用 `Auto once, then lock`：启动后用约 0.9 秒确定合适范围，随后锁定，
不会持续跟随抖动；默认最小跨度为 2 mV，可修改中心、跨度，或点击
`Re-adapt and lock` 重新适配。范围计算与当前绘图方式一致：平滑线依据实际显示的
分桶均值，不再被未显示的瞬时原始极值撑大。`Stable auto` 仍提供约 60 ms 快速扩展、
2.2 s 缓慢收缩，`Instant auto` 则保留 ImPlot 的逐帧自适应。

## 功能

- `ad_continu_conf` 启动连续采集。
- `Get_AdBuf_Size` + `Read_AdBuf` 持续分块读取，不等待整段采集结束。
- ImGui + ImPlot 实时显示最近一段波形。
- 8 个物理通道统一标为 `AIN1 (AD0)` 至 `AIN8 (AD7)`。
- 从 `Start acquisition` 第一帧起无损暂存完整 8 通道会话；停止后再决定保存为 Float32、IEEE Float32 WAV、CSV，或直接丢弃。
- `--headless --duration` 无 GUI 模式，便于命令行验收和自动测试。
- 停止、关闭窗口或异常时调用 `AD_continu_stop` 和 `closeUSB` 清理设备。

## 数据模型

当前 V5.2L 固件的 USB 数据流实测为 `16 × freq` 个 float/s。应用程序按 8 个物理
通道解交织，因此：

```text
DLL freq 参数 = 物理通道采样率 / 2
物理 AIN1-AIN8 = 软件 AD0-AD7
```

例如配置 `sampleRate=44100` 时，传给 DLL 的 `freq` 是 22050。

## 构建

### Windows

依赖与参考项目相同：Visual Studio、CMake、vcpkg SDL2。当前机器的 vcpkg 路径为
`C:\Users\DCOL\vcpkg`。

```powershell
cd D:\Projects\matlab_USB_DAQ\usb_daq_live_node
cmake -B build -DCMAKE_TOOLCHAIN_FILE="C:\Users\DCOL\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

CMake 会下载 ImGui、ImPlot 和 nlohmann/json，并将厂商 DLL 与默认配置复制到可执行
文件目录。

### Linux

Ubuntu/Debian 安装依赖：

```bash
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev \
  libsdl2-dev libgl1-mesa-dev
```

构建并运行不接硬件的协议测试：

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-linux -j
ctest --test-dir build-linux --output-on-failure
```

Linux 配置中的 `dllPath` 会被忽略。设备是 Vendor Specific USB，而非 USB Audio Class，
因此无需额外内核模块，但普通用户需要 usbfs 权限：

```bash
sudo install -m 0644 udev/60-usb-daq.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=04b4 --attr-match=idProduct=7809
```

## 运行

Linux 无界面短时采集并保存 WAV：

```bash
./build-linux/usb_daq_live_node --headless --duration 3 --record wav \
  --config ./daq_config.jsonc
```

Linux 图形界面：

```bash
./build-linux/usb_daq_live_node --config ./daq_config.jsonc
```

Windows 图形界面：

```powershell
.\build\Release\usb_daq_live_node.exe --config .\daq_config.jsonc
```

自动启动 GUI、显示 5 秒实时波形并正常退出：

```powershell
.\build\Release\usb_daq_live_node.exe --autostart --duration 5 --config .\daq_config.jsonc
```

三秒无界面验收：

```powershell
.\build\Release\usb_daq_live_node.exe --headless --duration 3 --config .\daq_config.jsonc
```

命令行覆盖采样率并保存 Float32：

```powershell
.\build\Release\usb_daq_live_node.exe --headless --duration 10 `
  --sample-rate 44100 --record f32
```

自动预览并在结束时导出完整 WAV 会话：

```powershell
.\build\Release\usb_daq_live_node.exe --autostart --duration 10 `
  --sample-rate 44100 --record wav
```

完整参数：

```powershell
.\build\Release\usb_daq_live_node.exe --help
```

## 落盘格式

`Start acquisition` 始终以预览模式启动，同时从第一帧起把 AIN1-AIN8 原始电压无损写入
临时会话文件；这不是仅保存屏幕上的滚动窗口，因此不受 `historySeconds`、`Follow window`
或图表缩放范围限制，也不会把长时间数据压在内存里。顶部的 `BUFFERING FROM START`
和面板中的帧数/容量表示临时会话正在完整增长。

预览完成后点击 `Stop acquisition`，再在按钮下方决定：

- 勾选 `Save complete session (from Start)`，选择 WAV/F32/CSV 和 `All 8 inputs` 或
  `Displayed channels`，点击 `Save entire session`；
- 不勾选时点击 `Discard entire session`，临时数据会被删除，不生成最终文件。

在保存或丢弃前不能开始下一次采集。通过界面的 `Quit` 也会先要求处理待定会话；若窗口
意外关闭，未决定的数据会保留为 `shared/.*.f32.tmp`，避免误删。默认临时目录及导出目录
均为配置文件相对路径 `shared/`。命令行 `--record wav|f32|csv` 会在采集结束后自动导出，
`--record none` 则自动丢弃。

### Float32

`.f32` 文件是所选录制范围的 little-endian 32-bit float 交织数据。程序同时生成同名
`.json`，记录采样率、量程和通道顺序。持续高速采集建议使用此格式。

### WAV

`.wav` 文件使用 IEEE Float32 多通道交织格式，可选择 `All 8 inputs` 或 `Displayed channels`。导出时写入并回填 WAV 帧数和文件长度。WAV 样本不做归一化，数值直接以 V 为单位；例如样本值 `0.125` 表示 `0.125 V`。同名 `.json` 会以 `units: volts` 和 `wavSamplesAreRawVolts: true` 明确记录此约定、采样率及实际通道顺序。由于通用音频软件可能按 `[-1, 1]` 音频满量程解释 Float32 WAV，分析时建议使用 MATLAB/Python 并按原始浮点值读取。

经典 RIFF WAV 的数据区上限约为 4 GiB。长时间、8 通道持续采集建议使用无此单文件限制的原始 `.f32`，后续再分段转换为 WAV。

### CSV

第一列为从 0 开始的帧号，后续列为启用的物理/软件通道。CSV 便于直接查看，但在
高采样率、多通道下开销明显大于 Float32。

## 安全说明

- 程序仅调用 ADC 连续采集接口，不操作 DA、DOUT、PWM 或单脉冲输出。
- 板卡不提供 IEPE/ICP 恒流供电；IEPE/ICP 麦克风仍需外置信号调理器。
- 同一时间不要让 MATLAB、厂商测试程序和本程序同时占用板卡。
