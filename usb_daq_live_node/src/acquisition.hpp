#pragma once

#include "config.hpp"
#include "history.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

struct AcquisitionStats {
    bool worker_alive = false;
    bool streaming = false;
    bool device_open = false;
    int device_count = 0;
    std::uint64_t chunks = 0;
    std::uint64_t frames = 0;
    std::uint64_t raw_floats = 0;
    int last_chunk_frames = 0;
    int current_backlog_floats = 0;
    int max_backlog_floats = 0;
    bool session_buffering = false;
    bool session_available = false;
    std::uint64_t session_frames = 0;
    int exported_channels = 0;
    std::uint64_t exported_frames = 0;
    double elapsed_seconds = 0.0;
    double effective_sample_rate = 0.0;
    std::string error;
    std::string export_path;
};

class Acquisition {
public:
    Acquisition() = default;
    Acquisition(const Acquisition&) = delete;
    Acquisition& operator=(const Acquisition&) = delete;
    ~Acquisition();

    bool start(DaqConfig config, std::string& error);
    void stop();
    bool export_session(DaqConfig config, std::string& error);
    bool discard_session(std::string& error);

    AcquisitionStats stats() const;
    RollingHistory& history() { return history_; }

private:
    void worker(DaqConfig config);
    void set_error(const std::string& error);
    void update_max_backlog(int value);

    RollingHistory history_;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> worker_alive_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> device_open_{false};
    std::atomic<int> device_count_{0};
    std::atomic<std::uint64_t> chunks_{0};
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<std::uint64_t> raw_floats_{0};
    std::atomic<int> last_chunk_frames_{0};
    std::atomic<int> current_backlog_floats_{0};
    std::atomic<int> max_backlog_floats_{0};
    std::atomic<bool> session_buffering_{false};
    std::atomic<bool> session_available_{false};
    std::atomic<std::uint64_t> session_frames_{0};
    std::atomic<int> exported_channels_{0};
    std::atomic<std::uint64_t> exported_frames_{0};

    mutable std::mutex session_mutex_;
    std::filesystem::path session_temp_path_;

    mutable std::mutex status_mutex_;
    std::string error_;
    std::string export_path_;
    std::chrono::steady_clock::time_point started_at_{};
    std::chrono::steady_clock::time_point stopped_at_{};
};
