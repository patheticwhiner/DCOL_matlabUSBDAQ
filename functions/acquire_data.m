function data = acquire_data(config)
    % 执行数据采集
    
    % 初始化数据缓冲区（为硬件通道范围分配空间）
    buffer = single(zeros(8, config.SamplesPerChannel));
    pv = libpointer('singlePtr', buffer);
    
    % 执行连续采集
    if(1)
        result = calllib('Usb_Daq_V52_Dll', 'ad_continu', ...
            config.DeviceID, ...           % dev
            config.Oversample, ...         % ad_os
            config.ADGain,...
            config.ChannelFirst,...
            config.ChannelLast,...
            config.SampleBand, ...         % freq
            0, ...                         % trig_sl
            0, ...                         % trig_pol
            0, ...                         % clk_sl
            0, ...                         % ext_clk_pol
            config.TotalSamples, ...       % num
            pv);                           % databuf
    if result ~= 0
        error('数据采集失败，错误代码: %d', result);
    end
    
    % 获取采集到的所有硬件通道数据
    all_data = get(pv, 'Value');
    
    % 提取用户指定的通道数据
    data = zeros(config.NumChannels, config.SamplesPerChannel, 'single');
    for i = 1:config.NumChannels
        data(i, :) = all_data(config.ChannelIndices(i) + 1, :);
    end
end