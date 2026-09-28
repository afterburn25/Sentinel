#include "Sentinel/Simulation/ResponseRuleMatchLog.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <stdexcept>

namespace sentinel::simulation {
namespace {

void Check(int rc,sqlite3* db,const char* what) {
    if(rc==SQLITE_OK || rc==SQLITE_ROW || rc==SQLITE_DONE) return;
    throw std::runtime_error(std::string(what)+": "+(db?sqlite3_errmsg(db):"sqlite error"));
}

std::string Col(sqlite3_stmt* stmt,int column) {
    const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(stmt,column));
    return value?value:"";
}

void Bind(sqlite3_stmt* stmt,int column,std::string_view value) {
    const std::string copy(value);
    sqlite3_bind_text(stmt,column,copy.c_str(),-1,SQLITE_TRANSIENT);
}

ResponseRuleMatchRecord Read(sqlite3_stmt* stmt) {
    ResponseRuleMatchRecord record;
    record.id=sqlite3_column_int64(stmt,0);
    record.personaName=Col(stmt,1);
    record.conversationId=Col(stmt,2);
    record.ruleId=sqlite3_column_int64(stmt,3);
    record.matchType=Col(stmt,4);
    record.matchScore=sqlite3_column_int(stmt,5);
    record.triggerText=Col(stmt,6);
    record.inputText=Col(stmt,7);
    record.responseMode=Col(stmt,8);
    record.outputText=Col(stmt,9);
    record.createdUtc=Col(stmt,10);
    return record;
}

}

long long ResponseRuleMatchLog::Append(
    std::string_view personaName,
    std::string_view conversationId,
    long long ruleId,
    std::string_view matchType,
    int matchScore,
    std::string_view triggerText,
    std::string_view inputText,
    std::string_view responseMode,
    std::string_view outputText)
{
    if(personaName.empty() || ruleId<=0)
        throw std::runtime_error("response rule match requires persona and rule id");

    auto* db=db_.Handle();
    sqlite3_stmt* stmt{};
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO persona_response_rule_matches("
        "persona_name,conversation_id,rule_id,match_type,match_score,"
        "trigger_text,input_text,response_mode,output_text"
        ") VALUES(?,?,?,?,?,?,?,?,?)",
        -1,&stmt,nullptr),db,"prepare response rule match insert");
    Bind(stmt,1,personaName);
    Bind(stmt,2,conversationId);
    sqlite3_bind_int64(stmt,3,ruleId);
    Bind(stmt,4,matchType);
    sqlite3_bind_int(stmt,5,std::clamp(matchScore,0,100));
    Bind(stmt,6,triggerText);
    Bind(stmt,7,inputText);
    Bind(stmt,8,responseMode);
    Bind(stmt,9,outputText);
    Check(sqlite3_step(stmt),db,"insert response rule match");
    const auto id=sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return id;
}

std::vector<ResponseRuleMatchRecord> ResponseRuleMatchLog::Recent(
    std::string_view personaName,
    size_t limit) const
{
    std::vector<ResponseRuleMatchRecord> out;
    if(personaName.empty()) return out;

    auto* db=db_.Handle();
    sqlite3_stmt* stmt{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,persona_name,conversation_id,rule_id,match_type,match_score,"
        "trigger_text,input_text,response_mode,output_text,created_utc "
        "FROM persona_response_rule_matches "
        "WHERE persona_name=? ORDER BY id DESC LIMIT ?",
        -1,&stmt,nullptr),db,"prepare response rule match list");
    Bind(stmt,1,personaName);
    sqlite3_bind_int(stmt,2,(int)std::min<size_t>(limit,200));
    while(sqlite3_step(stmt)==SQLITE_ROW)
        out.push_back(Read(stmt));
    sqlite3_finalize(stmt);
    return out;
}

long long ResponseRuleMatchLog::CountForRule(
    std::string_view personaName,
    long long ruleId) const
{
    if(personaName.empty() || ruleId<=0) return 0;
    auto* db=db_.Handle();
    sqlite3_stmt* stmt{};
    Check(sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM persona_response_rule_matches "
        "WHERE persona_name=? AND rule_id=?",
        -1,&stmt,nullptr),db,"prepare response rule match count");
    Bind(stmt,1,personaName);
    sqlite3_bind_int64(stmt,2,ruleId);
    long long count=0;
    if(sqlite3_step(stmt)==SQLITE_ROW) count=sqlite3_column_int64(stmt,0);
    sqlite3_finalize(stmt);
    return count;
}

}
