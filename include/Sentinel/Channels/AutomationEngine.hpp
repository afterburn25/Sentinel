#pragma once
#include "Sentinel/Channels/ChannelCore.hpp"
#include <string>

namespace sentinel::channels {

enum class ActionKind {
    OrdinaryReply,
    BenignMedia,
    ContactDisclosure,
    IdentityLink,
    ChannelMigration,
    MeetingArrangement,
    MoneyOrPayment,
    SensitiveContent
};

struct AutomationRequest {
    AutomationMode mode{AutomationMode::Manual};
    ActionKind action{ActionKind::OrdinaryReply};
    bool providerSupportsAutomation{false};
    bool policyAllowed{true};
    bool policyRequiresSupervisor{false};
};

struct AutomationOutcome {
    AutomationDecisionKind decision{AutomationDecisionKind::Draft};
    bool requiresHuman{true};
    std::string reason;
};

class AutomationEngine {
public:
    [[nodiscard]] AutomationOutcome Decide(const AutomationRequest&) const;
};

}
