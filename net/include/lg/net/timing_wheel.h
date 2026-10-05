#pragma once

#include <cstddef>
#include <memory>
#include <unordered_set>
#include <vector>

namespace lg::net {

// 用引用计数实现的时间轮（见 decisions.md D-005）。
// 每个槽保存一批条目的强引用；touch 把条目放进最新的槽，tick 清空最老的槽。
// 一个条目最后一次被 touch 之后，经过 slots 次 tick 仍未再被 touch，
// 它的最后一个强引用就会随槽一起释放，条目的析构函数就是超时动作。
// 只能在一个线程里使用。
class TimingWheel {
public:
    using Entry = std::shared_ptr<void>;

    explicit TimingWheel(std::size_t slots) : buckets_(slots == 0 ? 1 : slots) {}

    void touch(const Entry& entry) { buckets_[head_].insert(entry); }

    void tick() {
        head_ = (head_ + 1) % buckets_.size();
        Bucket expired;
        expired.swap(buckets_[head_]);
    }

    std::size_t slots() const noexcept { return buckets_.size(); }

private:
    using Bucket = std::unordered_set<Entry>;

    std::vector<Bucket> buckets_;
    std::size_t head_ = 0;
};

}  // namespace lg::net
