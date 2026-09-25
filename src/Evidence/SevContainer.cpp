#include "Sentinel/Evidence/SevContainer.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace sentinel {
namespace {

constexpr uint64_t kMaxBufferedPayload = 256ULL * 1024ULL * 1024ULL;

std::span<const std::byte> HeaderAad(const SevHeaderV1& h)
{
    return {
        reinterpret_cast<const std::byte*>(&h),
        sizeof(h)
    };
}

uint64_t ExpectedSize(const SevHeaderV1& h)
{
    return sizeof(SevHeaderV1)
        + static_cast<uint64_t>(h.nonceLength)
        + static_cast<uint64_t>(h.metadataSize)
        + h.ciphertextSize
        + static_cast<uint64_t>(h.tagLength);
}

void ValidateHeader(const SevHeaderV1& h)
{
    if (std::memcmp(h.magic, "SEV1", 4) != 0)
        throw std::runtime_error("invalid SEV magic");
    if (h.version != 1)
        throw std::runtime_error("unsupported SEV version");
    if (h.flags != 0 || h.reserved != 0)
        throw std::runtime_error("unsupported SEV flags");
    if (h.nonceLength != 12)
        throw std::runtime_error("invalid SEV nonce length");
    if (h.tagLength != 16)
        throw std::runtime_error("invalid SEV tag length");
    if (h.metadataSize != 0)
        throw std::runtime_error("SEV1 metadata block is not implemented yet");
    if (h.ciphertextSize != h.originalSize)
        throw std::runtime_error("invalid SEV1 payload size");
    if (h.ciphertextSize > kMaxBufferedPayload)
        throw std::runtime_error("SEV1 buffered reader limit exceeded; chunked SEV is required");
}

}

void SevContainer::EncryptFile(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    const EvidenceId& eid,
    const CaseId& cid,
    std::span<const std::byte> key,
    IAeadCipher& cipher,
    IHashService& hash,
    Hash256& originalHashOut)
{
    std::ifstream in(src, std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("cannot open evidence source");

    const auto end = in.tellg();
    if (end < 0)
        throw std::runtime_error("cannot determine evidence source size");

    const auto size = static_cast<uint64_t>(end);
    if (size > kMaxBufferedPayload)
        throw std::runtime_error("evidence exceeds current SEV1 buffered limit; chunked encryption not implemented yet");
    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        throw std::runtime_error("evidence is too large for this build");

    in.seekg(0);
    std::vector<std::byte> plain(static_cast<size_t>(size));
    if (size && !in.read(reinterpret_cast<char*>(plain.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("failed reading evidence source");

    originalHashOut = hash.Sha256(plain);

    SevHeaderV1 h{};
    std::memcpy(h.magic, "SEV1", 4);
    h.version = 1;
    h.flags = 0;
    h.evidenceId = eid.Bytes();
    h.caseId = cid.Bytes();
    h.originalSize = size;
    h.ciphertextSize = size; // AES-GCM preserves payload length. Must be finalized before AAD is authenticated.
    h.metadataSize = 0;
    h.nonceLength = 12;
    h.tagLength = 16;
    h.reserved = 0;

    const auto enc = cipher.Encrypt(plain, key, HeaderAad(h));
    if (enc.ciphertext.size() != size)
        throw std::runtime_error("unexpected AES-GCM ciphertext size");

    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot create SEV container");

    out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    out.write(reinterpret_cast<const char*>(enc.nonce.data()), static_cast<std::streamsize>(enc.nonce.size()));
    if (!enc.ciphertext.empty())
        out.write(reinterpret_cast<const char*>(enc.ciphertext.data()), static_cast<std::streamsize>(enc.ciphertext.size()));
    out.write(reinterpret_cast<const char*>(enc.tag.data()), static_cast<std::streamsize>(enc.tag.size()));
    out.flush();

    if (!out)
        throw std::runtime_error("failed writing SEV container");
}

SevHeaderV1 SevContainer::ReadHeader(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open SEV container");

    SevHeaderV1 h{};
    if (!in.read(reinterpret_cast<char*>(&h), sizeof(h)))
        throw std::runtime_error("truncated SEV header");

    ValidateHeader(h);
    return h;
}

bool SevContainer::BasicValidate(const std::filesystem::path& path)
{
    try {
        const auto h = ReadHeader(path);
        const auto actual = std::filesystem::file_size(path);
        return actual == ExpectedSize(h);
    }
    catch (...) {
        return false;
    }
}

std::vector<std::byte> SevContainer::DecryptFile(
    const std::filesystem::path& path,
    std::span<const std::byte> key,
    IAeadCipher& cipher,
    IHashService& hash,
    Hash256* plaintextHashOut)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open SEV container");

    SevHeaderV1 h{};
    if (!in.read(reinterpret_cast<char*>(&h), sizeof(h)))
        throw std::runtime_error("truncated SEV header");
    ValidateHeader(h);

    const auto actual = std::filesystem::file_size(path);
    if (actual != ExpectedSize(h))
        throw std::runtime_error("SEV container length mismatch");

    std::array<std::byte, 12> nonce{};
    std::array<std::byte, 16> tag{};
    std::vector<std::byte> ciphertext(static_cast<size_t>(h.ciphertextSize));

    if (!in.read(reinterpret_cast<char*>(nonce.data()), static_cast<std::streamsize>(nonce.size())))
        throw std::runtime_error("truncated SEV nonce");
    if (!ciphertext.empty() && !in.read(reinterpret_cast<char*>(ciphertext.data()), static_cast<std::streamsize>(ciphertext.size())))
        throw std::runtime_error("truncated SEV payload");
    if (!in.read(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tag.size())))
        throw std::runtime_error("truncated SEV authentication tag");

    auto plain = cipher.Decrypt(ciphertext, key, nonce, tag, HeaderAad(h));
    if (plain.size() != h.originalSize)
        throw std::runtime_error("SEV plaintext size mismatch");

    if (plaintextHashOut)
        *plaintextHashOut = hash.Sha256(plain);

    return plain;
}

}
