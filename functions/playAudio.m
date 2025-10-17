function playAudio(fig, channelStr)
% playAudio - 从 UI 会话中读取最近一次采集的数据并播放选定通道
% Usage: playAudio(fig, 'CH3')

try
    acquired = getappdata(fig, 'lastAcquiredData');
    if isempty(acquired)
        uialert(fig, '没有找到已采集的数据，请先执行采集。', '错误');
        return;
    end
catch
    uialert(fig, '无法访问会话数据，请确保 UI 窗口存在。', '错误');
    return;
end

% 解析通道号
channelIdx = sscanf(channelStr, 'CH%d');
if isempty(channelIdx)
    uialert(fig, '无效的通道选择。', '错误');
    return;
end

% 查找通道在 acquired.data 中的位置
chanList = acquired.selectedChannels;
pos = find(chanList == channelIdx, 1);
if isempty(pos)
    uialert(fig, sprintf('通道 %d 未在当前采集中被选中。', channelIdx), '错误');
    return;
end

signal = acquired.data(pos, :);
fs = acquired.config.SampleRate;

% 规范化信号到 [-1,1]（sound 要求通常在该范围内）
maxval = max(abs(signal));
if maxval > 0
    sigNorm = signal / maxval * 0.9; % 0.9 防止裁剪
else
    sigNorm = signal;
end

% 播放
try
    sound(sigNorm, fs);
catch ME
    uialert(fig, sprintf('播放失败: %s', ME.message), '错误');
end
end