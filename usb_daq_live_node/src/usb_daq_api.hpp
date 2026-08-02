#pragma once

#ifdef _WIN32
#include <windows.h>
#define USB_DAQ_CALL __cdecl
#else
#include "usb_daq_libusb_backend.hpp"
#define USB_DAQ_CALL
#endif

#include <filesystem>
#include <sstream>
#include <string>

class UsbDaqApi {
public:
    using OpenFn = int(USB_DAQ_CALL*)();
    using CloseFn = int(USB_DAQ_CALL*)();
    using DeviceCountFn = int(USB_DAQ_CALL*)();
    using ResetDeviceFn = int(USB_DAQ_CALL*)(int);
    using AdSingleFn = int(USB_DAQ_CALL*)(int, int, int, float*);
    using AdContinuConfFn = int(USB_DAQ_CALL*)(int, int, int, int, int,
                                               int, int, int, int, int);
    using GetAdBufSizeFn = int(USB_DAQ_CALL*)(int);
    using ReadAdBufFn = int(USB_DAQ_CALL*)(int, float*, int);
    using AdContinuStopFn = int(USB_DAQ_CALL*)(int);

    OpenFn openUSB = nullptr;
    CloseFn closeUSB = nullptr;
    DeviceCountFn get_device_num = nullptr;
    ResetDeviceFn Reset_Usb_Device = nullptr;
    AdSingleFn ad_single = nullptr;
    AdContinuConfFn ad_continu_conf = nullptr;
    GetAdBufSizeFn Get_AdBuf_Size = nullptr;
    ReadAdBufFn Read_AdBuf = nullptr;
    AdContinuStopFn AD_continu_stop = nullptr;

    UsbDaqApi() = default;
    UsbDaqApi(const UsbDaqApi&) = delete;
    UsbDaqApi& operator=(const UsbDaqApi&) = delete;

    ~UsbDaqApi() { unload(); }

    bool load(const std::filesystem::path& dll_path, std::string& error) {
        unload();
#ifdef _WIN32
        module_ = ::LoadLibraryW(dll_path.wstring().c_str());
        if (!module_) {
            std::ostringstream stream;
            stream << "LoadLibrary failed for " << dll_path.u8string()
                   << " (Win32 error " << ::GetLastError() << ')';
            error = stream.str();
            return false;
        }

        bool ok = true;
        ok &= bind(openUSB, "openUSB", error);
        ok &= bind(closeUSB, "closeUSB", error);
        ok &= bind(get_device_num, "get_device_num", error);
        ok &= bind(Reset_Usb_Device, "Reset_Usb_Device", error);
        ok &= bind(ad_single, "ad_single", error);
        ok &= bind(ad_continu_conf, "ad_continu_conf", error);
        ok &= bind(Get_AdBuf_Size, "Get_AdBuf_Size", error);
        ok &= bind(Read_AdBuf, "Read_AdBuf", error);
        ok &= bind(AD_continu_stop, "AD_continu_stop", error);
        if (!ok) unload();
        return ok;
#else
        (void)dll_path;
        (void)error;
        openUSB = &usb_daq_libusb::open_usb;
        closeUSB = &usb_daq_libusb::close_usb;
        get_device_num = &usb_daq_libusb::device_count;
        Reset_Usb_Device = &usb_daq_libusb::reset_device;
        ad_single = &usb_daq_libusb::ad_single;
        ad_continu_conf = &usb_daq_libusb::configure_continuous;
        Get_AdBuf_Size = &usb_daq_libusb::buffer_size;
        Read_AdBuf = &usb_daq_libusb::read_buffer;
        AD_continu_stop = &usb_daq_libusb::stop_continuous;
        return true;
#endif
    }

    std::string error_detail() const {
#ifdef _WIN32
        return {};
#else
        return usb_daq_libusb::last_error();
#endif
    }

    void unload() {
        openUSB = nullptr;
        closeUSB = nullptr;
        get_device_num = nullptr;
        Reset_Usb_Device = nullptr;
        ad_single = nullptr;
        ad_continu_conf = nullptr;
        Get_AdBuf_Size = nullptr;
        Read_AdBuf = nullptr;
        AD_continu_stop = nullptr;
#ifdef _WIN32
        if (module_) {
            ::FreeLibrary(module_);
            module_ = nullptr;
        }
#endif
    }

private:
#ifdef _WIN32
    template <typename T>
    bool bind(T& target, const char* name, std::string& error) {
        target = reinterpret_cast<T>(::GetProcAddress(module_, name));
        if (target) return true;
        if (!error.empty()) error += "; ";
        error += "missing DLL export: ";
        error += name;
        return false;
    }

    HMODULE module_ = nullptr;
#endif
};

#undef USB_DAQ_CALL
