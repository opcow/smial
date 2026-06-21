#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

class HidDevice {
public:
    HidDevice() = default;
    ~HidDevice() { close(); }
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    bool open();
    void close();
    bool isOpen() const { return dev_ != nullptr; }
    std::string productName() const { return name_; }

    // Blocking 32-byte request/reply, 1500 ms timeout. Throws on error.
    std::array<uint8_t, 32> xfer(std::initializer_list<uint8_t> payload);

    // Non-blocking read (timeout=0). Returns false if no data available.
    bool tryRead(std::array<uint8_t, 32>& out);

    void setIdentifyCallback(std::function<void(int row, int col, uint16_t kc)> cb);
    void clearIdentifyCallback();

private:
    void* dev_ = nullptr;  // hid_device*
    std::string name_;
    std::function<void(int, int, uint16_t)> identifyCb_;
};
