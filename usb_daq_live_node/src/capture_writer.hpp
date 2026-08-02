#pragma once

#include "config.hpp"
#include "history.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

class CaptureWriter {
public:
    bool open(const DaqConfig& cfg, std::string& error) {
        close();
        format_ = cfg.recording_format;
        if (cfg.recording_all_channels) enabled_.fill(true);
        else enabled_ = cfg.channels;
        selected_count_ = 0;
        for (bool enabled : enabled_) selected_count_ += enabled ? 1u : 0u;
        sample_rate_ = cfg.sample_rate;
        if (format_ == RecordingFormat::None) return true;
        if (selected_count_ == 0) {
            error = "Recording requires at least one selected channel";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(cfg.recording_directory, ec);
        if (ec) {
            error = "Cannot create recording directory: " + ec.message();
            return false;
        }

        const std::string stamp = timestamp();
        const std::string extension = format_ == RecordingFormat::Csv ? ".csv"
                                      : format_ == RecordingFormat::Wav ? ".wav"
                                                                        : ".f32";
        path_ = cfg.recording_directory / (cfg.recording_prefix + "_" + stamp + extension);
        stream_.open(path_, std::ios::binary | std::ios::trunc);
        if (!stream_) {
            error = "Cannot open recording file: " + path_.u8string();
            return false;
        }

        if (format_ == RecordingFormat::Csv) {
            stream_ << "sample";
            for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                if (enabled_[channel]) stream_ << ",AIN" << channel + 1 << "_AD" << channel;
            }
            stream_ << '\n';
        } else if (format_ == RecordingFormat::Wav) {
            if (!write_wav_header(error)) {
                close();
                return false;
            }
            write_metadata(cfg);
        } else {
            write_metadata(cfg);
        }
        return true;
    }

    bool write(const float* interleaved, std::size_t frames, std::string& error) {
        if (format_ == RecordingFormat::None) return true;
        if (!stream_) {
            error = "Recording stream is not open";
            return false;
        }

        if (format_ == RecordingFormat::Csv) {
            stream_ << std::setprecision(9);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                stream_ << frames_written_ + frame;
                for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                    if (enabled_[channel])
                        stream_ << ',' << interleaved[frame * kPhysicalChannels + channel];
                }
                stream_ << '\n';
            }
        } else {
            const std::uint64_t block_bytes =
                static_cast<std::uint64_t>(frames) * selected_count_ * sizeof(float);
            if (format_ == RecordingFormat::Wav &&
                wav_data_bytes_ + block_bytes >
                    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 50u) {
                error = "WAV reached the 4 GiB RIFF limit; use continuous .f32 for longer captures";
                return false;
            }
            packed_.clear();
            packed_.reserve(frames * selected_count_);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                    if (enabled_[channel])
                        packed_.push_back(interleaved[frame * kPhysicalChannels + channel]);
                }
            }
            stream_.write(reinterpret_cast<const char*>(packed_.data()),
                          static_cast<std::streamsize>(packed_.size() * sizeof(float)));
            if (format_ == RecordingFormat::Wav) wav_data_bytes_ += block_bytes;
        }

        if (!stream_) {
            error = "Recording write failed: " + path_.u8string();
            return false;
        }
        frames_written_ += frames;
        return true;
    }

    void close() {
        if (stream_.is_open()) {
            if (format_ == RecordingFormat::Wav) finalize_wav_header();
            stream_.close();
        }
        packed_.clear();
        frames_written_ = 0;
        wav_data_bytes_ = 0;
        if (format_ == RecordingFormat::None) path_.clear();
    }

    const std::filesystem::path& path() const { return path_; }
    std::uint64_t frames_written() const { return frames_written_; }
    std::size_t channel_count() const { return selected_count_; }

private:
    static std::string timestamp() {
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

    void write_u16(std::uint16_t value) {
        const std::array<char, 2> bytes = {
            static_cast<char>(value & 0xffu),
            static_cast<char>((value >> 8u) & 0xffu)};
        stream_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    void write_u32(std::uint32_t value) {
        const std::array<char, 4> bytes = {
            static_cast<char>(value & 0xffu),
            static_cast<char>((value >> 8u) & 0xffu),
            static_cast<char>((value >> 16u) & 0xffu),
            static_cast<char>((value >> 24u) & 0xffu)};
        stream_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    bool write_wav_header(std::string& error) {
        const auto channels = static_cast<std::uint16_t>(selected_count_);
        const auto rate = static_cast<std::uint32_t>(sample_rate_);
        const auto block_align = static_cast<std::uint16_t>(channels * sizeof(float));
        const auto byte_rate = rate * block_align;

        stream_.write("RIFF", 4);
        write_u32(50);  // File size - 8; updated when recording stops.
        stream_.write("WAVE", 4);
        stream_.write("fmt ", 4);
        write_u32(18);
        write_u16(3);  // WAVE_FORMAT_IEEE_FLOAT
        write_u16(channels);
        write_u32(rate);
        write_u32(byte_rate);
        write_u16(block_align);
        write_u16(32);
        write_u16(0);  // WAVEFORMATEX cbSize
        stream_.write("fact", 4);
        write_u32(4);
        write_u32(0);  // Frame count; updated when recording stops.
        stream_.write("data", 4);
        write_u32(0);  // Payload size; updated when recording stops.
        if (!stream_) {
            error = "Cannot write WAV header: " + path_.u8string();
            return false;
        }
        return true;
    }

    void finalize_wav_header() {
        const auto data_bytes = static_cast<std::uint32_t>(wav_data_bytes_);
        const auto frames = static_cast<std::uint32_t>(
            selected_count_ == 0 ? 0 : wav_data_bytes_ / (selected_count_ * sizeof(float)));
        stream_.seekp(4, std::ios::beg);
        write_u32(50u + data_bytes);
        stream_.seekp(46, std::ios::beg);
        write_u32(frames);
        stream_.seekp(54, std::ios::beg);
        write_u32(data_bytes);
        stream_.seekp(0, std::ios::end);
        stream_.flush();
    }

    void write_metadata(const DaqConfig& cfg) {
        const std::filesystem::path metadata_path = path_.parent_path() / (path_.stem().u8string() + ".json");
        std::ofstream metadata(metadata_path, std::ios::trunc);
        if (!metadata) return;
        const bool wav = format_ == RecordingFormat::Wav;
        metadata << "{\n"
                 << "  \"format\": \""
                 << (wav ? "wav-ieee-float32-interleaved" : "float32-le-interleaved")
                 << "\",\n"
                 << "  \"sampleRate\": " << cfg.sample_rate << ",\n"
                 << "  \"rangeVolts\": " << cfg.range_volts << ",\n"
                 << "  \"units\": \"volts\",\n";
        if (wav) {
            metadata << "  \"wavSamplesAreRawVolts\": true,\n";
        }
        metadata
                 << "  \"continuousFullStream\": true,\n"
                 << "  \"sessionFromAcquisitionStart\": true,\n"
                 << "  \"channels\": [";
        bool first = true;
        for (int channel = 0; channel < kPhysicalChannels; ++channel) {
            if (!enabled_[channel]) continue;
            if (!first) metadata << ", ";
            metadata << "\"AIN" << channel + 1 << " (AD" << channel << ")\"";
            first = false;
        }
        metadata << "]\n}\n";
    }

    RecordingFormat format_ = RecordingFormat::None;
    std::array<bool, kPhysicalChannels> enabled_{};
    std::size_t selected_count_ = 0;
    int sample_rate_ = 0;
    std::filesystem::path path_;
    std::ofstream stream_;
    std::vector<float> packed_;
    std::uint64_t frames_written_ = 0;
    std::uint64_t wav_data_bytes_ = 0;
};
