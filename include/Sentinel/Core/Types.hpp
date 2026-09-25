#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sentinel {

using Timestamp = std::chrono::system_clock::time_point;

struct Hash256 {
    std::array<std::byte, 32> bytes{};
    [[nodiscard]] std::string ToHex() const;
    [[nodiscard]] static std::optional<Hash256> FromHex(std::string_view value);
    bool operator==(const Hash256&) const = default;
};

class Uuid {
public:
    Uuid() = default;
    explicit Uuid(std::array<std::byte,16> bytes) : bytes_(bytes) {}
    [[nodiscard]] static Uuid Random();
    [[nodiscard]] static std::optional<Uuid> Parse(std::string_view value);
    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] const std::array<std::byte,16>& Bytes() const noexcept { return bytes_; }
    bool operator==(const Uuid&) const = default;
private:
    std::array<std::byte,16> bytes_{};
};

template<typename Tag>
class StrongUuid {
public:
    StrongUuid() = default;
    explicit StrongUuid(Uuid value) : value_(std::move(value)) {}
    [[nodiscard]] static StrongUuid Random() { return StrongUuid{Uuid::Random()}; }
    [[nodiscard]] static std::optional<StrongUuid> Parse(std::string_view value) {
        if (auto parsed = Uuid::Parse(value)) return StrongUuid{*parsed};
        return std::nullopt;
    }
    [[nodiscard]] std::string ToString() const { return value_.ToString(); }
    [[nodiscard]] const std::array<std::byte,16>& Bytes() const noexcept { return value_.Bytes(); }
    bool operator==(const StrongUuid&) const = default;
private:
    Uuid value_{};
};

struct CaseIdTag{}; struct EvidenceIdTag{}; struct AuditIdTag{}; struct UserIdTag{};
struct ConversationIdTag{}; struct MessageIdTag{}; struct PersonaIdTag{}; struct ApprovalIdTag{};
using CaseId=StrongUuid<CaseIdTag>; using EvidenceId=StrongUuid<EvidenceIdTag>;
using AuditId=StrongUuid<AuditIdTag>; using UserId=StrongUuid<UserIdTag>;
using ConversationId=StrongUuid<ConversationIdTag>; using MessageId=StrongUuid<MessageIdTag>;
using PersonaId=StrongUuid<PersonaIdTag>; using ApprovalId=StrongUuid<ApprovalIdTag>;

[[nodiscard]] std::string ToIso8601Utc(Timestamp tp);
[[nodiscard]] Timestamp NowUtc();

}
