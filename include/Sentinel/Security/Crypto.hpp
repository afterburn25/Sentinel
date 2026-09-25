#pragma once
#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Security/SecureBuffer.hpp"
#include <array>
#include <filesystem>
#include <span>
#include <vector>
namespace sentinel {
struct AesGcmCiphertext{ std::vector<std::byte> ciphertext; std::array<std::byte,12> nonce{}; std::array<std::byte,16> tag{}; };
class ISecureRandom{public:virtual ~ISecureRandom()=default;virtual void Fill(std::span<std::byte>)=0;virtual SecureBuffer Bytes(size_t)=0;};
class IHashService{public:virtual ~IHashService()=default;virtual Hash256 Sha256(std::span<const std::byte>)=0;virtual Hash256 Sha256File(const std::filesystem::path&)=0;};
class IAeadCipher{public:virtual ~IAeadCipher()=default;virtual AesGcmCiphertext Encrypt(std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>)=0;virtual std::vector<std::byte> Decrypt(std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>)=0;};
class WindowsSecureRandom final:public ISecureRandom{public:void Fill(std::span<std::byte>) override;SecureBuffer Bytes(size_t) override;};
class WindowsHashService final:public IHashService{public:Hash256 Sha256(std::span<const std::byte>) override;Hash256 Sha256File(const std::filesystem::path&) override;};
class WindowsAesGcmCipher final:public IAeadCipher{public:explicit WindowsAesGcmCipher(ISecureRandom& random):random_(random){} AesGcmCiphertext Encrypt(std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>) override;std::vector<std::byte> Decrypt(std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>,std::span<const std::byte>) override;private:ISecureRandom& random_;};
}
