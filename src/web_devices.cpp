#include "web_devices.h"
#include "document.h"
#include <cmath>

// Sizes of the newest devices come from public viewport tables, not from the makers: treat them as close, not exact.
const WebDevice kWebDevices[] = {
    { "desktop", "Desktop", "Desktop", 1440, 900, DeviceKind::Desktop, 0, nullptr },
    { "desktop-hd", "Desktop", "Desktop Full HD", 1920, 1080, DeviceKind::Desktop, 0, nullptr },
    { "laptop", "Desktop", "Laptop", 1280, 800, DeviceKind::Desktop, 0, nullptr },
    { "laptop-hd", "Desktop", "Laptop 1366", 1366, 768, DeviceKind::Desktop, 0, nullptr },
    { "ipad-pro", "Tablet", "iPad Pro 13\"", 1032, 1376, DeviceKind::Tablet, 0, nullptr },
    { "ipad-air", "Tablet", "iPad Air 11\"", 820, 1180, DeviceKind::Tablet, 0, nullptr },
    { "ipad-mini", "Tablet", "iPad mini", 744, 1133, DeviceKind::Tablet, 0, nullptr },
    { "android-tablet", "Tablet", "Android tablet", 800, 1280, DeviceKind::Tablet, 0, nullptr },
    { "iphone-17-pro-max", "iPhone", "iPhone 17 Pro Max", 440, 956, DeviceKind::Phone, 0, nullptr },
    { "iphone-air", "iPhone", "iPhone Air", 420, 912, DeviceKind::Phone, 0, nullptr },
    { "iphone-17", "iPhone", "iPhone 17 / 17 Pro", 402, 874, DeviceKind::Phone, 0, nullptr },
    { "iphone-16e", "iPhone", "iPhone 16e", 390, 844, DeviceKind::Phone, 0, nullptr },
    { "iphone-se", "iPhone", "iPhone SE", 375, 667, DeviceKind::Phone, 0, nullptr },
    { "iphone-duo", "iPhone Duo", "iPhone Duo, unfolded", 890, 626, DeviceKind::Foldable, 1, "iphone-duo-folded" },
    { "iphone-duo-folded", "iPhone Duo", "iPhone Duo, folded", 466, 678, DeviceKind::Phone, 0, "iphone-duo" },
    { "pixel-10", "Android", "Pixel 10", 412, 923, DeviceKind::Phone, 0, nullptr },
    { "galaxy-s25-ultra", "Android", "Galaxy S25 Ultra", 412, 891, DeviceKind::Phone, 0, nullptr },
    { "galaxy-s25", "Android", "Galaxy S25", 360, 780, DeviceKind::Phone, 0, nullptr },
    { "android-compact", "Android", "Android compact", 360, 800, DeviceKind::Phone, 0, nullptr },
    { "galaxy-z-fold7", "Android foldables", "Galaxy Z Fold7, unfolded", 984, 1092, DeviceKind::Foldable, 1, "galaxy-z-fold7-folded" },
    { "galaxy-z-fold7-folded", "Android foldables", "Galaxy Z Fold7, folded", 412, 960, DeviceKind::Phone, 0, "galaxy-z-fold7" },
    { "pixel-10-pro-fold", "Android foldables", "Pixel 10 Pro Fold, unfolded", 791, 820, DeviceKind::Foldable, 1, "pixel-10-pro-fold-folded" },
    { "pixel-10-pro-fold-folded", "Android foldables", "Pixel 10 Pro Fold, folded", 412, 900, DeviceKind::Phone, 0, "pixel-10-pro-fold" },
    { "galaxy-z-flip7", "Android foldables", "Galaxy Z Flip7, open", 360, 840, DeviceKind::Foldable, 2, "galaxy-z-flip7-cover" },
    { "galaxy-z-flip7-cover", "Android foldables", "Galaxy Z Flip7, cover screen", 361, 399, DeviceKind::Phone, 0, "galaxy-z-flip7" },
};
const int kWebDeviceCount = (int)(sizeof(kWebDevices) / sizeof(kWebDevices[0]));
const char* const kWebDeviceGroups[] = { "Desktop", "Tablet", "iPhone", "iPhone Duo", "Android", "Android foldables" };
const int kWebDeviceGroupCount = (int)(sizeof(kWebDeviceGroups) / sizeof(kWebDeviceGroups[0]));

const WebDevice* FindWebDevice(const std::string& key)
{
    for (auto& d : kWebDevices) if (key == d.key) return &d;
    return nullptr;
}

const WebDevice* WebDeviceOf(const Node* f, bool* rotated)
{
    const WebDevice* d = FindWebDevice(f->device);
    if (!d) return nullptr;
    bool rot = std::fabs(f->w - d->w) >= 0.5f;
    if (rot && std::fabs(f->w - d->h) >= 0.5f) return nullptr;
    if (rotated) *rotated = rot;
    return d;
}
