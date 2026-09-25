#pragma once

#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Security/Crypto.hpp"

#include <filesystem>
#include <vector>

namespace sentinel {

#pragma pack(push,1)
struct SevHeaderV1 {
    char magic[4];
    uint16_t version;
    uint16_t flags;
    std::array<std::byte,16> evidenceId;
    std::array<std::byte,16> caseId;
    uint64_t originalSize;
    uint64_t ciphertextSize;
    uint32_t metadataSize;
    uint8_t nonceLength;
    uint8_t tagLength;
    uint16_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(SevHeaderV1) == 64, "SEV1 header size must remain stable");

class SevContainer {
public:
    static void EncryptFile(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const EvidenceId&,
        const CaseId&,
        std::span<const std::byte> key,
        IAeadCipher& cipher,
        IHashService& hash,
        Hash256& originalHashOut);

    [[nodiscard]] static SevHeaderV1 ReadHeader(
        const std::filesystem::path& path);

    [[nodiscard]] static bool BasicValidate(
        const std::filesystem::path& path);

    [[nodiscard]] static std::vector<std::byte> DecryptFile(
        const std::filesystem::path& path,
        std::span<const std::byte> key,
        IAeadCipher& cipher,
        IHashService& hash,
        Hash256* plaintextHashOut = nullptr);
};

}
