#pragma once
#include <cstddef>
#include <span>
#include <vector>

namespace sentinel {
class SecureBuffer {
public:
    explicit SecureBuffer(size_t size=0);
    explicit SecureBuffer(std::span<const std::byte> source);
    ~SecureBuffer();
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;
    SecureBuffer(const SecureBuffer&)=delete;
    SecureBuffer& operator=(const SecureBuffer&)=delete;
    [[nodiscard]] std::span<std::byte> Span(){ return data_; }
    [[nodiscard]] std::span<const std::byte> Span() const { return data_; }
    [[nodiscard]] size_t Size() const noexcept { return data_.size(); }
private:
    void Wipe() noexcept;
    std::vector<std::byte> data_;
};
}
