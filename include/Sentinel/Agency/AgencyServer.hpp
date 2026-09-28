#pragma once

#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::agency {

struct AgencyServerConfig {
    std::string endpoint;
    std::string agencyId;
    std::string workstationId;
    bool enabled{false};
};

enum class SyncItemType {
    CaseMetadata,
    EvidenceManifest,
    AuditRecord,
    PolicyPackage,
    ModelProfile
};

struct SyncQueueItem {
    std::string id;
    SyncItemType type{SyncItemType::CaseMetadata};
    std::string localReference;
    int attempts{0};
    bool complete{false};
};

class AgencySyncQueue {
public:
    void Enqueue(SyncQueueItem item);
    const std::vector<SyncQueueItem>& Items() const;
    size_t PendingCount() const;
private:
    std::vector<SyncQueueItem> items_;
};

// Persistent offline-first state. This intentionally does not implement a
// network transport; it persists configuration and work that a future
// authenticated Agency Server transport may consume.
class AgencyServerStore {
public:
    explicit AgencyServerStore(SqliteDatabase& db):db_(db){}

    [[nodiscard]] AgencyServerConfig LoadConfig() const;
    void SaveConfig(const AgencyServerConfig& config);

    SyncQueueItem Enqueue(SyncQueueItem item);
    [[nodiscard]] std::vector<SyncQueueItem> Items(size_t limit=100) const;
    [[nodiscard]] size_t PendingCount() const;
    bool MarkComplete(std::string_view id);

private:
    SqliteDatabase& db_;
};

}
