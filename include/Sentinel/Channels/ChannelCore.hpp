#pragma once
#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::channels {

enum class ChannelType {
    LocalSimulation,
    Sms,
    Mms,
    Rcs,
    Telegram,
    Discord,
    Messenger,
    WhatsApp,
    SnapchatAssist,
    Email,
    Unknown
};

enum class Direction { Inbound=0, Outbound=1, Internal=2 };
enum class IdentityLinkState { Candidate=0, Confirmed=1, Rejected=2 };
enum class ConversationState { Active=0, Paused=1, Closed=2 };
enum class AutomationMode { Manual=0, DraftOnly=1, ApprovalRequired=2, AuthorizedAutomatic=3 };
enum class AutomationDecisionKind { Block=0, Draft=1, RequireApproval=2, AutoSend=3 };

enum Capability : std::uint64_t {
    ReceiveText       = 1ull<<0,
    SendText          = 1ull<<1,
    ReceiveImage      = 1ull<<2,
    SendImage         = 1ull<<3,
    ReceiveVideo      = 1ull<<4,
    SendVideo         = 1ull<<5,
    TypingIndicator   = 1ull<<6,
    ReadReceipts      = 1ull<<7,
    DeliveryReceipts  = 1ull<<8,
    EditMessages      = 1ull<<9,
    DeleteMessages    = 1ull<<10,
    OutboundInitiation= 1ull<<11,
    AutomatedSending  = 1ull<<12,
    AccountAutomation = 1ull<<13
};

struct ChannelCapabilities {
    std::uint64_t mask{};
    [[nodiscard]] bool Has(Capability value) const noexcept {
        return (mask & static_cast<std::uint64_t>(value)) != 0;
    }
    void Set(Capability value,bool enabled=true) noexcept {
        const auto bit=static_cast<std::uint64_t>(value);
        if(enabled) mask|=bit; else mask&=~bit;
    }
};

struct ChannelAccount {
    std::string id;
    ChannelType type{ChannelType::Unknown};
    std::string provider;
    std::string externalAccountId;
    std::string displayName;
    std::string address;
    std::string jurisdiction;
    std::string complianceStatus;
    ChannelCapabilities capabilities;
    bool enabled{true};
};

struct ChannelConversation {
    std::string id;
    std::string subjectId;
    std::string personaName;
    std::string channelAccountId;
    ChannelType type{ChannelType::Unknown};
    std::string provider;
    std::string providerConversationId;
    std::string externalPeerId;
    std::string automationProfileId;
    ConversationState state{ConversationState::Active};
};

struct RawChannelEvent {
    std::string id;
    std::string channelConversationId;
    std::string provider;
    ChannelType type{ChannelType::Unknown};
    std::string providerAccountId;
    std::string providerConversationId;
    std::string providerMessageId;
    std::string eventType;
    Direction direction{Direction::Inbound};
    std::string senderExternalId;
    std::string recipientExternalId;
    std::string providerTimestamp;
    std::string rawPayload;
    std::string payloadSha256;
};

struct NormalizedMessage {
    std::string id;
    std::string eventId;
    std::string channelConversationId;
    std::string subjectId;
    std::string personaName;
    Direction direction{Direction::Inbound};
    std::string senderExternalId;
    std::string recipientExternalId;
    std::string body;
    int deliveryState{};
    AutomationMode automationMode{AutomationMode::Manual};
    std::string modelRunId;
    std::string policyDecisionId;
    std::string approvalId;
    std::string providerMessageId;
};

struct OutboundText {
    std::string conversationId;
    std::string text;
    std::string idempotencyKey;
};

struct OutboundMedia {
    std::string conversationId;
    std::string caption;
    std::string localPath;
    std::string mimeType;
    std::string sha256;
    std::string idempotencyKey;
};

struct SendResult {
    bool accepted{};
    std::string providerMessageId;
    std::string error;
};

class IChannelAdapter {
public:
    virtual ~IChannelAdapter()=default;
    [[nodiscard]] virtual std::string AdapterName() const=0;
    [[nodiscard]] virtual ChannelType Type() const=0;
    [[nodiscard]] virtual ChannelCapabilities Capabilities() const=0;
    [[nodiscard]] virtual bool Connected() const=0;
    virtual std::vector<RawChannelEvent> Poll()=0;
    virtual SendResult SendText(const OutboundText&)=0;
    virtual SendResult SendMedia(const OutboundMedia&)=0;
    virtual bool MarkRead(std::string_view providerConversationId,std::string_view providerMessageId)=0;
    virtual bool SetTyping(std::string_view providerConversationId,bool active)=0;
};

class ChannelCoreStore {
public:
    explicit ChannelCoreStore(SqliteDatabase& db):db_(db){}

    std::string CreateSubject(std::string caseId,std::string displayName);
    std::string AddSubjectIdentity(
        std::string subjectId,
        std::string identityType,
        std::string provider,
        std::string externalId,
        std::string displayValue,
        IdentityLinkState state,
        double confidence,
        std::string sourceEventId={});
    bool ConfirmSubjectIdentity(std::string_view identityId,std::string reviewer);

    std::string UpsertChannelAccount(const ChannelAccount&);
    std::string OpenConversation(const ChannelConversation&);
    std::optional<ChannelConversation> FindConversation(
        std::string_view provider,
        std::string_view channelAccountId,
        std::string_view providerConversationId) const;

    void RecordRawEvent(const RawChannelEvent&);
    void RecordMessage(const NormalizedMessage&);
    std::vector<NormalizedMessage> RecentMessages(std::string_view conversationId,int limit=50) const;

private:
    SqliteDatabase& db_;
};

[[nodiscard]] std::string ToString(ChannelType);
[[nodiscard]] ChannelType ChannelTypeFromString(std::string_view);

}
