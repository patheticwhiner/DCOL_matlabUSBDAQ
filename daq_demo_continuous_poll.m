% daq_demo_continuous_poll.m
% 演示使用 ad_continu_conf + Get_AdBuf_Size + Read_AdBuf + AD_continu_stop 的轮询式连续采样。
% 该脚本会：
% - 配置连续采样（指定通道范围、采样率等）
% - 轮询设备缓冲区直到采集到目标样本数
% - 读取数据并按通道拆分
% - 打印采集耗时与估算有效采样率

clear; clc; close all;
fprintf('=== daq_demo_continuous_poll: 配置+轮询连续采样示例 ===\n');

% 1. 配置参数（按需修改）
dev = 0;
ad_os = 0;         % 无过采样，SampleRate可设最大值为100kHz, 3dB带宽15kHz 
ad_range = 0;
ch_first = 0;      % 硬件通道起始
ch_last = 3;       % 硬件通道结束（包含）
SampleRate = 44100; % 期望采样率（Hz）
SampleTime = 10; % 期望采样时长（s）
SamplesPerChannel = SampleTime * SampleRate; % 每通道采样点数（示例：1 秒）

trig_sl = 0;      % 触发模式设计为软件启动（而非外部触发）
trig_pol = 0;     % 触发输入极性选择为上升沿触发（而非下降沿触发）
clk_sl = 0;       % 时钟模式选择为内部时钟
ext_clk_pol = 0;  % （可选）外部时钟极性

dllName = 'Usb_Daq_V52_Dll';
try
    % 2. 加载库并打开设备
    if ~libisloaded(dllName)
        loadlibrary(dllName, fullfile(fileparts(mfilename('fullpath')), 'Usb_Daq_V52_Dll.h'));
    end
    resOpen = calllib(dllName, 'openUSB');
    if resOpen ~= 0
        error('openUSB 返回错误: %d', resOpen);
    end

    numChannels = 8;    % 硬件具有8个模拟输入通道
    totalSamples = SamplesPerChannel * numChannels; % num 参数通常为总样点数

    % 3. 配置连续采集节段
    fprintf('调用 ad_continu_conf 启动连续采集 (ch %d-%d, rate=%d Hz)\n', ch_first, ch_last, SampleRate);
    resConf = calllib(dllName, 'ad_continu_conf', dev, ad_os, ad_range, ch_first, ch_last, SampleRate, trig_sl, trig_pol, clk_sl, ext_clk_pol);
    if resConf ~= 0
        error('ad_continu_conf 返回错误: %d', resConf);
    end

    % 4. 轮询缓冲区直到采集到足够数据
    fprintf('开始轮询缓冲区，目标总样本数：%d\n', totalSamples);
    available = calllib(dllName, 'Get_AdBuf_Size', dev);
    fprintf('检测到数据 (available=%d)\n', available);
    tstart = tic;
    max_wait = 60; % 最长等待秒数
    elapsed = 0;
    available = 0;
    ticStart = tic;
    while true
        available = calllib(dllName, 'Get_AdBuf_Size', dev);
        % Get_AdBuf_Size 返回缓冲区中可读取的数据长度（float 数目）
        if available >= totalSamples
            break;
        end
        elapsed = toc(ticStart);
        if elapsed > max_wait
            error('等待缓冲超时 (%.1f s)，可用样本: %d', elapsed, available);
        end
        pause(0.01); % 小延时，避免忙等
    end
    t_poll = toc(tstart);
    fprintf('检测到足够数据 (available=%d)，准备 Read_AdBuf，轮询耗时: %.6f s\n', available, t_poll);

    % 分批读出（确保不超过 available）
    numToRead = totalSamples; % 这里一次性读出全部
    pv = libpointer('singlePtr', single(zeros(8, SamplesPerChannel)));
    tReadStart = tic;
    nread = calllib(dllName, 'Read_AdBuf', dev, pv, int32(numToRead));
    tRead = toc(tReadStart);
    if nread <= 0
        error('Read_AdBuf 读取失败或无数据，返回: %d', nread);
    end
    dataFlat = get(pv, 'Value');
    fprintf('Read_AdBuf 读取到 %d 个浮点样本，耗时: %.6f s\n', nread, tRead);

    % 停止采集并清理缓冲
    calllib(dllName, 'AD_continu_stop', dev);

    % 关闭设备与卸载
    calllib(dllName, 'closeUSB');
    unloadlibrary(dllName);

    total_time = t_poll + tRead;
    effective_rate = SamplesPerChannel / total_time;
    fprintf('总耗时(轮询+读出): %.6f s, 目标每通道样点: %d, 估算有效采样率: %.2f Hz\n', total_time, SamplesPerChannel, effective_rate);
catch ME
    fprintf('错误: %s\n', ME.message);
    if libisloaded(dllName)
        try
            calllib(dllName, 'AD_continu_stop', dev);
        catch
        end
        try
            calllib(dllName, 'closeUSB');
        catch
        end
        unloadlibrary(dllName);
    end
end

fprintf('脚本结束。\n');