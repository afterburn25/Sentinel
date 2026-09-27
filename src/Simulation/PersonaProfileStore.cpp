#include "Sentinel/Simulation/PersonaProfileStore.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace sentinel::simulation {
namespace {

void Bind(sqlite3_stmt* stmt,int index,const std::string& value) {
    sqlite3_bind_text(stmt,index,value.c_str(),-1,SQLITE_TRANSIENT);
}

std::string Col(sqlite3_stmt* stmt,int index) {
    const auto* p=reinterpret_cast<const char*>(sqlite3_column_text(stmt,index));
    return p?p:"";
}

std::string JoinFacts(const std::vector<std::string>& facts) {
    std::string out;
    for(size_t i=0;i<facts.size();++i) {
        if(i) out+='\n';
        out+=facts[i];
    }
    return out;
}

std::vector<std::string> SplitFacts(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while(std::getline(in,line)) {
        if(!line.empty()) out.push_back(line);
    }
    return out;
}

StoredPersonaProfile ReadProfile(sqlite3_stmt* stmt) {
    StoredPersonaProfile stored;
    auto& p=stored.profile;
    int i=0;
    p.name=Col(stmt,i++);
    p.age=sqlite3_column_int(stmt,i++);
    p.location=Col(stmt,i++);
    p.gender=Col(stmt,i++);
    p.pronouns=Col(stmt,i++);
    p.occupation=Col(stmt,i++);
    p.education=Col(stmt,i++);
    p.relationshipStatus=Col(stmt,i++);
    p.familyContext=Col(stmt,i++);
    p.personality=Col(stmt,i++);
    p.socialStyle=Col(stmt,i++);
    p.confidenceLevel=Col(stmt,i++);
    p.background=Col(stmt,i++);
    p.interests=Col(stmt,i++);
    p.writingStyle=Col(stmt,i++);
    p.intelligenceLevel=Col(stmt,i++);
    p.slangLevel=Col(stmt,i++);
    p.grammarQuality=Col(stmt,i++);
    p.typoTendency=Col(stmt,i++);
    p.emojiTendency=Col(stmt,i++);
    p.mood=Col(stmt,i++);
    p.lockedFacts=SplitFacts(Col(stmt,i++));
    stored.minDelayMs=sqlite3_column_int(stmt,i++);
    stored.maxDelayMs=sqlite3_column_int(stmt,i++);
    stored.updatedUtc=Col(stmt,i++);
    return stored;
}

constexpr const char* kSelectColumns=
    "name,age,location,gender,pronouns,occupation,education,relationship_status,family_context,"
    "personality,social_style,confidence_level,background,interests,writing_style,intelligence_level,"
    "slang_level,grammar_quality,typo_tendency,emoji_tendency,mood,locked_facts,min_delay_ms,max_delay_ms,updated_utc";

}

void PersonaProfileStore::Save(const PersonaProfile& p,int minDelayMs,int maxDelayMs) {
    if(p.name.empty()) throw std::runtime_error("persona name is required");
    minDelayMs=std::clamp(minDelayMs,500,30000);
    maxDelayMs=std::clamp(maxDelayMs,minDelayMs,60000);

    sqlite3_stmt* stmt{};
    const char* sql=
        "INSERT INTO persona_profiles("
        "name,age,location,gender,pronouns,occupation,education,relationship_status,family_context,"
        "personality,social_style,confidence_level,background,interests,writing_style,intelligence_level,"
        "slang_level,grammar_quality,typo_tendency,emoji_tendency,mood,locked_facts,min_delay_ms,max_delay_ms) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(name) DO UPDATE SET "
        "age=excluded.age,location=excluded.location,gender=excluded.gender,pronouns=excluded.pronouns,"
        "occupation=excluded.occupation,education=excluded.education,relationship_status=excluded.relationship_status,"
        "family_context=excluded.family_context,personality=excluded.personality,social_style=excluded.social_style,"
        "confidence_level=excluded.confidence_level,background=excluded.background,interests=excluded.interests,"
        "writing_style=excluded.writing_style,intelligence_level=excluded.intelligence_level,"
        "slang_level=excluded.slang_level,grammar_quality=excluded.grammar_quality,"
        "typo_tendency=excluded.typo_tendency,emoji_tendency=excluded.emoji_tendency,mood=excluded.mood,"
        "locked_facts=excluded.locked_facts,min_delay_ms=excluded.min_delay_ms,max_delay_ms=excluded.max_delay_ms,"
        "updated_utc=CURRENT_TIMESTAMP";

    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare persona profile save failed");

    int i=1;
    Bind(stmt,i++,p.name);
    sqlite3_bind_int(stmt,i++,p.age);
    Bind(stmt,i++,p.location);
    Bind(stmt,i++,p.gender);
    Bind(stmt,i++,p.pronouns);
    Bind(stmt,i++,p.occupation);
    Bind(stmt,i++,p.education);
    Bind(stmt,i++,p.relationshipStatus);
    Bind(stmt,i++,p.familyContext);
    Bind(stmt,i++,p.personality);
    Bind(stmt,i++,p.socialStyle);
    Bind(stmt,i++,p.confidenceLevel);
    Bind(stmt,i++,p.background);
    Bind(stmt,i++,p.interests);
    Bind(stmt,i++,p.writingStyle);
    Bind(stmt,i++,p.intelligenceLevel);
    Bind(stmt,i++,p.slangLevel);
    Bind(stmt,i++,p.grammarQuality);
    Bind(stmt,i++,p.typoTendency);
    Bind(stmt,i++,p.emojiTendency);
    Bind(stmt,i++,p.mood);
    Bind(stmt,i++,JoinFacts(p.lockedFacts));
    sqlite3_bind_int(stmt,i++,minDelayMs);
    sqlite3_bind_int(stmt,i++,maxDelayMs);

    if(sqlite3_step(stmt)!=SQLITE_DONE) {
        const std::string error=sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(stmt);
        throw std::runtime_error("persona profile save failed: "+error);
    }
    sqlite3_finalize(stmt);
}

std::optional<StoredPersonaProfile> PersonaProfileStore::Load(std::string_view name) const {
    sqlite3_stmt* stmt{};
    const std::string sql=std::string("SELECT ")+kSelectColumns+" FROM persona_profiles WHERE name=?";
    if(sqlite3_prepare_v2(db_.Handle(),sql.c_str(),-1,&stmt,nullptr)!=SQLITE_OK) return std::nullopt;
    Bind(stmt,1,std::string(name));
    if(sqlite3_step(stmt)!=SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    auto result=ReadProfile(stmt);
    sqlite3_finalize(stmt);
    return result;
}

std::vector<StoredPersonaProfile> PersonaProfileStore::List() const {
    std::vector<StoredPersonaProfile> out;
    sqlite3_stmt* stmt{};
    const std::string sql=std::string("SELECT ")+kSelectColumns+
        " FROM persona_profiles ORDER BY updated_utc DESC,name COLLATE NOCASE";
    if(sqlite3_prepare_v2(db_.Handle(),sql.c_str(),-1,&stmt,nullptr)!=SQLITE_OK) return out;
    while(sqlite3_step(stmt)==SQLITE_ROW) out.push_back(ReadProfile(stmt));
    sqlite3_finalize(stmt);
    return out;
}

bool PersonaProfileStore::Delete(std::string_view name) {
    sqlite3_stmt* stmt{};
    if(sqlite3_prepare_v2(db_.Handle(),"DELETE FROM persona_profiles WHERE name=?",-1,&stmt,nullptr)!=SQLITE_OK)
        return false;
    Bind(stmt,1,std::string(name));
    const bool ok=sqlite3_step(stmt)==SQLITE_DONE && sqlite3_changes(db_.Handle())>0;
    sqlite3_finalize(stmt);
    return ok;
}

size_t PersonaProfileStore::Count() const {
    sqlite3_stmt* stmt{};
    size_t count=0;
    if(sqlite3_prepare_v2(db_.Handle(),"SELECT COUNT(*) FROM persona_profiles",-1,&stmt,nullptr)==SQLITE_OK &&
       sqlite3_step(stmt)==SQLITE_ROW) {
        count=(size_t)sqlite3_column_int64(stmt,0);
    }
    sqlite3_finalize(stmt);
    return count;
}

}
