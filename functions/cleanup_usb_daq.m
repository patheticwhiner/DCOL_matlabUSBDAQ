function cleanup_usb_daq()
    % 清理USB DAQ设备
    
    try
        % 关闭USB设备
        calllib('Usb_Daq_V52_Dll','closeUSB');
        
        % 卸载库
        if libisloaded('Usb_Daq_V52_Dll')
            unloadlibrary('Usb_Daq_V52_Dll');
        end
    catch ME
        warning('USB_DAQ:CleanupWarning', '设备清理时出现警告: %s', ME.message);
    end
end