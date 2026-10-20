#pragma once

#include "game_reader.h"
#include <psapi.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cwchar>
#include <iomanip>
#include <sstream>

namespace overlay {

struct SessionMetric {
    uint64_t time_ms{};
    double fps{}, refresh_hz{}, read_ms{}, age_ms{-1}, visibility_ms{}, input_queue_ms{};
    double work_ms{}, present_ms{}, queue_wait_ms{};
    double cpu_percent{}, private_mb{}, working_set_mb{};
    bool rendering{}, connected{}, current_data{}, resources_valid{};
};

// Five samples per second, bounded to the most recent minute. Collection stays on
// the UI thread and uses the existing snapshot; no extra reads from the game.
class SessionMonitor {
    static constexpr uint64_t interval_ms = 200;
    static constexpr uint64_t history_ms = 60000;
    std::array<SessionMetric, 301> samples_{};
    size_t first_{}, count_{};
    uint64_t next_sample_{}, next_resources_{}, cpu_time_{}, cpu_wall_{};
    double work_sum_{}, present_sum_{}, wait_sum_{};
    unsigned frames_{};
    SessionMetric resources_{};

    static uint64_t ticks(FILETIME value) {
        return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    }
    void resources(uint64_t now) {
        if (now < next_resources_) return;
        next_resources_ = now + 1000;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        resources_.resources_valid = GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
        if (resources_.resources_valid) {
            resources_.private_mb = double(memory.PrivateUsage) / (1024. * 1024.);
            resources_.working_set_mb = double(memory.WorkingSetSize) / (1024. * 1024.);
        }
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
            const auto total = ticks(kernel) + ticks(user);
            const DWORD processors = std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
            if (cpu_wall_ && now > cpu_wall_ && total >= cpu_time_)
                resources_.cpu_percent = std::clamp(double(total - cpu_time_) /
                    (double(now - cpu_wall_) * 10000. * processors) * 100., 0., 100.);
            cpu_wall_ = now;
            cpu_time_ = total;
        }
    }
    static std::string quote(const std::string& input) {
        std::ostringstream out;
        out << '"';
        for (const unsigned char c : input) {
            switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
                else out << c;
            }
        }
        out << '"';
        return out.str();
    }
    static void number(std::ostream& out, double value, bool valid = true) {
        if (valid && std::isfinite(value)) out << value;
        else out << "null";
    }
    static void write_metric(std::ostream& out, const SessionMetric& p) {
        out << "{\"time_ms\":" << p.time_ms << ",\"overlay_fps\":";
        number(out, p.fps, p.rendering);
        out << ",\"refresh_hz\":"; number(out, p.refresh_hz);
        out << ",\"read_ms\":"; number(out, p.read_ms, p.current_data);
        out << ",\"data_age_ms\":"; number(out, p.age_ms, p.age_ms >= 0);
        out << ",\"visibility_ms\":"; number(out, p.visibility_ms, p.current_data);
        out << ",\"input_queue_ms\":"; number(out, p.input_queue_ms);
        out << ",\"work_ms\":"; number(out, p.work_ms, p.rendering);
        out << ",\"present_ms\":"; number(out, p.present_ms, p.rendering);
        out << ",\"gpu_queue_wait_ms\":"; number(out, p.queue_wait_ms, p.rendering);
        out << ",\"cpu_percent\":"; number(out, p.cpu_percent, p.resources_valid);
        out << ",\"private_mb\":"; number(out, p.private_mb, p.resources_valid);
        out << ",\"working_set_mb\":"; number(out, p.working_set_mb, p.resources_valid);
        out << ",\"rendering\":" << (p.rendering ? "true" : "false")
            << ",\"connected\":" << (p.connected ? "true" : "false")
            << ",\"current_data\":" << (p.current_data ? "true" : "false") << '}';
    }
public:
    SessionMetric latest;
    bool frozen{};
    std::string export_status{"Reports stay local. Capture a minute of timings and current view details."};
    std::filesystem::path last_report;

    size_t size() const { return count_; }
    const SessionMetric& at(size_t index) const { return samples_[(first_ + index) % samples_.size()]; }
    void reset() { first_ = count_ = 0; }
    uint64_t graph_time() const { return frozen && count_ ? at(count_ - 1).time_ms : latest.time_ms; }
    template<class Value, class Valid>
    double percentile(double fraction, Value value, Valid valid) const {
        std::array<double, 301> values{};
        size_t count{};
        for (size_t i = 0; i < count_; ++i) {
            const auto& p = at(i);
            const double v = value(p);
            if (valid(p) && std::isfinite(v)) values[count++] = v;
        }
        if (!count) return -1;
        const size_t rank = static_cast<size_t>(std::ceil(std::clamp(fraction, 0., 1.) * double(count - 1)));
        std::nth_element(values.begin(), values.begin() + rank, values.begin() + count);
        return values[rank];
    }
    void observe(const Snapshot& snap, uint64_t now, double fps, double hz, double input_queue,
        bool focused, bool rendered, double work, double present, double queue_wait) {
        if (rendered) { work_sum_ += work; present_sum_ += present; wait_sum_ += queue_wait; ++frames_; }
        if (now < next_sample_) return;
        // Keep the schedule anchored when the background loop runs more slowly.
        // Skip missed deadlines rather than replaying old observations or drifting.
        if (!next_sample_) next_sample_ = now;
        next_sample_ += ((now - next_sample_) / interval_ms + 1) * interval_ms;
        resources(now);
        latest = resources_;
        latest.time_ms = now;
        latest.fps = fps;
        latest.refresh_hz = hz;
        latest.read_ms = snap.sample_us / 1000.;
        latest.visibility_ms = snap.visibility_us / 1000.;
        latest.age_ms = snap.time && now >= snap.time ? double(now - snap.time) : -1.;
        latest.current_data = snap.pid && fresh(now, snap.time);
        latest.connected = snap.pid != 0;
        latest.rendering = focused && frames_ > 0;
        latest.input_queue_ms = input_queue;
        if (frames_) {
            latest.work_ms = work_sum_ / frames_;
            latest.present_ms = present_sum_ / frames_;
            latest.queue_wait_ms = wait_sum_ / frames_;
        }
        work_sum_ = present_sum_ = wait_sum_ = 0;
        frames_ = 0;
        if (frozen) return;
        while (count_ && now > at(0).time_ms && now - at(0).time_ms > history_ms) {
            first_ = (first_ + 1) % samples_.size(); --count_;
        }
        if (count_ == samples_.size()) { first_ = (first_ + 1) % samples_.size(); --count_; }
        samples_[(first_ + count_) % samples_.size()] = latest;
        ++count_;
    }
    const char* health(bool input_ready) const {
        if (!latest.connected) return "Reader disconnected";
        if (!latest.current_data) return "Game data stale";
        if (!latest.rendering) return "Rendering paused in background";
        if (!input_ready) return "Input device offline";
        if (latest.refresh_hz > 0 && latest.fps < latest.refresh_hz * .8) return "Overlay below refresh rate";
        return "Live data ready";
    }
    // Automatic diagnostics write just the latest point; full history is exported
    // only on request, so collecting the monitor does not add disk work per frame.
    void write_latest(const std::filesystem::path& path) const {
        std::ofstream out(path);
        out << "{\"history_samples\":" << count_ << ",\"frozen\":" << (frozen ? "true" : "false") << ",\"latest\":";
        write_metric(out, latest); out << "}\n";
    }
    bool export_report(const std::filesystem::path& directory, const Snapshot& snap,
        const std::string& input_status) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) { export_status = "Could not create reports folder: " + error.message(); return false; }
        SYSTEMTIME time{}; GetLocalTime(&time);
        wchar_t name[100]{};
        swprintf_s(name, L"session-%04u%02u%02u-%02u%02u%02u-%03u-%lu.json", time.wYear,
            time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId());
        const auto path = directory / name;
        std::ofstream out(path);
        if (!out) { export_status = "Could not write report. Check access to the reports folder."; return false; }
        out << std::setprecision(8) << "{\"format\":1,\"source\":\"Test session monitor\",\"sample_interval_ms\":200,\"history_limit_ms\":60000,"
            << "\"cpu_scale\":\"percent of total logical CPU capacity\",\"frozen\":" << (frozen ? "true" : "false")
            << ",\"game\":{\"status\":" << quote(snap.status) << ",\"map\":" << quote(snap.map_name)
            << ",\"reader_profile\":" << quote(snap.reader_profile) << ",\"hero\":" << quote(snap.hero.name)
            << ",\"mode\":" << quote(snap.replay ? "Replay" : snap.practice ? "Sandbox / bots" : "Live match")
            << ",\"players\":" << snap.players.size() << ",\"missing_anchors\":" << snap.missing_anchors
            << ",\"visibility_status\":" << quote(snap.visibility_status)
            << ",\"camera_valid\":" << (snap.camera_valid ? "true" : "false")
            << ",\"projection\":{\"horizontal_fov\":";
        number(out, snap.projection.horizontal_fov, snap.projection.valid);
        out << ",\"vertical_fov\":"; number(out, snap.projection.vertical_fov, snap.projection.valid);
        out << ",\"aspect\":"; number(out, snap.projection.aspect, snap.projection.valid);
        out << "},\"weapon_speed\":"; number(out, snap.weapon.speed, snap.weapon.valid);
        out << "},\"input_status\":" << quote(input_status) << ",\"latest\":";
        write_metric(out, latest);
        out << ",\"history\":[";
        for (size_t i = 0; i < count_; ++i) { if (i) out << ','; write_metric(out, at(i)); }
        out << "]}\n";
        out.close();
        if (!out) { export_status = "Report write failed. Check available disk space."; return false; }
        last_report = path;
        export_status = "Saved " + path.filename().string() + " / Open reports to view it";
        return true;
    }
};
} // namespace overlay
