#include "hid.h"
#include <hidapi.h>
#include <cstring>
#include <stdexcept>

static constexpr uint16_t VID        = 0x3434;
static constexpr uint16_t PID        = 0x0610;
static constexpr uint16_t USAGE_PAGE = 0xFF60;
static constexpr uint16_t USAGE      = 0x61;
static constexpr int      TIMEOUT_MS = 1500;

static hid_device* dev(void* p) { return static_cast<hid_device*>(p); }

bool HidDevice::open() {
    hid_init();
    auto* list = hid_enumerate(VID, PID);
    hid_device_info* found = nullptr;
    for (auto* d = list; d; d = d->next)
        if (d->usage_page == USAGE_PAGE && d->usage == USAGE) { found = d; break; }
    if (!found) { hid_free_enumeration(list); return false; }
    dev_ = hid_open_path(found->path);
    if (dev_ && found->product_string) {
        char buf[128] = {};
        wcstombs(buf, found->product_string, sizeof(buf) - 1);
        name_ = buf;
    }
    hid_free_enumeration(list);
    return dev_ != nullptr;
}

void HidDevice::close() {
    if (dev_) { hid_close(dev(dev_)); dev_ = nullptr; }
}

std::array<uint8_t, 32> HidDevice::xfer(std::initializer_list<uint8_t> payload) {
    if (!dev_) throw std::runtime_error("device not open");
    uint8_t buf[33] = {};  // hidapi prepends report-id 0
    size_t i = 1;
    for (auto b : payload) { if (i < 33) buf[i++] = b; }
    if (hid_write(dev(dev_), buf, 33) < 0)
        throw std::runtime_error("hid_write failed");
    std::array<uint8_t, 32> reply = {};
    int n = hid_read_timeout(dev(dev_), reply.data(), 32, TIMEOUT_MS);
    if (n < 0) throw std::runtime_error("hid_read failed");
    if (n == 0) throw std::runtime_error("timeout");
    return reply;
}

std::array<uint8_t, 32> HidDevice::xfer(const uint8_t* data, size_t len) {
    if (!dev_) throw std::runtime_error("device not open");
    uint8_t buf[33] = {};
    for (size_t i = 0; i < len && i < 32; i++) buf[1 + i] = data[i];
    if (hid_write(dev(dev_), buf, 33) < 0)
        throw std::runtime_error("hid_write failed");
    std::array<uint8_t, 32> reply = {};
    int n = hid_read_timeout(dev(dev_), reply.data(), 32, TIMEOUT_MS);
    if (n < 0) throw std::runtime_error("hid_read failed");
    if (n == 0) throw std::runtime_error("timeout");
    return reply;
}

bool HidDevice::tryRead(std::array<uint8_t, 32>& out) {
    if (!dev_) return false;
    int n = hid_read_timeout(static_cast<hid_device*>(dev_), out.data(), 32, 0);
    return n > 0;
}

void HidDevice::setIdentifyCallback(std::function<void(int, int, uint16_t)> cb) {
    identifyCb_ = std::move(cb);
}

void HidDevice::clearIdentifyCallback() { identifyCb_ = nullptr; }
