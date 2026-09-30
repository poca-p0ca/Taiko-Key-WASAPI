#pragma once
#include "core/Common.h"
#include <propkeydef.h> // DEFINE_PROPERTYKEY must precede the Windows SDK's device keys.

#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <vector>
#include <wrl/client.h>
namespace taiko {
using Microsoft::WRL::ComPtr;
struct Device {
    std::wstring id, name;
};
std::vector<Device> enumerateDevices();
std::wstring deviceName(IMMDevice* device);
enum DeviceChange : uint32_t {
    DeviceListChanged = 1,
    SelectedDeviceChanged = 2,
    DefaultOutputChanged = 4,
    DeviceAvailabilityChanged = 8,
};
class DeviceNotifications final : public IMMNotificationClient {
    std::atomic<ULONG> refs_{1};
    HANDLE wake_;
    std::atomic<uint32_t>& changes_;
    const std::wstring selected_;
    void signal(uint32_t changes) {
        changes_.fetch_or(changes, std::memory_order_release);
        SetEvent(wake_);
    }
    void changed(LPCWSTR id) {
        signal(DeviceListChanged | DeviceAvailabilityChanged |
               (!selected_.empty() && id && selected_ == id ? SelectedDeviceChanged : 0));
    }

  public:
    DeviceNotifications(HANDLE wake, std::atomic<uint32_t>& changes, std::wstring selected)
        : wake_(wake), changes_(changes), selected_(std::move(selected)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD) override {
        changed(id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override {
        changed(id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override {
        changed(id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (selected_.empty() && flow == eRender && role == eConsole)
            signal(DeviceListChanged | DefaultOutputChanged);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY key) override {
        // Opening a stream can itself generate property notifications. Restarting
        // on these creates a start -> notification -> stop/start feedback loop.
        // Names only affect the picker. Format/resource changes that invalidate
        // playback are recovered via the renderer's WASAPI error path instead.
        if (IsEqualPropertyKey(key, PKEY_Device_FriendlyName) ||
            IsEqualPropertyKey(key, PKEY_DeviceInterface_FriendlyName) ||
            IsEqualPropertyKey(key, PKEY_Device_DeviceDesc))
            signal(DeviceListChanged);
        return S_OK;
    }
};
} // namespace taiko
