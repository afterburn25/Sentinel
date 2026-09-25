#pragma once
#include "Sentinel/Security/SecureBuffer.hpp"
#include <span>
#include <vector>
namespace sentinel {
class ISecretProtector{public:virtual ~ISecretProtector()=default;virtual std::vector<std::byte> Protect(std::span<const std::byte>)=0;virtual SecureBuffer Unprotect(std::span<const std::byte>)=0;};
class WindowsDpapiSecretProtector final:public ISecretProtector{public:std::vector<std::byte> Protect(std::span<const std::byte>) override;SecureBuffer Unprotect(std::span<const std::byte>) override;};
}
