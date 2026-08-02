# USB-DAQ V5.2L 的 Linux USB 接入说明

## 1. 结论

恒凯科技 USB-DAQ V5.2L 可以在具备 USB Host 能力的 Linux 开发板上通过
`libusb-1.0` 访问。设备采用 USB 2.0 High-Speed、厂商自定义接口和 Bulk 端点，
不是 USB Audio、CDC 串口或 HID 设备。

当前已从工程附带的 2018 年 x64 厂商 DLL 中还原 ADC 通信路径，并把原生 libusb 后端
直接集成进 [`../usb_daq_live_node`](../usb_daq_live_node)。同一个程序在 Windows 动态
加载厂商 DLL，在 Linux 编译 libusb 后端；不再需要单独维护 `linux_usb_daq` 应用。
后端覆盖 `openUSB`、`ad_single`、连续采集配置、缓冲读取、停止、复位和关闭；DA、数字
I/O、PWM 暂未开放，因为这些功能会改变外部输出，需要另行做硬件安全验证。

本机实测设备能够稳定枚举，但默认设备节点为 `root:root 0664`，普通用户打开时会得到
`LIBUSB_ERROR_ACCESS`。安装仓库提供的 udev 规则后才能进行真实采集验收。这是权限问题，
不是缺少某个内核模块。

## 2. 实测 USB 描述符

以下信息来自当前实际连接的板卡，而不是根据产品名称推测：

| 项目 | 实测值 |
|---|---|
| 产品字符串 | `USB-DAQ-CARD` |
| 厂商描述 | `Cypress Semiconductor Corp.` |
| VID | `0x04B4` |
| PID | `0x7809` |
| 接口号 | `0` |
| Alternate Setting | `0` |
| 接口类型 | Vendor Specific，Class `0xFF` |
| Subclass / Protocol | `0x00 / 0x00` |
| Windows 当前服务 | `WinUSB` |
| Windows 设备接口 GUID | `{EA5B06C4-6B3B-481E-BD04-6DACCE3CC265}` |

接口 0 包含四个 Bulk 端点：

| 端点 | 方向 | 类型 | 最大包长 |
|---|---|---|---:|
| `0x02` | Host → Device | Bulk OUT | 512 B |
| `0x86` | Device → Host | Bulk IN | 512 B |
| `0x04` | Host → Device | Bulk OUT | 512 B |
| `0x88` | Device → Host | Bulk IN | 512 B |

Bulk 端点最大包长为 512 字节，说明设备当前工作在 USB High-Speed 模式。工程根目录
README 中“可能为 12 Mbps Full-Speed”的旧推测不适用于这块实测设备。High-Speed
的总线标称速率是 480 Mbps，但实际吞吐仍取决于设备固件、主控调度和应用读取方式。

## 3. 当前 Windows 软件栈

Windows 设备当前由 libwdi 生成的 INF 绑定至 `WinUSB.sys`。厂商
`Usb_Daq_V52_Dll.dll` 内部包含 libusb 相关代码，并能够使用 WinUSB/libusbK
后端；本工程的 C++ 和 MATLAB 程序调用的是 DLL 导出的高级接口，例如：

- `openUSB()` / `closeUSB()`；
- `ad_single()`；
- `ad_continu_conf()`；
- `Get_AdBuf_Size()` / `Read_AdBuf()`；
- `AD_continu_stop()`。

这些函数声明见 [`../Usb_Daq_V52_Dll.h`](../Usb_Daq_V52_Dll.h)。头文件只描述主机
API，没有描述函数调用对应的 USB 命令字、校验、端点选择或数据帧结构。

必须区分以下三层：

1. **USB 传输层**：VID/PID、接口和四个 Bulk 端点，已经确认；
2. **DLL API 层**：采样率、量程、过采样和连续读取函数，已经确认；
3. **厂商线上协议层**：ADC 配置、启停、同步标记和数据换算已经从 DLL 还原并实现。

USB 线上不是 Float32。`0x86` 端点传输 little-endian `int16` ADC 码，DLL 在主机端
完成同步、符号扩展和电压换算，`Read_AdBuf()` 才向应用返回以 V 为单位的 Float32。

## 4. Linux 端的基本条件

Linux 开发板需要满足：

- USB 端口能够工作在 Host 模式；
- 能为板卡提供稳定的 5 V USB 供电，必要时使用有源 USB Hub；
- 系统提供 `libusb-1.0` 和 usbfs；
- 用户具有访问 `04b4:7809` 的权限；
- 连续采集线程能够及时提交 Bulk IN 请求，避免设备 FIFO 溢出。

Debian/Ubuntu 类系统可安装：

```bash
sudo apt install libusb-1.0-0-dev
```

仓库已提供 [`../usb_daq_live_node/udev/60-usb-daq.rules`](../usb_daq_live_node/udev/60-usb-daq.rules)：

```udev
SUBSYSTEM=="usb", ATTR{idVendor}=="04b4", ATTR{idProduct}=="7809", MODE="0660", TAG+="uaccess"
```

安装并立即应用：

```bash
sudo install -m 0644 usb_daq_live_node/udev/60-usb-daq.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=04b4 --attr-match=idProduct=7809
```

`TAG+="uaccess"` 由 systemd-logind 给当前本地图形会话增加 ACL。无 logind 的无头系统
应在规则中增加真实存在的组，例如 `GROUP="plugdev"` 或 `GROUP="uucp"`，并把服务用户
加入该组。用 `getfacl /dev/bus/usb/BBB/DDD` 检查最终权限，不建议长期以 root 运行采集。

## 5. 已还原的 ADC 协议

### 5.1 连续采集配置

`ad_continu_conf()` 向 Bulk OUT `0x04` 发送 14 字节：

| 偏移 | 内容 |
|---:|---|
| 0 | 标志：bit7=`ad_range!=0`，bits6:4=`ad_os&7`，bit3=`clk_sl`，bit2=`trig_sl`，bit1=`ext_clk_pol`，bit0=`trig_pol` |
| 1 / 3 / 5 / 7 / 9 / 11 | 固定标签 `01 02 03 04 05 06` |
| 2 / 4 / 6 | `floor(12,000,000 / freq)` 的低/中/高字节 |
| 8 | `ch_last` |
| 10 | `ch_first`，向下对齐到 8 的倍数 |
| 12 / 13 | `00 FE` |

DLL 会把 `freq` 限制在 100—100000，把 `ch_first` 限制在 0—56，并把
`ch_last` 限制在 1—63。发送配置包成功后，再向同一端点发送 `01 FE` 启动；停止时
发送 `00 FE`。

### 5.2 ADC 数据流

连续数据从 Bulk IN `0x86` 读取。流起始标记为：

```text
AA 55 55 AA
```

标记之后是连续 little-endian signed `int16` ADC 码。实现会处理标记或单个采样跨 USB
transfer 边界的情况，解码后在主机内存中缓冲 Float32 电压，供 `Get_AdBuf_Size()` 和
`Read_AdBuf()` 读取。

厂商 PDF 写的是 `ad_range=0` 对应 ±10 V、`ad_range=1` 对应 ±5 V，但附带 DLL 的实际
换算代码却是：

```text
ad_range == 0: volts = raw * 5 / 32768
ad_range != 0: volts = raw * 10 / 32768
```

Linux 后端目前选择兼容 DLL 的实际输出，保证同一 raw code 在 Windows/Linux 返回相同
Float32。正式用于幅值测量前，仍必须用已知直流基准分别校验两个量程；不能只相信 PDF
标签或函数参数名。

`0x02/0x88` 以及 DA、数字 I/O、PWM 对应的协议不属于当前 ADC 接入范围。

## 6. Linux 实现、构建与运行

Linux 实现位于 [`../usb_daq_live_node`](../usb_daq_live_node) 内部：

```text
usb_daq_live_node/
├── src/usb_daq_api.hpp               # Windows DLL / Linux 后端统一入口
├── src/usb_daq_libusb_backend.cpp     # libusb、读取线程与 Float32 缓冲
├── src/usb_daq_protocol.cpp           # ADC 命令编码和 int16 流解码
├── tests/protocol_tests.cpp           # 不接硬件即可运行的协议回归测试
└── udev/60-usb-daq.rules
```

Ubuntu/Debian 安装构建依赖后，可编译同一个实时节点并运行协议测试：

```bash
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev \
  libsdl2-dev libgl1-mesa-dev
cmake -S usb_daq_live_node -B usb_daq_live_node/build-linux \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build usb_daq_live_node/build-linux -j
ctest --test-dir usb_daq_live_node/build-linux --output-on-failure
```

安装 udev 规则后，无 GUI 验收和导出可直接使用节点原有命令行：

```bash
./usb_daq_live_node/build-linux/usb_daq_live_node \
  --headless --duration 3 --record wav \
  --config usb_daq_live_node/daq_config.jsonc
```

去掉 `--headless` 可启动 SDL2 + ImGui + ImPlot 实时界面。Linux 会忽略配置中的
`dllPath`；Windows 仍按原逻辑加载 `Usb_Daq_V52_Dll.dll`。当前后端使用短超时 Bulk IN
读取线程，既能连续填充原有 `Get_AdBuf_Size` / `Read_AdBuf` 缓冲，也能在停止时及时退出。

### 6.1 当前实机结果

由于宿主 udev 规则尚未安装，本轮使用仅映射该 USB 节点的临时容器取得设备权限，不改变
宿主设备权限；容器运行的仍是仓库刚构建的 `usb_daq_live_node`。结果如下：

- 成功打开并声明 1 台 `04b4:7809`；
- `sampleRate=44100`、`freq=22050` 连续采集约 3.02 秒，取得 133,116 个八通道帧、
  1,064,928 个 Float32，等效每通道采样率约 44,016 Hz；
- 成功导出 4,259,770 字节的 8 通道 IEEE Float32 WAV 和 JSON 元数据；
- 后端独立验收中，`ad_single`、停止后重启和 Float32 文件长度也均已通过。

这些结果验证了统一节点中的枚举、配置包、启动、同步标记、ADC 解码、缓冲读取、停止和
WAV 导出。量程绝对精度、通道顺序和长时间丢帧仍需接入已知信号后做计量级验收。

## 7. i.MX6ULL 音视频伴录

面向 i.MX6ULL 的轻量实现位于另一个 portfolio 仓库的
[`imx6ull_usb_daq_av_recorder`](../../EmbeddedSystem/portfolio/projects/imx6ull_usb_daq_av_recorder)。
它复用相同的 14 字节配置命令、启停命令和 ADC 解码规则，但不链接 SDL/OpenGL：

- 通过 libusb 直接采集 DAQ 的 8 路物理通道；
- 默认选择一路并按量程归一化为单声道 IEEE Float32 WAV，也可保存全部 8 路；
- 可启动外部 V4L2/GStreamer 视频命令，并记录视频进程、DAQ 启动和首个音频帧的
  `CLOCK_MONOTONIC` 时间；
- 生成 JSON sidecar，供后期合并时修正固定启动偏移；
- 附带 ffmpeg 合并脚本、udev 规则和 Buildroot/交叉编译说明。

当前代码已在 PC 上用同一块真机跑过 2 秒板端路径：得到 88,304 个单声道帧，ffprobe
识别为 `pcm_f32le / 44100 Hz / mono / 2.002358 s`。这证明主机协议和文件链路可工作；
同时已用 100ask GCC 6.2.1 + ARM libusb 交叉生成 EABI5 hard-float 可执行文件。这证明
主机协议、文件链路和 ARM 工具链均可工作；在 i.MX6ULL 实板上仍需补做 USB 摄像头并发、
存储持续写入、温升和 30 分钟以上漂移测试。

这块 DAQ 不是 USB Audio Class 声卡，因此不会自动出现 ALSA 设备。使用它获取“音频”
必须经过本项目的 Vendor Bulk 协议采集；麦克风还必须有合适的偏置/前置放大或 IEPE
调理器。当前同步属于可校准的软同步，不是共享硬件时钟的样本级同步。

## 8. 带宽估算

当前应用层在 44.1 kHz、8 通道、Float32 下的数据量为：

```text
44,100 × 8 × 4 = 1,411,200 byte/s ≈ 1.35 MiB/s
```

即使 8 通道达到 200 kHz，应用层 Float32 数据也约为 6.1 MiB/s，低于 USB 2.0
High-Speed Bulk 的可用吞吐。Linux 实现的主要风险不是理论带宽，而是私有协议、
USB 请求队列深度、开发板 Host 控制器质量和应用线程调度。

## 9. Linux 验收清单

当前已完成静态协议回归测试、权限失败路径复现和容器内真机短采集；安装 udev 规则后，
按以下顺序继续完成宿主及计量验收：

- [x] `lsusb -d 04b4:7809` 能稳定发现设备；
- [x] 描述符与本文记录的接口和四个端点一致；
- [ ] 普通用户能够打开设备并声明 Interface 0；
- [x] `ad_single` 返回 16 个电压值；
- [x] `usb_daq_live_node` 在 Linux 使用内置 libusb 后端完成 44.1 kHz 应用采样；
- [x] 停止后能重新启动采集；
- [x] WAV/F32 文件长度、样本数和值域通过短时检查；
- [ ] `ad_single` 与 Windows DLL 对同一输入逐值一致；
- [ ] ±5 V、±10 V 量程及全部 oversample 模式逐项一致；
- [ ] 连续采集的通道顺序为 `AIN1` 至 `AIN8`；
- [ ] 44.1 kHz 下进行至少 30 分钟无丢帧测试；
- [ ] 验证 USB 拔插和设备复位恢复；
- [ ] 同一输入信号同时在 Windows 和 Linux 采集，对比均值、幅值、频率和量化步进；
- [ ] 确认保存的 WAV/F32 数据仍以 V 为单位且不被归一化。

只有在命令格式、ADC 数据格式和异常恢复全部通过上述对照后，才能认为 Linux 原生
采集后端完成，而不能只以“libusb 可以打开设备”作为完成标准。
