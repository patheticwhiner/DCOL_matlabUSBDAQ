#pragma once

#include <string>

namespace usb_daq_libusb {

int open_usb();
int close_usb();
int device_count();
int reset_device(int dev);
int ad_single(int dev, int ad_os, int ad_range, float* databuf);
int configure_continuous(int dev, int ad_os, int ad_range,
                         int ch_first, int ch_last, int freq,
                         int trig_sl, int trig_pol,
                         int clk_sl, int ext_clk_pol);
int buffer_size(int dev);
int read_buffer(int dev, float* databuf, int num);
int stop_continuous(int dev);
std::string last_error();

} // namespace usb_daq_libusb
