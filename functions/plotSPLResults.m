function plotSPLResults(SPL_total, SPL_oct, fc, channel_indices, mode, bandwidth_type)
%PLOTSPLRESULTS  绘制SPL分析结果
%
%   plotSPLResults(SPL_total, SPL_oct, fc, channel_indices, mode, bandwidth_type)
%
%   输入参数:
%     SPL_total       - 总声压级 [通道数 × 1]
%     SPL_oct         - 频带声压级 [频带数 × 通道数]
%     fc              - 中心频率 [频带数 × 1] (可为空)
%     channel_indices - 通道索引 [通道数 × 1]
%     mode            - 加权模式 ('A', 'C', 'Z')
%     bandwidth_type  - 带宽类型 ('fullband', 'octave', 'third_octave')

    num_channels = length(SPL_total);
    
    % 创建新图窗
    fig = figure('Name', 'SPL分析结果', 'Position', [200, 200, 1200, 800]);
    
    if strcmp(bandwidth_type, 'fullband')
        % 全频带模式 - 只显示总SPL
        subplot(2, 1, 1);
        bar(channel_indices, SPL_total, 'FaceColor', [0.2, 0.6, 0.8]);
        xlabel('通道');
        ylabel('SPL (dB)');
        title(sprintf('总声压级 (%s加权)', mode));
        grid on;
        
        % 添加数值标签
        for i = 1:num_channels
            text(channel_indices(i), SPL_total(i) + 1, sprintf('%.1f', SPL_total(i)), ...
                 'HorizontalAlignment', 'center', 'FontSize', 10);
        end
        
        % 下半部分显示总SPL表格
        subplot(2, 1, 2);
        axis off;
        
        % 创建表格数据
        table_data = cell(num_channels + 1, 2);
        table_data{1, 1} = '通道';
        table_data{1, 2} = sprintf('SPL (dB %s)', mode);
        
        for i = 1:num_channels
            table_data{i+1, 1} = sprintf('CH%d', channel_indices(i));
            table_data{i+1, 2} = sprintf('%.2f', SPL_total(i));
        end
        
        % 显示表格
        table_pos = [0.1, 0.1, 0.8, 0.3];
        uitable('Parent', fig, 'Data', table_data(2:end, :), ...
               'ColumnName', table_data(1, :), ...
               'Position', table_pos, 'Units', 'normalized');
        
    else
        % 倍频程模式 - 显示频带分析和总SPL
        
        % 上半部分：频带SPL
        subplot(2, 1, 1);
        if num_channels == 1
            % 单通道
            semilogx(fc, SPL_oct(:, 1), 'o-', 'LineWidth', 2, 'MarkerSize', 6);
            xlabel('频率 (Hz)');
            ylabel('Leq (dB)');
            title(sprintf('%s频带分析 - 通道 %d (%s加权)', ...
                  get_bandwidth_name(bandwidth_type), channel_indices(1), mode));
            grid on;
            
            % 设置x轴刻度
            set(gca, 'XTick', fc);
            if length(fc) <= 10
                set(gca, 'XTickLabel', arrayfun(@num2str, fc, 'UniformOutput', false));
            end
            
        else
            % 多通道
            colors = lines(num_channels);
            hold on;
            legend_entries = cell(num_channels, 1);
            
            for ch = 1:num_channels
                semilogx(fc, SPL_oct(:, ch), 'o-', 'Color', colors(ch, :), ...
                        'LineWidth', 2, 'MarkerSize', 6);
                legend_entries{ch} = sprintf('CH%d', channel_indices(ch));
            end
            
            xlabel('频率 (Hz)');
            ylabel('Leq (dB)');
            title(sprintf('%s频带分析 (%s加权)', get_bandwidth_name(bandwidth_type), mode));
            legend(legend_entries, 'Location', 'best');
            grid on;
            
            % 设置x轴刻度
            set(gca, 'XTick', fc);
            if length(fc) <= 10
                set(gca, 'XTickLabel', arrayfun(@num2str, fc, 'UniformOutput', false));
            end
            hold off;
        end
        
        % 下半部分：总SPL柱状图
        subplot(2, 1, 2);
        bar(channel_indices, SPL_total, 'FaceColor', [0.8, 0.4, 0.2]);
        xlabel('通道');
        ylabel('总SPL (dB)');
        title(sprintf('总声压级 (%s加权)', mode));
        grid on;
        
        % 添加数值标签
        for i = 1:num_channels
            text(channel_indices(i), SPL_total(i) + 1, sprintf('%.1f', SPL_total(i)), ...
                 'HorizontalAlignment', 'center', 'FontSize', 10);
        end
    end
    
    % 在命令窗口输出结果摘要
    fprintf('\n=== SPL分析结果摘要 ===\n');
    fprintf('分析模式: %s加权, %s\n', mode, get_bandwidth_name(bandwidth_type));
    for i = 1:num_channels
        fprintf('通道 %d: %.2f dB\n', channel_indices(i), SPL_total(i));
    end
    fprintf('====================\n\n');
end

function name = get_bandwidth_name(bandwidth_type)
    switch lower(bandwidth_type)
        case 'fullband'
            name = '全频带';
        case 'octave'
            name = '1/1倍频程';
        case 'third_octave'
            name = '1/3倍频程';
        otherwise
            name = bandwidth_type;
    end
end