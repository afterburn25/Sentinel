#pragma once
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <vector>

namespace sentinel::channels {

enum class LegalReviewStatus { Draft=0, LegallyReviewed=1, Active=2, Expired=3 };

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
    std::string countryCode{"US"};
    std::string regionCode;
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
    std::optional<JurisdictionProfile> ActiveProfile(std::string_view country,std::string_view region) const;
    std::optional<JurisdictionProfile> LatestProfile(std::string_view country,std::string_view region) const;
    void SelectForOperation(
        std::string operationKey,
        std::string country,
        std::string region,
        std::string profileId,
        std::string selectedBy);
    std::optional<JurisdictionProfile> SelectedForOperation(std::string_view operationKey) const;

private:
    SqliteDatabase& db_;
};

}
