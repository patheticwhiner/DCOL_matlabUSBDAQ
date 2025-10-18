% SPL分析功能
function performSPLAnalysis(fig, weighting_mode, bandwidth_selection)
    % 从 UI 的 appdata 中获取最后一次采集的数据
    acquired = getappdata(fig, 'lastAcquiredData');
    
    if isempty(acquired)
        uialert(fig, '没有可分析的数据。请先进行数据采集。', 'SPL分析失败');
        return;
    end
    
    try
        % 转换带宽选择
        switch bandwidth_selection
            case '全频带'
                bandwidth_type = 'fullband';
            case '1/1倍频程'
                bandwidth_type = 'octave';
            case '1/3倍频程'
                bandwidth_type = 'third_octave';
            otherwise
                bandwidth_type = 'third_octave';
        end
        
        % 获取麦克风校准信息
        micSensitivity = getappdata(fig, 'microphoneSensitivity');
        micEnabled = getappdata(fig, 'microphoneEnabled');
        
        % 如果没有校准信息，使用默认值
        if isempty(micSensitivity)
            micSensitivity = 50 * ones(8, 1); % 默认50 mV/Pa
            micEnabled = true(8, 1);
            fprintf('警告: 未设置麦克风灵敏度，使用默认值 50 mV/Pa\n');
            fprintf('建议点击"麦克风校准"按钮设置正确的灵敏度值\n');
        end
        
        % 执行SPL分析
        fprintf('开始SPL分析...\n');
        fprintf('数据大小: %d × %d (通道 × 采样点)\n', size(acquired.data));
        fprintf('采样率: %.0f Hz\n', acquired.config.SampleRate);
        fprintf('加权模式: %s\n', weighting_mode);
        fprintf('带宽类型: %s\n', bandwidth_selection);
        
        % 显示麦克风校准信息
        activeChannels = acquired.config.ChannelIndices;
        fprintf('麦克风灵敏度设置:\n');
        for i = 1:length(activeChannels)
            ch_idx = activeChannels(i) + 1; % 转换为1-based索引
            if ch_idx <= length(micSensitivity)
                fprintf('  通道 %d: %.2f mV/Pa\n', activeChannels(i), micSensitivity(ch_idx));
            else
                fprintf('  通道 %d: 使用默认值 50 mV/Pa\n', activeChannels(i));
            end
        end
        
        % 调用统一的SPL分析函数，传入麦克风灵敏度
        [SPL_total, SPL_oct, fc] = analyze_spl(acquired.data, acquired.config.SampleRate, ...
                                              weighting_mode, bandwidth_type, ...
                                              'MicSensitivity', micSensitivity, ...
                                              'ChannelIndices', acquired.config.ChannelIndices);
        
        % 将SPL结果保存到工作区
        assignin('base', 'splTotal', SPL_total);
        assignin('base', 'splOctave', SPL_oct);
        assignin('base', 'splCenterFreq', fc);
        
        % 绘制SPL结果
        plotSPLResults(SPL_total, SPL_oct, fc, acquired.config.ChannelIndices, ...
                      weighting_mode, bandwidth_type);
        
        % 显示成功消息
        msg = sprintf(['SPL分析完成！\n\n' ...
                      '结果已保存到工作区:\n' ...
                      '• splTotal - 总声压级 (dB SPL)\n' ...
                      '• splOctave - 频带声压级 (dB SPL)\n' ...
                      '• splCenterFreq - 中心频率 (Hz)\n\n' ...
                      '分析参数:\n' ...
                      '• 加权: %s\n' ...
                      '• 带宽: %s\n' ...
                      '• 通道数: %d\n' ...
                      '• 麦克风校准: 已应用\n\n' ...
                      '注意: 结果为校准后的声压级 (dB SPL)'], ...
                      weighting_mode, bandwidth_selection, length(SPL_total));
        
        uialert(fig, msg, 'SPL分析完成', 'Icon', 'success');
        
        fprintf('SPL分析完成！结果已保存到工作区变量。\n\n');
        
    catch ME
        error_msg = sprintf('SPL分析失败: %s', ME.message);
        uialert(fig, error_msg, '错误');
        fprintf('SPL分析失败: %s\n', ME.message);
        
        % 检查是否缺少Audio Toolbox
        if contains(ME.message, 'splMeter') || contains(ME.message, 'Undefined')
            uialert(fig, ['SPL分析需要 MATLAB Audio Toolbox。' newline ...
                         '请确保已安装 Audio Toolbox 并且 MATLAB 版本为 R2018a 或更高。'], ...
                   '缺少工具箱');
        end
    end
end