function [SPL_total, SPL_oct, fc] = analyze_spl(data, fs, mode, bandwidth_type, varargin)
%ANALYZE_SPL  对DAQ采集数据进行声压级分析
%
%   [SPL_total, SPL_oct, fc] = analyze_spl(data, fs, mode, bandwidth_type)
%   [SPL_total, SPL_oct, fc] = analyze_spl(..., 'MicSensitivity', sensitivity, 'ChannelIndices', indices)
%
%   使用 Audio Toolbox 的 splMeter 系统对象计算等效连续声压级 (Leq)
%   支持可选的麦克风灵敏度校准功能
%
%   输入参数:
%     data           - 时域数据 [通道 × 采样点] 或 [采样点 × 通道]
%     fs             - 采样率 (Hz)
%     mode           - 'A' 为 A 加权 (默认), 'C' 为 C 加权, 'Z' 为线性 (无加权)
%     bandwidth_type - 'fullband' 为全频带, 'octave' 为 1/1 倍频程, 
%                      'third_octave' 为 1/3 倍频程 (默认)
%
%   名称-值对参数:
%     'MicSensitivity'  - 麦克风灵敏度 (mV/Pa) [8×1 向量]，默认 50 mV/Pa
%     'ChannelIndices'  - 通道索引 [N×1 向量，0-based]，默认 [0, 1]
%
%   输出参数:
%     SPL_total - 总声压级 dB SPL (每个通道一个值)
%     SPL_oct   - 频带 Leq 值 dB SPL (频带 × 通道)
%     fc        - 中心频率 Hz (仅对倍频程分析有效)

    arguments
        data double
        fs double {mustBePositive}
        mode char {mustBeMember(mode,{'A','C','Z'})} = 'A'
        bandwidth_type char {mustBeMember(bandwidth_type,{'fullband','octave','third_octave'})} = 'third_octave'
    end
    
    arguments (Repeating)
        varargin
    end

    
    % 解析名称-值对参数
    p = inputParser;
    addParameter(p, 'MicSensitivity', 50 * ones(8, 1), @(x) isnumeric(x) && length(x) == 8);
    addParameter(p, 'ChannelIndices', [0, 1], @(x) isnumeric(x));
    parse(p, varargin{:});
    
    micSensitivity = p.Results.MicSensitivity;
    channelIndices = p.Results.ChannelIndices;
    useCalibration = ~isequal(micSensitivity, 50 * ones(8, 1));
    
    % 智能识别数据格式
    [rows, cols] = size(data);
    if rows > cols
        % 很可能是 [采样点 × 通道] 格式，转置
        data = data';
        fprintf('数据已转置为 [通道 × 采样点] 格式\n');
    end

    num_channels = size(data, 1);
    num_samples = size(data, 2);
    
    fprintf('分析 %d 通道，每通道 %d 采样点', num_channels, num_samples);
    if useCalibration
        fprintf('（使用麦克风校准）');
    end
    fprintf('\n');
    
    % 设置频率加权
    switch upper(mode)
        case 'A'
            weighting = 'A-weighting';
        case 'C'
            weighting = 'C-weighting';
        case 'Z'
            weighting = 'Z-weighting';
        otherwise
            weighting = 'A-weighting';
    end
    
    % 设置带宽和频率范围
    switch lower(bandwidth_type)
        case 'fullband'
            bandwidth = 'Full band';
            fc = [];
        case 'octave'
            bandwidth = '1/1 octave';
            fc = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000];
            fc = fc(fc <= fs/2);  % 移除超过Nyquist频率的
        case 'third_octave'
            bandwidth = '1/3 octave';
            fc = [25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, ...
                  500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, ...
                  5000, 6300, 8000, 10000, 12500, 16000, 20000];
            fc = fc(fc <= fs/2);  % 移除超过Nyquist频率的
        otherwise
            bandwidth = '1/3 octave';
            fc = [25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, ...
                  500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, ...
                  5000, 6300, 8000, 10000, 12500, 16000, 20000];
            fc = fc(fc <= fs/2);
    end
    
    
    % 初始化输出
    SPL_total = zeros(num_channels, 1);
    
    try
        % 为每个通道计算SPL
        for ch = 1:num_channels
            fprintf('处理通道 %d/%d...', ch, num_channels);
            
            % 获取当前通道数据（列向量）
            channel_data = data(ch, :)';
            
            % 麦克风校准处理
            if useCalibration
                % 获取当前通道的麦克风灵敏度
                if ch <= length(channelIndices)
                    channel_idx = channelIndices(ch); % 0-based通道索引
                else
                    channel_idx = ch - 1; % 默认使用通道号-1
                end
                
                if channel_idx + 1 <= length(micSensitivity)
                    sensitivity_mV_Pa = micSensitivity(channel_idx + 1);
                else
                    sensitivity_mV_Pa = 50; % 默认值
                    fprintf(' (使用默认灵敏度)');
                end
                
                % 将电压数据转换为声压数据
                % 声压 (Pa) = 电压 (V) / 灵敏度 (V/Pa)
                sensitivity_V_Pa = sensitivity_mV_Pa / 1000; % 转换为 V/Pa
                channel_data = channel_data / sensitivity_V_Pa; % 转换为 Pa
                
                fprintf(' 灵敏度=%.2f mV/Pa', sensitivity_mV_Pa);
            end
            
            % 创建splMeter对象
            if strcmp(bandwidth, 'Full band')
                splObj = splMeter('SampleRate', fs, 'FrequencyWeighting', weighting);
            else
                splObj = splMeter('SampleRate', fs, 'FrequencyWeighting', weighting, ...
                                 'Bandwidth', bandwidth);
            end
            
            % 分块处理以避免内存问题
            block_length = min(fs * 2, length(channel_data)); % 2秒块
            num_blocks = ceil(length(channel_data) / block_length);
            
            block_results = [];
            
            for block = 1:num_blocks
                start_idx = (block - 1) * block_length + 1;
                end_idx = min(block * block_length, length(channel_data));
                block_data = channel_data(start_idx:end_idx);
                
                % 计算当前块的SPL
                [~, leq_block] = splObj(block_data);
                block_results = [block_results; leq_block(:)];
            end
            
            % 计算平均SPL
            if strcmp(bandwidth, 'Full band')
                % 全频带：能量平均所有块
                SPL_total(ch) = 10 * log10(mean(10.^(block_results/10)));
                if ch == 1
                    SPL_oct = zeros(1, num_channels);
                end
                SPL_oct(1, ch) = SPL_total(ch);
            else
                % 频带分析：对每个频带分别平均
                if size(block_results, 2) > 1
                    % 多频带结果
                    band_averages = 10 * log10(mean(10.^(block_results/10), 1));
                else
                    % 单列结果，可能是时间序列
                    band_averages = 10 * log10(mean(10.^(block_results/10)));
                end
                
                % 初始化频带结果矩阵
                if ch == 1
                    SPL_oct = zeros(length(band_averages), num_channels);
                end
                
                SPL_oct(:, ch) = band_averages(:);
                
                % 计算总SPL（能量求和）
                SPL_total(ch) = 10 * log10(sum(10.^(band_averages/10)));
            end
            
            release(splObj);
            
            if useCalibration
                fprintf(' 完成 (%.1f dB SPL)\n', SPL_total(ch));
            else
                fprintf(' 完成 (%.2f dB)\n', SPL_total(ch));
            end
        end
        
        if useCalibration
            fprintf('SPL分析完成: %d 通道, %s, %s加权（校准后的声压级 dB SPL）\n', ...
                    num_channels, bandwidth_type, mode);
        else
            fprintf('SPL分析完成: %d 通道, %s, %s加权\n', ...
                    num_channels, bandwidth_type, mode);
        end
        
    catch ME
        warning('SPL计算失败: %s', ME.message);
        % 返回NaN结果
        SPL_total = NaN(num_channels, 1);
        if exist('SPL_oct', 'var')
            SPL_oct(:) = NaN;
        else
            SPL_oct = NaN(1, num_channels);
        end
        fc = [];
    end

end