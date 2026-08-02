#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace usb_daq {

constexpr std::uint16_t kVendorId = 0x04b4;
constexpr std::uint16_t kProductId = 0x7809;
constexpr int kInterface = 0;
constexpr unsigned char kCommandEndpoint = 0x04;
constexpr unsigned char kAdcEndpoint = 0x86;
constexpr int kUsbTimeoutMs = 6000;

using Command = std::array<std::uint8_t, 14>;

Command build_continuous_command(int ad_os, int ad_range,
                                 int ch_first, int ch_last, int freq,
                                 int trig_sl, int trig_pol,
                                 int clk_sl, int ext_clk_pol);

class StreamDecoder {
public:
    explicit StreamDecoder(int ad_range);
    void consume(const std::uint8_t* bytes, std::size_t size,
                 std::vector<float>& output);
    bool synchronized() const { return synchronized_; }

private:
    void append_sample(std::uint8_t low, std::uint8_t high,
                       std::vector<float>& output) const;

    float scale_ = 0.0f;
    bool synchronized_ = false;
    std::array<std::uint8_t, 4> marker_window_{};
    std::size_t marker_size_ = 0;
    bool have_low_byte_ = false;
    std::uint8_t low_byte_ = 0;
};

} // namespace usb_daq
