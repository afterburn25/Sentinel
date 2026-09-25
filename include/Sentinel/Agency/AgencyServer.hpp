#pragma once
#include <string>
#include <vector>

namespace sentinel::agency {

struct AgencyServerConfig {
    std::string endpoint;
    std::string agencyId;
    std::string workstationId;
    bool enabled{false};
};

enum class SyncItemType { CaseMetadata, EvidenceManifest, AuditRecord, PolicyPackage, ModelProfile };

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

}
