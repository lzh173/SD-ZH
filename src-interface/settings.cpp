#include "settings.h"
#include "imgui/imgui.h"
#include <string>
#include "core/params.h"

#include "core/config.h"

#include "main_ui.h"
#include "core/opencl.h"

#include "init.h"
#include "common/tracking/tle.h"
#include "common/widgets/timed_message.h"
#include "common/widgets/json_editor.h"

#include "core/style.h"

namespace satdump
{
    namespace settings
    {
        std::vector<std::pair<std::string, satdump::params::EditableParameter>> settings_user_interface;
        std::vector<std::pair<std::string, satdump::params::EditableParameter>> settings_general;
        std::vector<std::pair<std::string, satdump::params::EditableParameter>> settings_output_directories;

#ifdef USE_OPENCL
        // OpenCL Selection
        int opencl_devices_id = 0;
        std::string opencl_devices_str;
        std::vector<opencl::OCLDevice> opencl_devices_enum;
#endif

        int selected_theme = 0;
        std::vector<std::string> themes;
        std::string themes_str = "";

        bool tles_are_update = false;
        char tle_last_update[80];

        bool show_imgui_demo = false;
        bool advanced_mode = false;

        widgets::TimedMessage saved_message;

        void setup()
        {
            nlohmann::ordered_json params = satdump::config::main_cfg["user_interface"];

            for (nlohmann::detail::iteration_proxy_value<nlohmann::detail::iter_impl<nlohmann::ordered_json>> cfg : params.items())
            {
                // Check setting type, and create an EditableParameter if possible
                if (cfg.value().contains("type") && cfg.value().contains("value") && cfg.value().contains("name"))
                    settings_user_interface.push_back({cfg.key(), params::EditableParameter(nlohmann::json(cfg.value()))});
            }

            params = satdump::config::main_cfg["satdump_general"];

            for (nlohmann::detail::iteration_proxy_value<nlohmann::detail::iter_impl<nlohmann::ordered_json>> cfg : params.items())
            {
                // Check setting type, and create an EditableParameter if possible
                if (cfg.value().contains("type") && cfg.value().contains("value") && cfg.value().contains("name"))
                    settings_general.push_back({cfg.key(), params::EditableParameter(nlohmann::json(cfg.value()))});
            }

            params = satdump::config::main_cfg["satdump_directories"];

            for (nlohmann::detail::iteration_proxy_value<nlohmann::detail::iter_impl<nlohmann::ordered_json>> cfg : params.items())
            {
                // Check setting type, and create an EditableParameter if possible
                if (cfg.value().contains("type") && cfg.value().contains("value") && cfg.value().contains("name"))
                    settings_output_directories.push_back({cfg.key(), params::EditableParameter(nlohmann::json(cfg.value()))});
            }

            int theme_id = 0;
            std::string current_theme = satdump::config::main_cfg["user_interface"]["theme"]["value"].get<std::string>();
            for (const auto& entry : std::filesystem::directory_iterator(resources::getResourcePath("themes")))
            {
                if (entry.path().filename().extension() != ".json")
                    continue;
                std::string this_name = entry.path().filename().stem().string();
                themes.push_back(this_name);
                themes_str += this_name;
                themes_str.push_back('\0');
                if (this_name == current_theme)
                    selected_theme = theme_id;
                theme_id++;
            }

            advanced_mode = getValueOrDefault(satdump::config::main_cfg["user_interface"]["advanced_mode"]["value"], false);

#ifdef USE_OPENCL
            opencl_devices_enum = opencl::getAllDevices();
            opencl_devices_enum.push_back({ -1, -1, "None (Use CPU)" });
            int p = satdump::config::main_cfg["satdump_general"]["opencl_device"]["platform"].get<int>();
            int d = satdump::config::main_cfg["satdump_general"]["opencl_device"]["device"].get<int>();
            int dev_id = 0;
            opencl_devices_str = "";
            for (opencl::OCLDevice &dev : opencl_devices_enum)
            {
                opencl_devices_str += dev.name;
                if (dev.platform_id == p && dev.device_id == d)
                    opencl_devices_id = dev_id;
                dev_id++;
            }
            opencl_devices_str.push_back('\0');
#endif
        }

        void render()
        {
            ImGui::SeparatorText("核心设置");
            if (ImGui::CollapsingHeader("用户界面"))
            {
                if (ImGui::BeginTable("##satdumpuisettings", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    // Theme Selection
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("主题");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("设置SatDump的样式和颜色");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Combo("##themeselection", &selected_theme, themes_str.c_str());

                    // Standard user interface settings
                    for (std::pair<std::string, satdump::params::EditableParameter> &p : settings_user_interface)
                        p.second.draw();

                    // ImGui Demo
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("显示ImGui调试窗口");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("仅限开发者使用！");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Checkbox("##showimguidebugcheckbox", &show_imgui_demo);

                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("常规设置"))
            {
                if (ImGui::BeginTable("##satdumpgeneralsettings", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
#ifdef USE_OPENCL
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("OpenCL 设备");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("SatDump将使用的OpenCL设备，用于加速计算（如图像投影处理等）。");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Combo("##opencldeviceselection", &opencl_devices_id, opencl_devices_str.c_str());
#endif

                    for (std::pair<std::string, satdump::params::EditableParameter> &p : settings_general)
                        p.second.draw();

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("立即更新TLE");
                    ImGui::TableSetColumnIndex(1);
                    bool disable_update_button = tles_are_update;
                    if (disable_update_button)
                        style::beginDisabled();
                    if (ImGui::Button("更新###updateTLEs"))
                    {
                        ui_thread_pool.push([](int)
                                            {   tles_are_update = true;
                                                updateTLEFile(satdump::user_path + "/satdump_tles.txt");
                                                tles_are_update = false; });
                    }
                    if (disable_update_button)
                        style::endDisabled();

                    time_t last_update = getValueOrDefault<time_t>(config::main_cfg["user"]["tles_last_updated"], 0);
                    if (last_update == 0)
                        strcpy(tle_last_update, "从未更新");
                    else
                    {
                        struct tm ts;
                        ts = *gmtime(&last_update);
                        strftime(tle_last_update, sizeof(tle_last_update), "%Y-%m-%d %H:%M:%S UTC", &ts);
                    }
                    ImGui::SameLine(0.0f, 10.0f * ui_scale);
                    ImGui::TextDisabled("上次更新: %s", tle_last_update);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("清除瓦片地图(OSM)缓存");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("删除所有缓存的瓦片（OSM及其他来源）。");
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::Button("清除缓存###deleteosmtiles"))
                        if (std::filesystem::exists(satdump::user_path + "/osm_tiles/"))
                            std::filesystem::remove_all(satdump::user_path + "/osm_tiles/");

                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("文件输入/输出"))
            {
                if (ImGui::BeginTable("##satdumpoutput_directories", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    for (std::pair<std::string, satdump::params::EditableParameter> &p : settings_output_directories)
                        p.second.draw();
                    ImGui::EndTable();
                }
            }

            if (config::plugin_config_handlers.size() > 0)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10 * ui_scale);
                ImGui::SeparatorText("插件设置");
                for (auto &plugin_hdl : config::plugin_config_handlers)
                {
                    if (ImGui::CollapsingHeader(plugin_hdl.name.c_str()))
                    {
                        plugin_hdl.render();
                    }
                }
            }

            if (advanced_mode)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10 * ui_scale);
                ImGui::SeparatorText("高级设置");
                if (ImGui::CollapsingHeader("TLE 设置"))
                {
                    widgets::JSONTreeEditor(satdump::config::main_cfg["tle_settings"], "tle_settings", false);
                    if (ImGui::Button("重置###tle_settings"))
                        satdump::config::main_cfg["tle_settings"] = satdump::config::master_cfg["tle_settings"];
                }
                if (ImGui::CollapsingHeader("高级设置"))
                {
                    widgets::JSONTreeEditor(satdump::config::main_cfg["advanced_settings"], "advanced_settings");
                    ImGui::SameLine();
                    if (ImGui::Button("重置###advanced_settings"))
                        satdump::config::main_cfg["advanced_settings"] = satdump::config::master_cfg["advanced_settings"];
                }
                if (ImGui::CollapsingHeader("仪器配置"))
                {
                    widgets::JSONTreeEditor(satdump::config::main_cfg["viewer"]["instruments"], "instrument_settings");
                    ImGui::SameLine();
                    if (ImGui::Button("重置###instrument_settings"))
                        satdump::config::main_cfg["viewer"]["instruments"] = satdump::config::master_cfg["viewer"]["instruments"];
                }
                if (ImGui::CollapsingHeader("默认管道配置"))
                {
                    widgets::JSONTreeEditor(pipelines_json, "pipelines");
                    ImGui::SameLine();
                    if (ImGui::Button("重置###pipelines"))
                        pipelines_json = pipelines_system_json;
                }
            }

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5 * ui_scale);
            if (ImGui::Button("保存"))
            {
#ifdef USE_OPENCL
                satdump::config::main_cfg["satdump_general"]["opencl_device"]["platform"] = opencl_devices_enum[opencl_devices_id].platform_id;
                satdump::config::main_cfg["satdump_general"]["opencl_device"]["device"] = opencl_devices_enum[opencl_devices_id].device_id;
                opencl::resetOCLContext();
#endif

                for (std::pair<std::string, satdump::params::EditableParameter> &p : settings_user_interface)
                    satdump::config::main_cfg["user_interface"][p.first]["value"] = p.second.getValue();
                for (std::pair<std::string, satdump::params::EditableParameter> &p : settings_general)
                    satdump::config::main_cfg["satdump_general"][p.first]["value"] = p.second.getValue();
                for (std::pair<std::string, satdump::params::EditableParameter> &p : settings_output_directories)
                    satdump::config::main_cfg["satdump_directories"][p.first]["value"] = p.second.getValue();

                satdump::config::main_cfg["user_interface"]["theme"]["value"] = themes[selected_theme];

                for (auto &plugin_hdl : config::plugin_config_handlers)
                    plugin_hdl.save();

                config::saveUserConfig();
                if (advanced_mode)
                    savePipelines();
                advanced_mode = getValueOrDefault(satdump::config::main_cfg["user_interface"]["advanced_mode"]["value"], false);
                saved_message.set_message(style::theme.green, "设置已保存");
                satdump::update_ui = true;
            }

            saved_message.draw();
            ImGui::TextColored(style::theme.yellow, "注意：部分设置需要重启SatDump才能生效！");
        }
    }
}
