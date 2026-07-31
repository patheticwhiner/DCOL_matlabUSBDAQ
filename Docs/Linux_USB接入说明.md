# USB-DAQ V5.2L 的 Linux USB 接入说明

## 1. 结论

恒凯科技 USB-DAQ V5.2L 可以在具备 USB Host 能力的 Linux 开发板上通过
`libusb-1.0` 访问。设备采用 USB 2.0 High-Speed、厂商自定义接口和 Bulk 端点，
不是 USB Audio、CDC 串口或 HID 设备。

当前已经确认 USB 枚举、接口和端点信息，因此 Linux 端能够完成设备发现、打开、
声明接口及 Bulk 读写。尚未公开的是厂商在 Bulk 端点上传输的命令包和 ADC 数据帧
格式；在获得协议文档、Linux SDK 或完成 Windows USB 抓包之前，还不能仅凭端点号
实现可靠采集。

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
3. **厂商线上协议层**：API 如何编码为 Bulk 数据包，目前未知。

`Read_AdBuf()` 向应用返回以 V 为单位的 Float32 数据，但这不能证明 USB 线上也传输
Float32。USB 固件很可能发送 ADC 整数码，由 DLL 完成解帧、通道重排和电压换算。

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

建议增加 udev 规则 `/etc/udev/rules.d/88-usb-daq.rules`：

```udev
SUBSYSTEM=="usb", ATTR{idVendor}=="04b4", ATTR{idProduct}=="7809", \
  MODE="0660", GROUP="plugdev", TAG+="uaccess"
```

修改后执行：

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

部分嵌入式发行版没有 `plugdev` 组，此时应改为设备实际使用的用户组。

## 5. libusb 枚举骨架

以下代码只能验证 Linux 是否能打开并声明设备接口，不会启动 ADC。未确认厂商命令
前，不应向四个端点随意发送数据。

```cpp
#include <libusb-1.0/libusb.h>

libusb_context* context = nullptr;
libusb_device_handle* device = nullptr;

int rc = libusb_init(&context);
if (rc != 0) {
    // 处理初始化错误
}

device = libusb_open_device_with_vid_pid(context, 0x04B4, 0x7809);
if (device == nullptr) {
    // 检查 USB Host、udev 权限和设备连接
}

libusb_set_auto_detach_kernel_driver(device, 1);
rc = libusb_claim_interface(device, 0);
if (rc != 0) {
    // 处理接口占用或权限错误
}

// 此处只有在厂商协议明确后，才能调用 libusb_bulk_transfer()
// 或提交异步 libusb_transfer。

libusb_release_interface(device, 0);
libusb_close(device);
libusb_exit(context);
```

libusb 官方 API 支持 Control、Bulk、Interrupt 和 Isochronous 传输，并同时支持同步和
异步接口：<https://libusb.sourceforge.io/api-1.0/>。

## 6. 厂商协议的获取方式

### 6.1 首选方案

向板卡厂商索取以下任一资料：

- Linux SDK 或 ARM/Linux 动态库；
- `Usb_Daq_V52_Dll` 源码；
- USB 命令协议和 ADC 帧格式；
- FPGA/USB 固件的端点定义；
- ADC 原始码到电压的标定公式。

这条路线风险最低，也最容易覆盖复位、异常恢复、触发和输出控制等边界情况。

### 6.2 Windows 抓包方案

如果厂商不能提供协议，可在 Windows 上使用 USBPcap + Wireshark，对 DLL 的每个操作
单独抓包。抓包时一次只改变一个参数，以便建立命令字段映射。

建议按以下顺序记录：

1. 仅执行 `openUSB()` 和 `closeUSB()`，取得枚举/初始化基线；
2. 执行一次 `ad_single()`；
3. 固定其他参数，仅改变输入量程；
4. 固定其他参数，仅改变 oversample；
5. 固定其他参数，仅改变 `freq`；
6. 执行 `ad_continu_conf()`，记录启动命令与应答；
7. 记录稳定连续采集的数据包；
8. 执行 `AD_continu_stop()`；
9. 拔插、复位和错误恢复各记录一次。

需要从抓包中确认：

- `0x02/0x04` 分别承担哪类命令或数据；
- `0x86/0x88` 哪个是 ADC 流、哪个是命令应答；
- 命令头、命令字、长度、序号、校验和超时；
- ADC 位宽、大小端、通道排列、符号和帧边界；
- 短包、零长度包和停止命令的语义。

Infineon 也提供基于 libusb 的 CyUSB Linux 工具，并说明可以添加自定义 VID/PID：
<https://community.infineon.com/t5/Knowledge-Base-Articles/Using-the-cyusb-linux-GUI-for-Devices-with-a-Custom-VID-PID/ta-p/259429>。
它可用于描述符和端点测试，但不会自动知道本板卡的厂商采集协议。

## 7. 推荐的软件结构

现有 C++ 工程的 GUI、历史缓冲、绘图和 WAV/F32/CSV 会话导出逻辑可以继续复用。
建议把设备访问抽象为统一后端：

```text
Acquisition
└── IDaqBackend
    ├── WindowsDllBackend   -> Usb_Daq_V52_Dll.dll
    └── LinuxLibusbBackend -> libusb-1.0 + 厂商协议
```

建议 `IDaqBackend` 至少提供：

```cpp
open();
close();
configure_continuous(...);
read_frames(...);
stop_continuous();
reset();
```

上层统一接收按 `AIN1` 至 `AIN8` 排列、以 V 为单位的 Float32 帧。ADC 原始码的解析和
电压换算应放在具体后端内，避免 GUI 和文件导出代码依赖 USB 线上的私有格式。

连续采集建议使用 libusb 异步传输，同时预提交多个 Bulk IN 缓冲区。同步
`libusb_bulk_transfer()` 适合最初的协议验证，但单请求串行读取更容易因调度延迟造成
吞吐抖动。

## 8. 带宽估算

当前应用层在 44.1 kHz、8 通道、Float32 下的数据量为：

```text
44,100 × 8 × 4 = 1,411,200 byte/s ≈ 1.35 MiB/s
```

即使 8 通道达到 200 kHz，应用层 Float32 数据也约为 6.1 MiB/s，低于 USB 2.0
High-Speed Bulk 的可用吞吐。Linux 实现的主要风险不是理论带宽，而是私有协议、
USB 请求队列深度、开发板 Host 控制器质量和应用线程调度。

## 9. Linux 验收清单

实现协议后，应按以下顺序验收：

- `lsusb -d 04b4:7809` 能稳定发现设备；
- 描述符与本文记录的接口和四个端点一致；
- 普通用户能够打开设备并声明 Interface 0；
- `ad_single` 等效操作与 Windows DLL 电压结果一致；
- ±5 V、±10 V 量程及全部 oversample 模式逐项一致；
- 连续采集的通道顺序为 `AIN1` 至 `AIN8`；
- 44.1 kHz 下进行至少 30 分钟无丢帧测试；
- 验证停止、重新启动、USB 拔插和设备复位；
- 同一输入信号同时在 Windows 和 Linux 采集，对比均值、幅值、频率和量化步进；
- 确认保存的 WAV/F32 数据仍以 V 为单位且不被归一化。

只有在命令格式、ADC 数据格式和异常恢复全部通过上述对照后，才能认为 Linux 原生
采集后端完成，而不能只以“libusb 可以打开设备”作为完成标准。
