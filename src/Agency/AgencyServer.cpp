#include "Sentinel/Agency/AgencyServer.hpp"
namespace sentinel::agency {
void AgencySyncQueue::Enqueue(SyncQueueItem item) { items_.push_back(std::move(item)); }
const std::vector<SyncQueueItem>& AgencySyncQueue::Items() const { return items_; }
size_t AgencySyncQueue::PendingCount() const {
    size_t n=0; for(const auto& i:items_) if(!i.complete) ++n; return n;
}
}
