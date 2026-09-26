#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/SecretProtector.hpp"
#include "Sentinel/Security/KeyManager.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include "Sentinel/Storage/MigrationService.hpp"
#include "Sentinel/Core/CaseRepository.hpp"
#include "Sentinel/Channels/ChannelCore.hpp"
#include "Sentinel/Channels/JurisdictionRules.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void TestIdsAndHashes()
{
    auto id = sentinel::CaseId::Random();
    auto s = id.ToString();
    auto parsed = sentinel::CaseId::Parse(s);
    assert(parsed.has_value());
    assert(parsed->ToString() == s);

    auto h = sentinel::Hash256::FromHex(std::string(64, '0'));
    assert(h.has_value());
    assert(h->ToHex() == std::string(64, '0'));
}

#ifdef _WIN32
void TestWindowsCryptoAndSev()
{
    sentinel::WindowsSecureRandom random;
    sentinel::WindowsHashService hash;
    sentinel::WindowsAesGcmCipher cipher(random);

    auto key = random.Bytes(32);
    const auto root = std::filesystem::temp_directory_path() / ("sentinel-test-" + sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    const auto source = root / "evidence.bin";
    const auto container = root / "evidence.sev";

    const std::string payload = "Sentinel authenticated evidence payload\nline two\n";
    {
        std::ofstream out(source, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    const auto evidenceId = sentinel::EvidenceId::Random();
    const auto caseId = sentinel::CaseId::Random();
    sentinel::Hash256 original{};

    sentinel::SevContainer::EncryptFile(
        source,
        container,
        evidenceId,
        caseId,
        key.Span(),
        cipher,
        hash,
        original);

    assert(sentinel::SevContainer::BasicValidate(container));

    sentinel::Hash256 decryptedHash{};
    const auto plain = sentinel::SevContainer::DecryptFile(
        container,
        key.Span(),
        cipher,
        hash,
        &decryptedHash);

    const std::string recovered(
        reinterpret_cast<const char*>(plain.data()),
        plain.size());

    assert(recovered == payload);
    assert(decryptedHash == original);

    {
        std::fstream io(container, std::ios::binary | std::ios::in | std::ios::out);
        const auto offset = static_cast<std::streamoff>(sizeof(sentinel::SevHeaderV1) + 12);
        io.seekg(offset);
        char c{};
        io.read(&c, 1);
        c ^= 0x01;
        io.seekp(offset);
        io.write(&c, 1);
    }

    assert(sentinel::SevContainer::BasicValidate(container));
    bool tamperRejected = false;
    try {
        (void)sentinel::SevContainer::DecryptFile(container, key.Span(), cipher, hash);
    }
    catch (...) {
        tamperRejected = true;
    }
    assert(tamperRejected);

    sentinel::WindowsDpapiSecretProtector dpapi;
    auto protectedKey = dpapi.Protect(key.Span());
    auto unprotectedKey = dpapi.Unprotect(protectedKey);
    assert(unprotectedKey.Span().size() == key.Span().size());
    for (size_t i = 0; i < key.Span().size(); ++i)
        assert(unprotectedKey.Span()[i] == key.Span()[i]);

    sentinel::SqliteDatabase db;
    const auto dbPath = root / "sentinel.db";
    db.Open(dbPath);
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR) / "migrations");

    sentinel::SqliteCaseRepository cases(db);
    sentinel::CaseRecord caseRecord{
        caseId,
        "TEST-KEY-0001",
        "Key manager integration test",
        "",
        sentinel::CaseStatus::Open,
        sentinel::UserId::Random(),
        sentinel::NowUtc(),
        sentinel::NowUtc()
    };
    cases.Insert(caseRecord);

    sentinel::KeyManager keyManager(root / "keys" / "master.dpapi", db, dpapi, random, cipher);
    keyManager.Initialize();
    keyManager.CreateCaseKey(caseId);
    assert(keyManager.HasCaseKey(caseId));
    auto loadedCaseKey = keyManager.GetCaseKey(caseId);
    assert(loadedCaseKey.Span().size() == 32);

    sentinel::KeyManager reopened(root / "keys" / "master.dpapi", db, dpapi, random, cipher);
    reopened.Initialize();
    auto loadedAgain = reopened.GetCaseKey(caseId);
    assert(loadedAgain.Span().size() == loadedCaseKey.Span().size());
    for (size_t i = 0; i < loadedCaseKey.Span().size(); ++i)
        assert(loadedAgain.Span()[i] == loadedCaseKey.Span()[i]);

    db.Close();
    std::filesystem::remove_all(root);
}
#endif

#ifdef _WIN32
void TestUnifiedChannelCore()
{
    const auto root = std::filesystem::temp_directory_path() / ("sentinel-channel-test-" + sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"sentinel.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    sentinel::channels::JurisdictionRuleStore rules(db);
    rules.EnsureBuiltInBaselines();

    auto federal=rules.LatestProfile(sentinel::channels::RuleLayerType::Federal,"US");
    auto louisiana=rules.LatestProfile(sentinel::channels::RuleLayerType::State,"US","LA");
    assert(federal.has_value());
    assert(louisiana.has_value());
    assert(!federal->AutomationLegallyActive());
    assert(!louisiana->AutomationLegallyActive());

    rules.SelectForOperation(
        "unit-operation","US","LA",federal->id,louisiana->id,{},"unit-test");
    auto stack=rules.SelectedForOperation("unit-operation");
    assert(stack.has_value());
    assert(stack->federal.has_value());
    assert(stack->state.has_value());
    assert(!stack->fullyActive);

    sentinel::channels::ChannelCoreStore store(db);
    const auto subject=store.CreateSubject("case-test","Cross-channel subject");
    const auto identity=store.AddSubjectIdentity(
        subject,"phone","twilio","+13375550199","+1 337-555-0199",
        sentinel::channels::IdentityLinkState::Candidate,0.75);
    assert(store.ConfirmSubjectIdentity(identity,"unit-test"));

    sentinel::channels::ChannelAccount account;
    account.type=sentinel::channels::ChannelType::Sms;
    account.provider="twilio";
    account.externalAccountId="acct-test";
    account.displayName="Sentinel SMS";
    account.address="+13375550100";
    account.capabilities.Set(sentinel::channels::ReceiveText);
    account.capabilities.Set(sentinel::channels::SendText);
    account.capabilities.Set(sentinel::channels::DeliveryReceipts);
    const auto accountId=store.UpsertChannelAccount(account);

    sentinel::channels::ChannelConversation conversation;
    conversation.subjectId=subject;
    conversation.personaName="Casey";
    conversation.channelAccountId=accountId;
    conversation.type=sentinel::channels::ChannelType::Sms;
    conversation.provider="twilio";
    conversation.providerConversationId="sms:+13375550199";
    conversation.externalPeerId="+13375550199";
    const auto conversationId=store.OpenConversation(conversation);

    sentinel::channels::RawChannelEvent event;
    event.id=sentinel::Uuid::Random().ToString();
    event.channelConversationId=conversationId;
    event.provider="twilio";
    event.type=sentinel::channels::ChannelType::Sms;
    event.providerAccountId="acct-test";
    event.providerConversationId="sms:+13375550199";
    event.providerMessageId="SM-unit-1";
    event.eventType="message.received";
    event.direction=sentinel::channels::Direction::Inbound;
    event.senderExternalId="+13375550199";
    event.recipientExternalId="+13375550100";
    event.rawPayload="{\"Body\":\"hello\"}";
    event.payloadSha256=std::string(64,'a');
    store.RecordRawEvent(event);

    sentinel::channels::NormalizedMessage message;
    message.id=sentinel::Uuid::Random().ToString();
    message.eventId=event.id;
    message.channelConversationId=conversationId;
    message.subjectId=subject;
    message.personaName="Casey";
    message.direction=sentinel::channels::Direction::Inbound;
    message.senderExternalId=event.senderExternalId;
    message.recipientExternalId=event.recipientExternalId;
    message.body="hello";
    message.automationMode=sentinel::channels::AutomationMode::ApprovalRequired;
    message.providerMessageId=event.providerMessageId;
    store.RecordMessage(message);

    auto found=store.FindConversation("twilio",accountId,"sms:+13375550199");
    assert(found.has_value());
    assert(found->subjectId==subject);

    auto recent=store.RecentMessages(conversationId,10);
    assert(recent.size()==1);
    assert(recent[0].body=="hello");
    assert(recent[0].subjectId==subject);

    db.Close();
    std::filesystem::remove_all(root);
}
#endif

}

int main()
{
    TestIdsAndHashes();
#ifdef _WIN32
    TestWindowsCryptoAndSev();
    TestUnifiedChannelCore();
#endif
    std::cout << "SentinelCoreTests passed\n";
}
