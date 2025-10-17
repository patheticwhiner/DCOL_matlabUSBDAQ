% 绘图函数（从startSampling中提取，供重新绘图使用）
function plotDaqData(fig, data, config, timeChannelCheckboxes, freqChannelCheckboxes, logScaleCheckbox)
    % 获取轴对象
    allAxes = findobj(fig, 'Type', 'axes');
    
    if length(allAxes) >= 2
        positions = zeros(length(allAxes), 4);
        for j = 1:length(allAxes)
            try
                positions(j, :) = get(allAxes(j), 'Position');
            catch
                positions(j, :) = [0, j*100, 100, 100];
            end
        end
        [~, idx] = sort(positions(:,2), 'descend');
        axTimeDomain = allAxes(idx(1));
        axFrequencyDomain = allAxes(idx(2));
    else
        error('未找到足够的轴对象');
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
end