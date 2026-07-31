#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

enum class RecordingFormat { None, Float32, Wav, Csv };

struct DaqConfig {
    std::filesystem::path dll_path = "Usb_Daq_V52_Dll.dll";
    int device = 0;
    int sample_rate = 44100;
    int range_volts = 5;
    int oversample = 0;
    float history_seconds = 10.0f;
    float preview_seconds = 2.0f;
    int read_block_frames = 4096;
    int poll_interval_ms = 2;
    std::array<bool, 8> channels = {true, true, false, false, false, false, false, false};
    RecordingFormat recording_format = RecordingFormat::None;
    bool recording_all_channels = true;
    std::filesystem::path recording_directory = "shared";
    std::string recording_prefix = "usb_daq";

    int range_index() const { return range_volts == 10 ? 0 : 1; }
    int freq_argument() const { return sample_rate / 2; }
};

inline std::string recording_format_name(RecordingFormat format) {
    switch (format) {
        case RecordingFormat::Float32: return "f32";
        case RecordingFormat::Wav: return "wav";
        case RecordingFormat::Csv: return "csv";
        default: return "none";
    }
}

inline RecordingFormat parse_recording_format(const std::string& value) {
    if (value == "f32" || value == "float32" || value == "bin") return RecordingFormat::Float32;
    if (value == "wav" || value == "wave") return RecordingFormat::Wav;
    if (value == "csv") return RecordingFormat::Csv;
    return RecordingFormat::None;
}

inline int max_freq_for_oversample(int oversample) {
    static constexpr int limits[] = {100000, 100000, 50000, 25000, 12500, 6250, 3125};
    if (oversample < 0 || oversample > 6) return 0;
    return limits[oversample];
}

inline bool validate_config(DaqConfig& cfg, std::string& error) {
    if (cfg.device < 0) {
        error = "device must be >= 0";
        return false;
    }
    if (cfg.sample_rate < 200 || (cfg.sample_rate % 2) != 0) {
        error = "sampleRate must be an even integer >= 200";
        return false;
    }
    if (cfg.oversample < 0 || cfg.oversample > 6) {
        error = "oversample must be in [0, 6]";
        return false;
    }
    if (cfg.freq_argument() > max_freq_for_oversample(cfg.oversample)) {
        std::ostringstream oss;
        oss << "sampleRate/2=" << cfg.freq_argument()
            << " exceeds the oversample-mode limit " << max_freq_for_oversample(cfg.oversample);
        error = oss.str();
        return false;
    }
    if (cfg.range_volts != 5 && cfg.range_volts != 10) {
        error = "rangeVolts must be 5 or 10";
        return false;
    }
    cfg.history_seconds = std::clamp(cfg.history_seconds, 0.25f, 120.0f);
    cfg.preview_seconds = std::clamp(cfg.preview_seconds, 0.1f, cfg.history_seconds);
    cfg.read_block_frames = std::clamp(cfg.read_block_frames, 128, 65536);
    cfg.poll_interval_ms = std::clamp(cfg.poll_interval_ms, 0, 100);
    return true;
}

inline DaqConfig load_config(const std::filesystem::path& path, std::string& warning) {
    DaqConfig cfg;
    std::ifstream stream(path);
    if (!stream) {
        warning = "Config not found; using built-in defaults: " + path.u8string();
        return cfg;
    }

    try {
        std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const auto root = nlohmann::json::parse(text, nullptr, true, true);

        cfg.dll_path = root.value("dllPath", cfg.dll_path.u8string());
        cfg.device = root.value("device", cfg.device);
        cfg.sample_rate = root.value("sampleRate", cfg.sample_rate);
        cfg.range_volts = root.value("rangeVolts", cfg.range_volts);
        cfg.oversample = root.value("oversample", cfg.oversample);
        cfg.history_seconds = root.value("historySeconds", cfg.history_seconds);
        cfg.preview_seconds = root.value("previewSeconds", cfg.preview_seconds);
        cfg.read_block_frames = root.value("readBlockFrames", cfg.read_block_frames);
        cfg.poll_interval_ms = root.value("pollIntervalMs", cfg.poll_interval_ms);

        if (root.contains("channels") && root["channels"].is_array()) {
            const auto& values = root["channels"];
            for (std::size_t i = 0; i < cfg.channels.size() && i < values.size(); ++i)
                cfg.channels[i] = values[i].get<bool>();
        }

        if (root.contains("recording") && root["recording"].is_object()) {
            const auto& rec = root["recording"];
            cfg.recording_format = parse_recording_format(rec.value("format", "none"));
            cfg.recording_all_channels = rec.value("allChannels", cfg.recording_all_channels);
            cfg.recording_directory = rec.value("directory", cfg.recording_directory.u8string());
            cfg.recording_prefix = rec.value("prefix", cfg.recording_prefix);
        }
    } catch (const std::exception& ex) {
        warning = std::string("Config parse failed; using defaults: ") + ex.what();
        return DaqConfig{};
    }

    std::string validation_error;
    if (!validate_config(cfg, validation_error))
        warning = "Config validation: " + validation_error;
    return cfg;
}
