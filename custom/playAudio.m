function playAudio(fig, channelSelection)
% playAudio - 从 UI 会话中读取最近一次采集的数据并播放选定通道
% Usage: playAudio(fig, 0) 或 playAudio(fig, 'AIN1 (AD0)')

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

% 解析软件通道索引。GUI通过ItemsData直接传入0-based数值；同时保留字符串兼容。
if isnumeric(channelSelection) && isscalar(channelSelection)
    channelIdx = double(channelSelection);
else
    token = regexp(char(channelSelection), '(?:AD|CH)(\d+)', 'tokens', 'once');
    if isempty(token)
        channelIdx = [];
    else
        channelIdx = str2double(token{1});
    end
end
if isempty(channelIdx)
    uialert(fig, '无效的通道选择。', '错误');
    return;
end

% 查找通道在 acquired.data 中的位置
chanList = acquired.selectedChannels;
pos = find(chanList == channelIdx, 1);
if isempty(pos)
    uialert(fig, sprintf('AIN%d (AD%d) 未在当前采集中被选中。', channelIdx+1, channelIdx), '错误');
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
