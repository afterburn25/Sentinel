#include "Sentinel/Channels/AutomationEngine.hpp"

namespace sentinel::channels {

AutomationOutcome AutomationEngine::Decide(const AutomationRequest& r) const {
    if(!r.policyAllowed)
        return {AutomationDecisionKind::Block,true,"policy blocked the requested action"};

    if(r.policyRequiresSupervisor)
        return {AutomationDecisionKind::RequireApproval,true,"policy requires supervisor review"};

    if(r.mode==AutomationMode::Manual)
        return {AutomationDecisionKind::Draft,true,"manual mode"};

    if(r.mode==AutomationMode::DraftOnly)
        return {AutomationDecisionKind::Draft,true,"draft-only mode"};

    if(r.mode==AutomationMode::ApprovalRequired)
        return {AutomationDecisionKind::RequireApproval,true,"approval-required mode"};

    if(!r.jurisdictionProfileActive)
        return {AutomationDecisionKind::RequireApproval,true,"jurisdiction profile is not legally active"};

    if(!r.jurisdictionAllowsAutomation || r.jurisdictionRequiresReview)
        return {AutomationDecisionKind::RequireApproval,true,"jurisdiction rules require investigator review"};

    if(!r.providerSupportsAutomation)
        return {AutomationDecisionKind::RequireApproval,true,"channel does not support automated sending"};

    switch(r.action) {
        case ActionKind::OrdinaryReply:
            return {AutomationDecisionKind::AutoSend,false,"authorized automatic ordinary reply"};
        case ActionKind::BenignMedia:
            return {AutomationDecisionKind::RequireApproval,true,"media remains approval-gated"};
        case ActionKind::ContactDisclosure:
            return {AutomationDecisionKind::RequireApproval,true,"contact disclosure requires review"};
        case ActionKind::IdentityLink:
            return {AutomationDecisionKind::RequireApproval,true,"cross-channel identity links require investigator confirmation"};
        case ActionKind::ChannelMigration:
            return {AutomationDecisionKind::RequireApproval,true,"channel migration requires investigator confirmation"};
        case ActionKind::MeetingArrangement:
            return {AutomationDecisionKind::RequireApproval,true,"meeting-related actions require review"};
        case ActionKind::MoneyOrPayment:
            return {AutomationDecisionKind::RequireApproval,true,"money/payment actions require review"};
        case ActionKind::SensitiveContent:
            return {AutomationDecisionKind::RequireApproval,true,"sensitive content requires review"};
    }
    return {AutomationDecisionKind::RequireApproval,true,"unrecognized action requires review"};
}

}
