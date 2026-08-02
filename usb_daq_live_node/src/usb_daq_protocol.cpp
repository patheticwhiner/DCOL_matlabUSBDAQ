#include "usb_daq_protocol.hpp"

#include <algorithm>
#include <cstdint>

namespace usb_daq {

namespace {

std::uint8_t bool_bit(int value, std::uint8_t mask) {
    return value == 0 ? 0 : mask;
}

} // namespace

Command build_continuous_command(int ad_os, int ad_range,
                                 int ch_first, int ch_last, int freq,
                                 int trig_sl, int trig_pol,
                                 int clk_sl, int ext_clk_pol) {
    ch_first = std::clamp(ch_first, 0, 56);
    ch_first -= ch_first % 8;
    ch_last = std::clamp(ch_last, 1, 63);
    freq = std::clamp(freq, 100, 100000);

    const int divider = 12000000 / freq;
    const std::uint8_t flags = static_cast<std::uint8_t>(
        bool_bit(ad_range, 0x80) |
        static_cast<std::uint8_t>((ad_os & 0x07) << 4) |
        bool_bit(clk_sl, 0x08) |
        bool_bit(trig_sl, 0x04) |
        bool_bit(ext_clk_pol, 0x02) |
        bool_bit(trig_pol, 0x01));

    return Command{
        flags,
        0x01, static_cast<std::uint8_t>(divider & 0xff),
        0x02, static_cast<std::uint8_t>((divider >> 8) & 0xff),
        0x03, static_cast<std::uint8_t>((divider >> 16) & 0xff),
        0x04, static_cast<std::uint8_t>(ch_last),
        0x05, static_cast<std::uint8_t>(ch_first),
        0x06, 0x00, 0xfe};
}

StreamDecoder::StreamDecoder(int ad_range)
    // Preserve the supplied 2018 DLL's conversion, including its disagreement
    // with the range table in the PDF manual.
    : scale_((ad_range == 0 ? 5.0f : 10.0f) / 32768.0f) {}

void StreamDecoder::append_sample(std::uint8_t low, std::uint8_t high,
                                  std::vector<float>& output) const {
    const auto word = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(low) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(high) << 8));
    const auto sample = static_cast<std::int16_t>(word);
    output.push_back(static_cast<float>(sample) * scale_);
}

void StreamDecoder::consume(const std::uint8_t* bytes, std::size_t size,
                            std::vector<float>& output) {
    static constexpr std::array<std::uint8_t, 4> marker{0xaa, 0x55, 0x55, 0xaa};

    std::size_t index = 0;
    while (!synchronized_ && index < size) {
        const std::uint8_t value = bytes[index++];
        if (marker_size_ < marker_window_.size()) {
            marker_window_[marker_size_++] = value;
        } else {
            std::move(marker_window_.begin() + 1, marker_window_.end(),
                      marker_window_.begin());
            marker_window_.back() = value;
        }
        if (marker_size_ == marker.size() && marker_window_ == marker) {
            synchronized_ = true;
            have_low_byte_ = false;
        }
    }

    if (!synchronized_) return;
    if (have_low_byte_ && index < size) {
        append_sample(low_byte_, bytes[index++], output);
        have_low_byte_ = false;
    }
    while (index + 1 < size) {
        append_sample(bytes[index], bytes[index + 1], output);
        index += 2;
    }
    if (index < size) {
        low_byte_ = bytes[index];
        have_low_byte_ = true;
    }
}

} // namespace usb_daq
