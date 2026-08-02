#include "usb_daq_libusb_backend.hpp"

#include "usb_daq_protocol.hpp"

#include <libusb.h>

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace usb_daq_libusb {
namespace {

struct DeviceState {
    libusb_device_handle* handle = nullptr;
    std::thread reader;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> worker_running{false};
    std::atomic<bool> worker_failed{false};
    std::mutex buffer_mutex;
    std::vector<float> buffer;
    std::size_t read_offset = 0;
    int ad_range = 0;
};

std::mutex g_state_mutex;
std::mutex g_error_mutex;
libusb_context* g_context = nullptr;
std::vector<std::unique_ptr<DeviceState>> g_devices;
std::string g_last_error;

void set_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_error_mutex);
    g_last_error = message;
}

void set_libusb_error(const char* operation, int result) {
    std::ostringstream stream;
    stream << operation << ": " << libusb_error_name(result);
    set_error(stream.str());
}

DeviceState* device_at(int dev) {
    if (dev < 0 || static_cast<std::size_t>(dev) >= g_devices.size()) {
        set_error("invalid device index");
        return nullptr;
    }
    return g_devices[static_cast<std::size_t>(dev)].get();
}

bool bulk_out(DeviceState& device, const std::uint8_t* bytes, int size) {
    int transferred = 0;
    const int result = libusb_bulk_transfer(
        device.handle, usb_daq::kCommandEndpoint,
        const_cast<unsigned char*>(bytes), size,
        &transferred, usb_daq::kUsbTimeoutMs);
    if (result != LIBUSB_SUCCESS) {
        set_libusb_error("bulk OUT endpoint 0x04 failed", result);
        return false;
    }
    if (transferred != size) {
        set_error("short bulk OUT transfer on endpoint 0x04");
        return false;
    }
    return true;
}

void compact_buffer(DeviceState& device) {
    if (device.read_offset == 0) return;
    if (device.read_offset == device.buffer.size()) {
        device.buffer.clear();
        device.read_offset = 0;
        return;
    }
    if (device.read_offset >= 65536 &&
        device.read_offset * 2 >= device.buffer.size()) {
        device.buffer.erase(device.buffer.begin(),
                            device.buffer.begin() +
                                static_cast<std::ptrdiff_t>(device.read_offset));
        device.read_offset = 0;
    }
}

void reader_loop(DeviceState* device) {
    device->worker_running.store(true);
    device->worker_failed.store(false);
    const std::array<std::uint8_t, 2> start{0x01, 0xfe};
    if (!bulk_out(*device, start.data(), static_cast<int>(start.size()))) {
        device->worker_failed.store(true);
        device->worker_running.store(false);
        return;
    }

    usb_daq::StreamDecoder decoder(device->ad_range);
    std::array<std::uint8_t, 2048> raw{};
    int consecutive_timeouts = 0;
    int transfers_without_sync = 0;

    while (!device->stop_requested.load()) {
        int transferred = 0;
        const int result = libusb_bulk_transfer(
            device->handle, usb_daq::kAdcEndpoint, raw.data(),
            static_cast<int>(raw.size()), &transferred, 250);

        if (transferred > 0) {
            std::vector<float> decoded;
            decoded.reserve(static_cast<std::size_t>(transferred / 2));
            decoder.consume(raw.data(), static_cast<std::size_t>(transferred), decoded);
            if (!decoder.synchronized()) {
                ++transfers_without_sync;
                if (transfers_without_sync >= 6) {
                    set_error("ADC stream synchronization marker was not found");
                    device->worker_failed.store(true);
                    break;
                }
            }
            if (!decoded.empty()) {
                std::lock_guard<std::mutex> lock(device->buffer_mutex);
                device->buffer.insert(device->buffer.end(), decoded.begin(), decoded.end());
            }
            consecutive_timeouts = 0;
        }

        if (result == LIBUSB_SUCCESS || result == LIBUSB_ERROR_TIMEOUT) {
            if (result == LIBUSB_ERROR_TIMEOUT && transferred == 0) {
                ++consecutive_timeouts;
                if (consecutive_timeouts >= 24 && !device->stop_requested.load()) {
                    set_libusb_error("bulk IN endpoint 0x86 timed out", result);
                    device->worker_failed.store(true);
                    break;
                }
            }
            continue;
        }

        if (!device->stop_requested.load()) {
            set_libusb_error("bulk IN endpoint 0x86 failed", result);
            device->worker_failed.store(true);
        }
        break;
    }
    device->worker_running.store(false);
}

bool stop_device(DeviceState& device, bool send_stop) {
    device.stop_requested.store(true);
    if (device.reader.joinable()) device.reader.join();
    if (!send_stop || device.handle == nullptr) return true;
    const std::array<std::uint8_t, 2> stop{0x00, 0xfe};
    return bulk_out(device, stop.data(), static_cast<int>(stop.size()));
}

void close_all_unlocked() {
    for (auto& device : g_devices) {
        stop_device(*device, device->reader.joinable() || device->worker_running.load());
        if (device->handle != nullptr) {
            libusb_release_interface(device->handle, usb_daq::kInterface);
            libusb_close(device->handle);
            device->handle = nullptr;
        }
    }
    g_devices.clear();
    if (g_context != nullptr) {
        libusb_exit(g_context);
        g_context = nullptr;
    }
}

} // namespace

int open_usb() {
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    close_all_unlocked();
    set_error("");

    int result = libusb_init(&g_context);
    if (result != LIBUSB_SUCCESS) {
        set_libusb_error("libusb_init failed", result);
        close_all_unlocked();
        return -1;
    }

    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(g_context, &list);
    if (count < 0) {
        set_libusb_error("libusb_get_device_list failed", static_cast<int>(count));
        close_all_unlocked();
        return -1;
    }

    bool found = false;
    for (ssize_t index = 0; index < count; ++index) {
        libusb_device_descriptor descriptor{};
        result = libusb_get_device_descriptor(list[index], &descriptor);
        if (result != LIBUSB_SUCCESS ||
            descriptor.idVendor != usb_daq::kVendorId ||
            descriptor.idProduct != usb_daq::kProductId) {
            continue;
        }
        found = true;

        auto device = std::make_unique<DeviceState>();
        result = libusb_open(list[index], &device->handle);
        if (result != LIBUSB_SUCCESS) {
            set_libusb_error("found 04b4:7809 but could not open it", result);
            libusb_free_device_list(list, 1);
            close_all_unlocked();
            return -1;
        }
        (void)libusb_set_auto_detach_kernel_driver(device->handle, 1);
        result = libusb_claim_interface(device->handle, usb_daq::kInterface);
        if (result != LIBUSB_SUCCESS) {
            set_libusb_error("claiming USB interface 0 failed", result);
            libusb_close(device->handle);
            device->handle = nullptr;
            libusb_free_device_list(list, 1);
            close_all_unlocked();
            return -1;
        }
        g_devices.push_back(std::move(device));
    }
    libusb_free_device_list(list, 1);
    if (!found) set_error("USB DAQ 04b4:7809 was not found");
    return 0;
}

int close_usb() {
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    close_all_unlocked();
    return 0;
}

int device_count() {
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    return static_cast<int>(g_devices.size());
}

int reset_device(int dev) {
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    DeviceState* device = device_at(dev);
    if (device == nullptr) return -1;
    if (device->reader.joinable() || device->worker_running.load()) {
        set_error("cannot reset while acquisition is active");
        return -1;
    }
    const int result = libusb_reset_device(device->handle);
    if (result != LIBUSB_SUCCESS) {
        set_libusb_error("libusb_reset_device failed", result);
        return -1;
    }
    return 0;
}

int configure_continuous(int dev, int ad_os, int ad_range,
                         int ch_first, int ch_last, int freq,
                         int trig_sl, int trig_pol,
                         int clk_sl, int ext_clk_pol) {
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    DeviceState* device = device_at(dev);
    if (device == nullptr) return -1;
    if (device->reader.joinable() || device->worker_running.load()) {
        set_error("acquisition is already active");
        return -1;
    }

    const usb_daq::Command command = usb_daq::build_continuous_command(
        ad_os, ad_range, ch_first, ch_last, freq,
        trig_sl, trig_pol, clk_sl, ext_clk_pol);
    if (!bulk_out(*device, command.data(), static_cast<int>(command.size()))) return -1;

    {
        std::lock_guard<std::mutex> buffer_lock(device->buffer_mutex);
        device->buffer.clear();
        device->read_offset = 0;
    }
    device->ad_range = ad_range;
    device->stop_requested.store(false);
    device->worker_failed.store(false);
    device->reader = std::thread(reader_loop, device);
    return 0;
}

int buffer_size(int dev) {
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    DeviceState* device = device_at(dev);
    if (device == nullptr || device->worker_failed.load()) return -1;
    std::lock_guard<std::mutex> buffer_lock(device->buffer_mutex);
    const std::size_t available = device->buffer.size() - device->read_offset;
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    return available > maximum ? std::numeric_limits<int>::max()
                               : static_cast<int>(available);
}

int read_buffer(int dev, float* databuf, int num) {
    if (databuf == nullptr || num < 0) {
        set_error("read_buffer received an invalid output buffer or count");
        return -1;
    }
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    DeviceState* device = device_at(dev);
    if (device == nullptr || device->worker_failed.load()) return -1;

    std::lock_guard<std::mutex> buffer_lock(device->buffer_mutex);
    const std::size_t available = device->buffer.size() - device->read_offset;
    const std::size_t count = std::min(available, static_cast<std::size_t>(num));
    if (count != 0) {
        std::memcpy(databuf, device->buffer.data() + device->read_offset,
                    count * sizeof(float));
        device->read_offset += count;
        compact_buffer(*device);
    }
    return static_cast<int>(count);
}

int stop_continuous(int dev) {
    std::lock_guard<std::mutex> state_lock(g_state_mutex);
    DeviceState* device = device_at(dev);
    if (device == nullptr) return -1;
    const bool started = device->reader.joinable() || device->worker_running.load();
    if (!started) {
        set_error("acquisition is not active");
        return -1;
    }
    return stop_device(*device, true) ? 0 : -1;
}

int ad_single(int dev, int ad_os, int ad_range, float* databuf) {
    if (databuf == nullptr) {
        set_error("ad_single received a null output buffer");
        return -1;
    }
    if (configure_continuous(dev, ad_os, ad_range, 0, 15, 10000,
                             0, 0, 0, 0) != 0) return -1;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline) {
        const int available = buffer_size(dev);
        if (available < 0) {
            (void)stop_continuous(dev);
            return -1;
        }
        if (available >= 512) {
            std::array<float, 16> discard{};
            if (read_buffer(dev, discard.data(), static_cast<int>(discard.size())) != 16 ||
                read_buffer(dev, databuf, 16) != 16) {
                (void)stop_continuous(dev);
                return -1;
            }
            return stop_continuous(dev);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    set_error("ad_single timed out waiting for ADC samples");
    (void)stop_continuous(dev);
    return -1;
}

std::string last_error() {
    std::lock_guard<std::mutex> lock(g_error_mutex);
    return g_last_error;
}

} // namespace usb_daq_libusb
