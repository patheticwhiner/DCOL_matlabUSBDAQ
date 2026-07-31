#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

static constexpr int kPhysicalChannels = 8;

struct HistorySnapshot {
    std::array<std::vector<float>, kPhysicalChannels> channels;
    std::size_t frames = 0;
    int sample_rate = 0;
    std::uint64_t first_frame_index = 0;
    std::uint64_t total_frames = 0;
};

struct HistoryInfo {
    std::size_t frames = 0;
    std::size_t capacity = 0;
    int sample_rate = 0;
    std::uint64_t total_frames = 0;
};

struct OverviewSnapshot {
    std::vector<double> x;
    std::array<std::vector<double>, kPhysicalChannels> mean;
    std::array<std::vector<double>, kPhysicalChannels> minimum;
    std::array<std::vector<double>, kPhysicalChannels> maximum;
    int sample_rate = 0;
    std::uint64_t total_frames = 0;
};

class RollingHistory {
public:
    void reset(int sample_rate, float seconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        sample_rate_ = sample_rate;
        capacity_ = std::max<std::size_t>(1, static_cast<std::size_t>(sample_rate * seconds));
        for (auto& channel : channels_) channel.assign(capacity_, 0.0f);
        write_pos_ = 0;
        size_ = 0;
        total_frames_ = 0;
        overview_.clear();
        overview_.reserve(kOverviewMaxBins);
        overview_pending_ = {};
        overview_bin_span_ = kOverviewBaseBinFrames;
    }

    void push_interleaved(const float* samples, std::size_t frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ == 0) return;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const float* values = samples + frame * kPhysicalChannels;
            push_overview_frame(values);
            for (int channel = 0; channel < kPhysicalChannels; ++channel)
                channels_[channel][write_pos_] = values[channel];
            write_pos_ = (write_pos_ + 1) % capacity_;
            size_ = std::min(size_ + 1, capacity_);
            ++total_frames_;
        }
    }

    HistoryInfo info() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return HistoryInfo{size_, capacity_, sample_rate_, total_frames_};
    }

    HistorySnapshot snapshot(const std::array<bool, kPhysicalChannels>& enabled,
                             std::size_t max_frames) const {
        std::lock_guard<std::mutex> lock(mutex_);
        HistorySnapshot result;
        result.sample_rate = sample_rate_;
        result.frames = std::min(size_, max_frames);
        result.total_frames = total_frames_;
        result.first_frame_index = total_frames_ - result.frames;
        if (result.frames == 0 || capacity_ == 0) return result;

        const std::size_t oldest = (write_pos_ + capacity_ - result.frames) % capacity_;
        for (int channel = 0; channel < kPhysicalChannels; ++channel) {
            if (!enabled[channel]) continue;
            auto& output = result.channels[channel];
            output.resize(result.frames);
            for (std::size_t i = 0; i < result.frames; ++i)
                output[i] = channels_[channel][(oldest + i) % capacity_];
        }
        return result;
    }

    OverviewSnapshot overview_snapshot(
        const std::array<bool, kPhysicalChannels>& enabled,
        double visible_x_min, double visible_x_max,
        std::size_t max_points = 4096) const {
        std::lock_guard<std::mutex> lock(mutex_);
        OverviewSnapshot result;
        result.sample_rate = sample_rate_;
        result.total_frames = total_frames_;
        if (sample_rate_ <= 0 || max_points == 0) return result;

        const bool has_pending = overview_pending_.frames > 0;
        const std::size_t source_count = overview_.size() + (has_pending ? 1u : 0u);
        if (source_count == 0) return result;

        auto source_at = [&](std::size_t index) -> const OverviewBin& {
            return index < overview_.size() ? overview_[index] : overview_pending_;
        };
        auto start_seconds = [&](const OverviewBin& bin) {
            return static_cast<double>(bin.first_frame) / sample_rate_;
        };
        auto end_seconds = [&](const OverviewBin& bin) {
            return static_cast<double>(bin.first_frame + bin.frames) / sample_rate_;
        };

        std::size_t first = 0;
        while (first < source_count && end_seconds(source_at(first)) < visible_x_min) ++first;
        std::size_t last = first;
        while (last < source_count && start_seconds(source_at(last)) <= visible_x_max) ++last;
        if (last <= first) return result;

        const std::size_t visible_bins = last - first;
        const std::size_t output_count = std::min(visible_bins, max_points);
        result.x.resize(output_count);
        for (int channel = 0; channel < kPhysicalChannels; ++channel) {
            if (!enabled[channel]) continue;
            result.mean[channel].resize(output_count);
            result.minimum[channel].resize(output_count);
            result.maximum[channel].resize(output_count);
        }

        for (std::size_t output = 0; output < output_count; ++output) {
            const std::size_t begin = first + (output * visible_bins) / output_count;
            const std::size_t end = first + ((output + 1) * visible_bins) / output_count;
            const OverviewBin& first_bin = source_at(begin);
            const OverviewBin& last_bin = source_at(std::max(begin + 1, end) - 1);
            result.x[output] = (static_cast<double>(first_bin.first_frame)
                              + static_cast<double>(last_bin.first_frame + last_bin.frames))
                             * 0.5 / sample_rate_;

            for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                if (!enabled[channel]) continue;
                double weighted_sum = 0.0;
                std::uint64_t frame_sum = 0;
                double minimum = first_bin.minimum[channel];
                double maximum = first_bin.maximum[channel];
                for (std::size_t index = begin; index < end; ++index) {
                    const OverviewBin& bin = source_at(index);
                    minimum = std::min(minimum, static_cast<double>(bin.minimum[channel]));
                    maximum = std::max(maximum, static_cast<double>(bin.maximum[channel]));
                    weighted_sum += static_cast<double>(bin.mean[channel]) * bin.frames;
                    frame_sum += bin.frames;
                }
                result.mean[channel][output] = weighted_sum / std::max<std::uint64_t>(1, frame_sum);
                result.minimum[channel][output] = minimum;
                result.maximum[channel][output] = maximum;
            }
        }
        return result;
    }

private:
    struct OverviewBin {
        std::uint64_t first_frame = 0;
        std::uint64_t frames = 0;
        std::array<float, kPhysicalChannels> mean{};
        std::array<float, kPhysicalChannels> minimum{};
        std::array<float, kPhysicalChannels> maximum{};
    };

    void push_overview_frame(const float* values) {
        if (overview_pending_.frames == 0) {
            overview_pending_.first_frame = total_frames_;
            overview_pending_.frames = 1;
            for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                overview_pending_.mean[channel] = values[channel];
                overview_pending_.minimum[channel] = values[channel];
                overview_pending_.maximum[channel] = values[channel];
            }
        } else {
            const std::uint64_t next_count = overview_pending_.frames + 1;
            for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                overview_pending_.mean[channel] +=
                    (values[channel] - overview_pending_.mean[channel])
                    / static_cast<float>(next_count);
                overview_pending_.minimum[channel] =
                    std::min(overview_pending_.minimum[channel], values[channel]);
                overview_pending_.maximum[channel] =
                    std::max(overview_pending_.maximum[channel], values[channel]);
            }
            overview_pending_.frames = next_count;
        }

        if (overview_pending_.frames >= overview_bin_span_) {
            overview_.push_back(overview_pending_);
            overview_pending_ = {};
            if (overview_.size() >= kOverviewMaxBins) compact_overview();
        }
    }

    void compact_overview() {
        const std::size_t pair_count = overview_.size() / 2;
        for (std::size_t pair = 0; pair < pair_count; ++pair) {
            const OverviewBin left = overview_[pair * 2];
            const OverviewBin right = overview_[pair * 2 + 1];
            OverviewBin combined;
            combined.first_frame = left.first_frame;
            combined.frames = left.frames + right.frames;
            for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                combined.minimum[channel] = std::min(left.minimum[channel], right.minimum[channel]);
                combined.maximum[channel] = std::max(left.maximum[channel], right.maximum[channel]);
                combined.mean[channel] = static_cast<float>(
                    (static_cast<double>(left.mean[channel]) * left.frames
                     + static_cast<double>(right.mean[channel]) * right.frames)
                    / combined.frames);
            }
            overview_[pair] = combined;
        }
        overview_.resize(pair_count);
        overview_bin_span_ *= 2;
    }

    static constexpr std::size_t kOverviewMaxBins = 65536;
    static constexpr std::uint64_t kOverviewBaseBinFrames = 64;
    mutable std::mutex mutex_;
    std::array<std::vector<float>, kPhysicalChannels> channels_;
    std::size_t capacity_ = 0;
    std::size_t write_pos_ = 0;
    std::size_t size_ = 0;
    std::uint64_t total_frames_ = 0;
    int sample_rate_ = 0;
    std::vector<OverviewBin> overview_;
    OverviewBin overview_pending_{};
    std::uint64_t overview_bin_span_ = kOverviewBaseBinFrames;
};
