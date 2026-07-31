%% USB DAQ 数据采集系统
% 封装后的数据采集程序，只需要设置必要参数即可使用
% 作者：GitHub Copilot
% 日期：2025-08-23

%% 主程序 
clear; close all; clc;
addpath(fullfile(fileparts(mfilename('fullpath')), 'custom'));
fprintf('=== USB DAQ 数据采集系统启动 ===\n');

% 配置参数（对应原始程序的设置）
SampleRate = 44100;    
SampleBand = SampleRate/2;
SampleTime = 10;        % 采样时间 12秒
CorrectSampleTime = SampleTime;
ChannelIndices = (0:7);   % 只采集一个通道，如5

% 执行数据采集
[data, config] = usb_daq_acquire('SampleBand', SampleBand, ...
                                 'SampleTime', CorrectSampleTime, ...
                                 'ChannelIndices', ChannelIndices);

%% 绘制图形（循环绘制所有通道）
fprintf('绘制数据图形...\n');
% 绘制图形（单通道）
for i = 1:config.NumChannels
    figure;
    plot(data(i, :));
    title(sprintf('通道: %d', config.ChannelIndices(i)));
    ylabel('幅值');
    xlabel('采样点');
    grid on;
end

% 显示采集结果摘要
fprintf('\n=== 数据采集完成 ===\n');
fprintf('采样频率: %d Hz\n', config.SampleRate);
fprintf('采样时间: %.1f 秒\n', config.SampleTime);
fprintf('每通道采样点数: %d\n', config.SamplesPerChannel);
fprintf('采集通道序号: [%s]\n', num2str(config.ChannelIndices));
fprintf('采集通道数量: %d\n', config.NumChannels);
fprintf('硬件通道范围: %d-%d\n', config.ChannelFirst, config.ChannelLast);
fprintf('数据矩阵大小: %dx%d\n', size(data));

% 可选：显示每个通道的统计信息
fprintf('\n=== 通道数据统计 ===\n');

for i = 1:config.NumChannels
    fprintf('通道%d: 最大值=%.6f, 最小值=%.6f, 均值=%.6f, 标准差=%.6f\n', ...
        config.ChannelIndices(i), max(data(i,:)), min(data(i,:)), mean(data(i,:)), std(data(i,:)));
end

fprintf('\n程序执行完毕！\n');
