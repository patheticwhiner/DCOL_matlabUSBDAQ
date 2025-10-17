function config = parse_inputs(varargin)
    % 解析输入参数并设置默认值
    
    % 默认配置
    config.SampleBand = 192000;         % 采样带宽 (Hz)
    config.SampleTime = 12;             % 采样时间 (秒)
    config.DeviceID = 0;                % 设备ID
    config.ADGain = 0;                  % AD增益
    config.Oversample = 0;              % 过采样

    % 解析可变参数
    for i = 1:2:length(varargin)
        param = varargin{i};
        value = varargin{i+1};
        
        switch param
            case 'SampleBand'
                config.SampleBand = value;
            case 'SampleTime'
                config.SampleTime = value;
            case 'ChannelIndices'
                config.ChannelIndices = value;
            case 'DeviceID'
                config.DeviceID = value;
            case 'ADGain'
                config.ADGain = value;
            case 'Oversample'
                config.Oversample = value;
            otherwise
                warning('未知参数: %s', param);
        end
    end
    
    if any(config.ChannelIndices < 0) || any(config.ChannelIndices > 7)
        error('通道序号必须在 0-7 范围内');
    end
    
    % 计算相关参数
    config.NumChannels = length(config.ChannelIndices);  % 实际采集的通道数
    config.ChannelFirst = min(config.ChannelIndices);    % 起始通道
    config.ChannelLast = max(config.ChannelIndices);     % 结束通道
    config.TotalHardwareChannels = 8;  % 硬件通道范围(内部编程，不可修改)
    
    % 计算采样点数
    samples_per_channel = round(config.SampleBand * 2 * config.SampleTime);
    config.TotalSamples = samples_per_channel * config.TotalHardwareChannels;
    config.SamplesPerChannel = samples_per_channel;
end