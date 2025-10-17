function startSampling(fig, sampleRate, sampleTime, channelCheckboxes, timeChannelCheckboxes, freqChannelCheckboxes, logScaleCheckbox, saveDataCheckbox)
    % 获取选中的采集通道
    selectedChannels = [];
    for i = 1:length(channelCheckboxes)
        if channelCheckboxes(i).Value
            selectedChannels = [selectedChannels, i-1]; % 通道索引从0开始
        end
    end
    
    if isempty(selectedChannels)
        uialert(fig, '请至少选择一个采集通道。', '错误');
        return;
    end

    % 执行数据采集
    fprintf('=== USB DAQ 数据采集系统启动 ===\n');
    addpath('functions');
    [data, config] = usb_daq_acquire('SampleBand', sampleRate/2, ...
                                     'SampleTime', sampleTime, ...
                                     'ChannelIndices', selectedChannels);
    config.SampleRate = config.SampleBand*2;
    % 绘制图像到 UI
    % 获取所有 axes 对象（包括 uiaxes）
    allAxes = findobj(fig, 'Type', 'axes');
    
    % 如果没找到 axes，尝试查找所有子对象
    if isempty(allAxes)
        allChildren = findobj(fig);
        fprintf('图形对象的所有子对象类型：\n');
        for j = 1:length(allChildren)
            fprintf('  %s\n', class(allChildren(j)));
        end
        
        % 尝试查找包含 'axes' 的对象
        allAxes = findobj(fig, '-regexp', 'Type', '.*axes.*');
    end
    
    % 根据位置确定是时域还是频域轴（时域在上方，频域在下方）
    if length(allAxes) >= 2
        % 根据 Y 位置排序，Y 值较大的是时域轴（上方）
        positions = zeros(length(allAxes), 4);
        for j = 1:length(allAxes)
            try
                positions(j, :) = get(allAxes(j), 'Position');
            catch
                positions(j, :) = [0, j*100, 100, 100]; % 默认位置
            end
        end
        [~, idx] = sort(positions(:,2), 'descend'); % 按 Y 坐标降序排列
        axTimeDomain = allAxes(idx(1));      % Y 值最大的（上方）
        axFrequencyDomain = allAxes(idx(2)); % Y 值较小的（下方）
    else
        fprintf('找到 %d 个轴对象，需要至少 2 个\n', length(allAxes));
        if length(allAxes) == 1
            axTimeDomain = allAxes(1);
            axFrequencyDomain = allAxes(1); % 使用同一个轴
        else
            error('未找到足够的轴对象');
        end
    end

    % 定义颜色映射（8种不同颜色）
    colors = [
        0 0.4470 0.7410;     % 蓝色
        0.8500 0.3250 0.0980; % 橙色
        0.9290 0.6940 0.1250; % 黄色
        0.4940 0.1840 0.5560; % 紫色
        0.4660 0.6740 0.1880; % 绿色
        0.3010 0.7450 0.9330; % 青色
        0.6350 0.0780 0.1840; % 红色
        0 0 0                 % 黑色
    ];

    % 清空图形
    cla(axTimeDomain);
    cla(axFrequencyDomain);
    
    % 绘制时域信号
    hold(axTimeDomain, 'on');
    timeDisplayCount = 0;
    for i = 1:config.NumChannels
        channelIdx = config.ChannelIndices(i);
        % 检查该通道是否需要显示时域信号
        if channelIdx < length(timeChannelCheckboxes) && timeChannelCheckboxes(channelIdx+1).Value
            colorIdx = mod(channelIdx, size(colors, 1)) + 1;
            plot(axTimeDomain, data(i, :), 'Color', colors(colorIdx, :), ...
                 'DisplayName', sprintf('通道 %d', channelIdx), 'LineWidth', 1);
            timeDisplayCount = timeDisplayCount + 1;
        end
    end
    hold(axTimeDomain, 'off');
    
    if timeDisplayCount > 0
        title(axTimeDomain, sprintf('时域信号 (%d个通道)', timeDisplayCount));
        legend(axTimeDomain, 'show', 'Location', 'best');
    else
        title(axTimeDomain, '时域信号（无选中通道）');
    end

    % 绘制功率谱
    hold(axFrequencyDomain, 'on');
    freqDisplayCount = 0;
    useLogScale = logScaleCheckbox.Value; % 获取对数坐标选项
    
    for i = 1:config.NumChannels
        channelIdx = config.ChannelIndices(i);
        % 检查该通道是否需要显示频域信号
        if channelIdx < length(freqChannelCheckboxes) && freqChannelCheckboxes(channelIdx+1).Value
            % 功率谱分析
            N = length(data(i, :));
            
            % 去除直流分量（可选，提高频谱质量）
            signal = data(i, :) - mean(data(i, :));
            
            % 应用窗函数（汉宁窗，减少频谱泄漏）
            window = hann(N)';
            signal_windowed = signal .* window;
            
            X = fft(signal_windowed);
            
            % 修正的功率谱密度计算
            % 考虑窗函数的功率损失
            window_power = sum(window.^2) / N;
            
            % 计算功率谱密度 (PSD) - 修正版本
            P2 = (abs(X).^2) / (config.SampleRate * N * window_power); % 双侧功率谱密度
            P1 = P2(1:N/2+1); % 单侧功率谱密度
            P1(2:end-1) = 2 * P1(2:end-1); % 除直流和奈奎斯特频率外，其他频率分量乘以2
            
            f = config.SampleRate * (0:(N/2)) / N;
            
            % 根据用户选择使用线性或对数坐标绘制
            colorIdx = mod(channelIdx, size(colors, 1)) + 1;
            if useLogScale
                semilogy(axFrequencyDomain, f, P1, 'Color', colors(colorIdx, :), ...
                         'DisplayName', sprintf('通道 %d', channelIdx), 'LineWidth', 1);
            else
                plot(axFrequencyDomain, f, P1, 'Color', colors(colorIdx, :), ...
                     'DisplayName', sprintf('通道 %d', channelIdx), 'LineWidth', 1);
            end
            freqDisplayCount = freqDisplayCount + 1;
        end
    end
    hold(axFrequencyDomain, 'off');
    
    if freqDisplayCount > 0
        scaleType = '';
        if useLogScale
            scaleType = ' (对数坐标)';
            set(axFrequencyDomain, 'YScale', 'log');
        else
            scaleType = ' (线性坐标)';
            set(axFrequencyDomain, 'YScale', 'linear');
        end
        title(axFrequencyDomain, sprintf('功率谱密度 (%d个通道)%s', freqDisplayCount, scaleType));
        legend(axFrequencyDomain, 'show', 'Location', 'best');
    else
        title(axFrequencyDomain, '功率谱密度（无选中通道）');
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

    % ---------------------------
    % 持久化保存采集数据（受 UI 复选框控制）
    % ---------------------------
    try
        acquired.selectedChannels = selectedChannels;
        acquired.data = data;
        acquired.config = config;
        acquired.timeChannelCheckboxes = arrayfun(@(c) c.Value, timeChannelCheckboxes);
        acquired.freqChannelCheckboxes = arrayfun(@(c) c.Value, freqChannelCheckboxes);
        acquired.logScale = logScaleCheckbox.Value;

        % 将数据先保存到 UI 会话（appdata）以便后续访问
        setappdata(fig, 'lastAcquiredData', acquired);

        % 如果用户选中了保存选项，则写入磁盘
        if saveDataCheckbox.Value
            timestamp = datestr(now, 'yyyymmdd_HHMMSS');
            saveDir = fullfile(pwd, 'data');
            if ~exist(saveDir, 'dir')
                mkdir(saveDir);
            end
            savePath = fullfile(saveDir, sprintf('usb_daq_%s.mat', timestamp));
            save(savePath, 'acquired');
            % 提示用户
            uialert(fig, sprintf('数据已保存：%s', savePath), '保存成功');
        else
            % 仅在会话中保留数据
            uialert(fig, '数据已保存在会话内（未写入磁盘）。可使用“保存”按钮导出。', '会话保存');
        end
    catch ME
        warning('保存采集数据时发生错误: %s', ME.message);
    end
end