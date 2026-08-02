#include "usb_daq_protocol.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 1.0e-6f;
}

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_command_defaults() {
    const usb_daq::Command command = usb_daq::build_continuous_command(
        0, 1, 0, 7, 22050, 0, 0, 0, 0);
    const usb_daq::Command expected{
        0x80, 0x01, 0x20, 0x02, 0x02, 0x03, 0x00,
        0x04, 0x07, 0x05, 0x00, 0x06, 0x00, 0xfe};
    expect(command == expected, "default continuous command differs from vendor DLL");
}

void test_command_flags_and_clamping() {
    const usb_daq::Command command = usb_daq::build_continuous_command(
        7, 1, 11, 100, 1, 1, 1, 1, 1);
    expect(command[0] == 0xff, "command flag packing failed");
    expect(command[2] == 0xc0 && command[4] == 0xd4 && command[6] == 0x01,
           "frequency divider packing failed");
    expect(command[8] == 63, "last-channel clamping failed");
    expect(command[10] == 8, "first-channel alignment failed");
}

void test_single_command_matches_disassembly() {
    const usb_daq::Command command = usb_daq::build_continuous_command(
        0, 1, 0, 15, 10000, 0, 0, 0, 0);
    const usb_daq::Command expected{
        0x80, 0x01, 0xb0, 0x02, 0x04, 0x03, 0x00,
        0x04, 0x0f, 0x05, 0x00, 0x06, 0x00, 0xfe};
    expect(command == expected, "single-sample command differs from vendor DLL");
}

void test_decoder_marker_split_and_signed_samples() {
    usb_daq::StreamDecoder decoder(0);
    std::vector<float> output;
    const std::array<std::uint8_t, 2> first{0xaa, 0x55};
    decoder.consume(first.data(), first.size(), output);
    expect(!decoder.synchronized(), "partial stream marker was accepted");

    const std::array<std::uint8_t, 7> second{
        0x55, 0xaa, 0x01, 0x00, 0x00, 0x80, 0xff};
    decoder.consume(second.data(), second.size(), output);
    expect(decoder.synchronized(), "split stream marker was not detected");
    expect(output.size() == 2, "decoder did not emit two complete samples");
    expect(near(output[0], 5.0f / 32768.0f), "positive sample conversion failed");
    expect(near(output[1], -5.0f), "negative full-scale conversion failed");

    const std::array<std::uint8_t, 1> third{0x7f};
    decoder.consume(third.data(), third.size(), output);
    expect(output.size() == 3, "split sample was not reassembled");
    expect(near(output[2], 32767.0f * 5.0f / 32768.0f),
           "positive full-scale conversion failed");
}

void test_vendor_range_conversion_is_preserved() {
    usb_daq::StreamDecoder decoder(1);
    std::vector<float> output;
    const std::array<std::uint8_t, 6> bytes{0xaa, 0x55, 0x55, 0xaa, 0x00, 0x40};
    decoder.consume(bytes.data(), bytes.size(), output);
    expect(output.size() == 1, "range test did not emit a sample");
    expect(near(output[0], 5.0f), "vendor range conversion was not preserved");
}

} // namespace

int main() {
    test_command_defaults();
    test_command_flags_and_clamping();
    test_single_command_matches_disassembly();
    test_decoder_marker_split_and_signed_samples();
    test_vendor_range_conversion_is_preserved();
    std::cout << "protocol tests passed\n";
    return 0;
}
