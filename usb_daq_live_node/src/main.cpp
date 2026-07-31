#include "acquisition.hpp"
#include "config.hpp"
#include "history.hpp"

#include <windows.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct CliOptions {
    std::filesystem::path config_path;
    std::filesystem::path dll_override;
    bool headless = false;
    bool autostart = false;
    double duration_seconds = 5.0;
    int sample_rate_override = 0;
    std::string recording_override;
    std::filesystem::path output_directory_override;
    bool show_help = false;
};

std::filesystem::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const DWORD size = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}

void print_usage() {
    std::cout
        << "USB DAQ Live Node\n\n"
        << "Usage:\n"
        << "  usb_daq_live_node [--config daq_config.jsonc]\n"
        << "  usb_daq_live_node --headless --duration 3 [options]\n\n"
        << "Options:\n"
        << "  -c, --config <path>       JSONC configuration file\n"
        << "      --dll <path>          Override vendor DLL path\n"
        << "      --headless            Run acquisition without GUI\n"
        << "      --autostart           Start GUI capture immediately and exit after duration\n"
        << "      --duration <seconds>  Headless capture duration (default 5)\n"
        << "      --sample-rate <Hz>    Override physical-channel sample rate\n"
        << "      --record <format>     Export completed session: none, f32, wav, or csv\n"
        << "      --output-dir <path>   Temporary-session and export directory\n"
        << "  -h, --help                Show this help\n";
}

bool parse_cli(int argc, char** argv, CliOptions& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                error = std::string("Missing value for ") + name;
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") options.show_help = true;
        else if (arg == "--headless") options.headless = true;
        else if (arg == "--autostart") options.autostart = true;
        else if (arg == "-c" || arg == "--config") {
            const char* v = value(arg.c_str());
            if (!v) return false;
            options.config_path = v;
        } else if (arg == "--dll") {
            const char* v = value("--dll");
            if (!v) return false;
            options.dll_override = v;
        } else if (arg == "--duration") {
            const char* v = value("--duration");
            if (!v) return false;
            options.duration_seconds = std::strtod(v, nullptr);
        } else if (arg == "--sample-rate") {
            const char* v = value("--sample-rate");
            if (!v) return false;
            options.sample_rate_override = std::atoi(v);
        } else if (arg == "--record") {
            const char* v = value("--record");
            if (!v) return false;
            options.recording_override = v;
        } else if (arg == "--output-dir") {
            const char* v = value("--output-dir");
            if (!v) return false;
            options.output_directory_override = v;
        } else {
            error = "Unknown argument: " + arg;
            return false;
        }
    }
    if (options.duration_seconds <= 0.0) {
        error = "duration must be > 0";
        return false;
    }
    return true;
}

std::filesystem::path resolve_config_path(const CliOptions& options) {
    if (!options.config_path.empty()) return options.config_path;
    const std::filesystem::path local = "daq_config.jsonc";
    if (std::filesystem::exists(local)) return local;
    return executable_directory() / "daq_config.jsonc";
}

void resolve_relative_paths(DaqConfig& config, const std::filesystem::path& config_path) {
    const std::filesystem::path base = std::filesystem::absolute(config_path).parent_path();
    if (config.dll_path.is_relative()) {
        const std::filesystem::path beside_config = base / config.dll_path;
        const std::filesystem::path beside_exe = executable_directory() / config.dll_path;
        if (std::filesystem::exists(beside_config)) config.dll_path = beside_config;
        else config.dll_path = beside_exe;
    }
    if (config.recording_directory.is_relative())
        config.recording_directory = base / config.recording_directory;
}

int run_headless(DaqConfig config, double duration_seconds) {
    Acquisition acquisition;
    DaqConfig recording_config = config;
    config.recording_format = RecordingFormat::None;
    std::string error;
    if (!acquisition.start(config, error)) {
        std::cerr << "START_ERROR=" << error << '\n';
        return 2;
    }

    const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < startup_deadline) {
        const AcquisitionStats status = acquisition.stats();
        if (status.streaming) break;
        if (!status.worker_alive) {
            std::cerr << "START_ERROR=" << status.error << '\n';
            acquisition.stop();
            return 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    AcquisitionStats started = acquisition.stats();
    if (!started.streaming) {
        acquisition.stop();
        std::cerr << "START_ERROR=stream did not become ready\n";
        return 4;
    }
    std::cout << "OPEN=0 DEVICE_NUM=" << started.device_count
              << " SAMPLE_RATE=" << config.sample_rate
              << " FREQ_ARG=" << config.freq_argument()
              << " RANGE=+/-" << config.range_volts << "V"
              << " OVERSAMPLE=" << config.oversample << '\n';

    const auto end = std::chrono::steady_clock::now()
                   + std::chrono::duration<double>(duration_seconds);
    auto next_report = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const AcquisitionStats status = acquisition.stats();
        if (!status.worker_alive) break;
        if (std::chrono::steady_clock::now() >= next_report) {
            std::cout << "STREAM elapsed=" << status.elapsed_seconds
                      << " chunks=" << status.chunks
                      << " frames=" << status.frames
                      << " effective_fs=" << status.effective_sample_rate
                      << " backlog=" << status.current_backlog_floats << '\n';
            next_report += std::chrono::seconds(1);
        }
    }

    acquisition.stop();
    bool session_action_ok = false;
    if (recording_config.recording_format == RecordingFormat::None)
        session_action_ok = acquisition.discard_session(error);
    else
        session_action_ok = acquisition.export_session(recording_config, error);
    if (!session_action_ok)
        std::cerr << "SESSION_ERROR=" << error << '\n';
    const AcquisitionStats final = acquisition.stats();
    std::cout << "SUMMARY elapsed=" << final.elapsed_seconds
              << " chunks=" << final.chunks
              << " frames=" << final.frames
              << " raw_floats=" << final.raw_floats
              << " effective_fs=" << final.effective_sample_rate
              << " max_backlog=" << final.max_backlog_floats
              << " error=\"" << final.error << "\"";
    if (!final.export_path.empty())
        std::cout << " recording=\"" << final.export_path << "\"";
    std::cout << '\n';
    return final.frames > 0 && final.error.empty() && session_action_ok ? 0 : 5;
}

const std::array<ImVec4, kPhysicalChannels> kChannelColors = {
    ImVec4{0.10f, 0.45f, 0.86f, 1.00f},
    ImVec4{0.95f, 0.40f, 0.12f, 1.00f},
    ImVec4{0.15f, 0.64f, 0.34f, 1.00f},
    ImVec4{0.60f, 0.30f, 0.82f, 1.00f},
    ImVec4{0.88f, 0.66f, 0.08f, 1.00f},
    ImVec4{0.04f, 0.65f, 0.68f, 1.00f},
    ImVec4{0.86f, 0.22f, 0.36f, 1.00f},
    ImVec4{0.38f, 0.45f, 0.58f, 1.00f}
};

enum class ViewMode { FitAll = 0, Follow = 1, Free = 2 };
enum class PlotRenderMode { AdaptiveLine = 0, MinMaxEnvelope = 1 };

float g_ui_scale = 1.0f;
float scaled(float value) { return value * g_ui_scale; }

struct StableYAxis {
    double minimum = -0.05;
    double maximum = 0.05;
    bool initialized = false;
};

bool measure_visible_y_range(const HistorySnapshot& snapshot,
                             const std::array<bool, kPhysicalChannels>& enabled,
                             double visible_x_min, double visible_x_max,
                             PlotRenderMode render_mode,
                             double& measured_min, double& measured_max) {
    if (snapshot.frames == 0 || snapshot.sample_rate <= 0) return false;
    bool found = false;
    measured_min = 0.0;
    measured_max = 0.0;
    auto include_value = [&](double value) {
        if (!found) {
            measured_min = measured_max = value;
            found = true;
        } else {
            measured_min = std::min(measured_min, value);
            measured_max = std::max(measured_max, value);
        }
    };

    const double data_x_min = static_cast<double>(snapshot.first_frame_index)
                            / snapshot.sample_rate;
    for (int channel = 0; channel < kPhysicalChannels; ++channel) {
        if (!enabled[channel] || snapshot.channels[channel].empty()) continue;
        const auto& data = snapshot.channels[channel];
        const double data_x_max = static_cast<double>(snapshot.first_frame_index + data.size())
                                / snapshot.sample_rate;
        const double clipped_min = std::max(data_x_min, visible_x_min);
        const double clipped_max = std::min(data_x_max, visible_x_max);
        if (clipped_max <= clipped_min) continue;

        const std::size_t begin = std::min<std::size_t>(
            data.size() - 1,
            static_cast<std::size_t>(
                std::max(0.0, std::floor((clipped_min - data_x_min) * snapshot.sample_rate))));
        const std::size_t end = std::min<std::size_t>(
            data.size(),
            std::max(begin + 1,
                     static_cast<std::size_t>(
                         std::ceil((clipped_max - data_x_min) * snapshot.sample_rate))));
        if (render_mode == PlotRenderMode::AdaptiveLine) {
            const std::size_t visible_count = end - begin;
            const std::size_t buckets = std::min<std::size_t>(visible_count, 2048);
            for (std::size_t bucket = 0; bucket < buckets; ++bucket) {
                const std::size_t bucket_begin = begin + (bucket * visible_count) / buckets;
                const std::size_t bucket_end = begin + ((bucket + 1) * visible_count) / buckets;
                double sum = 0.0;
                for (std::size_t index = bucket_begin; index < bucket_end; ++index)
                    sum += data[index];
                include_value(sum / static_cast<double>(bucket_end - bucket_begin));
            }
        } else {
            const std::size_t step = std::max<std::size_t>(1, (end - begin) / 12000);
            for (std::size_t index = begin; index < end; index += step)
                include_value(data[index]);
            include_value(data[end - 1]);
        }
    }
    return found;
}

bool measure_overview_y_range(
    const OverviewSnapshot& snapshot,
    const std::array<bool, kPhysicalChannels>& enabled,
    PlotRenderMode render_mode,
    double& measured_min, double& measured_max) {
    bool found = false;
    for (int channel = 0; channel < kPhysicalChannels; ++channel) {
        if (!enabled[channel]) continue;
        const auto& low = render_mode == PlotRenderMode::AdaptiveLine
            ? snapshot.mean[channel] : snapshot.minimum[channel];
        const auto& high = render_mode == PlotRenderMode::AdaptiveLine
            ? snapshot.mean[channel] : snapshot.maximum[channel];
        for (std::size_t index = 0; index < low.size() && index < high.size(); ++index) {
            if (!found) {
                measured_min = low[index];
                measured_max = high[index];
                found = true;
            } else {
                measured_min = std::min(measured_min, low[index]);
                measured_max = std::max(measured_max, high[index]);
            }
        }
    }
    return found;
}

void target_y_range(double measured_min, double measured_max, double minimum_span,
                    double& target_min, double& target_max) {
    const double center = (measured_min + measured_max) * 0.5;
    const double span = std::max(minimum_span, (measured_max - measured_min) * 1.24);
    target_min = center - span * 0.5;
    target_max = center + span * 0.5;
}

void update_stable_y_axis(StableYAxis& axis, double measured_min, double measured_max,
                          double minimum_span, double delta_seconds) {
    double target_min = 0.0;
    double target_max = 0.0;
    target_y_range(measured_min, measured_max, minimum_span, target_min, target_max);
    if (!axis.initialized) {
        axis.minimum = target_min;
        axis.maximum = target_max;
        axis.initialized = true;
        return;
    }

    const double current_span = std::max(1e-9, axis.maximum - axis.minimum);
    const double hysteresis = current_span * 0.08;
    const double fast_alpha = 1.0 - std::exp(-delta_seconds / 0.06);
    const double slow_alpha = 1.0 - std::exp(-delta_seconds / 2.2);

    if (target_min < axis.minimum)
        axis.minimum += (target_min - axis.minimum) * fast_alpha;
    else if (target_min > axis.minimum + hysteresis)
        axis.minimum += (target_min - axis.minimum) * slow_alpha;

    if (target_max > axis.maximum)
        axis.maximum += (target_max - axis.maximum) * fast_alpha;
    else if (target_max < axis.maximum - hysteresis)
        axis.maximum += (target_max - axis.maximum) * slow_alpha;
}

void apply_style_geometry(ImGuiStyle& style) {
    style.WindowRounding = 0.0f;
    style.ChildRounding = scaled(8.0f);
    style.FrameRounding = scaled(5.0f);
    style.GrabRounding = scaled(5.0f);
    style.ScrollbarRounding = scaled(6.0f);
    style.TabRounding = scaled(5.0f);
    style.PopupRounding = scaled(6.0f);
    style.WindowPadding = ImVec2(scaled(14.0f), scaled(14.0f));
    style.FramePadding = ImVec2(scaled(9.0f), scaled(6.0f));
    style.ItemSpacing = ImVec2(scaled(9.0f), scaled(7.0f));
    style.ItemInnerSpacing = ImVec2(scaled(6.0f), scaled(5.0f));
    style.ScrollbarSize = scaled(11.0f);
    style.GrabMinSize = scaled(9.0f);
}

void apply_light_theme() {
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    apply_style_geometry(style);
    ImVec4* color = style.Colors;
    color[ImGuiCol_WindowBg] = {0.945f, 0.957f, 0.976f, 1.00f};
    color[ImGuiCol_ChildBg] = {0.995f, 0.997f, 1.000f, 1.00f};
    color[ImGuiCol_PopupBg] = {1.000f, 1.000f, 1.000f, 0.98f};
    color[ImGuiCol_Border] = {0.78f, 0.81f, 0.87f, 1.00f};
    color[ImGuiCol_BorderShadow] = {0.00f, 0.00f, 0.00f, 0.00f};
    color[ImGuiCol_Text] = {0.10f, 0.13f, 0.19f, 1.00f};
    color[ImGuiCol_TextDisabled] = {0.45f, 0.49f, 0.58f, 1.00f};
    color[ImGuiCol_FrameBg] = {0.91f, 0.93f, 0.97f, 1.00f};
    color[ImGuiCol_FrameBgHovered] = {0.84f, 0.89f, 0.98f, 1.00f};
    color[ImGuiCol_FrameBgActive] = {0.74f, 0.83f, 0.97f, 1.00f};
    color[ImGuiCol_TitleBg] = {0.91f, 0.93f, 0.97f, 1.00f};
    color[ImGuiCol_TitleBgActive] = {0.84f, 0.89f, 0.98f, 1.00f};
    color[ImGuiCol_ScrollbarBg] = {0.92f, 0.94f, 0.97f, 0.65f};
    color[ImGuiCol_ScrollbarGrab] = {0.66f, 0.70f, 0.79f, 1.00f};
    color[ImGuiCol_ScrollbarGrabHovered] = {0.54f, 0.61f, 0.75f, 1.00f};
    color[ImGuiCol_ScrollbarGrabActive] = {0.19f, 0.47f, 0.86f, 1.00f};
    color[ImGuiCol_CheckMark] = {0.10f, 0.43f, 0.86f, 1.00f};
    color[ImGuiCol_SliderGrab] = {0.12f, 0.47f, 0.88f, 1.00f};
    color[ImGuiCol_SliderGrabActive] = {0.08f, 0.36f, 0.76f, 1.00f};
    color[ImGuiCol_Button] = {0.88f, 0.91f, 0.96f, 1.00f};
    color[ImGuiCol_ButtonHovered] = {0.75f, 0.83f, 0.97f, 1.00f};
    color[ImGuiCol_ButtonActive] = {0.61f, 0.74f, 0.95f, 1.00f};
    color[ImGuiCol_Header] = {0.69f, 0.80f, 0.97f, 0.55f};
    color[ImGuiCol_HeaderHovered] = {0.57f, 0.73f, 0.98f, 0.78f};
    color[ImGuiCol_HeaderActive] = {0.45f, 0.64f, 0.95f, 1.00f};
    color[ImGuiCol_Separator] = {0.78f, 0.81f, 0.87f, 1.00f};

    ImPlotStyle& plot = ImPlot::GetStyle();
    plot.PlotPadding = ImVec2(scaled(12.0f), scaled(10.0f));
    plot.LabelPadding = ImVec2(scaled(7.0f), scaled(5.0f));
    plot.LegendPadding = ImVec2(scaled(9.0f), scaled(9.0f));
    plot.Colors[ImPlotCol_PlotBg] = {0.995f, 0.997f, 1.000f, 1.0f};
    plot.Colors[ImPlotCol_PlotBorder] = {0.78f, 0.81f, 0.87f, 1.0f};
    plot.Colors[ImPlotCol_AxisGrid] = {0.76f, 0.79f, 0.86f, 0.52f};
    plot.Colors[ImPlotCol_AxisText] = {0.35f, 0.39f, 0.48f, 1.0f};
    plot.Colors[ImPlotCol_AxisTick] = {0.56f, 0.60f, 0.68f, 1.0f};
    plot.Colors[ImPlotCol_LegendBg] = {1.00f, 1.00f, 1.00f, 0.92f};
    plot.Colors[ImPlotCol_LegendBorder] = {0.78f, 0.81f, 0.87f, 1.0f};
}

void apply_dark_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    apply_style_geometry(style);
    ImVec4* color = style.Colors;
    color[ImGuiCol_WindowBg] = {0.075f, 0.086f, 0.11f, 1.00f};
    color[ImGuiCol_ChildBg] = {0.105f, 0.12f, 0.15f, 1.00f};
    color[ImGuiCol_PopupBg] = {0.105f, 0.12f, 0.15f, 0.98f};
    color[ImGuiCol_Border] = {0.22f, 0.25f, 0.32f, 1.00f};
    color[ImGuiCol_Text] = {0.90f, 0.92f, 0.96f, 1.00f};
    color[ImGuiCol_TextDisabled] = {0.48f, 0.52f, 0.62f, 1.00f};
    color[ImGuiCol_FrameBg] = {0.15f, 0.17f, 0.22f, 1.00f};
    color[ImGuiCol_FrameBgHovered] = {0.20f, 0.25f, 0.35f, 1.00f};
    color[ImGuiCol_FrameBgActive] = {0.18f, 0.30f, 0.49f, 1.00f};
    color[ImGuiCol_CheckMark] = {0.30f, 0.65f, 1.00f, 1.00f};
    color[ImGuiCol_SliderGrab] = {0.28f, 0.58f, 0.92f, 1.00f};
    color[ImGuiCol_SliderGrabActive] = {0.36f, 0.68f, 1.00f, 1.00f};
    color[ImGuiCol_Button] = {0.18f, 0.21f, 0.28f, 1.00f};
    color[ImGuiCol_ButtonHovered] = {0.25f, 0.35f, 0.55f, 1.00f};
    color[ImGuiCol_ButtonActive] = {0.20f, 0.43f, 0.76f, 1.00f};
    color[ImGuiCol_Header] = {0.20f, 0.31f, 0.52f, 0.60f};
    color[ImGuiCol_HeaderHovered] = {0.25f, 0.40f, 0.65f, 0.82f};
    color[ImGuiCol_HeaderActive] = {0.22f, 0.44f, 0.74f, 1.00f};
    color[ImGuiCol_Separator] = {0.22f, 0.25f, 0.32f, 1.00f};

    ImPlotStyle& plot = ImPlot::GetStyle();
    plot.PlotPadding = ImVec2(scaled(12.0f), scaled(10.0f));
    plot.LabelPadding = ImVec2(scaled(7.0f), scaled(5.0f));
    plot.LegendPadding = ImVec2(scaled(9.0f), scaled(9.0f));
    plot.Colors[ImPlotCol_PlotBg] = {0.09f, 0.10f, 0.13f, 1.0f};
    plot.Colors[ImPlotCol_PlotBorder] = {0.22f, 0.25f, 0.32f, 1.0f};
    plot.Colors[ImPlotCol_AxisGrid] = {0.25f, 0.28f, 0.36f, 0.60f};
    plot.Colors[ImPlotCol_AxisText] = {0.62f, 0.66f, 0.75f, 1.0f};
    plot.Colors[ImPlotCol_AxisTick] = {0.42f, 0.46f, 0.55f, 1.0f};
    plot.Colors[ImPlotCol_LegendBg] = {0.11f, 0.12f, 0.16f, 0.92f};
    plot.Colors[ImPlotCol_LegendBorder] = {0.22f, 0.25f, 0.32f, 1.0f};
}

ImFont* load_font(ImGuiIO& io, float size) {
    const char* font_paths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/tahoma.ttf",
        nullptr
    };
    for (int index = 0; font_paths[index] != nullptr; ++index) {
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(font_paths[index], size)) return font;
    }
    ImFontConfig fallback;
    fallback.SizePixels = size;
    return io.Fonts->AddFontDefault(&fallback);
}

void draw_section_title(const char* title, const char* subtitle = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.43f, 0.84f, 1.0f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (subtitle != nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", subtitle);
    }
    ImGui::Separator();
}

void draw_channel_selector(int channel, bool& selected) {
    char color_id[32];
    char label[32];
    std::snprintf(color_id, sizeof(color_id), "##channel_color_%d", channel);
    std::snprintf(label, sizeof(label), "AIN%d (AD%d)", channel + 1, channel);
    ImGui::ColorButton(color_id, kChannelColors[channel],
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                       ImVec2(scaled(12.0f), scaled(12.0f)));
    ImGui::SameLine();
    ImGui::Checkbox(label, &selected);
}

void draw_waveform(const char* label, const std::vector<float>& data, int sample_rate,
                   std::uint64_t first_frame_index, double visible_x_min,
                   double visible_x_max, float plot_width, const ImVec4& color,
                   PlotRenderMode render_mode) {
    if (data.empty() || sample_rate <= 0) return;

    const double data_x_min = static_cast<double>(first_frame_index) / sample_rate;
    const double data_x_max = static_cast<double>(first_frame_index + data.size()) / sample_rate;
    const double clipped_x_min = std::max(data_x_min, visible_x_min);
    const double clipped_x_max = std::min(data_x_max, visible_x_max);
    if (clipped_x_max <= clipped_x_min) return;

    const std::size_t visible_begin = std::min<std::size_t>(
        data.size() - 1,
        static_cast<std::size_t>(
            std::max(0.0, std::floor((clipped_x_min - data_x_min) * sample_rate))));
    const std::size_t visible_end = std::min<std::size_t>(
        data.size(),
        std::max(visible_begin + 1,
                 static_cast<std::size_t>(
                     std::ceil((clipped_x_max - data_x_min) * sample_rate))));
    const std::size_t visible_count = visible_end - visible_begin;
    const std::size_t bucket_count = std::min<std::size_t>(
        visible_count,
        static_cast<std::size_t>(std::clamp(plot_width * 1.5f, 256.0f, 4096.0f)));
    const double samples_per_bucket = static_cast<double>(visible_count) / bucket_count;

    std::vector<double> x(bucket_count);
    std::vector<double> primary(bucket_count);
    std::vector<double> low;
    if (render_mode == PlotRenderMode::MinMaxEnvelope) low.resize(bucket_count);

    for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
        const std::size_t begin = visible_begin
            + static_cast<std::size_t>(bucket * samples_per_bucket);
        const std::size_t end = std::min(
            visible_end,
            std::max(begin + 1,
                     visible_begin
                         + static_cast<std::size_t>((bucket + 1) * samples_per_bucket)));
        float minimum = data[begin];
        float maximum = data[begin];
        double sum = data[begin];
        for (std::size_t index = begin + 1; index < end; ++index) {
            minimum = std::min(minimum, data[index]);
            maximum = std::max(maximum, data[index]);
            sum += data[index];
        }

        x[bucket] = (static_cast<double>(first_frame_index)
                   + static_cast<double>(begin + end) * 0.5) / sample_rate;
        if (render_mode == PlotRenderMode::AdaptiveLine)
            primary[bucket] = sum / static_cast<double>(end - begin);
        else {
            low[bucket] = minimum;
            primary[bucket] = maximum;
        }
    }

    if (render_mode == PlotRenderMode::AdaptiveLine) {
        ImPlot::SetNextLineStyle(color, 1.25f);
        ImPlot::PlotLine(label, x.data(), primary.data(), static_cast<int>(bucket_count));
        return;
    }

    ImPlot::SetNextFillStyle(color, 0.13f);
    ImPlot::PlotShaded(label, x.data(), low.data(), primary.data(), static_cast<int>(bucket_count));
    ImPlot::SetNextLineStyle(color, 1.0f);
    const std::string high_label = std::string("##") + label + "_high";
    ImPlot::PlotLine(high_label.c_str(), x.data(), primary.data(), static_cast<int>(bucket_count));
    ImPlot::SetNextLineStyle(color, 1.0f);
    const std::string low_label = std::string("##") + label + "_low";
    ImPlot::PlotLine(low_label.c_str(), x.data(), low.data(), static_cast<int>(bucket_count));
}

void draw_overview_waveform(const char* label, const OverviewSnapshot& snapshot,
                            int channel, const ImVec4& color,
                            PlotRenderMode render_mode) {
    if (channel < 0 || channel >= kPhysicalChannels || snapshot.x.empty()) return;
    const int count = static_cast<int>(snapshot.x.size());
    if (render_mode == PlotRenderMode::AdaptiveLine) {
        if (snapshot.mean[channel].size() != snapshot.x.size()) return;
        ImPlot::SetNextLineStyle(color, 1.25f);
        ImPlot::PlotLine(label, snapshot.x.data(), snapshot.mean[channel].data(), count);
        return;
    }
    if (snapshot.minimum[channel].size() != snapshot.x.size()
        || snapshot.maximum[channel].size() != snapshot.x.size()) return;
    ImPlot::SetNextFillStyle(color, 0.13f);
    ImPlot::PlotShaded(label, snapshot.x.data(), snapshot.minimum[channel].data(),
                       snapshot.maximum[channel].data(), count);
    ImPlot::SetNextLineStyle(color, 1.0f);
    const std::string high_label = std::string("##") + label + "_overview_high";
    ImPlot::PlotLine(high_label.c_str(), snapshot.x.data(),
                     snapshot.maximum[channel].data(), count);
    ImPlot::SetNextLineStyle(color, 1.0f);
    const std::string low_label = std::string("##") + label + "_overview_low";
    ImPlot::PlotLine(low_label.c_str(), snapshot.x.data(),
                     snapshot.minimum[channel].data(), count);
}

int run_gui(DaqConfig config, const std::string& config_warning,
            bool autostart, double autostart_duration) {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 10;
    }

    int display_index = 0;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
    const int display_count = std::max(1, SDL_GetNumVideoDisplays());
    for (int index = 0; index < display_count; ++index) {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(index, &bounds) == 0
            && mouse_x >= bounds.x && mouse_x < bounds.x + bounds.w
            && mouse_y >= bounds.y && mouse_y < bounds.y + bounds.h) {
            display_index = index;
            break;
        }
    }

    SDL_Rect display_bounds{};
    SDL_Rect usable_bounds{};
    SDL_GetDisplayBounds(display_index, &display_bounds);
    SDL_GetDisplayUsableBounds(display_index, &usable_bounds);
    float diagonal_dpi = 96.0f;
    float horizontal_dpi = 96.0f;
    float vertical_dpi = 96.0f;
    if (SDL_GetDisplayDPI(display_index, &diagonal_dpi, &horizontal_dpi, &vertical_dpi) != 0)
        diagonal_dpi = horizontal_dpi = vertical_dpi = 96.0f;
    g_ui_scale = std::clamp(diagonal_dpi / 96.0f, 1.0f, 2.25f);

    std::ostringstream display_summary_stream;
    display_summary_stream << (SDL_GetDisplayName(display_index) ? SDL_GetDisplayName(display_index)
                                                               : "Display")
                           << "  " << display_bounds.w << 'x' << display_bounds.h
                           << "  work " << usable_bounds.w << 'x' << usable_bounds.h
                           << "  " << static_cast<int>(std::lround(diagonal_dpi)) << " DPI"
                           << "  UI " << std::fixed << std::setprecision(2) << g_ui_scale << 'x';
    const std::string display_summary = display_summary_stream.str();
    std::cout << "DISPLAY index=" << display_index
              << " bounds=" << display_bounds.w << 'x' << display_bounds.h
              << " usable=" << usable_bounds.w << 'x' << usable_bounds.h
              << " dpi=" << diagonal_dpi
              << " ui_scale=" << g_ui_scale << '\n';

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const int window_width = std::max(1000, static_cast<int>(usable_bounds.w * 0.82f));
    const int window_height = std::max(700, static_cast<int>(usable_bounds.h * 0.82f));

    SDL_Window* window = SDL_CreateWindow(
        "USB DAQ Live Node",
        SDL_WINDOWPOS_CENTERED_DISPLAY(display_index),
        SDL_WINDOWPOS_CENTERED_DISPLAY(display_index),
        window_width,
        window_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 11;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->TexDesiredWidth = 2048;
    ImFont* body_font = load_font(io, scaled(16.0f));
    ImFont* title_font = load_font(io, scaled(22.0f));
    io.FontDefault = body_font;
    bool dark_theme = false;
    apply_light_theme();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    Acquisition acquisition;
    bool done = false;
    int y_axis_choice = 2; // 0=stable auto, 1=instant auto, 2=auto once then lock
    float minimum_y_span = 0.002f;
    StableYAxis stable_y_axis;
    double locked_y_min = -0.05;
    double locked_y_max = 0.05;
    bool lock_y_pending = true;
    bool auto_lock_settling = false;
    std::chrono::steady_clock::time_point auto_lock_deadline{};
    std::uint64_t previous_y_session_frames = 0;
    int view_choice = static_cast<int>(ViewMode::FitAll);
    int render_choice = static_cast<int>(PlotRenderMode::AdaptiveLine);
    double view_x_min = 0.0;
    double view_x_max = config.preview_seconds;
    double expected_x_min = 0.0;
    double expected_x_max = config.preview_seconds;
    bool expected_axis_valid = false;
    bool view_mode_changed = false;
    std::string ui_message = config_warning;
    bool keep_session_choice = config.recording_format != RecordingFormat::None;
    int recording_choice = config.recording_format == RecordingFormat::Float32 ? 0
                         : config.recording_format == RecordingFormat::Csv ? 2 : 1;
    std::chrono::steady_clock::time_point automatic_exit{};
    if (autostart) {
        std::string error;
        if (!acquisition.start(config, error)) ui_message = error;
        else automatic_exit = std::chrono::steady_clock::now()
                            + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  std::chrono::duration<double>(autostart_duration));
    }

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) done = true;
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0
                && event.key.keysym.sym == SDLK_f && !io.WantTextInput) {
                view_choice = (view_choice + 1) % 3;
                view_mode_changed = true;
                expected_axis_valid = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int display_width = 0;
        int display_height = 0;
        SDL_GetWindowSize(window, &display_width, &display_height);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(display_width),
                                       static_cast<float>(display_height)));
        ImGui::Begin("USB DAQ Live Node", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

        const AcquisitionStats status = acquisition.stats();
        if (automatic_exit != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() >= automatic_exit) {
            done = true;
        }
        ImGui::BeginChild("TopBar", ImVec2(0, scaled(66.0f)), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushFont(title_font);
        ImGui::TextUnformatted("USB DAQ Live Node");
        ImGui::PopFont();
        const char* theme_label = dark_theme ? "Light mode" : "Dark mode";
        const float theme_width = ImGui::CalcTextSize(theme_label).x + scaled(28.0f);
        ImGui::SameLine(ImGui::GetWindowWidth() - theme_width - scaled(14.0f));
        if (ImGui::Button(theme_label, ImVec2(theme_width, scaled(30.0f)))) {
            dark_theme = !dark_theme;
            dark_theme ? apply_dark_theme() : apply_light_theme();
        }
        const ImVec4 status_color = status.streaming
            ? ImVec4(0.10f, 0.65f, 0.35f, 1.0f)
            : ImVec4(0.52f, 0.56f, 0.64f, 1.0f);
        ImGui::TextColored(status_color, "%s", status.streaming ? "LIVE" : "IDLE");
        if (status.session_buffering && status.worker_alive) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.90f, 0.48f, 0.12f, 1.0f), "BUFFERING FROM START");
        } else if (status.session_available) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.10f, 0.62f, 0.34f, 1.0f), "SESSION READY");
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "USB DAQ V5.2L  |  %d device%s  |  AIN1 (AD0) - AIN8 (AD7)  |  %s",
            status.device_count, status.device_count == 1 ? "" : "s",
            display_summary.c_str());
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0.0f, scaled(3.0f)));
        ImGui::BeginChild("Sidebar", ImVec2(scaled(420.0f), 0), true);
        draw_section_title("Acquisition", "hardware settings");

        ImGui::BeginDisabled(status.worker_alive);
        if (ImGui::BeginTable("AcquisitionSettings", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, scaled(132.0f));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Sample rate");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputInt("##sample_rate", &config.sample_rate, 100, 1000);

            const char* ranges[] = {"+/-5 V", "+/-10 V"};
            int range_choice = config.range_volts == 10 ? 1 : 0;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Input range");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##input_range", &range_choice, ranges, 2))
                config.range_volts = range_choice == 1 ? 10 : 5;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Oversample");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderInt("##oversample", &config.oversample, 0, 6);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("History");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputFloat("##history", &config.history_seconds, 0.5f, 2.0f, "%.1f s");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Read block");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputInt("##read_block", &config.read_block_frames, 128, 1024);

            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        draw_section_title("Display");
        if (ImGui::BeginTable("DisplaySettings", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, scaled(132.0f));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Time view [F]");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            const char* view_items[] = {
                "Fit all (full history)",
                "Follow (fixed window)",
                "Free pan / zoom"
            };
            if (ImGui::Combo("##time_view", &view_choice, view_items, 3)) {
                view_mode_changed = true;
                expected_axis_valid = false;
            }

            if (view_choice == static_cast<int>(ViewMode::Follow)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Follow window");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##preview", &config.preview_seconds, 0.1f,
                                   std::max(0.1f, config.history_seconds), "%.1f s");
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Plot style");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            const char* render_items[] = {"Adaptive smooth line", "Min/max envelope"};
            ImGui::Combo("##plot_style", &render_choice, render_items, 2);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Smooth affects preview only. Recorded samples remain unchanged.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Y axis");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            const char* y_axis_items[] = {
                "Stable auto (fast / slow)",
                "Instant auto",
                "Auto once, then lock"
            };
            if (ImGui::Combo("##y_axis_mode", &y_axis_choice, y_axis_items, 3)) {
                if (y_axis_choice == 0) stable_y_axis.initialized = false;
                if (y_axis_choice == 2) {
                    stable_y_axis.initialized = false;
                    lock_y_pending = true;
                    auto_lock_settling = false;
                }
            }

            if (y_axis_choice == 0 || y_axis_choice == 2) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Auto min span");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputFloat("##minimum_y_span", &minimum_y_span,
                                      0.001f, 0.010f, "%.4f V")) {
                    minimum_y_span = std::clamp(minimum_y_span, 0.0001f, 20.0f);
                    stable_y_axis.initialized = false;
                    if (y_axis_choice == 2) lock_y_pending = true;
                }
            }
            if (y_axis_choice == 2) {
                double locked_center = (locked_y_min + locked_y_max) * 0.5;
                double locked_span = std::max(0.0001, locked_y_max - locked_y_min);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Locked center");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputDouble("##locked_center", &locked_center,
                                       0.001, 0.010, "%.5f V")) {
                    locked_y_min = locked_center - locked_span * 0.5;
                    locked_y_max = locked_center + locked_span * 0.5;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Locked span");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputDouble("##locked_span", &locked_span,
                                       0.001, 0.010, "%.5f V")) {
                    locked_span = std::clamp(locked_span, 0.0001, 20.0);
                    locked_y_min = locked_center - locked_span * 0.5;
                    locked_y_max = locked_center + locked_span * 0.5;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("");
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Re-adapt and lock", ImVec2(-1.0f, 0.0f))) {
                    stable_y_axis.initialized = false;
                    lock_y_pending = true;
                    auto_lock_settling = false;
                }
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        draw_section_title("Channels", "physical (software)");
        if (ImGui::BeginTable("ChannelSelection", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                ImGui::TableNextColumn();
                draw_channel_selector(channel, config.channels[channel]);
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        const float action_gap = ImGui::GetStyle().ItemSpacing.x;
        const float action_width = (ImGui::GetContentRegionAvail().x - action_gap) * 0.5f;
        if (!status.worker_alive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.43f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.50f, 0.94f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.07f, 0.35f, 0.74f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            if (ImGui::Button("Start acquisition", ImVec2(action_width, scaled(38.0f)))) {
                std::string error;
                DaqConfig preview_config = config;
                preview_config.recording_format = RecordingFormat::None;
                if (!acquisition.start(preview_config, error)) ui_message = error;
                else ui_message.clear();
            }
            ImGui::PopStyleColor(4);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.88f, 0.30f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.36f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            if (ImGui::Button("Stop acquisition", ImVec2(action_width, scaled(38.0f)))) {
                acquisition.stop();
                ui_message = "Acquisition stopped. Save or discard the complete buffered session below.";
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(action_width, scaled(38.0f)))) {
            if (status.worker_alive) {
                acquisition.stop();
                ui_message = "Acquisition stopped. Save or discard the complete buffered session before quitting.";
            } else if (status.session_available) {
                ui_message = "Save or discard the complete buffered session before quitting.";
            } else {
                done = true;
            }
        }

        ImGui::Spacing();
        const bool has_buffered_session = status.session_buffering || status.session_available;
        const float session_panel_height = !has_buffered_session ? 112.0f
                                         : keep_session_choice ? 228.0f : 164.0f;
        ImGui::BeginChild("RecordingPanel", ImVec2(0, scaled(session_panel_height)),
                          true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::BeginDisabled(!has_buffered_session);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.90f, 0.24f, 0.18f, 1.0f));
        if (ImGui::Checkbox("##keep_complete_session", &keep_session_choice)) {
            static constexpr RecordingFormat formats[] = {
                RecordingFormat::Float32, RecordingFormat::Wav, RecordingFormat::Csv};
            config.recording_format = keep_session_choice
                                    ? formats[std::clamp(recording_choice, 0, 2)]
                                    : RecordingFormat::None;
        }
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (keep_session_choice) {
            ImGui::TextColored(ImVec4(0.88f, 0.24f, 0.20f, 1.0f),
                               "Save complete session (from Start)");
        } else {
            ImGui::TextUnformatted("Save complete session (from Start)");
        }
        if (status.session_buffering) {
            ImGui::SameLine(ImGui::GetWindowWidth() - scaled(58.0f));
            ImGui::TextColored(ImVec4(0.90f, 0.48f, 0.12f, 1.0f), "BUF");
        } else if (status.session_available) {
            ImGui::SameLine(ImGui::GetWindowWidth() - scaled(72.0f));
            ImGui::TextColored(ImVec4(0.10f, 0.62f, 0.34f, 1.0f), "READY");
        }

        if (has_buffered_session) {
            const double buffered_mib = static_cast<double>(status.session_frames) *
                                        kPhysicalChannels * sizeof(float) / (1024.0 * 1024.0);
            ImGui::TextDisabled("Buffered: %llu frames / 8 ch / %.1f MiB",
                                static_cast<unsigned long long>(status.session_frames), buffered_mib);
        }

        if (keep_session_choice && has_buffered_session) {
            const char* recording_items[] = {
                "Raw volts Float32 (.f32)",
                "Raw volts IEEE Float32 WAV (.wav)",
                "Raw volts CSV (.csv)"};
            const char* recording_scopes[] = {"All 8 inputs", "Displayed channels"};
            int scope_choice = config.recording_all_channels ? 0 : 1;
            static constexpr RecordingFormat formats[] = {
                RecordingFormat::Float32, RecordingFormat::Wav, RecordingFormat::Csv};
            if (ImGui::BeginTable("RecordingSettings", 2,
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, scaled(92.0f));
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Format");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##recording_format", &recording_choice, recording_items, 3))
                    config.recording_format = formats[recording_choice];

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Channels");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##recording_scope", &scope_choice, recording_scopes, 2))
                    config.recording_all_channels = scope_choice == 0;
                ImGui::EndTable();
            }

            ImGui::BeginDisabled(status.worker_alive || !status.session_available);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.43f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.50f, 0.94f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            const char* save_session_label = status.worker_alive
                                           ? "Stop acquisition to save entire session"
                                           : "Save entire session";
            if (ImGui::Button(save_session_label, ImVec2(-1.0f, scaled(34.0f)))) {
                std::string error;
                if (!acquisition.export_session(config, error)) {
                    ui_message = error;
                } else {
                    const AcquisitionStats saved = acquisition.stats();
                    ui_message = "Saved complete session: " + saved.export_path;
                    keep_session_choice = false;
                    config.recording_format = RecordingFormat::None;
                }
            }
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
        } else if (status.session_available) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.32f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.82f, 0.38f, 0.29f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            if (ImGui::Button("Discard entire session", ImVec2(-1.0f, scaled(34.0f)))) {
                std::string error;
                if (!acquisition.discard_session(error)) ui_message = error;
                else ui_message = "Buffered session discarded; no output file was kept.";
            }
            ImGui::PopStyleColor(3);
        } else if (status.worker_alive) {
            ImGui::BeginDisabled();
            ImGui::Button("Stop acquisition to save or discard", ImVec2(-1.0f, scaled(34.0f)));
            ImGui::EndDisabled();
        } else {
            ImGui::TextDisabled("Start acquisition to create a lossless full-session buffer");
        }
        if (has_buffered_session) {
            ImGui::TextDisabled("Temporary + exports: %s",
                                config.recording_directory.u8string().c_str());
        }
        ImGui::EndChild();

        if (!status.export_path.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.10f, 0.62f, 0.34f, 1.0f),
                               "Last export: %d ch, %llu frames",
                               status.exported_channels,
                               static_cast<unsigned long long>(status.exported_frames));
            ImGui::TextWrapped("%s", status.export_path.c_str());
        }
        if (!status.error.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.20f, 0.17f, 1.0f));
            ImGui::TextWrapped("Error: %s", status.error.c_str());
            ImGui::PopStyleColor();
        } else if (!ui_message.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", ui_message.c_str());
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("PlotPanel", ImVec2(0, 0), true);
        ImGui::TextUnformatted("Live waveform");
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
            render_choice == static_cast<int>(PlotRenderMode::AdaptiveLine)
                ? "adaptive smooth preview" : "min/max envelope preview");

        if (ImGui::BeginTable("LiveMetrics", 4,
                              ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("EFFECTIVE RATE");
            ImGui::Text("%.1f Hz", status.effective_sample_rate);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("CAPTURED FRAMES");
            ImGui::Text("%llu", static_cast<unsigned long long>(status.frames));
            ImGui::TableNextColumn();
            ImGui::TextDisabled("SESSION BUFFER");
            if (status.session_buffering || status.session_available)
                ImGui::Text("%llu frames / 8 ch",
                            static_cast<unsigned long long>(status.session_frames));
            else if (status.exported_frames > 0)
                ImGui::Text("Saved %llu / %d ch",
                            static_cast<unsigned long long>(status.exported_frames),
                            status.exported_channels);
            else
                ImGui::TextDisabled("No session");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("USB BUFFER");
            ImGui::Text("%d floats", status.current_backlog_floats);
            ImGui::EndTable();
        }
        ImGui::Separator();
        const HistoryInfo history_info = acquisition.history().info();
        std::size_t requested_frames = 0;
        if (view_choice == static_cast<int>(ViewMode::Follow)) {
            requested_frames = static_cast<std::size_t>(
                std::max(1.0f, config.preview_seconds * static_cast<float>(config.sample_rate)));
        } else if (view_choice == static_cast<int>(ViewMode::Free)) {
            requested_frames = history_info.frames;
        }
        const HistorySnapshot snapshot = acquisition.history().snapshot(config.channels, requested_frames);

        if (!view_mode_changed && view_choice != static_cast<int>(ViewMode::Free)
            && expected_axis_valid
            && (std::abs(view_x_min - expected_x_min) > 1e-4
                || std::abs(view_x_max - expected_x_max) > 1e-4)) {
            view_choice = static_cast<int>(ViewMode::Free);
        }
        view_mode_changed = false;

        const double snapshot_x_min = snapshot.sample_rate > 0
            ? static_cast<double>(snapshot.first_frame_index) / snapshot.sample_rate : 0.0;
        const double snapshot_x_max = snapshot.sample_rate > 0
            ? static_cast<double>(snapshot.first_frame_index + snapshot.frames) / snapshot.sample_rate
            : std::max(0.1, static_cast<double>(config.preview_seconds));
        const double session_x_max = history_info.sample_rate > 0
            ? static_cast<double>(history_info.total_frames) / history_info.sample_rate : 0.0;
        if (view_choice == static_cast<int>(ViewMode::FitAll)) {
            view_x_min = 0.0;
            view_x_max = std::max(0.1, session_x_max);
        } else if (view_choice == static_cast<int>(ViewMode::Follow)) {
            view_x_max = std::max(0.1, snapshot_x_max);
            view_x_min = std::max(snapshot_x_min,
                                  view_x_max - static_cast<double>(config.preview_seconds));
        } else if (view_x_max <= view_x_min) {
            view_x_min = snapshot_x_min;
            view_x_max = std::max(snapshot_x_min + 0.1, snapshot_x_max);
        }
        if (view_choice != static_cast<int>(ViewMode::Free)) {
            expected_x_min = view_x_min;
            expected_x_max = view_x_max;
            expected_axis_valid = true;
        } else {
            expected_axis_valid = false;
        }

        const bool use_overview = view_choice == static_cast<int>(ViewMode::FitAll)
            || (view_choice == static_cast<int>(ViewMode::Free)
                && (snapshot.frames == 0 || view_x_min < snapshot_x_min));
        const OverviewSnapshot overview = use_overview
            ? acquisition.history().overview_snapshot(
                  config.channels, view_x_min, view_x_max, 4096)
            : OverviewSnapshot{};
        const PlotRenderMode active_render_mode =
            static_cast<PlotRenderMode>(render_choice);

        double measured_y_min = 0.0;
        double measured_y_max = 0.0;
        const bool has_visible_signal = use_overview
            ? measure_overview_y_range(overview, config.channels,
                                       active_render_mode,
                                       measured_y_min, measured_y_max)
            : measure_visible_y_range(snapshot, config.channels, view_x_min, view_x_max,
                                      active_render_mode,
                                      measured_y_min, measured_y_max);
        if (status.frames < previous_y_session_frames && y_axis_choice == 2) {
            stable_y_axis.initialized = false;
            lock_y_pending = true;
            auto_lock_settling = false;
        }
        previous_y_session_frames = status.frames;
        if (y_axis_choice == 0 && has_visible_signal) {
            update_stable_y_axis(stable_y_axis, measured_y_min, measured_y_max,
                                 minimum_y_span, std::max(1e-4f, io.DeltaTime));
        }
        if (y_axis_choice == 2 && lock_y_pending && has_visible_signal) {
            stable_y_axis.initialized = false;
            auto_lock_settling = true;
            auto_lock_deadline = std::chrono::steady_clock::now()
                               + std::chrono::milliseconds(900);
            lock_y_pending = false;
        }
        if (y_axis_choice == 2 && auto_lock_settling && has_visible_signal) {
            update_stable_y_axis(stable_y_axis, measured_y_min, measured_y_max,
                                 minimum_y_span, std::max(1e-4f, io.DeltaTime));
            locked_y_min = stable_y_axis.minimum;
            locked_y_max = stable_y_axis.maximum;
            if (std::chrono::steady_clock::now() >= auto_lock_deadline)
                auto_lock_settling = false;
        }

        ImPlotFlags plot_flags = ImPlotFlags_NoBoxSelect;
        if (ImPlot::BeginPlot("##RealtimeWaveform", ImVec2(-1, -1), plot_flags)) {
            const ImPlotAxisFlags y_flags = y_axis_choice == 1
                ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None;
            ImPlot::SetupAxes("Time since start (s)", "Voltage (V)", ImPlotAxisFlags_None, y_flags);
            ImPlot::SetupAxisLinks(ImAxis_X1, &view_x_min, &view_x_max);
            if (y_axis_choice == 0 && stable_y_axis.initialized) {
                ImPlot::SetupAxisLimits(ImAxis_Y1, stable_y_axis.minimum,
                                        stable_y_axis.maximum, ImGuiCond_Always);
            } else if (y_axis_choice == 2) {
                ImPlot::SetupAxisLimits(ImAxis_Y1, locked_y_min,
                                        locked_y_max, ImGuiCond_Always);
            }
            const float plot_width = ImGui::GetContentRegionAvail().x;
            for (int channel = 0; channel < kPhysicalChannels; ++channel) {
                if (!config.channels[channel]) continue;
                char label[32];
                std::snprintf(label, sizeof(label), "AIN%d (AD%d)", channel + 1, channel);
                if (use_overview) {
                    draw_overview_waveform(label, overview, channel,
                                           kChannelColors[channel],
                                           static_cast<PlotRenderMode>(render_choice));
                } else {
                    draw_waveform(label, snapshot.channels[channel], snapshot.sample_rate,
                                  snapshot.first_frame_index, view_x_min, view_x_max,
                                  plot_width, kChannelColors[channel],
                                  static_cast<PlotRenderMode>(render_choice));
                }
            }
            ImPlot::EndPlot();
        }
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        glViewport(0, 0, drawable_width, drawable_height);
        if (dark_theme) glClearColor(0.075f, 0.086f, 0.11f, 1.0f);
        else glClearColor(0.945f, 0.957f, 0.976f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    acquisition.stop();
    int exit_code = 0;
    if (autostart) {
        std::string session_error;
        const AcquisitionStats completed = acquisition.stats();
        if (completed.session_available) {
            const bool ok = config.recording_format == RecordingFormat::None
                          ? acquisition.discard_session(session_error)
                          : acquisition.export_session(config, session_error);
            if (!ok) {
                std::cerr << "SESSION_ERROR=" << session_error << '\n';
                exit_code = 6;
            }
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}

} // namespace

int main(int argc, char** argv) {
    CliOptions options;
    std::string cli_error;
    if (!parse_cli(argc, argv, options, cli_error)) {
        std::cerr << cli_error << "\n\n";
        print_usage();
        return 1;
    }
    if (options.show_help) {
        print_usage();
        return 0;
    }

    const std::filesystem::path config_path = resolve_config_path(options);
    std::string config_warning;
    DaqConfig config = load_config(config_path, config_warning);
    resolve_relative_paths(config, config_path);

    if (!options.dll_override.empty()) config.dll_path = options.dll_override;
    if (options.sample_rate_override > 0) config.sample_rate = options.sample_rate_override;
    if (!options.recording_override.empty())
        config.recording_format = parse_recording_format(options.recording_override);
    if (!options.output_directory_override.empty())
        config.recording_directory = options.output_directory_override;

    std::string validation_error;
    if (!validate_config(config, validation_error)) {
        std::cerr << "CONFIG_ERROR=" << validation_error << '\n';
        return 1;
    }
    if (!config_warning.empty()) std::cerr << "WARNING=" << config_warning << '\n';

    if (options.headless) return run_headless(config, options.duration_seconds);
    return run_gui(config, config_warning, options.autostart, options.duration_seconds);
}
