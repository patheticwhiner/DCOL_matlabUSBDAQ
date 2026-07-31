#pragma once

#include <windows.h>

#include <filesystem>
#include <sstream>
#include <string>

class UsbDaqApi {
public:
    using OpenFn = int(__cdecl*)();
    using CloseFn = int(__cdecl*)();
    using DeviceCountFn = int(__cdecl*)();
    using ResetDeviceFn = int(__cdecl*)(int);
    using AdSingleFn = int(__cdecl*)(int, int, int, float*);
    using AdContinuConfFn = int(__cdecl*)(int, int, int, int, int, int, int, int, int, int);
    using GetAdBufSizeFn = int(__cdecl*)(int);
    using ReadAdBufFn = int(__cdecl*)(int, float*, int);
    using AdContinuStopFn = int(__cdecl*)(int);

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
        module_ = ::LoadLibraryW(dll_path.wstring().c_str());
        if (!module_) {
            std::ostringstream oss;
            oss << "LoadLibrary failed for " << dll_path.u8string()
                << " (Win32 error " << ::GetLastError() << ")";
            error = oss.str();
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
        if (module_) {
            ::FreeLibrary(module_);
            module_ = nullptr;
        }
    }

private:
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
};
