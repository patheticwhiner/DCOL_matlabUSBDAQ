function [data, config] = usb_daq_acquire(varargin)
    % USB DAQ 数据采集函数
    %
    % 使用方法：
    % 1. [data, config] = usb_daq_acquire(); % 使用默认参数
    % 2. [data, config] = usb_daq_acquire('SampleBand', 192000, 'SampleTime', 12); % 自定义参数
    % 3. [data, config] = usb_daq_acquire('SampleBand', 200000, 'NumChannels', 4);
    %
    % 输入参数（可选）：
    %   'SampleBand'      - 采样带宽 (Hz)，默认: 192000
    %   'SampleTime'      - 采样时间 (秒)，默认: 12
    %   'ChannelIndices'  - 要采集的通道序号数组 (0-7)，默认: [0,1,2,3]
    %   'DeviceID'        - 设备ID，默认: 0
    %   'ADGain'          - AD增益，默认: 0
    %   'Oversample'      - 过采样，默认: 0
    %
    % 输出参数：
    %   data   - 采集到的数据矩阵 [选择的通道数 × 采样点数]
    %   config - 配置参数结构体
    
    % 解析输入参数
    config = parse_inputs(varargin{:});
    
    % 初始化USB DAQ设备
    % 检查库是否已加载，如果已加载则先卸载
    if libisloaded('Usb_Daq_V52_Dll')
        unloadlibrary('Usb_Daq_V52_Dll');
    end
    % 加载动态链接库
    loadlibrary('Usb_Daq_V52_Dll','Usb_Daq_V52_Dll.h');
    % 打开USB设备
    result = calllib('Usb_Daq_V52_Dll','openUSB');
    if result ~= 0
        warning('USB设备打开失败，错误代码: %d', result);
    end
    
    % 执行数据采集
    fprintf('开始数据采集...\n');
    fprintf('采样频率: %d Hz\n', config.SampleBand);
    fprintf('采样时间: %.1f 秒\n', config.SampleTime);
    fprintf('采集通道: [%s]\n', num2str(config.ChannelIndices));
    
    data = acquire_data(config);
    
    % 关闭设备：如何使得出现warning时也运行这一行？
    cleanup_usb_daq();

    fprintf('数据采集完成！\n');
end