#include "lg/gateway/stats.h"

#include <sstream>

namespace lg::gateway {

namespace {

template <class T>
T read(const std::atomic<T>& counter) noexcept {
    return counter.load(std::memory_order_relaxed);
}

}  // namespace

void StatsSnapshot::add(const ShardCounters& c) noexcept {
    devices_online += read(c.devices_online);
    subscribers_online += read(c.subscribers_online);
    device_connections += read(c.device_connections);
    subscriber_connections += read(c.subscriber_connections);
    bytes_in += read(c.bytes_in);
    frames_in += read(c.frames_in);
    samples_in += read(c.samples_in);
    heartbeats_in += read(c.heartbeats_in);
    bad_frames += read(c.bad_frames);
    dropped_bytes += read(c.dropped_bytes);
    samples_forwarded += read(c.samples_forwarded);
    heartbeats_sent += read(c.heartbeats_sent);
    bytes_out += read(c.bytes_out);
    subscriber_dropped_samples += read(c.subscriber_dropped_samples);
    subscriber_pauses += read(c.subscriber_pauses);
    subscriber_stall_disconnects += read(c.subscriber_stall_disconnects);
    device_idle_kicks += read(c.device_idle_kicks);
    device_id_conflicts += read(c.device_id_conflicts);
    device_id_mismatches += read(c.device_id_mismatches);
}

std::string StatsSnapshot::to_string() const {
    std::ostringstream out;
    out << "devices=" << devices_online << " subscribers=" << subscribers_online
        << " samples_in=" << samples_in << " heartbeats_in=" << heartbeats_in
        << " samples_forwarded=" << samples_forwarded << " bad_frames=" << bad_frames
        << " dropped_bytes=" << dropped_bytes << " sub_dropped=" << subscriber_dropped_samples
        << " sub_pauses=" << subscriber_pauses << " sub_stall_kicks=" << subscriber_stall_disconnects
        << " idle_kicks=" << device_idle_kicks << " id_conflicts=" << device_id_conflicts
        << " id_mismatches=" << device_id_mismatches << " bytes_in=" << bytes_in << " bytes_out=" << bytes_out;
    return out.str();
}

}  // namespace lg::gateway
