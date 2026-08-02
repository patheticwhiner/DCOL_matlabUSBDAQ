# 基于MATLAB的USB-DAQ采集程序

## 1 基本说明

### 1.1 恒凯科技（HKTech）USB-DAQ V5.2L 8通道电子数据采集卡简介

<img src="assets\Image1.jpg" width = 70% />

+ **电源**：DC 12V 3A
+ **USB连接**：USB Type-B
+ **模拟输入通道映射**：物理端口 AIN1-AIN8 分别对应软件通道 AD0-AD7（`ChannelIndices` 为 0-7）；GUI 统一按 `AIN1 (AD0)` 的形式显示。
+ **FPGA核心**：ACTEL ProASIC3 FPGA
  + **逻辑整合**：协调AD/DA、数字I/O、PWM模块的时序与控制。
  + **通信枢纽**：通过USB2.0 PHY芯片与PC端驱动交互，管理数据流（如FIFO缓冲）。

+ **16位100kHz ADC**：支持8路单端同步采样（物理端口AIN1-AIN8，软件通道AD0-AD7），输入量程可选±10V（分辨率305μV）或±5V（分辨率152μV）。
+ **2路12位DAC**：输出范围0-10V，低速无缓冲模式，建立时间8.5μs，精度0.05% FSR。
+ **数字I/O**：16路输入（DI0-DI15，兼容TTL/3.3V电平）和16路输出（DO0-DO15，5V电平，10mA驱动能力）。
+ **PWM模块**：2路32位PWM输出（复用DO0-DO1），支持方波或单脉冲（1μs-3s脉宽）；2路32位PWM测量输入（复用DI2-DI3），48MHz时钟等精度测量。

*详情参见附带的使用说明书*

<p><span style="color:red"><strong>IEPE/ICP 麦克风供电注意：</strong>本板卡的模拟输入端不提供 IEPE/ICP 恒流供电。连接 IEPE/ICP 麦克风时，需要在麦克风与 AIN 输入之间增加匹配的外置恒流供电器/信号调理器；板卡自身的 DC 12V 电源不能代替传感器恒流供电。</span></p>

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
  - 当前板卡实测以 480 Mbps High-Speed 枚举，使用 Vendor Specific Bulk 接口。
  - `usb_daq_live_node` 在 Windows 使用厂商 DLL，在 Linux 内置 libusb 后端；ADC 命令使用 `0x04`，数据流使用 `0x86`，无需维护另一套 Linux 应用。
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
├── custom/                       # 核心功能模块
│   ├── acquire_data.m            # 底层数据采集函数
│   ├── analyze_spl.m             # 统一的SPL声压级分析函数（支持麦克风校准）
│   ├── cleanup_usb_daq.m         # 设备清理函数
│   ├── daq_ui.m                  # 主UI界面函数（含麦克风校准功能）
│   ├── parse_inputs.m            # 参数解析函数
│   ├── performSPLAnalysis.m      # SPL分析协调函数
│   ├── playAudio.m               # 音频回放函数
│   ├── plotDaqData.m             # 数据绘图函数
│   ├── plotSPLResults.m          # SPL结果可视化函数
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
├── usb_daq_live_node/            # Windows/Linux 实时采集、显示及会话导出工具
├── daq_demo_ad_single.m          # 单次 AD 采样演示
├── daq_demo_continuous_poll.m    # 连续轮询采样演示
└── usb_daq_custom_channels.m     # 自定义通道采集脚本
```

### 2.1 核心功能模块说明

- **`daq_ui.m`**：主要的图形用户界面，提供参数设置、通道选择、数据采集、实时显示、音频回放、SPL分析和麦克风校准功能
- **`usb_daq_acquire.m`**：高级数据采集接口，封装了完整的采集流程
- **`acquire_data.m`**：底层数据采集实现，直接调用 DLL 函数
- **`parse_inputs.m`**：参数解析和验证，确保采集参数的正确性
- **`startSampling.m`**：UI 界面的采集逻辑，处理多通道显示和数据保存
- **`playAudio.m`**：音频回放功能，可播放采集到的信号数据
- **`analyze_spl.m`**：统一的SPL分析函数，使用Audio Toolbox的splMeter对象进行声压级分析，支持可选的麦克风校准功能
- **`performSPLAnalysis.m`**：SPL分析协调函数，整合麦克风校准数据并调用analyze_spl函数
- **`plotSPLResults.m`**：SPL结果可视化，绘制频带分析图表和总声压级

### 2.2 演示脚本说明

- **`daq_demo_ad_single.m`**：演示单次 AD 采样，读取 16 路模拟输入的瞬时值
- **`daq_demo_continuous_poll.m`**：演示连续采集的轮询模式，实时监控缓冲区状态
- **`usb_daq_custom_channels.m`**：自定义通道采集的简化版本
*注：为简化项目结构，已将原有的 `analyze_spl_simple.m` 和 `analyze_spl_with_calibration.m` 合并到 `analyze_spl.m` 统一函数中*



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

将工程目录下的 `custom` 文件夹添加到 MATLAB 路径：
```matlab
addpath('custom');
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
- **通道选择**：勾选要采集的通道；GUI 使用“物理通道（软件通道）”格式，例如 `AIN1 (AD0)`、`AIN8 (AD7)`
- **时域显示**：控制哪些通道在时域图中显示，支持"全选"快捷操作
- **频域显示**：控制哪些通道在功率谱密度图中显示，支持"全选"快捷操作
- **对数坐标**：选择功率谱使用线性坐标还是对数坐标显示

#### 4.2.3 数据管理
- **保存数据到文件**：勾选后将在 `data/` 目录下保存 MAT 文件
- **播放通道**：选择要回放的通道并点击"播放音频"按钮

#### 4.2.4 麦克风校准功能
- **麦克风校准按钮**：点击进入麦克风灵敏度设置对话框
- **灵敏度设置**：为每个通道设置麦克风灵敏度（mV/Pa）
- **常用麦克风预设值**：提供标准测量麦克风的典型灵敏度值
- **校准数据管理**：保存和应用麦克风校准设置到SPL分析

#### 4.2.5 实时显示
- **时域信号图**：显示采集到的原始信号波形，支持多通道叠加显示
- **功率谱密度图**：显示信号的频域特性，支持线性/对数坐标切换

### 4.3 数据保存规则

#### 4.3.1 自动工作区保存
每次通过 UI 进行数据采集后，数据会**自动保存**到 MATLAB 工作区中：
- `daqData`：采集的原始数据矩阵 [采样点数 × 通道数]
- `daqConfig`：采集配置参数结构体
- `daqTimeVector`：时间向量
- `daqResults`：完整结果结构体

这些变量可以直接在命令窗口中使用，无需额外操作。

#### 4.3.2 文件保存
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

#### 4.3.3 会话保存
无论是否保存文件，采集数据都会保存在 UI 会话中，可通过以下方式访问：
```matlab
% 获取 UI 窗口句柄
h = findall(0, 'Type', 'figure', 'Name', 'USB DAQ 数据采集系统');
% 读取最近一次采集的数据
acquired = getappdata(h, 'lastAcquiredData');
```

#### 4.3.4 数据访问示例
```matlab
% 1. 直接使用工作区变量（推荐）
plot(daqTimeVector, daqData);  % 绘制时域数据
fft_data = fft(daqData);       % 进行 FFT 分析

% 2. 加载保存的 MAT 文件
d = load('data/usb_daq_20251018_153045.mat');
acquired = d.acquired;
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

### 4.5 SPL (声压级) 分析功能

#### 4.5.1 SPL分析介绍
系统集成了基于 MATLAB Audio Toolbox `splMeter` 对象的专业声压级分析功能，支持：
- **全频带分析**：计算总声压级
- **1/1倍频程分析**：按倍频程频带分析
- **1/3倍频程分析**：按1/3倍频程频带分析
- **多种加权**：支持A加权、C加权和Z加权（线性）
- **麦克风校准**：支持麦克风灵敏度校准，输出真实的dB SPL值

#### 4.5.2 麦克风校准设置
在进行SPL分析前，建议先设置麦克风校准：

1. **打开校准对话框**：点击"麦克风校准"按钮
2. **设置麦克风灵敏度**：
   - 选择要启用的通道
   - 输入各通道麦克风的灵敏度值（mV/Pa）
   - 可使用"常用麦克风"按钮快速填入典型值
3. **保存设置**：点击"保存设置"完成校准配置

**常用麦克风灵敏度参考值**：
- Brüel & Kjær 1/2英寸麦克风：~50 mV/Pa
- PCB 1/4英寸麦克风：~10-20 mV/Pa
- GRAS 1/2英寸麦克风：~50 mV/Pa

#### 4.5.3 使用SPL分析（UI界面）
1. **麦克风校准**：设置各通道麦克风灵敏度（推荐步骤）
2. **数据采集**：完成数据采集（任意通道）
3. **选择分析参数**：
   - **加权模式**：选择 A、C 或 Z 加权
   - **带宽类型**：选择全频带、1/1倍频程或1/3倍频程
4. **执行分析**：点击"SPL分析"按钮
5. **查看结果**：
   - 自动弹出SPL分析结果图表
   - 结果保存到工作区变量：`splTotal`, `splOctave`, `splCenterFreq`
   - 显示校准后的真实声压级（dB SPL）

#### 4.5.4 使用SPL分析（脚本模式）
```matlab
% 基本分析（无校准）
[SPL_total, SPL_oct, fc] = analyze_spl(daqData, daqConfig.SampleRate, 'A', 'third_octave');

% 带麦克风校准的分析
micSensitivity = [50, 50, 10, 10, 50, 50, 20, 20]; % mV/Pa
channelIndices = [0, 1, 2, 3]; % 0-based通道索引
[SPL_total, SPL_oct, fc] = analyze_spl(daqData, daqConfig.SampleRate, 'A', 'third_octave', ...
                                      'MicSensitivity', micSensitivity, ...
                                      'ChannelIndices', channelIndices);

% 结果可视化
plotSPLResults(SPL_total, SPL_oct, fc, channelIndices, 'A', 'third_octave');
```

#### 4.5.5 SPL分析要求
- **必需工具箱**：MATLAB Audio Toolbox
- **最低版本**：MATLAB R2018a （`splMeter` 对象引入版本）
- **数据格式**：支持多通道同时分析
- **输出变量**：
  - `splTotal` - 各通道总声压级 [通道数×1] (dB 或 dB SPL)
  - `splOctave` - 频带声压级 [频带数×通道数] (dB 或 dB SPL)
  - `splCenterFreq` - 中心频率 [频带数×1] (Hz)

#### 4.5.6 校准与非校准模式对比
- **无校准模式**：输出相对声压级（dB），适用于信号分析和对比
- **校准模式**：输出绝对声压级（dB SPL），符合声学测量标准，可与标准声级计对比

### 4.6 使用流程示例

1. **基本采集流程**：
   ```
   启动 daq_ui → 选择通道 → 设置参数 → 开始采集 → 查看结果
   ```

2. **多通道对比分析**：
   ```
   选择多个通道 → 采集 → 在时域/频域图中对比不同通道特性
   ```

3. **专业声学SPL测量流程**：
   ```
   启动 daq_ui → 麦克风校准 → 采集声学信号 → SPL分析 → 查看校准后的dB SPL结果
   ```

4. **音频信号分析**：
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

### 5.2 SPL分析编程接口

统一的 `analyze_spl` 函数支持灵活的编程调用：

```matlab
% 基本用法
[SPL_total, SPL_oct, fc] = analyze_spl(data, fs, mode, bandwidth_type);

% 带麦克风校准
[SPL_total, SPL_oct, fc] = analyze_spl(data, fs, mode, bandwidth_type, ...
                                      'MicSensitivity', sensitivity_array, ...
                                      'ChannelIndices', channel_indices);

% 参数说明
% data: [通道×采样点] 或 [采样点×通道] 数据矩阵
% fs: 采样频率 (Hz)
% mode: 'A', 'C', 或 'Z' 加权
% bandwidth_type: 'fullband', 'octave', 或 'third_octave'
% sensitivity_array: [8×1] 麦克风灵敏度 (mV/Pa)
% channel_indices: [N×1] 0-based通道索引
```

### 5.3 演示脚本

项目提供了多个演示脚本，可帮助理解不同的采集模式：

```matlab
% 单次 AD 采样演示
daq_demo_ad_single

% 连续轮询采样演示  
daq_demo_continuous_poll

% 自定义通道采集
usb_daq_custom_channels
```
