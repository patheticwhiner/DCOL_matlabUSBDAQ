#include "acquisition.hpp"

#include "capture_writer.hpp"
#include "usb_daq_api.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace {

std::string session_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local, "%Y%m%d_%H%M%S_")
        << std::setw(3) << std::setfill('0') << milliseconds.count();
    return oss.str();
}

} // namespace

Acquisition::~Acquisition() {
    stop();
    // An undecided session is deliberately left as *.f32.tmp in shared/ so an
    // accidental application close does not destroy data captured from Start.
}

bool Acquisition::start(DaqConfig config, std::string& error) {
    stop();
    if (session_available_.load()) {
        error = "Save or discard the previous session before starting another acquisition";
        return false;
    }
    if (!validate_config(config, error)) return false;
    if (std::none_of(config.channels.begin(), config.channels.end(), [](bool value) { return value; })) {
        error = "At least one channel must be enabled";
        return false;
    }

    history_.reset(config.sample_rate, config.history_seconds);
    stop_requested_.store(false);
    worker_alive_.store(true);
    streaming_.store(false);
    device_open_.store(false);
    device_count_.store(0);
    chunks_.store(0);
    frames_.store(0);
    raw_floats_.store(0);
    last_chunk_frames_.store(0);
    current_backlog_floats_.store(0);
    max_backlog_floats_.store(0);
    session_buffering_.store(false);
    session_available_.store(false);
    session_frames_.store(0);
    exported_channels_.store(0);
    exported_frames_.store(0);
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_temp_path_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        error_.clear();
        export_path_.clear();
        started_at_ = std::chrono::steady_clock::now();
        stopped_at_ = {};
    }

    try {
        thread_ = std::thread(&Acquisition::worker, this, std::move(config));
    } catch (const std::exception& ex) {
        worker_alive_.store(false);
        error = ex.what();
        return false;
    }
    return true;
}

void Acquisition::stop() {
    stop_requested_.store(true);
    if (thread_.joinable()) thread_.join();
}

bool Acquisition::export_session(DaqConfig config, std::string& error) {
    if (worker_alive_.load() || session_buffering_.load()) {
        error = "Stop acquisition before exporting the complete session";
        return false;
    }
    if (!session_available_.load()) {
        error = "No completed session is available to export";
        return false;
    }
    if (config.recording_format == RecordingFormat::None) {
        error = "Choose WAV, Float32, or CSV before exporting";
        return false;
    }

    std::lock_guard<std::mutex> session_lock(session_mutex_);
    std::ifstream input(session_temp_path_, std::ios::binary);
    if (!input) {
        error = "Cannot open pending session: " + session_temp_path_.u8string();
        return false;
    }

    CaptureWriter writer;
    if (!writer.open(config, error)) return false;
    const std::filesystem::path output_path = writer.path();
    std::vector<float> raw(static_cast<std::size_t>(4096 * kPhysicalChannels));
    bool ok = true;
    while (input) {
        input.read(reinterpret_cast<char*>(raw.data()),
                   static_cast<std::streamsize>(raw.size() * sizeof(float)));
        const std::streamsize bytes = input.gcount();
        if (bytes == 0) break;
        const std::streamsize frame_bytes =
            static_cast<std::streamsize>(kPhysicalChannels * sizeof(float));
        if ((bytes % frame_bytes) != 0) {
            error = "Pending session is not frame-aligned";
            ok = false;
            break;
        }
        const auto frame_count = static_cast<std::size_t>(bytes / frame_bytes);
        if (!writer.write(raw.data(), frame_count, error)) {
            ok = false;
            break;
        }
    }
    if (input.bad()) {
        error = "Failed while reading pending session";
        ok = false;
    }
    input.close();

    const std::uint64_t exported_frames = writer.frames_written();
    const int exported_channels = static_cast<int>(writer.channel_count());
    writer.close();
    if (!ok) {
        std::error_code cleanup_error;
        std::filesystem::remove(output_path, cleanup_error);
        std::filesystem::remove(output_path.parent_path() /
                                (output_path.stem().u8string() + ".json"), cleanup_error);
        return false;
    }

    std::error_code remove_error;
    std::filesystem::remove(session_temp_path_, remove_error);
    if (remove_error) {
        error = "Export succeeded, but temporary session cleanup failed: " + remove_error.message();
        return false;
    }
    session_temp_path_.clear();
    session_available_.store(false);
    exported_channels_.store(exported_channels);
    exported_frames_.store(exported_frames);
    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        export_path_ = output_path.u8string();
    }
    return true;
}

bool Acquisition::discard_session(std::string& error) {
    if (worker_alive_.load() || session_buffering_.load()) {
        error = "Stop acquisition before discarding the session";
        return false;
    }
    if (!session_available_.load()) return true;

    std::lock_guard<std::mutex> session_lock(session_mutex_);
    std::error_code remove_error;
    std::filesystem::remove(session_temp_path_, remove_error);
    if (remove_error) {
        error = "Cannot discard pending session: " + remove_error.message();
        return false;
    }
    session_temp_path_.clear();
    session_available_.store(false);
    session_frames_.store(0);
    return true;
}

AcquisitionStats Acquisition::stats() const {
    AcquisitionStats result;
    result.worker_alive = worker_alive_.load();
    result.streaming = streaming_.load();
    result.device_open = device_open_.load();
    result.device_count = device_count_.load();
    result.chunks = chunks_.load();
    result.frames = frames_.load();
    result.raw_floats = raw_floats_.load();
    result.last_chunk_frames = last_chunk_frames_.load();
    result.current_backlog_floats = current_backlog_floats_.load();
    result.max_backlog_floats = max_backlog_floats_.load();
    result.session_buffering = session_buffering_.load();
    result.session_available = session_available_.load();
    result.session_frames = session_frames_.load();
    result.exported_channels = exported_channels_.load();
    result.exported_frames = exported_frames_.load();
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        result.error = error_;
        result.export_path = export_path_;
        const auto end = result.worker_alive ? std::chrono::steady_clock::now() : stopped_at_;
        if (started_at_ != std::chrono::steady_clock::time_point{} &&
            end != std::chrono::steady_clock::time_point{}) {
            result.elapsed_seconds = std::chrono::duration<double>(end - started_at_).count();
        }
    }
    if (result.elapsed_seconds > 0.0)
        result.effective_sample_rate = static_cast<double>(result.frames) / result.elapsed_seconds;
    return result;
}

void Acquisition::set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    error_ = error;
}

void Acquisition::update_max_backlog(int value) {
    int current = max_backlog_floats_.load();
    while (value > current && !max_backlog_floats_.compare_exchange_weak(current, value)) {}
}

void Acquisition::worker(DaqConfig config) {
    UsbDaqApi api;
    std::ofstream spool;
    bool opened = false;
    bool configured = false;
    std::string error;

    auto api_error = [&](const std::string& operation) {
        const std::string detail = api.error_detail();
        return detail.empty() ? operation : operation + ": " + detail;
    };

    auto finish = [&]() {
        if (configured) api.AD_continu_stop(config.device);
        streaming_.store(false);
        if (opened) api.closeUSB();
        device_open_.store(false);
        if (spool.is_open()) spool.close();
        session_buffering_.store(false);

        std::filesystem::path temp_path;
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            temp_path = session_temp_path_;
        }
        const bool has_session = session_frames_.load() > 0 && !temp_path.empty();
        session_available_.store(has_session);
        if (!has_session && !temp_path.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove(temp_path, cleanup_error);
        }
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            stopped_at_ = std::chrono::steady_clock::now();
        }
        worker_alive_.store(false);
    };

    try {
        if (!api.load(config.dll_path, error)) {
            set_error(error);
            finish();
            return;
        }
        if (api.openUSB() != 0) {
            set_error(api_error("openUSB failed"));
            finish();
            return;
        }
        opened = true;
        device_open_.store(true);
        const int count = api.get_device_num();
        device_count_.store(count);
        if (count <= config.device) {
            set_error("Requested device index is not available");
            finish();
            return;
        }

        std::error_code directory_error;
        std::filesystem::create_directories(config.recording_directory, directory_error);
        if (directory_error) {
            set_error("Cannot create session directory: " + directory_error.message());
            finish();
            return;
        }
        const std::filesystem::path temp_path = config.recording_directory /
            ("." + config.recording_prefix + "_pending_" + session_timestamp() + ".f32.tmp");
        spool.open(temp_path, std::ios::binary | std::ios::trunc);
        if (!spool) {
            set_error("Cannot create pending session: " + temp_path.u8string());
            finish();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            session_temp_path_ = temp_path;
        }
        session_buffering_.store(true);

        const int conf_result = api.ad_continu_conf(
            config.device,
            config.oversample,
            config.range_index(),
            0,
            7,
            config.freq_argument(),
            0,
            0,
            0,
            0);
        if (conf_result != 0) {
            set_error(api_error("ad_continu_conf failed"));
            finish();
            return;
        }
        configured = true;
        streaming_.store(true);
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            started_at_ = std::chrono::steady_clock::now();
        }

        const int max_raw_floats = config.read_block_frames * kPhysicalChannels;
        std::vector<float> raw(static_cast<std::size_t>(max_raw_floats));

        while (!stop_requested_.load()) {
            const int available = api.Get_AdBuf_Size(config.device);
            if (available < 0) {
                set_error(api_error("Get_AdBuf_Size failed"));
                break;
            }
            current_backlog_floats_.store(available);
            update_max_backlog(available);

            const int requested = (std::min(available, max_raw_floats) / kPhysicalChannels)
                                * kPhysicalChannels;
            if (requested == 0) {
                if (config.poll_interval_ms > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
                else
                    std::this_thread::yield();
                continue;
            }

            const int read = api.Read_AdBuf(config.device, raw.data(), requested);
            if (read <= 0) {
                set_error(api_error("Read_AdBuf failed"));
                break;
            }
            if ((read % kPhysicalChannels) != 0) {
                set_error("Read_AdBuf returned a non-frame-aligned sample count");
                break;
            }

            const std::size_t frame_count = static_cast<std::size_t>(read / kPhysicalChannels);
            spool.write(reinterpret_cast<const char*>(raw.data()),
                        static_cast<std::streamsize>(read * sizeof(float)));
            if (!spool) {
                set_error("Pending session write failed: " + temp_path.u8string());
                break;
            }
            session_frames_.fetch_add(frame_count);
            history_.push_interleaved(raw.data(), frame_count);
            chunks_.fetch_add(1);
            frames_.fetch_add(frame_count);
            raw_floats_.fetch_add(static_cast<std::uint64_t>(read));
            last_chunk_frames_.store(static_cast<int>(frame_count));
        }
    } catch (const std::exception& ex) {
        set_error(ex.what());
    } catch (...) {
        set_error("Unknown acquisition exception");
    }

    finish();
}
