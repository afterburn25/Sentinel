#include "Sentinel/Simulation/PersonaProfileStore.hpp"
#include <sqlite3.h>
#include <stdexcept>

namespace sentinel::simulation {
namespace {
void Bind(sqlite3_stmt* s,int i,const std::string& v){sqlite3_bind_text(s,i,v.c_str(),-1,SQLITE_TRANSIENT);}
std::string Col(sqlite3_stmt* s,int i){const auto* p=(const char*)sqlite3_column_text(s,i);return p?p:"";}
}

void PersonaProfileStore::Save(const PersonaProfile& p) {
    if(p.name.empty()) throw std::runtime_error("persona name is required");
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO persona_profiles("
        "name,age,location,gender,pronouns,occupation,education,relationship_status,family_context,"
        "personality,social_style,confidence_level,background,interests,writing_style,communication_level,cognitive_level,"
        "slang_level,grammar_quality,typo_frequency,emoji_level,vocabulary_level,capitalization_style,message_length,"
        "response_start_min_ms,response_start_max_ms,typing_ms_per_char_min,typing_ms_per_char_max) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(name) DO UPDATE SET "
        "age=excluded.age,location=excluded.location,gender=excluded.gender,pronouns=excluded.pronouns,"
        "occupation=excluded.occupation,education=excluded.education,relationship_status=excluded.relationship_status,"
        "family_context=excluded.family_context,personality=excluded.personality,social_style=excluded.social_style,"
        "confidence_level=excluded.confidence_level,background=excluded.background,interests=excluded.interests,"
        "writing_style=excluded.writing_style,communication_level=excluded.communication_level,cognitive_level=excluded.cognitive_level,"
        "slang_level=excluded.slang_level,grammar_quality=excluded.grammar_quality,"
        "typo_frequency=excluded.typo_frequency,emoji_level=excluded.emoji_level,"
        "vocabulary_level=excluded.vocabulary_level,capitalization_style=excluded.capitalization_style,"
        "message_length=excluded.message_length,response_start_min_ms=excluded.response_start_min_ms,"
        "response_start_max_ms=excluded.response_start_max_ms,typing_ms_per_char_min=excluded.typing_ms_per_char_min,"
        "typing_ms_per_char_max=excluded.typing_ms_per_char_max,updated_utc=CURRENT_TIMESTAMP";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare persona profile save failed");
    int i=1;
    Bind(s,i++,p.name); sqlite3_bind_int(s,i++,p.age); Bind(s,i++,p.location); Bind(s,i++,p.gender);
    Bind(s,i++,p.pronouns); Bind(s,i++,p.occupation); Bind(s,i++,p.education); Bind(s,i++,p.relationshipStatus);
    Bind(s,i++,p.familyContext); Bind(s,i++,p.personality); Bind(s,i++,p.socialStyle); Bind(s,i++,p.confidenceLevel);
    Bind(s,i++,p.background); Bind(s,i++,p.interests); Bind(s,i++,p.writingStyle); Bind(s,i++,p.communicationLevel);
    Bind(s,i++,p.cognitiveLevel); Bind(s,i++,p.slangLevel); Bind(s,i++,p.grammarQuality); Bind(s,i++,p.typoFrequency); Bind(s,i++,p.emojiLevel);
    Bind(s,i++,p.vocabularyLevel); Bind(s,i++,p.capitalizationStyle); Bind(s,i++,p.messageLength);
    sqlite3_bind_int(s,i++,p.responseStartMinMs); sqlite3_bind_int(s,i++,p.responseStartMaxMs);
    sqlite3_bind_int(s,i++,p.typingMsPerCharMin); sqlite3_bind_int(s,i++,p.typingMsPerCharMax);
    if(sqlite3_step(s)!=SQLITE_DONE){
        std::string e=sqlite3_errmsg(db_.Handle()); sqlite3_finalize(s);
        throw std::runtime_error("persona profile save failed: "+e);
    }
    sqlite3_finalize(s);
}

std::optional<PersonaProfile> PersonaProfileStore::Load(std::string_view name) const {
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT name,age,location,gender,pronouns,occupation,education,relationship_status,family_context,"
        "personality,social_style,confidence_level,background,interests,writing_style,communication_level,cognitive_level,"
        "slang_level,grammar_quality,typo_frequency,emoji_level,vocabulary_level,capitalization_style,message_length,"
        "response_start_min_ms,response_start_max_ms,typing_ms_per_char_min,typing_ms_per_char_max "
        "FROM persona_profiles WHERE name=?";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) return std::nullopt;
    Bind(s,1,std::string(name));
    if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;}
    PersonaProfile p;
    int i=0;
    p.name=Col(s,i++);p.age=sqlite3_column_int(s,i++);p.location=Col(s,i++);p.gender=Col(s,i++);
    p.pronouns=Col(s,i++);p.occupation=Col(s,i++);p.education=Col(s,i++);p.relationshipStatus=Col(s,i++);
    p.familyContext=Col(s,i++);p.personality=Col(s,i++);p.socialStyle=Col(s,i++);p.confidenceLevel=Col(s,i++);
    p.background=Col(s,i++);p.interests=Col(s,i++);p.writingStyle=Col(s,i++);p.communicationLevel=Col(s,i++);
    p.cognitiveLevel=Col(s,i++);p.slangLevel=Col(s,i++);p.grammarQuality=Col(s,i++);p.typoFrequency=Col(s,i++);p.emojiLevel=Col(s,i++);
    p.vocabularyLevel=Col(s,i++);p.capitalizationStyle=Col(s,i++);p.messageLength=Col(s,i++);
    p.responseStartMinMs=sqlite3_column_int(s,i++);p.responseStartMaxMs=sqlite3_column_int(s,i++);
    p.typingMsPerCharMin=sqlite3_column_int(s,i++);p.typingMsPerCharMax=sqlite3_column_int(s,i++);
    sqlite3_finalize(s);
    return p;
}

std::vector<std::string> PersonaProfileStore::ListNames() const {
    std::vector<std::string> out;
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db_.Handle(),
        "SELECT name FROM persona_profiles ORDER BY updated_utc DESC,name COLLATE NOCASE",
        -1,&s,nullptr)!=SQLITE_OK) return out;
    while(sqlite3_step(s)==SQLITE_ROW) out.push_back(Col(s,0));
    sqlite3_finalize(s);
    return out;
}

bool PersonaProfileStore::Delete(std::string_view name) {
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db_.Handle(),"DELETE FROM persona_profiles WHERE name=?",-1,&s,nullptr)!=SQLITE_OK)
        return false;
    Bind(s,1,std::string(name));
    const bool ok=sqlite3_step(s)==SQLITE_DONE && sqlite3_changes(db_.Handle())>0;
    sqlite3_finalize(s);
    return ok;
}

}
