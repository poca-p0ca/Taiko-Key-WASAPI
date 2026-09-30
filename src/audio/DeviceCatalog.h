#pragma once
#include "core/Common.h"
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
class DeviceNotifications final : public IMMNotificationClient {
    std::atomic<ULONG> refs_{1};
    HANDLE wake_;
    std::atomic<bool>& dirty_;
    const std::wstring selected_;
    void changed(LPCWSTR id) {
        if (selected_.empty() || (id && selected_ == id)) {
            dirty_.store(true);
            SetEvent(wake_);
        }
    }

  public:
    DeviceNotifications(HANDLE wake, std::atomic<bool>& dirty, std::wstring selected)
        : wake_(wake), dirty_(dirty), selected_(std::move(selected)) {}
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
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) override {
        if (selected_.empty() && flow == eRender && role == eConsole)
            changed(id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR id, const PROPERTYKEY) override {
        changed(id);
        return S_OK;
    }
};
} // namespace taiko
