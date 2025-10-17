% daq_demo_ad_single.m
% 演示 Usb_Daq_V52_Dll 的 ad_single 函数。
% 说明：ad_single 会一次性返回 16 路模拟输入的电压值（float 数组，长度 16）。
% 用法：在 MATLAB 中运行此脚本（需管理员权限或允许加载 DLL），观察每路通道的电压读数。

clear; clc;
fprintf('=== daq_demo_ad_single: 单次同步 AD 采样示例 ===\n');

dev = 0;           % 设备号（通常是 0）
ad_os = 0;         % 过采样或采样模式，按设备手册配置
ad_range = 0;      % 量程索引（0: +/-10V, 1: +/-5V 等，参考设备手册）

dllName = 'Usb_Daq_V52_Dll';
try
    if ~libisloaded(dllName)
        fprintf('加载库 %s...\n', dllName);
        loadlibrary(dllName, fullfile(fileparts(mfilename('fullpath')), 'Usb_Daq_V52_Dll.h'));
    else
        fprintf('库已加载：%s\n', dllName);
    end

    resOpen = calllib(dllName, 'openUSB');
    if resOpen ~= 0
        error('openUSB 返回错误: %d', resOpen);
    end
    fprintf('设备打开成功（dev=%d）。\n', dev);

    % 准备缓冲区并调用 ad_single
    ad_buf = single(zeros(1, 16));
    pv = libpointer('singlePtr', ad_buf);

    tic;
    result = calllib(dllName, 'ad_single', dev, ad_os, ad_range, pv);
    elapsed = toc;

    if result ~= 0
        error('ad_single 返回错误: %d', result);
    end

    values = get(pv, 'Value');
    fprintf('ad_single 完成（耗时 %.6f s），返回 %d 个通道值：\n', elapsed, numel(values));
    for ch = 0:15
        fprintf('  CH%02d: %.6f V\n', ch, values(ch+1));
    end

    % 简单统计
    fprintf('均值: %.6f, 标准差: %.6f\n', mean(values), std(values));

catch ME
    fprintf('错误: %s\n', ME.message);
end

% 清理
try
    if libisloaded(dllName)
        calllib(dllName, 'closeUSB');
        unloadlibrary(dllName);
        fprintf('已关闭设备并卸载库。\n');
    end
catch ME2
    warning('清理时发生异常: %s', ME2.message);
end

fprintf('脚本结束。\n');
