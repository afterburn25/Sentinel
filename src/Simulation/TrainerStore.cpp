#include "Sentinel/Simulation/TrainerStore.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>

namespace sentinel::simulation {
namespace {
void Check(int rc,sqlite3* db,const char* what) {
    if(rc==SQLITE_OK || rc==SQLITE_ROW || rc==SQLITE_DONE) return;
    throw std::runtime_error(std::string(what)+": "+sqlite3_errmsg(db));
}
std::string Col(sqlite3_stmt* s,int i) {
    const auto* p=(const char*)sqlite3_column_text(s,i);
    return p?p:"";
}
std::string JsonEscape(std::string_view input) {
    std::ostringstream out;
    for(unsigned char c:input) {
        switch(c) {
            case '\\': out<<"\\\\"; break;
            case '"': out<<"\\\""; break;
            case '\n': out<<"\\n"; break;
            case '\r': out<<"\\r"; break;
            case '\t': out<<"\\t"; break;
            default:
                if(c<0x20) {
                    static const char* hex="0123456789abcdef";
                    out<<"\\u00"<<hex[(c>>4)&0xf]<<hex[c&0xf];
                } else out<<(char)c;
        }
    }
    return out.str();
}
std::string NewId(const char* prefix) {
    const auto now=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    static unsigned long long seq=0;
    std::ostringstream out; out<<prefix<<"-"<<std::hex<<now<<"-"<<++seq; return out.str();
}
ModelFoundation ReadFoundation(sqlite3_stmt* s) {
    ModelFoundation f;
    f.id=Col(s,0); f.name=Col(s,1); f.parentId=Col(s,2); f.sourceModel=Col(s,3);
    f.trainableSourcePath=Col(s,4); f.runtimeGgufPath=Col(s,5); f.version=sqlite3_column_int(s,6);
    f.status=Col(s,7); f.notes=Col(s,8); return f;
}
PersonaLoraBinding ReadBinding(sqlite3_stmt* s) {
    PersonaLoraBinding b;
    b.id=sqlite3_column_int64(s,0); b.personaName=Col(s,1); b.foundationId=Col(s,2);
    b.loraName=Col(s,3); b.loraPath=Col(s,4); b.weight=sqlite3_column_double(s,5);
    b.active=sqlite3_column_int(s,6)!=0; return b;
}

TrainerDialogueSession ReadDialogueSession(sqlite3_stmt* s) {
    TrainerDialogueSession session;
    session.id=Col(s,0);
    session.personaName=Col(s,1);
    session.mode=TrainingModeFromString(Col(s,2));
    session.title=Col(s,3);
    session.active=sqlite3_column_int(s,4)!=0;
    session.createdUtc=Col(s,5);
    session.updatedUtc=Col(s,6);
    return session;
}

TrainerDialogueTurn ReadDialogueTurn(sqlite3_stmt* s) {
    TrainerDialogueTurn turn;
    turn.id=sqlite3_column_int64(s,0);
    turn.sessionId=Col(s,1);
    turn.role=Col(s,2);
    turn.text=Col(s,3);
    turn.payload=Col(s,4);
    turn.applied=sqlite3_column_int(s,5)!=0;
    turn.createdUtc=Col(s,6);
    turn.appliedUtc=Col(s,7);
    return turn;
}
}

std::string ToString(TrainingMode mode) {
    switch(mode) {
        case TrainingMode::Behavior: return "BEHAVIOR";
        case TrainingMode::Correction: return "CORRECTION";
        case TrainingMode::PersonaLora: return "PERSONA_LORA";
        case TrainingMode::FoundationSft: return "FOUNDATION_SFT";
        case TrainingMode::Preference: return "PREFERENCE";
    }
    return "BEHAVIOR";
}
TrainingMode TrainingModeFromString(std::string_view value) {
    if(value=="CORRECTION") return TrainingMode::Correction;
    if(value=="PERSONA_LORA") return TrainingMode::PersonaLora;
    if(value=="FOUNDATION_SFT") return TrainingMode::FoundationSft;
    if(value=="PREFERENCE") return TrainingMode::Preference;
    return TrainingMode::Behavior;
}

void TrainerStore::EnsureDefaultFoundation(std::string_view name,std::string_view sourceModel,std::string_view runtimeGgufPath) {
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM model_foundations",-1,&s,nullptr),db,"prepare foundation count");
    int count=0; if(sqlite3_step(s)==SQLITE_ROW) count=sqlite3_column_int(s,0); sqlite3_finalize(s);
    if(count>0) return;
    const auto id=NewId("foundation");
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO model_foundations(id,name,parent_id,source_model,trainable_source_path,runtime_gguf_path,version,status,notes) "
        "VALUES(?,?, '', ?, '', ?, 1, 'ACTIVE', 'Initial SARA runtime foundation')",
        -1,&s,nullptr),db,"prepare default foundation");
    sqlite3_bind_text(s,1,id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,2,std::string(name).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,3,std::string(sourceModel).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,4,std::string(runtimeGgufPath).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"insert default foundation"); sqlite3_finalize(s);
}

std::vector<ModelFoundation> TrainerStore::ListFoundations() const {
    std::vector<ModelFoundation> out; auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,name,parent_id,source_model,trainable_source_path,runtime_gguf_path,version,status,notes "
        "FROM model_foundations ORDER BY created_utc ASC",-1,&s,nullptr),db,"prepare foundation list");
    while(sqlite3_step(s)==SQLITE_ROW) out.push_back(ReadFoundation(s));
    sqlite3_finalize(s); return out;
}

std::optional<ModelFoundation> TrainerStore::GetFoundation(std::string_view id) const {
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,name,parent_id,source_model,trainable_source_path,runtime_gguf_path,version,status,notes "
        "FROM model_foundations WHERE id=? LIMIT 1",-1,&s,nullptr),db,"prepare foundation get");
    sqlite3_bind_text(s,1,std::string(id).c_str(),-1,SQLITE_TRANSIENT);
    std::optional<ModelFoundation> out; if(sqlite3_step(s)==SQLITE_ROW) out=ReadFoundation(s);
    sqlite3_finalize(s); return out;
}


bool TrainerStore::ApproveFoundation(std::string_view id) {
    if(id.empty()) return false;
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "UPDATE model_foundations SET status='APPROVED',updated_utc=CURRENT_TIMESTAMP "
        "WHERE id=? AND status IN ('DRAFT','TRAINING','CANDIDATE','APPROVED')",
        -1,&s,nullptr),db,"prepare foundation approve");
    sqlite3_bind_text(s,1,std::string(id).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"approve foundation");
    const bool changed=sqlite3_changes(db)>0;
    sqlite3_finalize(s);
    return changed;
}

bool TrainerStore::ActivateFoundation(std::string_view id) {
    if(id.empty()) return false;
    auto target=GetFoundation(id);
    if(!target || (target->status!="APPROVED" && target->status!="ACTIVE")) return false;

    SqliteTransaction tx(db_);
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "UPDATE model_foundations SET status='APPROVED',updated_utc=CURRENT_TIMESTAMP "
        "WHERE status='ACTIVE' AND id<>?",
        -1,&s,nullptr),db,"prepare foundation deactivate");
    sqlite3_bind_text(s,1,std::string(id).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"deactivate active foundation");
    sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,
        "UPDATE model_foundations SET status='ACTIVE',updated_utc=CURRENT_TIMESTAMP WHERE id=?",
        -1,&s,nullptr),db,"prepare foundation activate");
    sqlite3_bind_text(s,1,std::string(id).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"activate foundation");
    const bool changed=sqlite3_changes(db)>0;
    sqlite3_finalize(s);
    tx.Commit();
    return changed;
}


ModelFoundation TrainerStore::CreateFork(std::string_view name,std::string_view parentId,std::string_view sourceModel,std::string_view trainableSourcePath,std::string_view runtimeGgufPath) {
    const auto id=NewId("foundation"); auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO model_foundations(id,name,parent_id,source_model,trainable_source_path,runtime_gguf_path,version,status,notes) "
        "VALUES(?,?,?,?,?,?,1,'DRAFT','Fork created in SARA Trainer')",-1,&s,nullptr),db,"prepare foundation fork");
    const std::string values[]={id,std::string(name),std::string(parentId),std::string(sourceModel),std::string(trainableSourcePath),std::string(runtimeGgufPath)};
    for(int i=0;i<6;i++) sqlite3_bind_text(s,i+1,values[i].c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"insert foundation fork"); sqlite3_finalize(s);
    return *GetFoundation(id);
}

PersonaLoraBinding TrainerStore::BindPersonaLora(std::string_view personaName,std::string_view foundationId,std::string_view loraName,std::string_view loraPath,double weight) {
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,"UPDATE persona_lora_bindings SET active=0,updated_utc=CURRENT_TIMESTAMP WHERE persona_name=?",-1,&s,nullptr),db,"prepare deactivate persona loras");
    sqlite3_bind_text(s,1,std::string(personaName).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"deactivate persona loras"); sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,
        "INSERT INTO persona_lora_bindings(persona_name,foundation_id,lora_name,lora_path,weight,active) VALUES(?,?,?,?,?,1)",
        -1,&s,nullptr),db,"prepare persona lora bind");
    sqlite3_bind_text(s,1,std::string(personaName).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,2,std::string(foundationId).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,3,std::string(loraName).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,4,std::string(loraPath).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_double(s,5,std::clamp(weight,0.0,4.0));
    Check(sqlite3_step(s),db,"bind persona lora"); const auto id=sqlite3_last_insert_rowid(db); sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,"SELECT id,persona_name,foundation_id,lora_name,lora_path,weight,active FROM persona_lora_bindings WHERE id=?",-1,&s,nullptr),db,"prepare bound lora get");
    sqlite3_bind_int64(s,1,id); PersonaLoraBinding out; if(sqlite3_step(s)==SQLITE_ROW) out=ReadBinding(s);
    sqlite3_finalize(s); return out;
}

std::optional<PersonaLoraBinding> TrainerStore::ResolvePersonaLora(std::string_view personaName) const {
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,persona_name,foundation_id,lora_name,lora_path,weight,active "
        "FROM persona_lora_bindings WHERE persona_name=? AND active=1 ORDER BY updated_utc DESC,id DESC LIMIT 1",
        -1,&s,nullptr),db,"prepare persona lora resolve");
    sqlite3_bind_text(s,1,std::string(personaName).c_str(),-1,SQLITE_TRANSIENT);
    std::optional<PersonaLoraBinding> out; if(sqlite3_step(s)==SQLITE_ROW) out=ReadBinding(s);
    sqlite3_finalize(s); return out;
}


std::optional<PersonaLoraBinding> TrainerStore::GetPersonaLora(long long id) const {
    if(id<=0) return std::nullopt;
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,persona_name,foundation_id,lora_name,lora_path,weight,active "
        "FROM persona_lora_bindings WHERE id=? LIMIT 1",
        -1,&s,nullptr),db,"prepare persona lora get");
    sqlite3_bind_int64(s,1,id);
    std::optional<PersonaLoraBinding> out;
    if(sqlite3_step(s)==SQLITE_ROW) out=ReadBinding(s);
    sqlite3_finalize(s);
    return out;
}

bool TrainerStore::ActivatePersonaLora(long long id) {
    auto target=GetPersonaLora(id);
    if(!target) return false;

    SqliteTransaction tx(db_);
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "UPDATE persona_lora_bindings SET active=0,updated_utc=CURRENT_TIMESTAMP "
        "WHERE persona_name=?",
        -1,&s,nullptr),db,"prepare persona lora deactivate all");
    sqlite3_bind_text(s,1,target->personaName.c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"deactivate persona loras");
    sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,
        "UPDATE persona_lora_bindings SET active=1,updated_utc=CURRENT_TIMESTAMP WHERE id=?",
        -1,&s,nullptr),db,"prepare persona lora activate");
    sqlite3_bind_int64(s,1,id);
    Check(sqlite3_step(s),db,"activate persona lora");
    const bool changed=sqlite3_changes(db)>0;
    sqlite3_finalize(s);
    tx.Commit();
    return changed;
}



std::string TrainerStore::BuildPersonaLoraManifest(long long id) const {
    auto binding=GetPersonaLora(id);
    if(!binding) return {};
    auto foundation=GetFoundation(binding->foundationId);

    std::ostringstream out;
    out<<"{\n";
    out<<"  \"schema\": \"sara-persona-lora-v1\",\n";
    out<<"  \"binding_id\": "<<binding->id<<",\n";
    out<<"  \"persona_name\": \""<<JsonEscape(binding->personaName)<<"\",\n";
    out<<"  \"foundation_id\": \""<<JsonEscape(binding->foundationId)<<"\",\n";
    out<<"  \"foundation_name\": \""<<JsonEscape(foundation?foundation->name:std::string{})<<"\",\n";
    out<<"  \"lora_name\": \""<<JsonEscape(binding->loraName)<<"\",\n";
    out<<"  \"lora_path\": \""<<JsonEscape(binding->loraPath)<<"\",\n";
    out<<"  \"weight\": "<<binding->weight<<",\n";
    out<<"  \"active\": "<<(binding->active?"true":"false")<<"\n";
    out<<"}\n";
    return out.str();
}

std::vector<PersonaLoraBinding> TrainerStore::ListPersonaLoras(
    std::string_view personaName,
    size_t limit) const
{
    std::vector<PersonaLoraBinding> out;
    if(personaName.empty()) return out;
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,persona_name,foundation_id,lora_name,lora_path,weight,active "
        "FROM persona_lora_bindings WHERE persona_name=? "
        "ORDER BY active DESC,updated_utc DESC,id DESC LIMIT ?",
        -1,&s,nullptr),db,"prepare persona lora list");
    sqlite3_bind_text(s,1,std::string(personaName).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(s,2,(int)std::min<size_t>(limit,100));
    while(sqlite3_step(s)==SQLITE_ROW) out.push_back(ReadBinding(s));
    sqlite3_finalize(s);
    return out;
}

TrainerJobRecord TrainerStore::QueueJob(TrainingMode mode,std::string_view targetName,std::string_view personaName,std::string_view foundationId,std::string_view datasetPath,std::string_view baseModelPath,std::string_view outputPath,std::string_view configJson) {
    TrainerJobRecord out; out.id=NewId("job"); out.mode=mode; out.targetName=std::string(targetName);
    out.personaName=std::string(personaName); out.foundationId=std::string(foundationId); out.datasetPath=std::string(datasetPath);
    out.baseModelPath=std::string(baseModelPath); out.outputPath=std::string(outputPath); out.state="QUEUED";
    auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO trainer_jobs(id,training_mode,target_name,persona_name,foundation_id,dataset_path,base_model_path,output_path,config_json,state,progress) "
        "VALUES(?,?,?,?,?,?,?,?,?,'QUEUED',0)",-1,&s,nullptr),db,"prepare trainer job");
    const std::string values[]={out.id,ToString(mode),out.targetName,out.personaName,out.foundationId,out.datasetPath,out.baseModelPath,out.outputPath,std::string(configJson)};
    for(int i=0;i<9;i++) sqlite3_bind_text(s,i+1,values[i].c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"insert trainer job"); sqlite3_finalize(s); return out;
}

std::vector<TrainerJobRecord> TrainerStore::ListJobs(size_t limit) const {
    std::vector<TrainerJobRecord> out; auto* db=db_.Handle(); sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,training_mode,target_name,persona_name,foundation_id,dataset_path,base_model_path,output_path,"
        "state,progress,created_utc,COALESCE(started_utc,''),COALESCE(completed_utc,''),error_text "
        "FROM trainer_jobs ORDER BY created_utc DESC LIMIT ?",-1,&s,nullptr),db,"prepare trainer jobs list");
    sqlite3_bind_int(s,1,(int)std::min<size_t>(limit,100));
    while(sqlite3_step(s)==SQLITE_ROW) {
        TrainerJobRecord j; j.id=Col(s,0); j.mode=TrainingModeFromString(Col(s,1)); j.targetName=Col(s,2);
        j.personaName=Col(s,3); j.foundationId=Col(s,4); j.datasetPath=Col(s,5); j.baseModelPath=Col(s,6);
        j.outputPath=Col(s,7); j.state=Col(s,8); j.progress=sqlite3_column_int(s,9);
        j.createdUtc=Col(s,10); j.startedUtc=Col(s,11); j.completedUtc=Col(s,12); j.errorText=Col(s,13);
        out.push_back(std::move(j));
    }
    sqlite3_finalize(s); return out;
}

std::optional<TrainerDialogueSession> TrainerStore::ActiveDialogueSession(
    std::string_view personaName,
    TrainingMode mode) const
{
    if(personaName.empty()) return std::nullopt;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,persona_name,training_mode,title,active,created_utc,updated_utc "
        "FROM trainer_dialogue_sessions "
        "WHERE persona_name=? AND training_mode=? AND active=1 "
        "ORDER BY updated_utc DESC,id DESC LIMIT 1",
        -1,&s,nullptr),db,"prepare active trainer dialogue session");
    sqlite3_bind_text(s,1,std::string(personaName).c_str(),-1,SQLITE_TRANSIENT);
    const auto modeText=ToString(mode);
    sqlite3_bind_text(s,2,modeText.c_str(),-1,SQLITE_TRANSIENT);

    std::optional<TrainerDialogueSession> out;
    if(sqlite3_step(s)==SQLITE_ROW) out=ReadDialogueSession(s);
    sqlite3_finalize(s);
    return out;
}

TrainerDialogueSession TrainerStore::EnsureDialogueSession(
    std::string_view personaName,
    TrainingMode mode)
{
    if(auto existing=ActiveDialogueSession(personaName,mode)) return *existing;
    return NewDialogueSession(personaName,mode,{});
}

TrainerDialogueSession TrainerStore::NewDialogueSession(
    std::string_view personaName,
    TrainingMode mode,
    std::string_view title)
{
    if(personaName.empty()) throw std::runtime_error("trainer dialogue persona is required");

    SqliteTransaction tx(db_);
    auto* db=db_.Handle();
    sqlite3_stmt* s{};

    Check(sqlite3_prepare_v2(db,
        "UPDATE trainer_dialogue_sessions "
        "SET active=0,updated_utc=CURRENT_TIMESTAMP "
        "WHERE persona_name=? AND training_mode=? AND active=1",
        -1,&s,nullptr),db,"prepare trainer dialogue archive");
    const auto persona=std::string(personaName);
    const auto modeText=ToString(mode);
    sqlite3_bind_text(s,1,persona.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,2,modeText.c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"archive trainer dialogue");
    sqlite3_finalize(s);

    const auto id=NewId("trainer-dialogue");
    std::string resolvedTitle=std::string(title);
    if(resolvedTitle.empty())
        resolvedTitle=ToString(mode)+" trainer conversation";

    Check(sqlite3_prepare_v2(db,
        "INSERT INTO trainer_dialogue_sessions("
        "id,persona_name,training_mode,title,active"
        ") VALUES(?,?,?,?,1)",
        -1,&s,nullptr),db,"prepare trainer dialogue create");
    sqlite3_bind_text(s,1,id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,2,persona.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,3,modeText.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,4,resolvedTitle.c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"create trainer dialogue");
    sqlite3_finalize(s);
    tx.Commit();

    auto created=ActiveDialogueSession(personaName,mode);
    if(!created) throw std::runtime_error("trainer dialogue session could not be reloaded");
    return *created;
}

TrainerDialogueTurn TrainerStore::AppendDialogueTurn(
    std::string_view sessionId,
    std::string_view role,
    std::string_view text,
    std::string_view payload)
{
    if(sessionId.empty()) throw std::runtime_error("trainer dialogue session is required");
    if(role!="OPERATOR" && role!="SARA" && role!="SYSTEM")
        throw std::runtime_error("invalid trainer dialogue role");
    if(text.empty()) throw std::runtime_error("trainer dialogue text is required");

    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO trainer_dialogue_turns("
        "session_id,role,message_text,payload,applied"
        ") VALUES(?,?,?,?,0)",
        -1,&s,nullptr),db,"prepare trainer dialogue turn");
    const auto session=std::string(sessionId);
    const auto roleText=std::string(role);
    const auto message=std::string(text);
    const auto payloadText=std::string(payload);
    sqlite3_bind_text(s,1,session.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,2,roleText.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,3,message.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,4,payloadText.c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"insert trainer dialogue turn");
    const auto id=sqlite3_last_insert_rowid(db);
    sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,
        "UPDATE trainer_dialogue_sessions SET updated_utc=CURRENT_TIMESTAMP WHERE id=?",
        -1,&s,nullptr),db,"prepare trainer dialogue touch");
    sqlite3_bind_text(s,1,session.c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"touch trainer dialogue session");
    sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,
        "SELECT id,session_id,role,message_text,payload,applied,created_utc,"
        "COALESCE(applied_utc,'') FROM trainer_dialogue_turns WHERE id=?",
        -1,&s,nullptr),db,"prepare trainer dialogue turn reload");
    sqlite3_bind_int64(s,1,id);
    TrainerDialogueTurn out;
    if(sqlite3_step(s)==SQLITE_ROW) out=ReadDialogueTurn(s);
    sqlite3_finalize(s);
    return out;
}

std::vector<TrainerDialogueTurn> TrainerStore::ListDialogueTurns(
    std::string_view sessionId,
    size_t limit) const
{
    std::vector<TrainerDialogueTurn> out;
    if(sessionId.empty()) return out;

    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,session_id,role,message_text,payload,applied,created_utc,"
        "COALESCE(applied_utc,'') "
        "FROM trainer_dialogue_turns WHERE session_id=? "
        "ORDER BY id DESC LIMIT ?",
        -1,&s,nullptr),db,"prepare trainer dialogue list");
    const auto session=std::string(sessionId);
    sqlite3_bind_text(s,1,session.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(s,2,(int)std::min<size_t>(limit,200));
    while(sqlite3_step(s)==SQLITE_ROW) out.push_back(ReadDialogueTurn(s));
    sqlite3_finalize(s);
    std::reverse(out.begin(),out.end());
    return out;
}

bool TrainerStore::MarkDialogueTurnApplied(long long turnId)
{
    if(turnId<=0) return false;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "UPDATE trainer_dialogue_turns "
        "SET applied=1,applied_utc=CURRENT_TIMESTAMP "
        "WHERE id=? AND applied=0",
        -1,&s,nullptr),db,"prepare trainer dialogue apply");
    sqlite3_bind_int64(s,1,turnId);
    Check(sqlite3_step(s),db,"apply trainer dialogue turn");
    const bool changed=sqlite3_changes(db)>0;
    sqlite3_finalize(s);
    return changed;
}

} // namespace sentinel::simulation
