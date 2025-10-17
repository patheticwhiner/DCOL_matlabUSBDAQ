# 基于MATLAB的USB-DAQ采集程序

## 1 基本说明

### 1.1 恒凯科技（HKTech）USB-DAQ V5.2L 8通道电子数据采集卡简介

<img src="assets\Image1.jpg" width = 70% />

+ **电源**：DC 12V 3A
+ **USB连接**：USB Type-B
+ **FPGA核心**：ACTEL ProASIC3 FPGA
  + **逻辑整合**：协调AD/DA、数字I/O、PWM模块的时序与控制。
  + **通信枢纽**：通过USB2.0 PHY芯片与PC端驱动交互，管理数据流（如FIFO缓冲）。

+ **16位100kHz ADC**：支持16路单端同步采样（AD0-AD15），每通道独立采样保持器确保同步性，输入量程可选±10V（分辨率305μV）或±5V（分辨率152μV）。
+ **2路12位DAC**：输出范围0-10V，低速无缓冲模式，建立时间8.5μs，精度0.05% FSR。
+ **数字I/O**：16路输入（DI0-DI15，兼容TTL/3.3V电平）和16路输出（DO0-DO15，5V电平，10mA驱动能力）。
+ **PWM模块**：2路32位PWM输出（复用DO0-DO1），支持方波或单脉冲（1μs-3s脉宽）；2路32位PWM测量输入（复用DI2-DI3），48MHz时钟等精度测量。

*详情参见附带的使用说明书*

### 1.2 功能介绍

#### （1）AD数据采集

1. **初始化**：PC端调用`openUSB()`，FPGA复位各模块。
2. **用户调用函数**（如`ad_continu_conf()`）设置参数（包括`freq`）。
3. **驱动通过USB传输配置命令**到FPGA。
4. **FPGA更新分频器寄存器**，调整时钟分频比。
5. **硬件定时器按新频率输出时钟**，触发AD转换。
   - FPGA按配置采集数据→存入FIFO→USB批量传输至PC缓冲区。
   - PC周期调用`Read_AdBuf()`读取数据，直至`AD_continu_stop()`终止。
6. **限制与注意事项**：ProASIC3为固定逻辑架构，功能需预先烧写，不支持动态重配置（如更换PWM分辨率）。

#### （2）PWM生成

#### （3）外设通信

- **AD/DA控制**：
  - FPGA通过SPI/I2C配置ADC量程（±10V/±5V）和DAC输出电压（0-10V）。
  - 模拟输入阻抗>1MΩ，输出驱动能力20mA（需外接限流电阻）。
- **数字I/O与PWM**：
  - 复用逻辑：DI2/DI3兼作PWM测量输入，DO0/DO1兼作PWM输出（需软件使能）。
  - 电平标准：5V TTL（输入阈值：高>2V，低<0.8V；输出电平：高>2.5V，低<0.5V）。

- **USB2.0接口**：
  - 协议：符合USB2.0全速/高速标准（文档未明确速率，推测为12Mbps全速模式）。
  - 数据流：驱动（`libusbK`）通过端点传输AD数据、接收DA/PWM控制指令。
- **驱动功能**：
  - 封装DLL函数（如`ad_continu_conf()`），将用户API调用转为USB协议包。

### 1.3 快速使用

要快速开始使用该工程，请按以下步骤操作：

1. **环境准备**：安装驱动程序并配置 MATLAB C/C++ 编译器
2. **启动 UI 界面**：在 MATLAB 命令窗口运行 `daq_ui`
3. **配置参数**：设置采样频率、时间和选择通道
4. **开始采集**：点击"开始采集"按钮
5. **查看结果**：在界面中查看时域信号和功率谱密度图
6. **数据管理**：选择是否保存数据到文件，或从会话中播放音频

## 2 文件目录

```
matlab_USB_DAQ/
├── README.md                      # 项目说明文档
├── USB_DAQ_README.md             # DLL 接口详细说明
├── assets/                        # 资源文件夹
├── data/                         # 数据保存目录
|
├── Docs/                         # 文档目录
│   ├── CrossComplie.md           # 交叉编译说明
│   ├── Manual.md                 # 使用手册
│   └── Q&A记录.md                # 常见问题记录
|
├── functions/                    # 核心功能模块
│   ├── acquire_data.m            # 底层数据采集函数
│   ├── cleanup_usb_daq.m         # 设备清理函数
│   ├── daq_ui.m                  # 主UI界面函数
│   ├── parse_inputs.m            # 参数解析函数
│   ├── playAudio.m               # 音频回放函数
│   ├── plotDaqData.m             # 数据绘图函数
│   ├── startSampling.m           # 采集启动函数
│   └── usb_daq_acquire.m         # 高级采集接口
|
├── 附带文件/                     # 厂商提供的文件
│   ├── 驱动安装包/               # USB 驱动程序
│   └── USB数据采集卡测试程序V52/ # 厂商测试程序
├── usbCardV52.m                  # 厂商MATLAB例程脚本
├── Usb_Daq_V52_Dll.dll          # USB DAQ 动态链接库
├── Usb_Daq_V52_Dll.h            # 头文件，包含函数声明
|
├── daq_demo_ad_single.m          # 单次 AD 采样演示
├── daq_demo_continuous_poll.m    # 连续轮询采样演示
└── usb_daq_custom_channels.m     # 自定义通道采集脚本
```

### 2.1 核心功能模块说明

- **`daq_ui.m`**：主要的图形用户界面，提供参数设置、通道选择、数据采集、实时显示和音频回放功能
- **`usb_daq_acquire.m`**：高级数据采集接口，封装了完整的采集流程
- **`acquire_data.m`**：底层数据采集实现，直接调用 DLL 函数
- **`parse_inputs.m`**：参数解析和验证，确保采集参数的正确性
- **`startSampling.m`**：UI 界面的采集逻辑，处理多通道显示和数据保存
- **`playAudio.m`**：音频回放功能，可播放采集到的信号数据

### 2.2 演示脚本说明

- **`daq_demo_ad_single.m`**：演示单次 AD 采样，读取 16 路模拟输入的瞬时值
- **`daq_demo_continuous_poll.m`**：演示连续采集的轮询模式，实时监控缓冲区状态
- **`usb_daq_custom_channels.m`**：自定义通道采集的简化版本



## 3 环境配置

### 3.1 环境配置背景

#### （1）为什么需要安装PC驱动程序？

- **硬件抽象**：驱动（如`libusbK v3.0.7.0`）提供操作系统与采集卡USB2.0硬件的通信接口，管理数据传输、中断处理及资源分配。
- **功能实现**：
  - 设备枚举（`openUSB()`/`closeUSB()`）。
  - 配置AD/DA参数（如量程、触发模式）。
  - 数据缓冲管理（192k FIFO的读写）。

#### （2）MATLAB脚本的C编译器配置与动态链接库的调用

厂商提供的程序中包括头文件（.h）、静态链接库（.lib）和动态链接库（.dll）。其中头文件为文本文件，其中包含函数声明、常量定义与数据结构，它是开发者调用库功能的接口，告诉用户有哪些函数可用、如何传参，但隐藏了具体的实现细节。静态链接库与动态链接库均为二进制文件。静态链接库中存储函数实现的预编译二进制代码（目标文件的集合），在编译时链接。而动态链接库则存储函数的实际机器代码，它在运行时被加载。厂商通过提供DLL而非源代码来保护知识产权，同时价格年底用户集成的难度（不需要重新编译）。

在这样包装的工程中，无需编译源代码，而可以直接链接.lib。虽然MATLAB本身基于C，但直接调用硬件需通过DLL（如`Usb_Daq_V52_D11.dll`）实现底层操作，而DLL需C编译器（如MinGW）链接生成MEX文件。

- **动态链接库集成**：称为“外部接口（External Interfaces）”或“C/C++集成”。需将DLL、LIB和头文件（`.h`）放入工程目录，通过`loadlibrary`加载。
- **优势**：
  - 避免重复造轮子，直接复用厂商提供的硬件控制函数（如`ad_continu()`）。
  - 提升实时性，C编译的二进制代码比MATLAB解释执行更快。

#### （3）MATLAB工程的执行流程

1. **初始化**

   ```matlab
   loadlibrary('Usb_Daq_V52_D11.dll', 'Usb_Daq_V52_D11.h');
   calllib('Usb_Daq_V52_D11', 'openUSB');
   ```

2. **配置与采集**

   - 调用DLL函数`ad_continu_conf()`设置采样参数，如过采样率、量程、触发模式（如外部时钟+上升沿触发）。
   - FPGA按配置采集数据→存入FIFO→USB批量传输至PC缓冲区。PC周期循环读取缓冲区`Read_AdBuf()`直至数据量达标，`AD_continu_stop()`终止。

3. **终止**

   ```matlab
   calllib('Usb_Daq_V52_D11', 'closeUSB');
   unloadlibrary('Usb_Daq_V52_D11');
   ```

+ **关键点**
  - 需确保DLL路径正确，且MATLAB与编译器架构一致（如64位MATLAB配64位DLL）。
  - 示例参考文档中LabVIEW调用方式，MATLAB同理需严格匹配函数原型（参数类型、顺序）。

### 3.2 环境配置步骤

+ 安装驱动程序：见附带文件

  1. 运行安装程序，选择`libusbK(v3.0.7.0)`驱动。

  2. 完成安装后，设备管理器显示采集卡为正常识别设备。

+ 为MATLAB配置C/C++编译器，实现MATLAB与C联合编译（环境变量中添加C/C++编译器）

  ```
  >> mex -setup C
  ```

  显示有编译器即可，例如“MEX 配置为使用 **'MinGW64 Compiler (C)'** 以进行 C 语言编译。”

+ 根目录下放置特定的动态链接库与头文件等

### 3.3 小结

更多细节也可参考历史文档[交叉编译](Docs\CrossComplie.md)。

## 4 UI界面使用指南

### 4.1 启动界面

将工程目录下的 `functions` 文件夹添加到 MATLAB 路径：
```matlab
addpath('functions');
daq_ui
```

### 4.2 界面功能说明

<figure align = center>
    <img src="assets\UI.png" width = 39% />
    <img src="assets\UI2.png" width = 60% />
</figure>

#### 4.2.1 采集参数设置
- **采样频率 (Hz)**：设置 AD 转换的采样率，默认 44100 Hz
- **采样时间 (秒)**：设置采集持续时间，默认 10 秒

#### 4.2.2 通道管理
- **通道选择**：勾选要采集的硬件通道（CH0-CH7）
- **时域显示**：控制哪些通道在时域图中显示，支持"全选"快捷操作
- **频域显示**：控制哪些通道在功率谱密度图中显示，支持"全选"快捷操作
- **对数坐标**：选择功率谱使用线性坐标还是对数坐标显示

#### 4.2.3 数据管理
- **保存数据到文件**：勾选后将在 `data/` 目录下保存 MAT 文件
- **播放通道**：选择要回放的通道并点击"播放音频"按钮

#### 4.2.4 实时显示
- **时域信号图**：显示采集到的原始信号波形，支持多通道叠加显示
- **功率谱密度图**：显示信号的频域特性，支持线性/对数坐标切换

### 4.3 数据保存规则

#### 4.3.1 文件保存
当勾选"保存数据到文件"时，系统会：
- 在工程根目录下创建 `data/` 文件夹（如不存在）
- 保存文件命名格式：`usb_daq_YYYYMMDD_HHMMSS.mat`
- 保存内容包括：
  - `acquired.data`：采集到的数据矩阵 [通道数 × 采样点数]
  - `acquired.config`：采集配置参数
  - `acquired.selectedChannels`：选中的通道列表
  - `acquired.timeChannelCheckboxes`：时域显示选择状态
  - `acquired.freqChannelCheckboxes`：频域显示选择状态
  - `acquired.logScale`：对数坐标选择状态

#### 4.3.2 会话保存
无论是否保存文件，采集数据都会保存在 UI 会话中，可通过以下方式访问：
```matlab
% 获取 UI 窗口句柄
h = findall(0, 'Type', 'figure', 'Name', 'USB DAQ 数据采集系统');
% 读取最近一次采集的数据
acquired = getappdata(h, 'lastAcquiredData');
```

#### 4.3.3 加载保存的数据
```matlab
% 加载 MAT 文件
d = load('data/usb_daq_20251018_153045.mat');
acquired = d.acquired;
% 访问数据
data = acquired.data;        % 采集数据
config = acquired.config;    % 配置信息
```

### 4.4 音频回放功能

1. **确保已完成采集**：先执行一次数据采集
2. **选择播放通道**：在"播放通道"下拉菜单中选择要回放的通道
3. **开始播放**：点击"播放音频"按钮，系统会：
   - 从会话数据中提取选中通道的信号
   - 使用正确的采样率进行归一化处理
   - 调用 MATLAB 的 `sound()` 函数播放

**注意事项**：
- 播放质量依赖于系统音频设备和采样率设置
- 高采样率信号可能被音频驱动重采样
- 只能播放已采集通道中的数据

### 4.5 使用流程示例

1. **基本采集流程**：
   ```
   启动 daq_ui → 选择通道 → 设置参数 → 开始采集 → 查看结果
   ```

2. **多通道对比分析**：
   ```
   选择多个通道 → 采集 → 在时域/频域图中对比不同通道特性
   ```

3. **音频信号分析**：
   ```
   连接音频源到 AD 输入 → 采集 → 查看频谱 → 播放验证
   ```

## 5 编程接口

### 5.1 命令行使用

如果不使用 UI 界面，可以直接调用核心函数：

```matlab
% 简单采集
[data, config] = usb_daq_acquire('SampleRate', 44100, 'SampleTime', 5, 'ChannelIndices', [0,1,2,3]);

% 自定义参数采集
[data, config] = usb_daq_acquire('SampleRate', 192000, 'SampleTime', 10, ...
                                 'ChannelIndices', [0,3,5], 'ADGain', 1);
```

### 5.2 演示脚本

项目提供了多个演示脚本，可帮助理解不同的采集模式：

```matlab
% 单次 AD 采样演示
daq_demo_ad_single

% 连续轮询采样演示  
daq_demo_continuous_poll

% 自定义通道采集
usb_daq_custom_channels
```

