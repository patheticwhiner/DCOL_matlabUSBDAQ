%% USB DAQ 数据采集系统 - 带 UI 界面
% 通过图窗设置采样参数并完成采样，绘制信号图像和频谱分析
% 作者：GitHub Copilot

function daq_ui()
    % 创建 UI 界面
    fig = uifigure('Name', 'USB DAQ 数据采集系统', 'Position', [100, 100, 900, 700]);

    % 采样频率输入框
    uilabel(fig, 'Position', [20, 620, 100, 22], 'Text', '采样频率 (Hz):');
    sampleRateInput = uieditfield(fig, 'numeric', 'Position', [130, 620, 100, 22], 'Value', 44100);

    % 采样时间输入框
    uilabel(fig, 'Position', [20, 580, 100, 22], 'Text', '采样时间 (秒):');
    sampleTimeInput = uieditfield(fig, 'numeric', 'Position', [130, 580, 100, 22], 'Value', 10);

    % 通道选择区域
    uilabel(fig, 'Position', [20, 540, 100, 22], 'Text', '通道选择:');
    
    % 创建通道复选框 (0-7通道)
    channelCheckboxes = [];
    for i = 0:7
        cb = uicheckbox(fig, 'Position', [20 + mod(i,4)*60, 500 - floor(i/4)*30, 50, 22], ...
                       'Text', sprintf('CH%d', i)); % 默认选择通道3
        channelCheckboxes = [channelCheckboxes, cb];
    end

    % 显示控制区域
    uilabel(fig, 'Position', [20, 420, 100, 22], 'Text', '显示控制:');
    
    % 时域显示控制
    uilabel(fig, 'Position', [20, 390, 80, 22], 'Text', '时域显示:');
    timeShowAllCheckbox = uicheckbox(fig, 'Position', [100, 390, 50, 22], 'Text', '全选', 'Value', true);
    timeChannelCheckboxes = [];
    for i = 0:7
        cb = uicheckbox(fig, 'Position', [20 + mod(i,4)*60, 360 - floor(i/4)*30, 50, 22], ...
                       'Text', sprintf('CH%d', i));
        timeChannelCheckboxes = [timeChannelCheckboxes, cb];
    end

    % 频域显示控制
    uilabel(fig, 'Position', [20, 290, 80, 22], 'Text', '频域显示:');
    freqShowAllCheckbox = uicheckbox(fig, 'Position', [100, 290, 50, 22], 'Text', '全选', 'Value', true);
    
    % 对数坐标选项
    logScaleCheckbox = uicheckbox(fig, 'Position', [170, 290, 80, 22], 'Text', '对数坐标', 'Value', true);
    
    freqChannelCheckboxes = [];
    for i = 0:7
        cb = uicheckbox(fig, 'Position', [20 + mod(i,4)*60, 260 - floor(i/4)*30, 50, 22], ...
                       'Text', sprintf('CH%d', i));
        freqChannelCheckboxes = [freqChannelCheckboxes, cb];
    end

    % 全选控制回调函数
    timeShowAllCheckbox.ValueChangedFcn = @(src, event) toggleAllChannels(timeChannelCheckboxes, src.Value);
    freqShowAllCheckbox.ValueChangedFcn = @(src, event) toggleAllChannels(freqChannelCheckboxes, src.Value);

    % 开始采集按钮
    % 保存数据选项
    saveDataCheckbox = uicheckbox(fig, 'Position', [20, 120, 120, 22], 'Text', '保存数据到文件', 'Value', true);

    % 播放控制：选择回放通道与播放按钮
    uilabel(fig, 'Position', [20, 160, 80, 22], 'Text', '播放通道:');
    playbackChannelDrop = uidropdown(fig, 'Position', [100, 160, 80, 22], 'Items', {'CH0','CH1','CH2','CH3','CH4','CH5','CH6','CH7'}, 'Value', 'CH3');
    playButton = uibutton(fig, 'Position', [200, 160, 80, 22], 'Text', '播放音频', ...
        'ButtonPushedFcn', @(btn,event) playAudio(fig, playbackChannelDrop.Value));

    startButton = uibutton(fig, 'Position', [30, 80, 100, 30], 'Text', '开始采集', ...
        'ButtonPushedFcn', @(btn, event) startSampling(fig, sampleRateInput.Value, sampleTimeInput.Value, ...
                                                      channelCheckboxes, timeChannelCheckboxes, freqChannelCheckboxes, logScaleCheckbox, saveDataCheckbox));

    % 创建用于显示时域信号的轴
    axTimeDomain = uiaxes(fig, 'Position', [350, 400, 500, 280]);
    title(axTimeDomain, '时域信号');
    xlabel(axTimeDomain, '采样点');
    ylabel(axTimeDomain, '幅值');
    grid(axTimeDomain, 'on');

    % 创建用于显示频谱分析的轴
    axFrequencyDomain = uiaxes(fig, 'Position', [350, 50, 500, 280]);
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

