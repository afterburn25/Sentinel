#include "Sentinel/Security/Crypto.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace sentinel {

#ifdef _WIN32
namespace {

void Check(NTSTATUS status, const char* message)
{
    if (status < 0) throw std::runtime_error(message);
}

class Algorithm final {
public:
    Algorithm(LPCWSTR id)
    {
        Check(BCryptOpenAlgorithmProvider(&handle_, id, nullptr, 0),
              "BCryptOpenAlgorithmProvider failed");
    }
    ~Algorithm()
    {
        if (handle_) BCryptCloseAlgorithmProvider(handle_, 0);
    }
    BCRYPT_ALG_HANDLE get() const noexcept { return handle_; }
private:
    BCRYPT_ALG_HANDLE handle_{};
};

Hash256 HashBytes(std::span<const std::byte> data)
{
    Algorithm alg(BCRYPT_SHA256_ALGORITHM);
    DWORD objectLength = 0, bytes = 0;
    Check(BCryptGetProperty(alg.get(), BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytes, 0),
        "BCryptGetProperty failed");

    std::vector<UCHAR> object(objectLength);
    BCRYPT_HASH_HANDLE hash{};
    Check(BCryptCreateHash(alg.get(), &hash, object.data(), objectLength, nullptr, 0, 0),
          "BCryptCreateHash failed");

    try {
        if (!data.empty())
            Check(BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),
                static_cast<ULONG>(data.size()), 0),
                "BCryptHashData failed");

        Hash256 result{};
        Check(BCryptFinishHash(hash,
            reinterpret_cast<PUCHAR>(result.bytes.data()),
            static_cast<ULONG>(result.bytes.size()), 0),
            "BCryptFinishHash failed");
        BCryptDestroyHash(hash);
        return result;
    } catch (...) {
        BCryptDestroyHash(hash);
        throw;
    }
}

class AesKey final {
public:
    AesKey(std::span<const std::byte> key)
    {
        if (key.size() != 32) throw std::invalid_argument("AES-256 key must be 32 bytes");
        Check(BCryptOpenAlgorithmProvider(&alg_, BCRYPT_AES_ALGORITHM, nullptr, 0),
              "BCryptOpenAlgorithmProvider AES failed");
        Check(BCryptSetProperty(alg_, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0),
            "BCryptSetProperty GCM failed");
        Check(BCryptGenerateSymmetricKey(alg_, &key_, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(key.data())),
            static_cast<ULONG>(key.size()), 0),
            "BCryptGenerateSymmetricKey failed");
    }

    ~AesKey()
    {
        if (key_) BCryptDestroyKey(key_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }

    BCRYPT_KEY_HANDLE get() const noexcept { return key_; }

private:
    BCRYPT_ALG_HANDLE alg_{};
    BCRYPT_KEY_HANDLE key_{};
};

}

void WindowsSecureRandom::Fill(std::span<std::byte> output)
{
    Check(BCryptGenRandom(nullptr,
        reinterpret_cast<PUCHAR>(output.data()),
        static_cast<ULONG>(output.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG),
        "BCryptGenRandom failed");
}

SecureBuffer WindowsSecureRandom::Bytes(size_t count)
{
    SecureBuffer result(count);
    Fill(result.Span());
    return result;
}

Hash256 WindowsHashService::Sha256(std::span<const std::byte> data)
{
    return HashBytes(data);
}

Hash256 WindowsHashService::Sha256File(const std::filesystem::path& path)
{
    Algorithm alg(BCRYPT_SHA256_ALGORITHM);
    DWORD objectLength = 0, bytes = 0;
    Check(BCryptGetProperty(alg.get(), BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytes, 0),
        "BCryptGetProperty failed");

    std::vector<UCHAR> object(objectLength);
    BCRYPT_HASH_HANDLE hash{};
    Check(BCryptCreateHash(alg.get(), &hash, object.data(), objectLength, nullptr, 0, 0),
          "BCryptCreateHash failed");

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        BCryptDestroyHash(hash);
        throw std::runtime_error("cannot open file for hashing");
    }

    std::vector<char> buffer(4 * 1024 * 1024);
    try {
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = in.gcount();
            if (count > 0)
                Check(BCryptHashData(hash,
                    reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(count), 0),
                    "BCryptHashData file failed");
        }

        Hash256 result{};
        Check(BCryptFinishHash(hash,
            reinterpret_cast<PUCHAR>(result.bytes.data()),
            static_cast<ULONG>(result.bytes.size()), 0),
            "BCryptFinishHash failed");
        BCryptDestroyHash(hash);
        return result;
    } catch (...) {
        BCryptDestroyHash(hash);
        throw;
    }
}

AesGcmCiphertext WindowsAesGcmCipher::Encrypt(
    std::span<const std::byte> plaintext,
    std::span<const std::byte> key,
    std::span<const std::byte> aad)
{
    AesKey aes(key);
    AesGcmCiphertext result;
    random_.Fill(result.nonce);
    result.ciphertext.resize(plaintext.size());

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(result.nonce.data());
    info.cbNonce = static_cast<ULONG>(result.nonce.size());
    info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<std::byte*>(aad.data()));
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = reinterpret_cast<PUCHAR>(result.tag.data());
    info.cbTag = static_cast<ULONG>(result.tag.size());

    ULONG written = 0;
    Check(BCryptEncrypt(aes.get(),
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(plaintext.data())),
        static_cast<ULONG>(plaintext.size()),
        &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(result.ciphertext.data()),
        static_cast<ULONG>(result.ciphertext.size()),
        &written, 0),
        "AES-GCM encryption failed");

    result.ciphertext.resize(written);
    return result;
}

std::vector<std::byte> WindowsAesGcmCipher::Decrypt(
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> key,
    std::span<const std::byte> nonce,
    std::span<const std::byte> tag,
    std::span<const std::byte> aad)
{
    AesKey aes(key);
    std::vector<std::byte> plaintext(ciphertext.size());

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<std::byte*>(nonce.data()));
    info.cbNonce = static_cast<ULONG>(nonce.size());
    info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<std::byte*>(aad.data()));
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = reinterpret_cast<PUCHAR>(const_cast<std::byte*>(tag.data()));
    info.cbTag = static_cast<ULONG>(tag.size());

    ULONG written = 0;
    Check(BCryptDecrypt(aes.get(),
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(ciphertext.data())),
        static_cast<ULONG>(ciphertext.size()),
        &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(plaintext.data()),
        static_cast<ULONG>(plaintext.size()),
        &written, 0),
        "AES-GCM authentication/decryption failed");

    plaintext.resize(written);
    return plaintext;
}

#else

void WindowsSecureRandom::Fill(std::span<std::byte>)
{ throw std::runtime_error("Windows crypto implementation requires Windows"); }

SecureBuffer WindowsSecureRandom::Bytes(size_t)
{ throw std::runtime_error("Windows crypto implementation requires Windows"); }

Hash256 WindowsHashService::Sha256(std::span<const std::byte>)
{ throw std::runtime_error("Windows crypto implementation requires Windows"); }

Hash256 WindowsHashService::Sha256File(const std::filesystem::path&)
{ throw std::runtime_error("Windows crypto implementation requires Windows"); }

AesGcmCiphertext WindowsAesGcmCipher::Encrypt(
    std::span<const std::byte>, std::span<const std::byte>, std::span<const std::byte>)
{ throw std::runtime_error("Windows crypto implementation requires Windows"); }

std::vector<std::byte> WindowsAesGcmCipher::Decrypt(
    std::span<const std::byte>, std::span<const std::byte>,
    std::span<const std::byte>, std::span<const std::byte>,
    std::span<const std::byte>)
{ throw std::runtime_error("Windows crypto implementation requires Windows"); }

#endif

}
