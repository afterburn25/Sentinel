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
    auto has=[&](std::string_view key){return json.find("\""+std::string(key)+"\":true")!=std::string_view::npos;};
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

std::optional<JurisdictionProfile> ReadOne(sqlite3* db,const char* sql,const std::string& country,const std::string& region){
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK)return std::nullopt;
    Bind(s,1,country);Bind(s,2,region);
    if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;}
    JurisdictionProfile p;
    p.id=Col(s,0);p.countryCode=Col(s,1);p.regionCode=Col(s,2);p.name=Col(s,3);p.version=Col(s,4);
    p.effectiveFrom=Col(s,5);p.effectiveUntil=Col(s,6);p.reviewStatus=(LegalReviewStatus)sqlite3_column_int(s,7);
    p.reviewedBy=Col(s,8);p.rules=ParseRules(Col(s,9));
    sqlite3_finalize(s);

    if(sqlite3_prepare_v2(db,
        "SELECT authority_type,citation,source_url,note FROM jurisdiction_rule_sources WHERE profile_id=? ORDER BY checked_utc",
        -1,&s,nullptr)==SQLITE_OK){
        Bind(s,1,p.id);
        while(sqlite3_step(s)==SQLITE_ROW)p.sources.push_back({Col(s,0),Col(s,1),Col(s,2),Col(s,3)});
    }
    sqlite3_finalize(s);
    return p;
}
}

void JurisdictionRuleStore::EnsureBuiltInBaselines(){
    // Built-ins are conservative reference baselines, not self-authorizing legal opinions.
    // They remain Draft until an agency/legal reviewer explicitly activates a version.
    const std::string id="jur-us-la-2026-reference";
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT OR IGNORE INTO jurisdiction_rule_profiles("
        "id,country_code,region_code,name,version,effective_from,review_status,rules_json) "
        "VALUES(?,?,?,?,?,?,0,?)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("prepare jurisdiction baseline failed");
    Bind(s,1,id);Bind(s,2,"US");Bind(s,3,"LA");Bind(s,4,"Louisiana reference baseline");
    Bind(s,5,"2026-reference");Bind(s,6,"2026-09-26");
    Bind(s,7,
        "{\"allowAutomatedOrdinaryReplies\":false,"
        "\"allowAutomatedBenignMedia\":false,"
        "\"recordingConsentMode\":\"one-party-or-color-of-law-party\"}");
    if(sqlite3_step(s)!=SQLITE_DONE){sqlite3_finalize(s);throw std::runtime_error("insert jurisdiction baseline failed");}
    sqlite3_finalize(s);

    struct Src{const char* id;const char* type;const char* citation;const char* url;const char* note;};
    const Src srcs[]={
        {"jur-src-la-14813","statute","La. R.S. 14:81.3","https://www.legis.la.gov/legis/Law.aspx?d=320902",
         "Computer-aided solicitation statute includes communications with a person reasonably believed to be under seventeen."},
        {"jur-src-la-151303","statute","La. R.S. 15:1303","https://www.legis.la.gov/Legis/Law.aspx?d=78938",
         "Electronic Surveillance Act includes an exception for a person acting under color of law who is a party to the communication or has prior consent from a party."},
        {"jur-src-la-147310","statute","La. R.S. 14:73.10","https://www.legis.la.gov/legis/Law.aspx?d=814026",
         "Online impersonation statute; agency counsel should determine applicability to authorized synthetic identities and operational facts."}
    };
    for(const auto& x:srcs){
        if(sqlite3_prepare_v2(db_.Handle(),
            "INSERT OR IGNORE INTO jurisdiction_rule_sources(id,profile_id,authority_type,citation,source_url,note) VALUES(?,?,?,?,?,?)",
            -1,&s,nullptr)!=SQLITE_OK)continue;
        Bind(s,1,x.id);Bind(s,2,id);Bind(s,3,x.type);Bind(s,4,x.citation);Bind(s,5,x.url);Bind(s,6,x.note);
        sqlite3_step(s);sqlite3_finalize(s);
    }
}

std::optional<JurisdictionProfile> JurisdictionRuleStore::ActiveProfile(std::string_view country,std::string_view region) const {
    return ReadOne(db_.Handle(),
        "SELECT id,country_code,region_code,name,version,effective_from,COALESCE(effective_until,''),review_status,"
        "COALESCE(reviewed_by,''),rules_json FROM jurisdiction_rule_profiles "
        "WHERE country_code=? AND region_code=? AND review_status=2 "
        "AND (effective_until IS NULL OR effective_until='' OR effective_until>=date('now')) "
        "ORDER BY effective_from DESC LIMIT 1",std::string(country),std::string(region));
}
std::optional<JurisdictionProfile> JurisdictionRuleStore::LatestProfile(std::string_view country,std::string_view region) const {
    return ReadOne(db_.Handle(),
        "SELECT id,country_code,region_code,name,version,effective_from,COALESCE(effective_until,''),review_status,"
        "COALESCE(reviewed_by,''),rules_json FROM jurisdiction_rule_profiles "
        "WHERE country_code=? AND region_code=? ORDER BY review_status DESC,effective_from DESC LIMIT 1",
        std::string(country),std::string(region));
}

void JurisdictionRuleStore::SelectForOperation(
    std::string operationKey,std::string country,std::string region,std::string profileId,std::string selectedBy)
{
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO operation_jurisdiction(operation_key,country_code,region_code,profile_id,selected_by) VALUES(?,?,?,?,?) "
        "ON CONFLICT(operation_key) DO UPDATE SET country_code=excluded.country_code,region_code=excluded.region_code,"
        "profile_id=excluded.profile_id,selected_by=excluded.selected_by,selected_utc=CURRENT_TIMESTAMP";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("prepare operation jurisdiction failed");
    Bind(s,1,operationKey);Bind(s,2,country);Bind(s,3,region);Bind(s,4,profileId);Bind(s,5,selectedBy);
    if(sqlite3_step(s)!=SQLITE_DONE){sqlite3_finalize(s);throw std::runtime_error("save operation jurisdiction failed");}
    sqlite3_finalize(s);
}

std::optional<JurisdictionProfile> JurisdictionRuleStore::SelectedForOperation(std::string_view operationKey) const {
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db_.Handle(),
        "SELECT country_code,region_code,COALESCE(profile_id,'') FROM operation_jurisdiction WHERE operation_key=?",
        -1,&s,nullptr)!=SQLITE_OK)return std::nullopt;
    Bind(s,1,std::string(operationKey));
    if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;}
    const auto country=Col(s,0),region=Col(s,1),profileId=Col(s,2);
    sqlite3_finalize(s);
    if(profileId.empty())return LatestProfile(country,region);

    sqlite3_stmt* q{};
    if(sqlite3_prepare_v2(db_.Handle(),
        "SELECT country_code,region_code FROM jurisdiction_rule_profiles WHERE id=?",-1,&q,nullptr)!=SQLITE_OK)return std::nullopt;
    Bind(q,1,profileId);
    if(sqlite3_step(q)!=SQLITE_ROW){sqlite3_finalize(q);return std::nullopt;}
    const auto pc=Col(q,0),pr=Col(q,1);sqlite3_finalize(q);
    return LatestProfile(pc,pr);
}
}
