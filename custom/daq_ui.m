%% USB DAQ 数据采集系统 - 带 UI 界面
% 通过图窗设置采样参数并完成采样，绘制信号图像和频谱分析
% 作者：GitHub Copilot

function daq_ui()
    % 创建 UI 界面 - 增加窗口宽度以容纳右侧控制区域
    fig = uifigure('Name', 'USB DAQ 数据采集系统', 'Position', [100, 100, 1100, 700]);

    % 采样频率输入框
    uilabel(fig, 'Position', [20, 620, 100, 22], 'Text', '采样频率 (Hz):');
    sampleRateInput = uieditfield(fig, 'numeric', 'Position', [130, 620, 100, 22], 'Value', 44100);

    % 采样时间输入框
    uilabel(fig, 'Position', [20, 580, 100, 22], 'Text', '采样时间 (秒):');
    sampleTimeInput = uieditfield(fig, 'numeric', 'Position', [130, 580, 100, 22], 'Value', 10);

    % 通道选择区域：物理 AIN1-AIN8 对应软件 AD0-AD7
    uilabel(fig, 'Position', [20, 540, 100, 22], 'Text', '通道选择:');
    channelLabels = arrayfun(@formatChannelLabel, 0:7, 'UniformOutput', false);
    
    % 创建通道复选框（显示物理通道和软件通道，内部仍使用0-based索引）
    channelCheckboxes = [];
    for i = 0:7
        cb = uicheckbox(fig, 'Position', [20 + mod(i,2)*135, 510 - floor(i/2)*26, 125, 22], ...
                       'Text', channelLabels{i+1});
        channelCheckboxes = [channelCheckboxes, cb];
    end

    % 显示控制区域
    uilabel(fig, 'Position', [20, 400, 100, 22], 'Text', '显示控制:');
    
    % 时域显示控制
    uilabel(fig, 'Position', [20, 370, 80, 22], 'Text', '时域显示:');
    timeShowAllCheckbox = uicheckbox(fig, 'Position', [100, 370, 50, 22], 'Text', '全选', 'Value', true);
    timeChannelCheckboxes = [];
    for i = 0:7
        cb = uicheckbox(fig, 'Position', [20 + mod(i,2)*135, 340 - floor(i/2)*26, 125, 22], ...
                       'Text', channelLabels{i+1});
        timeChannelCheckboxes = [timeChannelCheckboxes, cb];
    end

    % 频域显示控制
    uilabel(fig, 'Position', [20, 230, 80, 22], 'Text', '频域显示:');
    freqShowAllCheckbox = uicheckbox(fig, 'Position', [100, 230, 50, 22], 'Text', '全选', 'Value', true);
    
    % 对数坐标选项
    logScaleCheckbox = uicheckbox(fig, 'Position', [170, 230, 80, 22], 'Text', '对数坐标', 'Value', true);
    
    freqChannelCheckboxes = [];
    for i = 0:7
        cb = uicheckbox(fig, 'Position', [20 + mod(i,2)*135, 200 - floor(i/2)*26, 125, 22], ...
                       'Text', channelLabels{i+1});
        freqChannelCheckboxes = [freqChannelCheckboxes, cb];
    end

    % 全选控制回调函数
    timeShowAllCheckbox.ValueChangedFcn = @(src, event) toggleAllChannels(timeChannelCheckboxes, src.Value);
    freqShowAllCheckbox.ValueChangedFcn = @(src, event) toggleAllChannels(freqChannelCheckboxes, src.Value);

    % =============== 右侧控制区域 ===============
    % 创建右侧控制面板背景
    rightPanelX = 850;  % 右侧面板起始X坐标
    
    % 添加右侧面板标题
    uilabel(fig, 'Position', [rightPanelX, 660, 150, 25], 'Text', '控制面板', ...
           'FontSize', 14, 'FontWeight', 'bold');
    
    % 保存数据选项 - 移动到右侧
    uilabel(fig, 'Position', [rightPanelX, 620, 100, 22], 'Text', '数据保存:', ...
           'FontWeight', 'bold', 'FontColor', [0.2, 0.2, 0.8]);
    saveDataCheckbox = uicheckbox(fig, 'Position', [rightPanelX+10, 600, 120, 22], 'Text', '保存数据到文件', 'Value', true);

    % 分隔线1
    uipanel(fig, 'Position', [rightPanelX, 585, 200, 2], 'BackgroundColor', [0.8, 0.8, 0.8]);

    % 播放控制：选择回放通道与播放按钮 - 移动到右侧
    uilabel(fig, 'Position', [rightPanelX, 560, 100, 22], 'Text', '音频播放:', ...
           'FontWeight', 'bold', 'FontColor', [0.8, 0.4, 0.2]);
    uilabel(fig, 'Position', [rightPanelX+10, 540, 80, 22], 'Text', '播放通道:');
    playbackChannelDrop = uidropdown(fig, 'Position', [rightPanelX+90, 540, 110, 22], ...
        'Items', channelLabels, 'ItemsData', 0:7, 'Value', 3);
    playButton = uibutton(fig, 'Position', [rightPanelX+10, 510, 100, 25], 'Text', '播放音频', ...
        'ButtonPushedFcn', @(btn,event) playAudio(fig, playbackChannelDrop.Value));

    % 分隔线2
    uipanel(fig, 'Position', [rightPanelX, 495, 200, 2], 'BackgroundColor', [0.8, 0.8, 0.8]);

    % SPL分析控制 - 移动到右侧
    uilabel(fig, 'Position', [rightPanelX, 470, 100, 22], 'Text', 'SPL分析:', ...
           'FontWeight', 'bold', 'FontColor', [0.6, 0.2, 0.6]);
    
    % SPL加权模式选择
    uilabel(fig, 'Position', [rightPanelX+10, 450, 60, 22], 'Text', '加权:');
    splWeightingDrop = uidropdown(fig, 'Position', [rightPanelX+70, 450, 60, 22], 'Items', {'A', 'C', 'Z'}, 'Value', 'A');
    
    % SPL带宽模式选择
    uilabel(fig, 'Position', [rightPanelX+10, 425, 60, 22], 'Text', '带宽:');
    splBandwidthDrop = uidropdown(fig, 'Position', [rightPanelX+70, 425, 120, 22], 'Items', ...
        {'全频带', '1/1倍频程', '1/3倍频程'}, 'Value', '1/3倍频程');
               
    % 麦克风校准按钮
    micCalibButton = uibutton(fig, 'Position', [rightPanelX, 390, 80, 25], 'Text', '麦克风校准', ...
        'ButtonPushedFcn', @(btn,event) openMicCalibrationDialog(fig));

    % SPL分析按钮
    splButton = uibutton(fig, 'Position', [rightPanelX+100, 390, 60, 25], 'Text', 'SPL分析', ...
        'ButtonPushedFcn', @(btn,event) performSPLAnalysis(fig, splWeightingDrop.Value, splBandwidthDrop.Value));

    % 分隔线3
    uipanel(fig, 'Position', [rightPanelX, 375, 200, 2], 'BackgroundColor', [0.8, 0.8, 0.8]);

    % 开始采集按钮 - 移动到右侧，设为醒目样式
    startButton = uibutton(fig, 'Position', [rightPanelX+25, 320, 150, 40], 'Text', '开始采集', ...
        'ButtonPushedFcn', @(btn, event) startSampling(fig, sampleRateInput.Value, sampleTimeInput.Value, ...
                                                      channelCheckboxes, timeChannelCheckboxes, freqChannelCheckboxes, logScaleCheckbox, saveDataCheckbox), ...
        'FontSize', 14, 'FontWeight', 'bold', 'BackgroundColor', [0.2, 0.7, 0.2]);

    % 添加工作区数据说明文本 - 移动到右侧
    uilabel(fig, 'Position', [rightPanelX+10, 280, 180, 30], 'Text', '✓ 数据将自动保存到工作区变量', ...
           'FontColor', [0.2, 0.6, 0.2], 'FontSize', 10, 'WordWrap', 'on');

    % 创建用于显示时域信号的轴 - 调整到左侧
    axTimeDomain = uiaxes(fig, 'Position', [300, 400, 500, 280]);
    title(axTimeDomain, '时域信号');
    xlabel(axTimeDomain, '采样点');
    ylabel(axTimeDomain, '幅值');
    grid(axTimeDomain, 'on');

    % 创建用于显示频谱分析的轴 - 调整到左侧
    axFrequencyDomain = uiaxes(fig, 'Position', [300, 50, 500, 280]);
    title(axFrequencyDomain, '功率谱密度');
    xlabel(axFrequencyDomain, '频率 (Hz)');
    ylabel(axFrequencyDomain, '功率谱密度 (V²/Hz)');
    grid(axFrequencyDomain, 'on');
end

% 全选/取消全选功能
function toggleAllChannels(checkboxes, value)
    for i = 1:length(checkboxes)
        checkboxes(i).Value = value;
    end
end

% 麦克风校准对话框
function openMicCalibrationDialog(parentFig)
    % 创建模态对话框
    dlg = uifigure('Name', '麦克风灵敏度校准', 'Position', [300, 200, 500, 600], ...
                  'WindowStyle', 'modal', 'Resize', 'off');
    
    % 标题
    uilabel(dlg, 'Position', [20, 560, 460, 25], 'Text', '麦克风灵敏度设置 (mV/Pa)', ...
           'FontSize', 14, 'FontWeight', 'bold', 'HorizontalAlignment', 'center');
    
    % 说明文本
    uilabel(dlg, 'Position', [20, 530, 460, 20], 'Text', '请为每个通道输入麦克风的灵敏度数值，用于SPL分析的声压校准', ...
           'FontSize', 10, 'HorizontalAlignment', 'center', 'FontColor', [0.5, 0.5, 0.5]);
    
    % 获取或初始化灵敏度数据
    micSensitivity = getappdata(parentFig, 'microphoneSensitivity');
    if isempty(micSensitivity)
        % 默认灵敏度值 (mV/Pa) - 典型的测量麦克风灵敏度
        micSensitivity = [50, 50, 50, 50, 50, 50, 50, 50]; % 8个通道，默认50 mV/Pa
    end
    
    % 创建通道灵敏度输入框
    channelInputs = cell(8, 1);
    enableCheckboxes = cell(8, 1);
    
    for ch = 1:8
        % 通道标签
        yPos = 480 - (ch-1) * 50;
        uilabel(dlg, 'Position', [20, yPos, 110, 22], 'Text', [formatChannelLabel(ch-1), ':'], ...
               'FontWeight', 'bold');
        
        % 启用复选框
        enableCheckboxes{ch} = uicheckbox(dlg, 'Position', [130, yPos, 55, 22], 'Text', '启用', 'Value', true);
        
        % 灵敏度输入框
        channelInputs{ch} = uieditfield(dlg, 'numeric', 'Position', [190, yPos, 80, 22], ...
                                       'Value', micSensitivity(ch), 'Limits', [0.1, 1000]);
        
        % 单位标签
        uilabel(dlg, 'Position', [280, yPos, 50, 22], 'Text', 'mV/Pa');
        
        % 常用麦克风类型快捷按钮
        commonMicBtn = uibutton(dlg, 'Position', [340, yPos, 80, 22], 'Text', '常用数值', ...
                               'ButtonPushedFcn', @(btn,event) showCommonMicValues(ch, channelInputs{ch}));
        
        % 校准按钮
        calibBtn = uibutton(dlg, 'Position', [430, yPos, 50, 22], 'Text', '校准', ...
                           'ButtonPushedFcn', @(btn,event) calibrateMicrophone(ch, channelInputs{ch}));
    end
    
    % 预设配置按钮
    uilabel(dlg, 'Position', [20, 80, 100, 22], 'Text', '快速配置:', 'FontWeight', 'bold');
    
    % 全部设为相同值按钮
    uibutton(dlg, 'Position', [20, 50, 100, 25], 'Text', '全部设为50', ...
            'ButtonPushedFcn', @(btn,event) setAllSensitivity(channelInputs, 50));
    
    uibutton(dlg, 'Position', [130, 50, 100, 25], 'Text', '全部设为12.5', ...
            'ButtonPushedFcn', @(btn,event) setAllSensitivity(channelInputs, 12.5));
    
    uibutton(dlg, 'Position', [240, 50, 100, 25], 'Text', '全部设为20', ...
            'ButtonPushedFcn', @(btn,event) setAllSensitivity(channelInputs, 20));
    
    % 底部按钮
    uibutton(dlg, 'Position', [300, 10, 80, 30], 'Text', '确定', ...
            'ButtonPushedFcn', @(btn,event) saveMicCalibration(parentFig, channelInputs, enableCheckboxes, dlg), ...
            'FontWeight', 'bold', 'BackgroundColor', [0.2, 0.7, 0.2]);
    
    uibutton(dlg, 'Position', [390, 10, 80, 30], 'Text', '取消', ...
            'ButtonPushedFcn', @(btn,event) close(dlg));
    
    % 显示当前设置
    uilabel(dlg, 'Position', [20, 110, 460, 20], 'Text', sprintf('当前设置: %s mV/Pa', ...
           mat2str(micSensitivity, 3)), 'FontColor', [0.2, 0.6, 0.2]);
end

% 显示常用麦克风灵敏度值
function showCommonMicValues(channelNum, inputField)
    % 常用麦克风类型和对应灵敏度
    micTypes = {
        'B&K 4189 (自由场)', 50;
        'B&K 4188 (压力场)', 12.5;
        'B&K 4190 (自由场)', 50;
        'GRAS 40PH', 50;
        'GRAS 46AE', 12.5;
        'PCB 377C10', 50;
        'PCB 378B02', 10;
        'ACO 7052', 50;
        '通用测量麦克风', 20;
        '自定义', NaN
    };
    
    % 创建选择对话框
    [selection, ok] = listdlg('ListString', micTypes(:,1), ...
                             'SelectionMode', 'single', ...
                             'Name', sprintf('%s 麦克风类型选择', formatChannelLabel(channelNum-1)), ...
                             'PromptString', '请选择麦克风类型:');
    
    if ok && selection <= size(micTypes, 1) - 1
        sensitivity = micTypes{selection, 2};
        inputField.Value = sensitivity;
    elseif ok && selection == size(micTypes, 1)
        % 自定义输入
        answer = inputdlg({sprintf('请输入 %s 的麦克风灵敏度 (mV/Pa):', formatChannelLabel(channelNum-1))}, ...
                         '自定义灵敏度', 1, {num2str(inputField.Value)});
        if ~isempty(answer)
            newValue = str2double(answer{1});
            if ~isnan(newValue) && newValue > 0
                inputField.Value = newValue;
            end
        end
    end
end

% 麦克风校准功能（未来扩展）
function calibrateMicrophone(channelNum, inputField)
    msgbox(sprintf(['%s 麦克风校准功能\n\n' ...
                   '此功能可用于:\n' ...
                   '• 声级计对比校准\n' ...
                   '• 标准声源校准\n' ...
                   '• 参考麦克风校准\n\n' ...
                   '当前为占位功能，可根据需要扩展实现。'], formatChannelLabel(channelNum-1)), ...
          '校准功能', 'help');
end

% 设置所有通道相同灵敏度
function setAllSensitivity(channelInputs, value)
    for i = 1:length(channelInputs)
        channelInputs{i}.Value = value;
    end
end

% 保存麦克风校准设置
function saveMicCalibration(parentFig, channelInputs, enableCheckboxes, dlg)
    % 收集所有输入值
    sensitivity = zeros(8, 1);
    enabled = false(8, 1);
    
    for i = 1:8
        sensitivity(i) = channelInputs{i}.Value;
        enabled(i) = enableCheckboxes{i}.Value;
    end
    
    % 保存到父窗口的appdata
    setappdata(parentFig, 'microphoneSensitivity', sensitivity);
    setappdata(parentFig, 'microphoneEnabled', enabled);
    
    % 显示确认消息
    msgbox(sprintf(['麦克风灵敏度设置已保存！\n\n' ...
                   '启用通道: %s\n' ...
                   '灵敏度值: %s mV/Pa\n\n' ...
                   'SPL分析将使用这些设置进行声压校准。'], ...
                   mat2str(find(enabled)-1), mat2str(sensitivity(enabled), 3)), ...
          '设置保存成功', 'none');
    
    % 在命令窗口输出设置信息
    fprintf('\n=== 麦克风灵敏度设置已更新 ===\n');
    for i = 1:8
        if enabled(i)
            fprintf('%s: %.2f mV/Pa (启用)\n', formatChannelLabel(i-1), sensitivity(i));
        else
            fprintf('%s: %.2f mV/Pa (禁用)\n', formatChannelLabel(i-1), sensitivity(i));
        end
    end
    fprintf('==============================\n\n');
    
    % 关闭对话框
    close(dlg);
end

% 将0-based软件通道转换为“物理通道（软件通道）”显示文本
function label = formatChannelLabel(channelIdx)
    label = sprintf('AIN%d (AD%d)', channelIdx + 1, channelIdx);
end
