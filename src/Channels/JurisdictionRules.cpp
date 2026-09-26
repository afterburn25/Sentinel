#include "Sentinel/Channels/JurisdictionRules.hpp"
#include "Sentinel/Core/Types.hpp"
#include <sqlite3.h>
#include <stdexcept>

namespace sentinel::channels {
namespace {
void Bind(sqlite3_stmt* s,int i,const std::string& v){sqlite3_bind_text(s,i,v.c_str(),-1,SQLITE_TRANSIENT);}
std::string Col(sqlite3_stmt* s,int i){const auto* p=(const char*)sqlite3_column_text(s,i);return p?p:"";}

JurisdictionRules ParseRules(std::string_view json){
    JurisdictionRules r;
    auto has=[&](std::string_view key){
        return json.find("\""+std::string(key)+"\":true")!=std::string_view::npos;
    };
    r.allowAutomatedOrdinaryReplies=has("allowAutomatedOrdinaryReplies");
    r.allowAutomatedBenignMedia=has("allowAutomatedBenignMedia");
    r.requireHumanContactDisclosure=!has("autoContactDisclosure");
    r.requireHumanIdentityLink=!has("autoIdentityLink");
    r.requireHumanChannelMigration=!has("autoChannelMigration");
    r.requireHumanMeetingArrangement=!has("autoMeetingArrangement");
    r.requireHumanMoneyOrPayment=!has("autoMoneyOrPayment");
    r.blockSexualizedMinorMedia=true;
    if(json.find("\"recordingConsentMode\":\"one-party-or-color-of-law-party\"")!=std::string_view::npos)
        r.recordingConsentMode="one-party-or-color-of-law-party";
    return r;
}

std::optional<JurisdictionProfile> ReadOne(
    sqlite3* db,const char* sql,RuleLayerType layer,
    const std::string& country,const std::string& region,const std::string& agencyId)
{
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK)return std::nullopt;
    sqlite3_bind_int(s,1,(int)layer);
    Bind(s,2,country);Bind(s,3,region);Bind(s,4,agencyId);
    if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;}

    JurisdictionProfile p;
    p.id=Col(s,0);
    p.layer=(RuleLayerType)sqlite3_column_int(s,1);
    p.countryCode=Col(s,2);
    p.regionCode=Col(s,3);
    p.agencyId=Col(s,4);
    p.name=Col(s,5);
    p.version=Col(s,6);
    p.effectiveFrom=Col(s,7);
    p.effectiveUntil=Col(s,8);
    p.reviewStatus=(LegalReviewStatus)sqlite3_column_int(s,9);
    p.reviewedBy=Col(s,10);
    p.rules=ParseRules(Col(s,11));
    sqlite3_finalize(s);

    if(sqlite3_prepare_v2(db,
        "SELECT authority_type,citation,source_url,note FROM jurisdiction_rule_sources "
        "WHERE profile_id=? ORDER BY checked_utc",-1,&s,nullptr)==SQLITE_OK){
        Bind(s,1,p.id);
        while(sqlite3_step(s)==SQLITE_ROW)
            p.sources.push_back({Col(s,0),Col(s,1),Col(s,2),Col(s,3)});
    }
    sqlite3_finalize(s);
    return p;
}

void InsertSource(sqlite3* db,const char* id,const char* profileId,const char* type,
                  const char* citation,const char* url,const char* note)
{
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO jurisdiction_rule_sources("
        "id,profile_id,authority_type,citation,source_url,note) VALUES(?,?,?,?,?,?)",
        -1,&s,nullptr)!=SQLITE_OK)return;
    Bind(s,1,id);Bind(s,2,profileId);Bind(s,3,type);Bind(s,4,citation);Bind(s,5,url);Bind(s,6,note);
    sqlite3_step(s);sqlite3_finalize(s);
}

JurisdictionRules MergeRestrictive(
    const JurisdictionRules& federal,
    const JurisdictionRules& state,
    const std::optional<JurisdictionRules>& agency)
{
    JurisdictionRules r;
    const auto agencyAllowsOrdinary=agency ? agency->allowAutomatedOrdinaryReplies : true;
    const auto agencyAllowsMedia=agency ? agency->allowAutomatedBenignMedia : true;

    r.allowAutomatedOrdinaryReplies=
        federal.allowAutomatedOrdinaryReplies &&
        state.allowAutomatedOrdinaryReplies &&
        agencyAllowsOrdinary;

    r.allowAutomatedBenignMedia=
        federal.allowAutomatedBenignMedia &&
        state.allowAutomatedBenignMedia &&
        agencyAllowsMedia;

    r.requireHumanContactDisclosure=
        federal.requireHumanContactDisclosure ||
        state.requireHumanContactDisclosure ||
        (agency && agency->requireHumanContactDisclosure);

    r.requireHumanIdentityLink=
        federal.requireHumanIdentityLink ||
        state.requireHumanIdentityLink ||
        (agency && agency->requireHumanIdentityLink);

    r.requireHumanChannelMigration=
        federal.requireHumanChannelMigration ||
        state.requireHumanChannelMigration ||
        (agency && agency->requireHumanChannelMigration);

    r.requireHumanMeetingArrangement=
        federal.requireHumanMeetingArrangement ||
        state.requireHumanMeetingArrangement ||
        (agency && agency->requireHumanMeetingArrangement);

    r.requireHumanMoneyOrPayment=
        federal.requireHumanMoneyOrPayment ||
        state.requireHumanMoneyOrPayment ||
        (agency && agency->requireHumanMoneyOrPayment);

    r.blockSexualizedMinorMedia=true;

    // The state layer controls the more specific recording-consent descriptor,
    // while federal authority remains separately cited in the rule stack.
    r.recordingConsentMode=
        state.recordingConsentMode!="unknown" ? state.recordingConsentMode : federal.recordingConsentMode;
    return r;
}
}

void JurisdictionRuleStore::EnsureBuiltInBaselines(){
    sqlite3_stmt* s{};
    const char* insert=
        "INSERT OR IGNORE INTO jurisdiction_rule_profiles("
        "id,layer_type,country_code,region_code,agency_id,name,version,effective_from,review_status,rules_json) "
        "VALUES(?,?,?,?,?,?,?,?,0,?)";

    // Federal reference layer. It intentionally remains Draft until the deploying
    // agency's legal authority reviews/activates the exact operational profile.
    if(sqlite3_prepare_v2(db_.Handle(),insert,-1,&s,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare federal jurisdiction baseline failed");
    Bind(s,1,"jur-us-federal-2026-reference");sqlite3_bind_int(s,2,(int)RuleLayerType::Federal);
    Bind(s,3,"US");Bind(s,4,"");Bind(s,5,"");Bind(s,6,"US federal undercover reference baseline");
    Bind(s,7,"2026-reference");Bind(s,8,"2026-09-26");
    Bind(s,9,
        "{\"allowAutomatedOrdinaryReplies\":false,"
        "\"allowAutomatedBenignMedia\":false,"
        "\"recordingConsentMode\":\"one-party-or-color-of-law-party\"}");
    if(sqlite3_step(s)!=SQLITE_DONE){sqlite3_finalize(s);throw std::runtime_error("insert federal baseline failed");}
    sqlite3_finalize(s);

    InsertSource(db_.Handle(),"jur-src-us-182511","jur-us-federal-2026-reference","statute",
        "18 U.S.C. § 2511",
        "https://uscode.house.gov/view.xhtml?req=(title:18%20section:2511)",
        "Federal interception statute includes a party-consent exception for a person acting under color of law.");
    InsertSource(db_.Handle(),"jur-src-us-doj-undercover","jur-us-federal-2026-reference","guideline",
        "Attorney General Guidelines on FBI Undercover Operations",
        "https://www.justice.gov/archives/ag/undercover-and-sensitive-operations-unit-attorney-generals-guidelines-fbi-undercover-operations",
        "Reference for authorization, risk review, and monitoring of undercover operations; applicability depends on agency and operation.");

    // Louisiana state reference layer.
    if(sqlite3_prepare_v2(db_.Handle(),insert,-1,&s,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare Louisiana jurisdiction baseline failed");
    Bind(s,1,"jur-us-la-2026-reference");sqlite3_bind_int(s,2,(int)RuleLayerType::State);
    Bind(s,3,"US");Bind(s,4,"LA");Bind(s,5,"");Bind(s,6,"Louisiana reference baseline");
    Bind(s,7,"2026-reference");Bind(s,8,"2026-09-26");
    Bind(s,9,
        "{\"allowAutomatedOrdinaryReplies\":false,"
        "\"allowAutomatedBenignMedia\":false,"
        "\"recordingConsentMode\":\"one-party-or-color-of-law-party\"}");
    if(sqlite3_step(s)!=SQLITE_DONE){sqlite3_finalize(s);throw std::runtime_error("insert Louisiana baseline failed");}
    sqlite3_finalize(s);

    InsertSource(db_.Handle(),"jur-src-la-14813","jur-us-la-2026-reference","statute",
        "La. R.S. 14:81.3",
        "https://www.legis.la.gov/legis/Law.aspx?d=320902",
        "Computer-aided solicitation statute includes communications with a person reasonably believed to be under seventeen.");
    InsertSource(db_.Handle(),"jur-src-la-151303","jur-us-la-2026-reference","statute",
        "La. R.S. 15:1303",
        "https://www.legis.la.gov/Legis/Law.aspx?d=78938",
        "Electronic Surveillance Act includes a party-consent exception for a person acting under color of law.");
    InsertSource(db_.Handle(),"jur-src-la-147310","jur-us-la-2026-reference","statute",
        "La. R.S. 14:73.10",
        "https://www.legis.la.gov/legis/Law.aspx?d=814026",
        "Online impersonation statute; deploying agency counsel should determine applicability to the operation's facts.");
}

std::optional<JurisdictionProfile> JurisdictionRuleStore::ActiveProfile(
    RuleLayerType layer,std::string_view country,std::string_view region,std::string_view agencyId) const
{
    return ReadOne(db_.Handle(),
        "SELECT id,layer_type,country_code,region_code,agency_id,name,version,effective_from,"
        "COALESCE(effective_until,''),review_status,COALESCE(reviewed_by,''),rules_json "
        "FROM jurisdiction_rule_profiles "
        "WHERE layer_type=? AND country_code=? AND region_code=? AND agency_id=? AND review_status=2 "
        "AND (effective_until IS NULL OR effective_until='' OR effective_until>=date('now')) "
        "ORDER BY effective_from DESC LIMIT 1",
        layer,std::string(country),std::string(region),std::string(agencyId));
}

std::optional<JurisdictionProfile> JurisdictionRuleStore::LatestProfile(
    RuleLayerType layer,std::string_view country,std::string_view region,std::string_view agencyId) const
{
    return ReadOne(db_.Handle(),
        "SELECT id,layer_type,country_code,region_code,agency_id,name,version,effective_from,"
        "COALESCE(effective_until,''),review_status,COALESCE(reviewed_by,''),rules_json "
        "FROM jurisdiction_rule_profiles "
        "WHERE layer_type=? AND country_code=? AND region_code=? AND agency_id=? "
        "ORDER BY CASE review_status WHEN 2 THEN 4 WHEN 1 THEN 3 WHEN 0 THEN 2 ELSE 1 END DESC,"
        "effective_from DESC LIMIT 1",
        layer,std::string(country),std::string(region),std::string(agencyId));
}

void JurisdictionRuleStore::SelectForOperation(
    std::string operationKey,std::string country,std::string region,
    std::string federalProfileId,std::string stateProfileId,std::string agencyProfileId,
    std::string selectedBy)
{
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO operation_jurisdiction("
        "operation_key,country_code,region_code,federal_profile_id,state_profile_id,agency_profile_id,selected_by) "
        "VALUES(?,?,?,?,?,?,?) "
        "ON CONFLICT(operation_key) DO UPDATE SET country_code=excluded.country_code,"
        "region_code=excluded.region_code,federal_profile_id=excluded.federal_profile_id,"
        "state_profile_id=excluded.state_profile_id,agency_profile_id=excluded.agency_profile_id,"
        "selected_by=excluded.selected_by,selected_utc=CURRENT_TIMESTAMP";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare operation jurisdiction failed");
    Bind(s,1,operationKey);Bind(s,2,country);Bind(s,3,region);Bind(s,4,federalProfileId);
    Bind(s,5,stateProfileId);Bind(s,6,agencyProfileId);Bind(s,7,selectedBy);
    if(sqlite3_step(s)!=SQLITE_DONE){sqlite3_finalize(s);throw std::runtime_error("save operation jurisdiction failed");}
    sqlite3_finalize(s);
}

std::optional<JurisdictionRuleStore::EffectiveRuleStack>
JurisdictionRuleStore::SelectedForOperation(std::string_view operationKey) const {
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db_.Handle(),
        "SELECT country_code,region_code,COALESCE(federal_profile_id,''),"
        "COALESCE(state_profile_id,''),COALESCE(agency_profile_id,'') "
        "FROM operation_jurisdiction WHERE operation_key=?",
        -1,&s,nullptr)!=SQLITE_OK)return std::nullopt;
    Bind(s,1,std::string(operationKey));
    if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;}
    const auto country=Col(s,0),region=Col(s,1);
    sqlite3_finalize(s);

    EffectiveRuleStack stack;
    stack.federal=LatestProfile(RuleLayerType::Federal,country);
    stack.state=LatestProfile(RuleLayerType::State,country,region);
    // An agency layer is optional. If deployed, it is selected by agency ID in a later
    // agency-policy administration workflow.
    stack.agency=std::nullopt;

    if(!stack.federal || !stack.state){
        stack.fullyActive=false;
        return stack;
    }

    stack.rules=MergeRestrictive(
        stack.federal->rules,stack.state->rules,
        stack.agency ? std::optional<JurisdictionRules>(stack.agency->rules) : std::nullopt);

    stack.fullyActive=
        stack.federal->AutomationLegallyActive() &&
        stack.state->AutomationLegallyActive() &&
        (!stack.agency || stack.agency->AutomationLegallyActive());

    stack.sources.insert(stack.sources.end(),stack.federal->sources.begin(),stack.federal->sources.end());
    stack.sources.insert(stack.sources.end(),stack.state->sources.begin(),stack.state->sources.end());
    if(stack.agency)stack.sources.insert(stack.sources.end(),stack.agency->sources.begin(),stack.agency->sources.end());
    return stack;
}

}
