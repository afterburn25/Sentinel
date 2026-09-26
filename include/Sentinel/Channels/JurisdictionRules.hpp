#pragma once
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <vector>

namespace sentinel::channels {

enum class LegalReviewStatus { Draft=0, LegallyReviewed=1, Active=2, Expired=3 };
enum class RuleLayerType { Federal=0, State=1, Agency=2 };

struct JurisdictionSource {
    std::string authorityType;
    std::string citation;
    std::string sourceUrl;
    std::string note;
};

struct JurisdictionRules {
    bool allowAutomatedOrdinaryReplies{false};
    bool allowAutomatedBenignMedia{false};
    bool requireHumanContactDisclosure{true};
    bool requireHumanIdentityLink{true};
    bool requireHumanChannelMigration{true};
    bool requireHumanMeetingArrangement{true};
    bool requireHumanMoneyOrPayment{true};
    bool blockSexualizedMinorMedia{true};
    std::string recordingConsentMode{"unknown"};
};

struct JurisdictionProfile {
    std::string id;
    RuleLayerType layer{RuleLayerType::State};
    std::string countryCode{"US"};
    std::string regionCode;
    std::string agencyId;
    std::string name;
    std::string version;
    std::string effectiveFrom;
    std::string effectiveUntil;
    LegalReviewStatus reviewStatus{LegalReviewStatus::Draft};
    std::string reviewedBy;
    JurisdictionRules rules;
    std::vector<JurisdictionSource> sources;

    [[nodiscard]] bool AutomationLegallyActive() const noexcept {
        return reviewStatus==LegalReviewStatus::Active;
    }
};

class JurisdictionRuleStore {
public:
    explicit JurisdictionRuleStore(SqliteDatabase& db):db_(db){}

    void EnsureBuiltInBaselines();
    std::optional<JurisdictionProfile> ActiveProfile(
        RuleLayerType layer,std::string_view country,std::string_view region={},std::string_view agencyId={}) const;
    std::optional<JurisdictionProfile> LatestProfile(
        RuleLayerType layer,std::string_view country,std::string_view region={},std::string_view agencyId={}) const;

    void SelectForOperation(
        std::string operationKey,
        std::string country,
        std::string region,
        std::string federalProfileId,
        std::string stateProfileId,
        std::string agencyProfileId,
        std::string selectedBy);

    struct EffectiveRuleStack {
        std::optional<JurisdictionProfile> federal;
        std::optional<JurisdictionProfile> state;
        std::optional<JurisdictionProfile> agency;
        JurisdictionRules rules;
        bool fullyActive{false};
        std::vector<JurisdictionSource> sources;
    };

    std::optional<EffectiveRuleStack> SelectedForOperation(std::string_view operationKey) const;

private:
    SqliteDatabase& db_;
};

}
