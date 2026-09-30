#include "audio/DeviceCatalog.h"
#include <functiondiscoverykeys_devpkey.h>
namespace taiko {
std::wstring deviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    PROPVARIANT name;
    PropVariantInit(&name);
    std::wstring result = L"Audio device";
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
        SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
        result = name.pwszVal;
    PropVariantClear(&name);
    return result;
}
std::vector<Device> enumerateDevices() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        throw std::runtime_error("Cannot enumerate audio devices");
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)))
        throw std::runtime_error("Cannot list active devices");
    UINT count{};
    collection->GetCount(&count);
    std::vector<Device> result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> d;
        LPWSTR id{};
        if (SUCCEEDED(collection->Item(i, &d)) && SUCCEEDED(d->GetId(&id))) {
            result.push_back({id, deviceName(d.Get())});
            CoTaskMemFree(id);
        }
    }
    return result;
}
} // namespace taiko
