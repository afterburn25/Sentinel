#include "Sentinel/Channels/ChannelCore.hpp"
#include "Sentinel/Core/Types.hpp"
#include <algorithm>
#include <stdexcept>
#include <sqlite3.h>

namespace sentinel::channels {
namespace {
std::string NewId(){ return Uuid::Random().ToString(); }

void Bind(sqlite3_stmt* s,int i,const std::string& v){
    sqlite3_bind_text(s,i,v.c_str(),-1,SQLITE_TRANSIENT);
}
std::string Col(sqlite3_stmt* s,int i){
    const auto* p=reinterpret_cast<const char*>(sqlite3_column_text(s,i));
    return p?p:"";
}
void DoneOrThrow(sqlite3_stmt* s,const char* what){
    if(sqlite3_step(s)!=SQLITE_DONE){
        std::string e=what;
        if(sqlite3_db_handle(s)) e+=": "+std::string(sqlite3_errmsg(sqlite3_db_handle(s)));
        sqlite3_finalize(s);
        throw std::runtime_error(e);
    }
    sqlite3_finalize(s);
}
}

std::string ToString(ChannelType t){
    switch(t){
        case ChannelType::LocalSimulation:return "local";
        case ChannelType::Sms:return "sms";
        case ChannelType::Mms:return "mms";
        case ChannelType::Rcs:return "rcs";
        case ChannelType::Telegram:return "telegram";
        case ChannelType::Discord:return "discord";
        case ChannelType::Messenger:return "messenger";
        case ChannelType::WhatsApp:return "whatsapp";
        case ChannelType::SnapchatAssist:return "snapchat_assist";
        case ChannelType::Email:return "email";
        default:return "unknown";
    }
}
ChannelType ChannelTypeFromString(std::string_view v){
    if(v=="local")return ChannelType::LocalSimulation;
    if(v=="sms")return ChannelType::Sms;
    if(v=="mms")return ChannelType::Mms;
    if(v=="rcs")return ChannelType::Rcs;
    if(v=="telegram")return ChannelType::Telegram;
    if(v=="discord")return ChannelType::Discord;
    if(v=="messenger")return ChannelType::Messenger;
    if(v=="whatsapp")return ChannelType::WhatsApp;
    if(v=="snapchat_assist")return ChannelType::SnapchatAssist;
    if(v=="email")return ChannelType::Email;
    return ChannelType::Unknown;
}

std::string ChannelCoreStore::CreateSubject(std::string caseId,std::string displayName){
    const auto id=NewId();
    sqlite3_stmt* s{};
    const char* sql="INSERT INTO subjects(id,case_id,display_name,status) VALUES(?,?,?,0)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) throw std::runtime_error("prepare CreateSubject failed");
    Bind(s,1,id); Bind(s,2,caseId); Bind(s,3,displayName);
    DoneOrThrow(s,"CreateSubject failed");
    return id;
}

std::string ChannelCoreStore::AddSubjectIdentity(
    std::string subjectId,std::string identityType,std::string provider,
    std::string externalId,std::string displayValue,IdentityLinkState state,
    double confidence,std::string sourceEventId)
{
    const auto id=NewId();
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO subject_identities(id,subject_id,identity_type,provider,external_id,display_value,link_state,confidence,source_event_id) "
        "VALUES(?,?,?,?,?,?,?,?,?)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) throw std::runtime_error("prepare AddSubjectIdentity failed");
    Bind(s,1,id); Bind(s,2,subjectId); Bind(s,3,identityType); Bind(s,4,provider);
    Bind(s,5,externalId); Bind(s,6,displayValue); sqlite3_bind_int(s,7,(int)state);
    sqlite3_bind_double(s,8,std::clamp(confidence,0.0,1.0)); Bind(s,9,sourceEventId);
    DoneOrThrow(s,"AddSubjectIdentity failed");
    return id;
}

bool ChannelCoreStore::ConfirmSubjectIdentity(std::string_view identityId,std::string reviewer){
    sqlite3_stmt* s{};
    const char* sql=
        "UPDATE subject_identities SET link_state=1,confidence=1.0,confirmed_by=?,confirmed_utc=CURRENT_TIMESTAMP WHERE id=?";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) return false;
    Bind(s,1,reviewer); Bind(s,2,std::string(identityId));
    const bool ok=sqlite3_step(s)==SQLITE_DONE && sqlite3_changes(db_.Handle())==1;
    sqlite3_finalize(s);
    return ok;
}

std::string ChannelCoreStore::UpsertChannelAccount(const ChannelAccount& input){
    const auto id=input.id.empty()?NewId():input.id;
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO channel_accounts(id,channel_type,provider,external_account_id,display_name,address,jurisdiction,compliance_status,capability_mask,enabled) "
        "VALUES(?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET channel_type=excluded.channel_type,provider=excluded.provider,"
        "external_account_id=excluded.external_account_id,display_name=excluded.display_name,address=excluded.address,"
        "jurisdiction=excluded.jurisdiction,compliance_status=excluded.compliance_status,"
        "capability_mask=excluded.capability_mask,enabled=excluded.enabled";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) throw std::runtime_error("prepare UpsertChannelAccount failed");
    Bind(s,1,id); Bind(s,2,ToString(input.type)); Bind(s,3,input.provider); Bind(s,4,input.externalAccountId);
    Bind(s,5,input.displayName); Bind(s,6,input.address); Bind(s,7,input.jurisdiction); Bind(s,8,input.complianceStatus);
    sqlite3_bind_int64(s,9,(sqlite3_int64)input.capabilities.mask); sqlite3_bind_int(s,10,input.enabled?1:0);
    DoneOrThrow(s,"UpsertChannelAccount failed");
    return id;
}

std::string ChannelCoreStore::OpenConversation(const ChannelConversation& input){
    const auto id=input.id.empty()?NewId():input.id;
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO channel_conversations(id,subject_id,persona_name,channel_account_id,channel_type,provider,"
        "provider_conversation_id,external_peer_id,automation_profile_id,state) VALUES(?,?,?,?,?,?,?,?,?,?)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) throw std::runtime_error("prepare OpenConversation failed");
    Bind(s,1,id); Bind(s,2,input.subjectId); Bind(s,3,input.personaName); Bind(s,4,input.channelAccountId);
    Bind(s,5,ToString(input.type)); Bind(s,6,input.provider); Bind(s,7,input.providerConversationId);
    Bind(s,8,input.externalPeerId); Bind(s,9,input.automationProfileId); sqlite3_bind_int(s,10,(int)input.state);
    DoneOrThrow(s,"OpenConversation failed");
    return id;
}

std::optional<ChannelConversation> ChannelCoreStore::FindConversation(
    std::string_view provider,std::string_view channelAccountId,std::string_view providerConversationId) const
{
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT id,subject_id,persona_name,channel_account_id,channel_type,provider,provider_conversation_id,"
        "external_peer_id,automation_profile_id,state FROM channel_conversations "
        "WHERE provider=? AND channel_account_id=? AND provider_conversation_id=? LIMIT 1";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) return std::nullopt;
    Bind(s,1,std::string(provider)); Bind(s,2,std::string(channelAccountId)); Bind(s,3,std::string(providerConversationId));
    ChannelConversation c;
    if(sqlite3_step(s)!=SQLITE_ROW){ sqlite3_finalize(s); return std::nullopt; }
    c.id=Col(s,0); c.subjectId=Col(s,1); c.personaName=Col(s,2); c.channelAccountId=Col(s,3);
    c.type=ChannelTypeFromString(Col(s,4)); c.provider=Col(s,5); c.providerConversationId=Col(s,6);
    c.externalPeerId=Col(s,7); c.automationProfileId=Col(s,8); c.state=(ConversationState)sqlite3_column_int(s,9);
    sqlite3_finalize(s);
    return c;
}

void ChannelCoreStore::RecordRawEvent(const RawChannelEvent& e){
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT OR IGNORE INTO channel_events(id,channel_conversation_id,provider,channel_type,provider_account_id,"
        "provider_conversation_id,provider_message_id,event_type,direction,sender_external_id,recipient_external_id,"
        "provider_timestamp,raw_payload,payload_sha256) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) throw std::runtime_error("prepare RecordRawEvent failed");
    Bind(s,1,e.id); Bind(s,2,e.channelConversationId); Bind(s,3,e.provider); Bind(s,4,ToString(e.type));
    Bind(s,5,e.providerAccountId); Bind(s,6,e.providerConversationId); Bind(s,7,e.providerMessageId); Bind(s,8,e.eventType);
    sqlite3_bind_int(s,9,(int)e.direction); Bind(s,10,e.senderExternalId); Bind(s,11,e.recipientExternalId);
    Bind(s,12,e.providerTimestamp); Bind(s,13,e.rawPayload); Bind(s,14,e.payloadSha256);
    DoneOrThrow(s,"RecordRawEvent failed");
}

void ChannelCoreStore::RecordMessage(const NormalizedMessage& m){
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO normalized_messages(id,event_id,channel_conversation_id,subject_id,persona_name,direction,"
        "sender_external_id,recipient_external_id,body,delivery_state,automation_mode,model_run_id,policy_decision_id,"
        "approval_id,provider_message_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) throw std::runtime_error("prepare RecordMessage failed");
    Bind(s,1,m.id); Bind(s,2,m.eventId); Bind(s,3,m.channelConversationId); Bind(s,4,m.subjectId); Bind(s,5,m.personaName);
    sqlite3_bind_int(s,6,(int)m.direction); Bind(s,7,m.senderExternalId); Bind(s,8,m.recipientExternalId); Bind(s,9,m.body);
    sqlite3_bind_int(s,10,m.deliveryState); sqlite3_bind_int(s,11,(int)m.automationMode);
    Bind(s,12,m.modelRunId); Bind(s,13,m.policyDecisionId); Bind(s,14,m.approvalId); Bind(s,15,m.providerMessageId);
    DoneOrThrow(s,"RecordMessage failed");
}

std::vector<NormalizedMessage> ChannelCoreStore::RecentMessages(std::string_view conversationId,int limit) const {
    std::vector<NormalizedMessage> out;
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT id,event_id,channel_conversation_id,subject_id,persona_name,direction,sender_external_id,"
        "recipient_external_id,body,delivery_state,automation_mode,model_run_id,policy_decision_id,approval_id,"
        "provider_message_id FROM normalized_messages WHERE channel_conversation_id=? ORDER BY created_utc DESC LIMIT ?";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) return out;
    Bind(s,1,std::string(conversationId)); sqlite3_bind_int(s,2,std::clamp(limit,1,500));
    while(sqlite3_step(s)==SQLITE_ROW){
        NormalizedMessage m;
        m.id=Col(s,0); m.eventId=Col(s,1); m.channelConversationId=Col(s,2); m.subjectId=Col(s,3);
        m.personaName=Col(s,4); m.direction=(Direction)sqlite3_column_int(s,5); m.senderExternalId=Col(s,6);
        m.recipientExternalId=Col(s,7); m.body=Col(s,8); m.deliveryState=sqlite3_column_int(s,9);
        m.automationMode=(AutomationMode)sqlite3_column_int(s,10); m.modelRunId=Col(s,11); m.policyDecisionId=Col(s,12);
        m.approvalId=Col(s,13); m.providerMessageId=Col(s,14);
        out.push_back(std::move(m));
    }
    sqlite3_finalize(s);
    std::reverse(out.begin(),out.end());
    return out;
}

}
