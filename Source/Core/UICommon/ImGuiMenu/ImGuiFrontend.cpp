// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ImGuiFrontend.h"

#include "DolphinWinRT/Host.h"
#include "DolphinWinRT/UWPUtils.h"

#include "ImGuiNetplay.h"
#include "ImageLoader.h"
#include "ImGuiMappingWindow.h"
#include "WinRTKeyboard.h"

#include <fmt/format.h>
#include <format>
#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <wil/com.h>
#include <future>
#include <cmath>

#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "../../../../Externals/tinygltf/tinygltf/stb_image.h"

#ifdef WINRT_XBOX
#include <gamingdeviceinformation.h>
#include <windows.applicationmodel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/windows.gaming.input.h>
#include <winrt/windows.graphics.display.core.h>
#endif

#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/CommonTitles.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core/WiiUtils.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/NetPlayClient.h"
#include "Core/NetPlayProto.h"
#include "Core/NetPlayServer.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Core/TitleDatabase.h"
#include "Core/WiiRoot.h"
#include "DiscIO/NANDImporter.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/AchievementManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/GeckoCodeConfig.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/ActionReplay.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/PatchEngine.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/Extension/Extension.h"

#include "DiscIO/Enums.h"

#include <rcheevos/include/rc_client_raintegration.h>
#include "rcheevos/include/rc_api_info.h"
#include "rcheevos/include/rc_api_runtime.h"
#include "rcheevos/include/rc_api_user.h"
#include "rcheevos/include/rc_client.h"
#include "rcheevos/include/rc_error.h"
#include "rcheevos/include/rc_hash.h"
#include "rcheevos/include/rc_runtime.h"
#include "rcheevos/include/rc_consoles.h"

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/Image.h"

#include "Common/Timer.h"
#include "Common/Config/Config.h"
#include "Common/HttpRequest.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"
#include "Common/Version.h"
#include "Common/MsgHandler.h"
#include "Common/Logging/Log.h"

#include "UICommon/GameFile.h"
#include "UICommon/GameFileCache.h"
#include "UICommon/UICommon.h"

#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/OnScreenUI.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/MixedTriggers.h"
#include "InputCommon/ControllerInterface/CoreDevice.h"
#include "InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPClient.h"
#include "InputCommon/ControllerInterface/MappingCommon.h"
#include "InputCommon/ControllerInterface/WGInput/WGInput.h"
#include "InputCommon/InputConfig.h"

#include "AudioCommon/AudioCommon.h"
#include "AudioCommon/Enums.h"
#include "AudioCommon/WASAPIStream.h"

#ifdef WINRT_XBOX
namespace WGI = winrt::Windows::Gaming::Input;
using winrt::Windows::UI::Core::CoreWindow;
using namespace winrt;
#endif

void OnHardcoreChangedStatic()
{
  if (Config::Get(Config::RA_HARDCORE_ENABLED))
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_DEBUGGING, false);
}

static bool show_update_progress_modal = false;
static std::atomic<bool> update_complete{false};
static std::atomic<float> update_progress{0.0f};
static WiiUtils::UpdateResult async_res = WiiUtils::UpdateResult::Cancelled;
static std::thread update_thread;
static bool show_update_result_modal = false;

#ifdef USE_RETRO_ACHIEVEMENTS
// Structure to cache achievement data for games not currently loaded
struct GameAchievementData
{
  bool loaded = false;
  bool loading = false;
  bool hasAchievements = false;
  u32 gameId = 0;
  std::string gameTitle;
  std::string errorMessage;
  std::vector<rc_api_achievement_definition_t> achievements;
  std::unordered_set<u32> unlockedAchievementIds;
  std::chrono::steady_clock::time_point loadTime;
  
  ~GameAchievementData()
  {
    // Clean up dynamically allocated strings in achievements
    for (auto& achievement : achievements)
    {
      if (achievement.title) delete[] achievement.title;
      if (achievement.description) delete[] achievement.description;
      if (achievement.author) delete[] achievement.author;
      if (achievement.badge_name) delete[] achievement.badge_name;
      if (achievement.definition) delete[] achievement.definition;
    }
  }
};

// Cache for game achievement data
static std::unordered_map<std::string, std::unique_ptr<GameAchievementData>> g_game_achievement_cache;
static std::mutex g_cache_mutex;

// Forward declarations
void LoadAchievementDataForGame(const std::string& file_path, const std::string& game_id);
void DrawSpinner(const char* label, float radius, float thickness, ImU32 color);

// Async function to load achievement data for a game
void LoadAchievementDataForGame(const std::string& file_path, const std::string& game_id)
{
#ifdef _WIN32
  OutputDebugStringA(("LoadAchievementDataForGame called for: " + file_path + " (ID: " + game_id + ")\n").c_str());
#endif

  auto& achievement_manager = AchievementManager::GetInstance();
  if (!achievement_manager.HasAPIToken())
  {
#ifdef _WIN32
    OutputDebugStringA("LoadAchievementDataForGame: No API token available\n");
#endif
    // Mark as failed
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
      it->second->errorMessage = "Not logged in to RetroAchievements";
    }
    return;
  }
    
  // Calculate game hash try different methods
  std::string game_hash;
  
#ifdef _WIN32
  OutputDebugStringA("LoadAchievementDataForGame: Calculating hash...\n");
#endif
  
  // First try using AchievementManager's hash calculation
  try 
  {
    game_hash = AchievementManager::CalculateHash(file_path);
#ifdef _WIN32
    OutputDebugStringA(("Hash from AchievementManager: " + game_hash + "\n").c_str());
#endif
  }
  catch (...)
  {
    game_hash = "";
#ifdef _WIN32
    OutputDebugStringA("AchievementManager::CalculateHash failed\n");
#endif
  }
  
  // If that fails, try using rc_hash directly
  if (game_hash.empty() || game_hash == "0")
  {
#ifdef _WIN32
    OutputDebugStringA("LoadAchievementDataForGame: Trying direct RC hash calculation...\n");
#endif
    // Try direct RC hash calculation for GameCube
    char hash_buffer[33]{};
    if (rc_hash_generate_from_file(hash_buffer, RC_CONSOLE_GAMECUBE, file_path.c_str()) == RC_OK)
    {
      game_hash = std::string(hash_buffer);
#ifdef _WIN32
      OutputDebugStringA(("GameCube hash: " + game_hash + "\n").c_str());
#endif
    }
    // If GameCube fails, try Wii
    else if (rc_hash_generate_from_file(hash_buffer, RC_CONSOLE_WII, file_path.c_str()) == RC_OK)
    {
      game_hash = std::string(hash_buffer);
#ifdef _WIN32
      OutputDebugStringA(("Wii hash: " + game_hash + "\n").c_str());
#endif
    }
  }
  
  if (game_hash.empty() || game_hash == "0")
  {
#ifdef _WIN32
    OutputDebugStringA("LoadAchievementDataForGame: Hash calculation failed completely\n");
#endif
    // Hash calculation failed mark as no achievements
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
      it->second->errorMessage = "Failed to calculate game hash - file may be unsupported";
      it->second->loadTime = std::chrono::steady_clock::now();
    }
    return;
  }
  
#ifdef _WIN32
  OutputDebugStringA(("LoadAchievementDataForGame: Final hash: " + game_hash + " (length: " + std::to_string(game_hash.length()) + ")\n").c_str());
#endif
    
  // Mark as loading
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto& data = g_game_achievement_cache[game_id];
    if (!data)
      data = std::make_unique<GameAchievementData>();
    data->loading = true;
  }
  
  // Use RC API to resolve hash and fetch game data
  rc_api_resolve_hash_request_t resolve_request = {};
  resolve_request.game_hash = game_hash.c_str();
  
#ifdef _WIN32
  OutputDebugStringA(("LoadAchievementDataForGame: Sending hash to server: " + game_hash + "\n").c_str());
#endif
  
  rc_api_request_t api_request;
  int result = rc_api_init_resolve_hash_request(&api_request, &resolve_request);
  if (result != RC_OK)
  {
#ifdef _WIN32
    OutputDebugStringA(("LoadAchievementDataForGame: Failed to init resolve hash request, result: " + std::to_string(result) + "\n").c_str());
#endif
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
    }
    return;
  }
  
#ifdef _WIN32
  OutputDebugStringA(("LoadAchievementDataForGame: API URL: " + std::string(api_request.url) + "\n").c_str());
#endif
  
  Common::HttpRequest http_request;
  Common::HttpRequest::Response http_response;
  if (api_request.post_data && std::strlen(api_request.post_data) > 0)
  {
    http_response = http_request.Post(api_request.url, std::string(api_request.post_data),
                                      {{"User-Agent", Common::GetUserAgentStr()}});
  }
  else
  {
    http_response = http_request.Get(api_request.url, {{"User-Agent", Common::GetUserAgentStr()}});
  }
  rc_api_destroy_request(&api_request);
  
#ifdef _WIN32
  if (http_response.has_value())
  {
    std::string response_str(reinterpret_cast<const char*>(http_response->data()), http_response->size());
    OutputDebugStringA(("LoadAchievementDataForGame: Server response: " + response_str.substr(0, 200) + "...\n").c_str());
  }
  else
  {
    OutputDebugStringA("LoadAchievementDataForGame: No HTTP response received\n");
  }
#endif
  
  if (!http_response.has_value() || http_response->empty())
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
      it->second->errorMessage = "Failed to connect to RetroAchievements server";
    }
    return;
  }
  
  // Process resolve hash response
  rc_api_resolve_hash_response_t resolve_response;
  result = rc_api_process_resolve_hash_response(&resolve_response, 
      reinterpret_cast<const char*>(http_response->data()));
  
  if (result != RC_OK)
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
    }
    return;
  }
  
  if (resolve_response.game_id == 0)
  {
#ifdef _WIN32
    OutputDebugStringA("LoadAchievementDataForGame: Server returned game_id = 0 (no achievements for this game)\n");
#endif
    // No achievements for this game - this is normal for many games
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
      it->second->loadTime = std::chrono::steady_clock::now();
    }
    rc_api_destroy_resolve_hash_response(&resolve_response);
    return;
  }
  
  // Preserve resolved game id before destroying the response
  u32 resolved_game_id = resolve_response.game_id;

  // Get user info for API credentials
  auto* user_info = rc_client_get_user_info(achievement_manager.GetClient());
  if (!user_info || !user_info->username || !user_info->token)
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
    }
    rc_api_destroy_resolve_hash_response(&resolve_response);
    return;
  }
  
  // Fetch game data
  rc_api_fetch_game_data_request_t fetch_request = {};
  fetch_request.username = user_info->username;
  fetch_request.api_token = user_info->token;
  fetch_request.game_id = resolved_game_id;
  
  rc_api_destroy_resolve_hash_response(&resolve_response);
  
  result = rc_api_init_fetch_game_data_request(&api_request, &fetch_request);
  if (result != RC_OK)
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
    }
    return;
  }
  
  if (api_request.post_data && std::strlen(api_request.post_data) > 0)
  {
    http_response = http_request.Post(api_request.url, std::string(api_request.post_data),
                                      {{"User-Agent", Common::GetUserAgentStr()}});
  }
  else
  {
    http_response = http_request.Get(api_request.url, {{"User-Agent", Common::GetUserAgentStr()}});
  }
  rc_api_destroy_request(&api_request);
  
  if (!http_response.has_value() || http_response->empty())
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
    }
    return;
  }
  
  // Process game data response
  rc_api_fetch_game_data_response_t game_data_response;
  result = rc_api_process_fetch_game_data_response(&game_data_response,
      reinterpret_cast<const char*>(http_response->data()));
      
  if (result != RC_OK)
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (auto it = g_game_achievement_cache.find(game_id); it != g_game_achievement_cache.end())
    {
      it->second->loading = false;
      it->second->loaded = true;
      it->second->hasAchievements = false;
    }
    return;
  }
  
  // Cache the achievement data
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto& data = g_game_achievement_cache[game_id];
    if (!data)
      data = std::make_unique<GameAchievementData>();
      
    data->loading = false;
    data->loaded = true;
    data->hasAchievements = (game_data_response.num_achievements > 0);
    data->gameId = game_data_response.id;
    data->gameTitle = game_data_response.title ? game_data_response.title : "";
    data->loadTime = std::chrono::steady_clock::now();
    
#ifdef _WIN32
    OutputDebugStringA(("LoadAchievementDataForGame: Found " + std::to_string(game_data_response.num_achievements) + " achievements for '" + data->gameTitle + "'\n").c_str());
#endif
    
    data->achievements.clear();
    data->achievements.reserve(game_data_response.num_achievements);
    
    for (u32 i = 0; i < game_data_response.num_achievements; i++)
    {
      const auto& src = game_data_response.achievements[i];
      rc_api_achievement_definition_t dst = {};
      
      dst.id = src.id;
      dst.points = src.points;
      dst.category = src.category;
      dst.type = src.type;
      dst.rarity = src.rarity;
      dst.rarity_hardcore = src.rarity_hardcore;
      dst.created = src.created;
      dst.updated = src.updated;
      
      if (src.title && strlen(src.title) > 0)
      {
        auto len = strlen(src.title) + 1;
        auto* title = new char[len];
        strcpy(title, src.title);
        dst.title = title;
      }
      else
      {
        dst.title = nullptr;
      }
      
      if (src.description && strlen(src.description) > 0)
      {
        auto len = strlen(src.description) + 1;
        auto* desc = new char[len];
        strcpy(desc, src.description);
        dst.description = desc;
      }
      else
      {
        dst.description = nullptr;
      }
      
      if (src.author && strlen(src.author) > 0)
      {
        auto len = strlen(src.author) + 1;
        auto* author = new char[len];
        strcpy(author, src.author);
        dst.author = author;
      }
      else
      {
        dst.author = nullptr;
      }
      
      if (src.badge_name && strlen(src.badge_name) > 0)
      {
        auto len = strlen(src.badge_name) + 1;
        auto* badge = new char[len];
        strcpy(badge, src.badge_name);
        dst.badge_name = badge;
      }
      else
      {
        dst.badge_name = nullptr;
      }
      
      if (src.definition && strlen(src.definition) > 0)
      {
        auto len = strlen(src.definition) + 1;
        auto* def = new char[len];
        strcpy(def, src.definition);
        dst.definition = def;
      }
      else
      {
        dst.definition = nullptr;
      }
      
      data->achievements.push_back(dst);
    }
  }
  
  rc_api_destroy_fetch_game_data_response(&game_data_response);

  auto fetch_unlocks = [&](int hardcore_flag) {
    rc_api_fetch_user_unlocks_request_t unlocks_request = {};
    unlocks_request.username = user_info->username;
    unlocks_request.api_token = user_info->token;
    unlocks_request.game_id = resolved_game_id;
    unlocks_request.hardcore = hardcore_flag;

    rc_api_request_t unlocks_api_request;
    int r = rc_api_init_fetch_user_unlocks_request(&unlocks_api_request, &unlocks_request);
    if (r != RC_OK)
      return;

    Common::HttpRequest::Response unlocks_http_response;
    if (unlocks_api_request.post_data && std::strlen(unlocks_api_request.post_data) > 0)
      unlocks_http_response = http_request.Post(unlocks_api_request.url, std::string(unlocks_api_request.post_data),
                                               {{"User-Agent", Common::GetUserAgentStr()}});
    else
      unlocks_http_response = http_request.Get(unlocks_api_request.url, {{"User-Agent", Common::GetUserAgentStr()}});
    rc_api_destroy_request(&unlocks_api_request);

    if (!unlocks_http_response.has_value() || unlocks_http_response->empty())
      return;

    rc_api_fetch_user_unlocks_response_t unlocks_response;
    if (rc_api_process_fetch_user_unlocks_response(&unlocks_response,
            reinterpret_cast<const char*>(unlocks_http_response->data())) == RC_OK)
    {
      std::lock_guard<std::mutex> lock(g_cache_mutex);
      auto it = g_game_achievement_cache.find(game_id);
      if (it != g_game_achievement_cache.end() && it->second)
      {
        for (u32 i = 0; i < unlocks_response.num_achievement_ids; ++i)
          it->second->unlockedAchievementIds.insert(unlocks_response.achievement_ids[i]);
      }
    }
    rc_api_destroy_fetch_user_unlocks_response(&unlocks_response);
  };

  fetch_unlocks(0);
  fetch_unlocks(1);
}
#endif  // USE_RETRO_ACHIEVEMENTS

namespace ImGuiFrontend
{

constexpr const char* PROFILES_DIR = "Profiles/";
std::vector<std::string> m_wiimote_profiles;
std::string m_selected_wiimote_profile[] = {"", "", "", ""};
static std::array<std::string, 4> m_selected_gc_profile = {"None", "None", "None", "None"};
static std::vector<std::string> m_gc_profiles = {"None"};
std::vector<std::string> m_paths;
bool m_show_path_warning = false;
float m_frame_scale = 1.0f;
int m_selectedGameIdx;
UIState m_state = UIState();
CarouselCategory m_ccat = CarouselCategory::CAll;
std::map<std::string, FrontendTheme> m_themes;
FrontendTheme* m_selected_theme;

static void ApplyColorTheme(int theme_index)
{
  ImGuiStyle& s = ImGui::GetStyle();
  
  switch (theme_index)
  {
    case 0: // Dark Theme
      ImGui::StyleColorsDark();
      break;
    case 1: // Light Theme
      ImGui::StyleColorsLight();
      break;
    case 2: // Classic Theme
      ImGui::StyleColorsClassic();
      break;
    case 3: // Ocean Blue
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.11f, 0.15f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.12f, 0.18f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.20f, 0.28f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.35f, 0.45f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.45f, 0.60f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.55f, 0.70f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.28f, 0.38f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.38f, 0.50f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.45f, 0.58f, 1.00f);
      break;
    case 4: // Purple Night
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.05f, 0.15f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.08f, 0.20f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.12f, 0.35f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.35f, 0.20f, 0.50f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.25f, 0.65f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.30f, 0.75f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.28f, 0.15f, 0.40f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.20f, 0.55f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.25f, 0.65f, 1.00f);
      break;
    case 5: // Forest Green
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.12f, 0.08f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.18f, 0.12f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.28f, 0.18f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.25f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.60f, 0.35f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.70f, 0.40f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.15f, 0.38f, 0.20f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.50f, 0.28f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.58f, 0.35f, 1.00f);
      break;
    case 6: // Cherry Red
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.05f, 0.05f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.25f, 0.08f, 0.08f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.40f, 0.15f, 0.15f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.20f, 0.20f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.25f, 0.25f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.35f, 0.35f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.38f, 0.15f, 0.15f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.50f, 0.20f, 0.20f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.58f, 0.28f, 0.28f, 1.00f);
      break;
    case 7: // Amber Gold
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.10f, 0.05f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.25f, 0.20f, 0.08f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.40f, 0.32f, 0.12f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.35f, 0.15f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.45f, 0.25f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.55f, 0.35f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.38f, 0.30f, 0.12f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.50f, 0.40f, 0.22f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.58f, 0.48f, 0.32f, 1.00f);
      s.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.85f, 0.70f, 1.00f);
      break;
    case 8: // Steel Blue
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.10f, 0.15f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.20f, 0.30f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.35f, 0.50f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.30f, 0.40f, 0.55f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.40f, 0.50f, 0.65f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.50f, 0.60f, 0.75f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.25f, 0.35f, 0.50f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.45f, 0.60f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.55f, 0.70f, 1.00f);
      break;
    case 9: // Sunset Orange
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.08f, 0.04f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.30f, 0.18f, 0.08f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.50f, 0.30f, 0.15f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.55f, 0.35f, 0.18f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.70f, 0.45f, 0.28f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.55f, 0.38f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.48f, 0.32f, 0.15f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.60f, 0.42f, 0.25f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.68f, 0.50f, 0.35f, 1.00f);
      s.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.85f, 0.70f, 1.00f);
      break;
    case 10: // Cyber Pink
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.05f, 0.12f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.25f, 0.08f, 0.25f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.40f, 0.15f, 0.40f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.20f, 0.45f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.25f, 0.60f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.35f, 0.70f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.38f, 0.15f, 0.38f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.50f, 0.22f, 0.50f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.58f, 0.32f, 0.58f, 1.00f);
      s.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.80f, 0.95f, 1.00f);
      break;
    case 11: // Mint Fresh
      ImGui::StyleColorsDark();
      s.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.12f, 0.10f, 0.94f);
      s.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.25f, 0.20f, 1.00f);
      s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.40f, 0.32f, 1.00f);
      s.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.35f, 0.60f);
      s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.60f, 0.48f, 1.00f);
      s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.70f, 0.58f, 1.00f);
      s.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.38f, 0.30f, 0.55f);
      s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.50f, 0.40f, 0.80f);
      s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.58f, 0.48f, 1.00f);
      s.Colors[ImGuiCol_Text] = ImVec4(0.85f, 0.95f, 0.90f, 1.00f);
      break;
    default: // Default to Dark Theme
      ImGui::StyleColorsDark();
      break;
  }
}

static void ApplyModernStyling()
{
  ImGuiStyle& style = ImGui::GetStyle();
  
  style.ScaleAllSizes(1.2f);
  style.WindowRounding = 15.0f;
  style.WindowBorderSize = 1.5f;
  style.WindowPadding = ImVec2(20.0f, 20.0f);
  style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
  style.WindowMinSize = ImVec2(300.0f, 150.0f);
  style.FrameRounding = 10.0f;
  style.FrameBorderSize = 0.0f;
  style.FramePadding = ImVec2(16.0f, 12.0f);
  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  style.ChildRounding = 10.0f;
  style.ChildBorderSize = 1.5f;
  style.PopupRounding = 10.0f;
  style.PopupBorderSize = 1.5f;
  style.TabRounding = 8.0f;
  style.TabBorderSize = 0.0f;
  style.ScrollbarRounding = 15.0f;
  style.ScrollbarSize = 20.0f;
  style.GrabRounding = 10.0f;
  style.GrabMinSize = 15.0f;
  style.ItemSpacing = ImVec2(12.0f, 8.0f);
  style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
  style.IndentSpacing = 25.0f;
  style.Alpha = 1.0f;
  style.DisabledAlpha = 0.6f;
  style.AntiAliasedLines = true;
  style.AntiAliasedLinesUseTex = true;
  style.AntiAliasedFill = true;
  style.SeparatorTextBorderSize = 1.5f;
  style.SeparatorTextAlign = ImVec2(0.5f, 0.5f);
  style.SeparatorTextPadding = ImVec2(25.0f, 4.0f);
  style.TabBorderSize = 0.0f;
}

static bool StyledButton(const char* label, const ImVec2& size = ImVec2(0, 0), ImGuiButtonFlags flags = 0)
{
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 14.0f));
  
  bool result = ImGui::ButtonEx(label, size, flags);
  
  ImGui::PopStyleVar(2);
  return result;
}

static bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.80f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(25.0f, 16.0f));
  
  bool result = ImGui::Button(label, size);
  
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
  return result;
}

static bool SecondaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.60f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 0.80f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 14.0f));
  
  bool result = ImGui::Button(label, size);
  
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
  return result;
}

static bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.80f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 14.0f));
  
  bool result = ImGui::Button(label, size);
  
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
  return result;
}

#ifdef USE_RETRO_ACHIEVEMENTS
void ImGuiFrontend::OnHardcoreChanged()
{
  if (Config::Get(Config::RA_HARDCORE_ENABLED))
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_DEBUGGING, false);
}
#endif  // USE_RETRO_ACHIEVEMENTS

ImGuiFrontend::ImGuiFrontend()
{
  if (!g_video_backend->Initialized())
  {
    WindowSystemInfo wsi;

#ifdef WINRT_XBOX
    // To-Do: Handle other platforms, extract this code so it can be done by the host!

    CoreWindow window = CoreWindow::GetForCurrentThread();
    void* abi = winrt::get_abi(window);

    wsi.type = WindowSystemType::Windows;
    wsi.render_surface = abi;
    wsi.render_width = window.Bounds().Width;
    wsi.render_height = window.Bounds().Height;

    GAMING_DEVICE_MODEL_INFORMATION info = {};
    GetGamingDeviceModelInformation(&info);
    if (info.vendorId == GAMING_DEVICE_VENDOR_ID_MICROSOFT)
    {
      winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation hdi =
          winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();
      if (hdi)
      {
        constexpr float frontend_modifier = 1.8f;
        uint32_t width = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();

        m_frame_scale = ((float)width / 1920.0f) * frontend_modifier;
        wsi.render_width = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();
        wsi.render_height = hdi.GetCurrentDisplayMode().ResolutionHeightInRawPixels();
        // Our UI is based on 1080p, and we're adding a modifier to zoom in by 80%
        wsi.render_surface_scale = ((float)wsi.render_width / 1920.0f) * 1.8f;
      }
    }
#endif

    // Manually reactivate the video backend in case a GameINI overrides the video backend setting.
    VideoBackendBase::PopulateBackendInfo(wsi);
    // Issue any API calls which must occur on the main thread for the graphics backend.
    WindowSystemInfo prepared_wsi(wsi);
    g_video_backend->PrepareWindow(prepared_wsi);

    VideoBackendBase::PopulateBackendInfo(wsi);
    if (!g_video_backend->Initialize(wsi))
    {
      PanicAlertFmt("Failed to initialize video backend!");
      return;
    }
  }

#ifdef WINRT_XBOX
  {
    GAMING_DEVICE_MODEL_INFORMATION model_info = {};
    if (SUCCEEDED(GetGamingDeviceModelInformation(&model_info)))
    {
      const bool is_series = (model_info.deviceId == GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_S ||
                              model_info.deviceId == GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_X ||
                              model_info.deviceId == GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_X_DEVKIT);
      const bool desired_default_dual_core = !is_series;  // true on Xbox One family, false on Series

      const auto base_layer = Config::GetLayer(Config::LayerType::Base);
      if (base_layer && !base_layer->Exists(Config::MAIN_CPU_THREAD.GetLocation()))
      {
        Config::SetBase(Config::MAIN_CPU_THREAD, desired_default_dual_core);
        Config::Save();
      }
    }
  }
#endif

  ImGuiIO& io = ImGui::GetIO();
  
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.ConfigWindowsResizeFromEdges = true;
  
  io.FontGlobalScale = 2.0f;
  
  ciface::WGInput::Init();
  g_controller_interface.RefreshDevices();

  io.AddKeyEvent(ImGuiKey_Backspace, true);  // When key is pressed

  io.AddKeyEvent(ImGuiKey_Backspace, false);  // When key is released

  std::string profiles_path = File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR +
                              Wiimote::GetConfig()->GetProfileDirectoryName();
  for (const auto& filename : Common::DoFileSearch({profiles_path}, {".ini"}))
  {
    std::string basename;
    SplitPath(filename, nullptr, &basename, nullptr);
    if (!basename.empty())
      m_wiimote_profiles.emplace_back(basename);
  }

  m_wiimote_profiles.emplace_back("None");
  m_wiimote_profiles.emplace_back("Wiimote + Nunchuk");
  m_wiimote_profiles.emplace_back("Classic Controller");
  m_wiimote_profiles.emplace_back("Sideways Wiimote");

  profiles_path =
      File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR + Pad::GetConfig()->GetProfileDirectoryName();
  for (const auto& filename : Common::DoFileSearch({profiles_path}, {".ini"}))
  {
    std::string basename;
    SplitPath(filename, nullptr, &basename, nullptr);
    if (!basename.empty())
      m_gc_profiles.emplace_back(basename);
  }

  m_gc_profiles.emplace_back("None");
  m_gc_profiles.emplace_back("Default");

  Wiimote::LoadConfig();
  Pad::LoadConfig();

  PopulateControls();
  LoadGameList();
  LoadThemes();

#ifdef USE_RETRO_ACHIEVEMENTS
#ifdef WINRT_XBOX
  // Get the CoreWindow handle for UWP applications
  CoreWindow window = CoreWindow::GetForCurrentThread();
  void* window_handle = winrt::get_abi(window);
  AchievementManager::GetInstance().Init(reinterpret_cast<void*>(window_handle));
#else
  // ImGui doesn't have a window handle, so pass nullptr for other platforms
  AchievementManager::GetInstance().Init(nullptr);
#endif
  // Disable debug mode if hardcore is active
  if (AchievementManager::GetInstance().IsHardcoreModeActive())
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_DEBUGGING, false);
  // This needs to trigger on both RA_HARDCORE_ENABLED and RA_ENABLED
  m_config_changed_callback_id = Config::AddConfigChangedCallback(
      [this] { this->OnHardcoreChanged(); });
  // If hardcore is enabled when the emulator starts, make sure it turns off what it needs to
  if (Config::Get(Config::RA_HARDCORE_ENABLED))
    OnHardcoreChanged();
#endif  // USE_RETRO_ACHIEVEMENTS

  m_ccat = (CarouselCategory)Config::Get(Config::FRONTEND_LAST_CATEGORY);
  m_last_category = m_ccat;
  FilterGamesForCategory();

  m_selectedGameIdx = Config::Get(Config::FRONTEND_LAST_GAME);

  if (m_selectedGameIdx >= m_displayed_games.size() || m_selectedGameIdx < 0)
    m_selectedGameIdx = 0;

  m_carousel_scroll_offset = 0.0f;
  m_target_scroll_offset = 0.0f;
  m_game_info_fade_alpha = 1.0f;
  m_game_selection_timer = 0.0f;
    
  int saved_color_theme = Config::Get(Config::FRONTEND_COLOR_THEME);
  ApplyColorTheme(saved_color_theme);
  ApplyModernStyling();
}

void ImGuiFrontend::PopulateControls()
{
  g_controller_interface.RefreshDevices();

  if (!g_controller_interface.HasDefaultDevice())
    return;

  for (auto& device_str : g_controller_interface.GetAllDeviceStrings())
  {
    ciface::Core::DeviceQualifier dq;
    dq.FromString(device_str);

    auto device = g_controller_interface.FindDevice(dq);
    if (device)
    {
      m_controllers.push_back(std::move(device));
    }
  }
}

void ImGuiFrontend::RefreshControls(bool updateGameSelection)
{
  if (m_controllers.empty())
    PopulateControls();

  bool input_handled = false;
  for (auto device : m_controllers)
  {
    if (input_handled || device == nullptr || !device->IsValid())
      return;

    device->UpdateInput();

    // wrap around if exceeding the max games or going below
    if (updateGameSelection)
    {
      auto now = std::chrono::high_resolution_clock::now();
      long timeSinceLastInput =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - m_scroll_last).count();
      if (TryInput("Pad W", device))
      {
        if (!m_direction_pressed)
        {
          int new_index = m_selectedGameIdx <= 0 ?
                              static_cast<int>(m_displayed_games.size()) - 1 :
                              m_selectedGameIdx - 1;
          SmoothScrollToGame(new_index);
          m_direction_pressed = true;
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Pad E", device))
      {
        if (!m_direction_pressed)
        {
          int new_index = m_selectedGameIdx >= static_cast<int>(m_displayed_games.size()) - 1 ?
                              0 :
                              m_selectedGameIdx + 1;
          SmoothScrollToGame(new_index);
          m_direction_pressed = true;
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Left X-", device))
      {
        if (timeSinceLastInput > 200L)
        {
          int new_index = m_selectedGameIdx <= 0 ?
                              static_cast<int>(m_displayed_games.size()) - 1 :
                              m_selectedGameIdx - 1;
          SmoothScrollToGame(new_index);
          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Left X+", device))
      {
        if (timeSinceLastInput > 200L)
        {
          int new_index = m_selectedGameIdx >= static_cast<int>(m_displayed_games.size()) - 1 ?
                              0 :
                              m_selectedGameIdx + 1;
          SmoothScrollToGame(new_index);
          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Bumper L", device))
      {
        if (timeSinceLastInput > 250L)
        {
          if (!m_state.showListView && !m_state.showSettingsWindow && !m_state.showPropertiesWindow && g_netplay_dialog == nullptr)
          {
            int idx = -1 + (int)m_ccat;
            if (idx < 1)
              idx = -1 + (int)CCount;

            m_ccat = (CarouselCategory)idx;
          }

          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Bumper R", device))
      {
        if (timeSinceLastInput > 250L)
        {
          if (!m_state.showListView && !m_state.showSettingsWindow && !m_state.showPropertiesWindow && g_netplay_dialog == nullptr)
          {
            int idx = 1 + (int)m_ccat;
            if (idx >= CCount)
              idx = 1;

            m_ccat = (CarouselCategory)idx;
          }

          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (m_displayed_games.size() > 10 && TryInput("Trigger L", device))
      {
        if (timeSinceLastInput > 500L)
        {
          int new_index = m_selectedGameIdx - 10;
          if (new_index < 0)
          {
            // wrap around, total games + -index
            new_index = static_cast<int>(m_displayed_games.size()) + new_index;
          }
          
          SmoothScrollToGame(new_index);
          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (m_displayed_games.size() > 10 && TryInput("Trigger R", device))
      {
        if (timeSinceLastInput > 500L)
        {
          int new_index = m_selectedGameIdx + 10;
          if (new_index >= static_cast<int>(m_displayed_games.size()))
          {
            // wrap around, i - total games
            new_index = new_index - static_cast<int>(m_displayed_games.size());
          }
          
          SmoothScrollToGame(new_index);
          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else
      {
        m_direction_pressed = false;
      }
    }
  }
}

FrontendResult ImGuiFrontend::RunUntilSelection()
{
  return RunMainLoop();
}

FrontendResult ImGuiFrontend::RunMainLoop()
{
  FrontendResult selection;

  if (g_netplay_dialog)
  {
    // Reset for if we're exiting a game into netplay again
    g_netplay_dialog->Reset();
  }

  // Main loop
  bool done = false;
  while (!done)
  {
#ifdef WINRT_XBOX
    CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(
        winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
#endif

    if (!m_state.controlsDisabled)
      RefreshControls(!m_state.showSettingsWindow && !m_state.showPropertiesWindow);

    for (auto device : m_controllers)
    {
      if (device && device->IsValid() && !m_state.controlsDisabled)
      {
        if (TryInput("Menu", device))
        {
          if (!m_state.menuPressed && !m_state.showListView && !g_netplay_dialog)
          {
            m_state.showSettingsWindow = !m_state.showSettingsWindow;
            m_state.menuPressed = true;
            LoadGameList();
            FilterGamesForCategory();
            if (m_selectedGameIdx >= m_displayed_games.size() || m_selectedGameIdx < 0)
              m_selectedGameIdx = 0;
          }

          break;
        }
        else if (TryInput("Button Y", device))
        {
          if (!m_state.menuPressed && !m_state.showSettingsWindow && !m_state.showPropertiesWindow && !g_netplay_dialog)
          {
            m_state.showListView = !m_state.showListView;
            m_state.menuPressed = true;
          }

          break;
        }
        else if (TryInput("Button X", device))
        {
          if (!m_state.menuPressed && !m_state.showSettingsWindow && !m_state.showListView && 
              !g_netplay_dialog && !m_displayed_games.empty())
          {
            // Open properties for currently selected game
            m_state.showPropertiesWindow = !m_state.showPropertiesWindow;
            if (m_state.showPropertiesWindow && m_selectedGameIdx < static_cast<int>(m_displayed_games.size()))
            {
              m_state.propertiesGame = m_displayed_games[m_selectedGameIdx];
            }
            m_state.menuPressed = true;
          }

          break;
        }
        else if (TryInput("View", device))
        {
          if (!m_state.menuPressed && !m_state.showListView && !m_state.showSettingsWindow && !m_state.showPropertiesWindow)
          {
            if (g_netplay_dialog)
            {
              g_netplay_client = nullptr;
              g_netplay_server = nullptr;
              g_netplay_dialog = nullptr;
            }
            else
            {
              g_netplay_dialog = std::make_shared<ImGuiNetPlay>(this, m_games, m_frame_scale);
            }

            m_state.menuPressed = true;
          }

          break;
        }
        else
        {
          m_state.menuPressed = false;
        }
      }
    }

    ImGuiIO& io = ImGui::GetIO();

    {
      std::unique_lock lk(UWP::g_buffer_mutex);

      for (uint32_t c : UWP::g_char_buffer)
      {
        io.AddInputCharacter(c);

        if (c == '\b')
        {
          io.AddKeyEvent(ImGuiKey_Backspace, true);
          io.AddKeyEvent(ImGuiKey_Backspace, false);
        }
      }

      UWP::g_char_buffer.clear();
    }

    // Draw Background first
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowPos(ImVec2(0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

   if (ImGui::Begin("Background", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav))
   {
        ImGui::Image((ImTextureID)(intptr_t)m_selected_theme->GetBackground(m_state.currentBG).get(),
                    ImGui::GetIO().DisplaySize);

      if (m_state.showListView)
      {
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Image((ImTextureID)(intptr_t)m_selected_theme->GetBackground(BG_List_UI).get(),
                     ImGui::GetIO().DisplaySize);
      }
      else if (!m_state.showSettingsWindow && g_netplay_dialog == nullptr)
      {
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Image((ImTextureID)(intptr_t)m_selected_theme->GetBackground(BG_Carousel_UI).get(),
                     ImGui::GetIO().DisplaySize);
      }
   }
   ImGui::End();
    ImGui::PopStyleVar(3);
    // -- Background

    if (g_netplay_dialog != nullptr)
    {
      m_state.currentBG = BG_Netplay;

      auto result = g_netplay_dialog->Draw();
      if (result == BootGame)
      {
        selection.netplay = true;
        break;
      }
      else if (result == ExitNetplay)
      {
        g_netplay_dialog = nullptr;
      }
    }
    else if (m_state.showSettingsWindow)
    {
      m_state.currentBG = BG_Menu;

      // Block all input to background when settings window is open
      ImGui::GetIO().WantCaptureMouse = true;
      ImGui::GetIO().WantCaptureKeyboard = true;

      ImGui::SetNextWindowSize(ImVec2(700 * m_frame_scale, 425 * m_frame_scale));
      ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (700 / 2) * m_frame_scale,
                                     ImGui::GetIO().DisplaySize.y / 2 - (425 / 2) * m_frame_scale),
                              ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Settings", nullptr,
                       ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
      {
        DrawSettingsMenu(&m_state, m_frame_scale);
      }
      ImGui::End();
    }
    else if (m_state.showPropertiesWindow && m_state.propertiesGame)
    {
      DrawPropertiesDialog(&m_state, m_frame_scale);
    }
    else if (m_state.showListView)
    {
      m_state.currentBG = BG_List;

      selection = CreateListPage();
      if (selection.game_result != nullptr || selection.netplay)
      {
        break;
      }
    }
    else
    {
      m_state.currentBG = static_cast<ThemeBG>(m_ccat);

      selection = CreateMainPage();
      if (selection.game_result != nullptr)
      {
        Config::SetBase(Config::FRONTEND_LAST_GAME, (int)m_selectedGameIdx);
        Config::SetBase(Config::FRONTEND_LAST_CATEGORY, (int)m_ccat);
        Config::Save();

        break;
      }
      else if (selection.netplay)
      {
        break;
      }
    }

    // Draw achievements window if enabled and game is loaded
    DrawAchievementsWindow(&m_state);

    g_presenter->Present();

    if (m_state.pending_boot_params)
      return FrontendResult(std::move(m_state.pending_boot_params));
  }

  ImGui::SetNextWindowSize(ImVec2(100 * m_frame_scale, 50 * m_frame_scale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (100 / 2) * m_frame_scale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (50 / 2) * m_frame_scale));
  if (ImGui::Begin("Loading..", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Loading..");
  }
  ImGui::End();

  g_presenter->Present();

  return selection;
}

void CreateGeneralTab(UIState* state)
{
  const Core::CPUThreadGuard guard(Core::System::GetInstance());

  bool dualCore = Config::Get(Config::MAIN_CPU_THREAD);
  if (ImGui::Checkbox("Dual Core", &dualCore))
  {
    Config::SetBaseOrCurrent(Config::MAIN_CPU_THREAD, dualCore);
    Config::Save();
  }

  bool SyncGPU = Config::Get(Config::MAIN_SYNC_GPU);
  if (ImGui::Checkbox("Synchronize GPU Thread", &SyncGPU))
  {
    Config::SetBaseOrCurrent(Config::MAIN_SYNC_GPU, SyncGPU);
    Config::Save();
  }
  
  bool cheats = Config::Get(Config::MAIN_ENABLE_CHEATS);
  if (ImGui::Checkbox("Enable Cheats", &cheats))
  {
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_CHEATS, cheats);
    Config::Save();
  }

  bool mismatchedRegion = Config::Get(Config::MAIN_OVERRIDE_REGION_SETTINGS);
  if (ImGui::Checkbox("Allow Mismatched Region Settings", &mismatchedRegion))
  {
    Config::SetBaseOrCurrent(Config::MAIN_OVERRIDE_REGION_SETTINGS, mismatchedRegion);
    Config::Save();
  }

  bool changeDiscs = Config::Get(Config::MAIN_AUTO_DISC_CHANGE);
  if (ImGui::Checkbox("Change Discs Automatically", &changeDiscs))
  {
    Config::SetBaseOrCurrent(Config::MAIN_AUTO_DISC_CHANGE, changeDiscs);
    Config::Save();
  }

  static const char* speed_limit_items[] = {
    "Unlimited", 
    "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%",
    "100% (Normal Speed)",
    "110%", "120%", "130%", "140%", "150%", "160%", "170%", "180%", "190%", "200%"
  };

  float current_speed = Config::Get(Config::MAIN_EMULATION_SPEED);
  static int current_speed_index = current_speed <= 0.0f ? 0 : static_cast<int>(current_speed * 10);
  
  if (ImGui::Combo("Speed Limit", &current_speed_index, speed_limit_items,
                   IM_ARRAYSIZE(speed_limit_items)))
  {
    float new_speed = current_speed_index <= 0 ? 0.0f : current_speed_index * 0.1f;
    Config::SetBaseOrCurrent(Config::MAIN_EMULATION_SPEED, new_speed);
    Config::Save();
  }

  const auto fallback = Config::Get(Config::MAIN_FALLBACK_REGION);
  if (ImGui::TreeNode("Fallback Region"))
  {
    if (ImGui::RadioButton("NTSC-J", fallback == DiscIO::Region::NTSC_J))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::NTSC_J);
      Config::Save();
    }

    if (ImGui::RadioButton("NTSC-U", fallback == DiscIO::Region::NTSC_U))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::NTSC_U);
      Config::Save();
    }

    if (ImGui::RadioButton("PAL", fallback == DiscIO::Region::PAL))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::PAL);
      Config::Save();
    }

    if (ImGui::RadioButton("Unknown", fallback == DiscIO::Region::Unknown))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::Unknown);
      Config::Save();
    }

    if (ImGui::RadioButton("NTSC-K", fallback == DiscIO::Region::NTSC_K))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::NTSC_K);
      Config::Save();
    }

    ImGui::TreePop();
  }
}

void CreateInterfaceTab(UIState* state)
{
  bool showOSD = Config::Get(Config::MAIN_OSD_MESSAGES);
  if (ImGui::Checkbox("Show On-Screen Messages", &showOSD))
  {
    Config::SetBaseOrCurrent(Config::MAIN_OSD_MESSAGES, showOSD);
    Config::Save();
  }

  ImGui::Spacing();

  if (ImGui::BeginCombo("Theme", m_selected_theme->GetName().c_str()))
  {
    for (auto& theme : m_themes)
    {
      if (ImGui::Selectable(theme.first.c_str(),
                            m_selected_theme->GetName() == theme.second.GetName()))
      {
        m_selected_theme = &theme.second;
        Config::SetBaseOrCurrent(Config::FRONTEND_SELECTED_THEME, theme.first);
      }
    }

    ImGui::EndCombo();
  }

  ImGui::Spacing();

    static int current_theme = Config::Get(Config::FRONTEND_COLOR_THEME);
    const char* theme_names[] = {
      "Dark Theme", "Light Theme", "Classic Theme", "Ocean Blue", "Purple Night",
      "Forest Green", "Cherry Red", "Amber Gold", "Steel Blue", "Sunset Orange",
      "Cyber Pink", "Mint Fresh"
    };

    if (ImGui::Combo("Color Theme", &current_theme, theme_names, IM_ARRAYSIZE(theme_names)))
    {
      ApplyColorTheme(current_theme);

      ApplyModernStyling();

      Config::SetBaseOrCurrent(Config::FRONTEND_COLOR_THEME, current_theme);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Sets the style of Dolphin's user interface.\n"
                       "If unsure, select Dark Theme.");
    }

  bool useTitleDatabase = Config::Get(Config::MAIN_USE_BUILT_IN_TITLE_DATABASE);
  if (ImGui::Checkbox("Use Built-In Database of Game Names", &useTitleDatabase))
  {
    Config::SetBaseOrCurrent(Config::MAIN_USE_BUILT_IN_TITLE_DATABASE, useTitleDatabase);
    Config::Save();
  }
  ImGui::TextWrapped("Use Dolphin's built-in database of game names instead of reading from "
                     "the game files.\n\nIf unsure, leave this checked.");

  bool disableScreensaver = Config::Get(Config::MAIN_DISABLE_SCREENSAVER);
  if (ImGui::Checkbox("Disable Screensaver", &disableScreensaver))
  {
    Config::SetBaseOrCurrent(Config::MAIN_DISABLE_SCREENSAVER, disableScreensaver);
    Config::Save();
  }
  ImGui::TextWrapped("Disable the system screensaver while Dolphin is running.\n\n"
                     "If unsure, leave this checked.");

  bool focusedHotkeys = Config::Get(Config::MAIN_FOCUSED_HOTKEYS);
  if (ImGui::Checkbox("Hotkeys Require Window Focus", &focusedHotkeys))
  {
    Config::SetBaseOrCurrent(Config::MAIN_FOCUSED_HOTKEYS, focusedHotkeys);
    Config::Save();
  }
  ImGui::TextWrapped("When checked, hotkeys will only work when Dolphin has focus.\n\n"
                     "If unsure, leave this checked.");

  bool confirmOnStop = Config::Get(Config::MAIN_CONFIRM_ON_STOP);
  if (ImGui::Checkbox("Confirm on Stop", &confirmOnStop))
  {
    Config::SetBaseOrCurrent(Config::MAIN_CONFIRM_ON_STOP, confirmOnStop);
    Config::Save();
  }
  ImGui::TextWrapped("Show a confirmation dialog when stopping emulation.\n\n"
                     "If unsure, leave this checked.");

  bool timeTracking = Config::Get(Config::MAIN_TIME_TRACKING);
  if (ImGui::Checkbox("Enable Play Time Tracking", &timeTracking))
  {
    Config::SetBaseOrCurrent(Config::MAIN_TIME_TRACKING, timeTracking);
    Config::Save();
  }
  ImGui::TextWrapped("Track play time for games.\n\n"
                     "If unsure, leave this checked.");

  if (ImGui::CollapsingHeader("Advanced Interface Options"))
  {
    bool usePanicHandlers = Config::Get(Config::MAIN_USE_PANIC_HANDLERS);
    if (ImGui::Checkbox("Use Panic Handlers", &usePanicHandlers))
    {
      Config::SetBaseOrCurrent(Config::MAIN_USE_PANIC_HANDLERS, usePanicHandlers);
      Config::Save();
    }
    ImGui::TextWrapped("Handle panic events (crashes) internally instead of letting the OS handle them. "
                       "Useful for debugging.\n\nIf unsure, leave this checked.");

    bool keepWindowOnTop = Config::Get(Config::MAIN_KEEP_WINDOW_ON_TOP);
    if (ImGui::Checkbox("Keep Window on Top", &keepWindowOnTop))
    {
      Config::SetBaseOrCurrent(Config::MAIN_KEEP_WINDOW_ON_TOP, keepWindowOnTop);
      Config::Save();
    }
    ImGui::TextWrapped("Keep the Dolphin window on top of other windows.\n\n"
                       "If unsure, leave this unchecked.");
  }
}

void CreateGraphicsTab(UIState* state)
{
  const Core::CPUThreadGuard guard(Core::System::GetInstance());
  if (ImGui::TreeNode("General"))
  {
    // Add backend selection
    std::vector<std::string> backend_names;
    for (auto& backend : VideoBackendBase::GetAvailableBackends())
    {
      backend_names.push_back(backend->GetName());
    }

    std::string current_backend = Config::Get(Config::MAIN_GFX_BACKEND);
    int current_backend_idx = 0;
    for (size_t i = 0; i < backend_names.size(); ++i)
    {
      if (backend_names[i] == current_backend)
      {
        current_backend_idx = static_cast<int>(i);
        break;
      }
    }

    if (ImGui::Combo(
            "Backend", &current_backend_idx,
            [](void* data, int idx, const char** out_text) {
              auto* backends = static_cast<std::vector<std::string>*>(data);
              *out_text = backends->at(idx).c_str();
              return true;
            },
            &backend_names, static_cast<int>(backend_names.size())))
    {
      Config::SetBaseOrCurrent(Config::MAIN_GFX_BACKEND, backend_names[current_backend_idx]);
      Config::Save();
    }

    /* For now we don't need it but you never know so I'll just keep it but commented out for now.
    if (!g_backend_info.Adapters.empty())
    {
      int current_adapter = Config::Get(Config::GFX_ADAPTER);
      if (ImGui::Combo("Adapter", &current_adapter, [](void* data, int idx, const char** out_text) {
        auto* adapters = static_cast<std::vector<std::string>*>(data);
        *out_text = adapters->at(idx).c_str();
        return true;
      }, &g_backend_info.Adapters, static_cast<int>(g_backend_info.Adapters.size())))
      {
        Config::SetBaseOrCurrent(Config::GFX_ADAPTER, current_adapter);
        Config::Save();
      }
    }
    */

    bool vSync = Config::Get(Config::GFX_VSYNC);
    if (ImGui::Checkbox("V-Sync", &vSync))
    {
      Config::SetBaseOrCurrent(Config::GFX_VSYNC, vSync);
      Config::Save();
    }

    const char* aspect_items[] = {"Auto", "Force 16:9", "Force 4:3", "Stretch"};
        int aspect_idx = 0;
        auto aspect = Config::Get(Config::GFX_ASPECT_RATIO);
        switch (aspect)
        {
        case AspectMode::Auto:
          aspect_idx = 0;
          break;
        case AspectMode::ForceWide:
          aspect_idx = 1;
          break;
        case AspectMode::ForceStandard:
          aspect_idx = 2;
          break;
        case AspectMode::Stretch:
          aspect_idx = 3;
          break;
        case AspectMode::Custom:
          aspect_idx = 4;
          break;
        case AspectMode::CustomStretch:
          aspect_idx = 5;
          break;
        }

        if (ImGui::Combo("Aspect Ratio", &aspect_idx, aspect_items, IM_ARRAYSIZE(aspect_items)))
        {
          switch (aspect_idx)
          {
          case 0:
            Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::Auto);
            break;
          case 1:
            Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::ForceWide);
            break;
          case 2:
            Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::ForceStandard);
            break;
          case 3:
            Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::Stretch);
            break;
          case 4:
            Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::Custom);
            break;
          case 5:
            Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::CustomStretch);
            break;
          }
          Config::Save();
        }

    const char* ir_items[] = {"Auto (Multiple of 640x528)",      "Native (640x528)",
                              "2x Native (1280x1056) for 720p",  "3x Native (1920x1584) for 1080p",
                              "4x Native (2560x2112) for 1440p", "5x Native (3200x2640)",
                              "6x Native (3840x3168) for 4K",    "7x Native (4480x3696)",
                              "8x Native (5120x4224) for 5K"};

    int ir_idx = Config::Get(Config::GFX_EFB_SCALE);
    if (ImGui::Combo("Internal Resolution", &ir_idx, ir_items, IM_ARRAYSIZE(ir_items)))
    {
      Config::SetBase(Config::GFX_EFB_SCALE, ir_idx);
      Config::Save();
    }

    const char* shader_items[] = {"Synchronous", "Hybrid Ubershaders", "Exclusive Ubershaders",
                                  "Skip Drawing"};
        int shader_idx = 0;
        auto shader = Config::Get(Config::GFX_SHADER_COMPILATION_MODE);
        switch (shader)
        {
        case ShaderCompilationMode::Synchronous:
          shader_idx = 0;
          break;
        case ShaderCompilationMode::SynchronousUberShaders:
          shader_idx = 1;
          break;
        case ShaderCompilationMode::AsynchronousUberShaders:
          shader_idx = 2;
          break;
        case ShaderCompilationMode::AsynchronousSkipRendering:
          shader_idx = 3;
          break;
        }

        if (ImGui::Combo("Shader Compilation Mode", &shader_idx, shader_items, IM_ARRAYSIZE(shader_items)))
        {
          switch (shader_idx)
          {
          case 0:
            Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE, ShaderCompilationMode::Synchronous);
            break;
          case 1:
            Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE, ShaderCompilationMode::SynchronousUberShaders);
            break;
          case 2:
            Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE, ShaderCompilationMode::AsynchronousUberShaders);
            break;
          case 3:
            Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE, ShaderCompilationMode::AsynchronousSkipRendering);
            break;
          }
          Config::Save();
        }
        ImGui::TextWrapped("Specialized: Ubershaders are never used. Hybrid: Ubershaders prevent stuttering during compilation. Exclusive: Always use ubershaders. Skip Drawing: Prevents stuttering by not rendering waiting objects.");

        bool waitForCompile = Config::Get(Config::GFX_WAIT_FOR_SHADERS_BEFORE_STARTING);
        if (ImGui::Checkbox("Compile Shaders Before Starting", &waitForCompile))
        {
          Config::SetBaseOrCurrent(Config::GFX_WAIT_FOR_SHADERS_BEFORE_STARTING, waitForCompile);
          Config::Save();
        }
        ImGui::TextWrapped("Waits for all shaders to finish compiling before starting a game.");

    bool shaderCache = Config::Get(Config::GFX_SHADER_CACHE);
    if (ImGui::Checkbox("Shader Cache", &shaderCache))
    {
      Config::SetBaseOrCurrent(Config::GFX_SHADER_CACHE, shaderCache);
      Config::Save();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Enhancements"))
  {
    bool perPixelLighting = Config::Get(Config::GFX_ENABLE_PIXEL_LIGHTING);
    if (ImGui::Checkbox("Per-Pixel Lighting", &perPixelLighting))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENABLE_PIXEL_LIGHTING, perPixelLighting);
      Config::Save();
    }

    bool disableFog = Config::Get(Config::GFX_DISABLE_FOG);
    if (ImGui::Checkbox("Disable Fog", &disableFog))
    {
      Config::SetBaseOrCurrent(Config::GFX_DISABLE_FOG, disableFog);
      Config::Save();
    }

    bool disableCopyFilter = Config::Get(Config::GFX_ENHANCE_DISABLE_COPY_FILTER);
    if (ImGui::Checkbox("Disable Copy Filter", &disableCopyFilter))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_DISABLE_COPY_FILTER, disableCopyFilter);
      Config::Save();
    }

    bool forceTrueColor = Config::Get(Config::GFX_ENHANCE_FORCE_TRUE_COLOR);
    if (ImGui::Checkbox("Force True Color", &forceTrueColor))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_FORCE_TRUE_COLOR, forceTrueColor);
          Config::Save();
        }
        ImGui::TextWrapped("Disables the blending of adjacent rows when copying the EFB. May improve quality in some cases.");

    bool arbitraryMipmapDetection = Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION);
    if (ImGui::Checkbox("Arbitrary Mipmap Detection", &arbitraryMipmapDetection))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION,
                               arbitraryMipmapDetection);
          Config::Save();
        }
        ImGui::TextWrapped("Enables detection of arbitrary mipmaps, which some games use for special distance-based effects.");

    if (arbitraryMipmapDetection)
    {
      float threshold = Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION_THRESHOLD);
      if (ImGui::SliderFloat("Mipmap Detection Threshold", &threshold, 1.0f, 50.0f, "%.1f"))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION_THRESHOLD,
                                 threshold);
        Config::Save();
      }
      ImGui::TextWrapped("Adjusts the threshold for detecting arbitrary mipmaps. Higher values "
                         "increase detection sensitivity but may cause false positives.");
    }

#ifndef WINRT_XBOX  // I don't have a hdr impl yet for UWP.
    bool hdrOutput = Config::Get(Config::GFX_ENHANCE_HDR_OUTPUT);
    if (ImGui::Checkbox("HDR Output", &hdrOutput))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_HDR_OUTPUT, hdrOutput);
      Config::Save();
    }
    ImGui::TextWrapped(
        "Enables HDR output when supported by the display. Requires a compatible GPU and display.");
#endif
    const char* anisolevel_items[] = {"1x", "2x", "4x", "8x", "16x"};
    auto aniso = Config::Get(Config::GFX_ENHANCE_MAX_ANISOTROPY);
    int aniso_idx = static_cast<int>(aniso);
    if (ImGui::Combo("Anisotropic Filtering", &aniso_idx, anisolevel_items,
                     IM_ARRAYSIZE(anisolevel_items)))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_MAX_ANISOTROPY,
                               static_cast<AnisotropicFilteringMode>(aniso_idx));
      Config::Save();
    }

    bool ssaa = Config::Get(Config::GFX_SSAA);
    if (ImGui::RadioButton("MSAA", !ssaa))
    {
      Config::SetBaseOrCurrent(Config::GFX_SSAA, false);
      Config::Save();
    }

    if (ImGui::RadioButton("SSAA", ssaa))
    {
      Config::SetBaseOrCurrent(Config::GFX_SSAA, true);
      Config::Save();
    }

    const char* aalevel_items[] = {"None", "2x", "4x", "8x"};
    auto msaa = Config::Get(Config::GFX_MSAA);
    int msaa_idx = msaa;
    if (ImGui::Combo("MSAA Level", &msaa_idx, aalevel_items, IM_ARRAYSIZE(aalevel_items)))
    {
      Config::SetBaseOrCurrent(Config::GFX_MSAA, msaa_idx);
      Config::Save();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Hacks"))
  {
    if (ImGui::TreeNode("Embedded Frame Buffer (EFB)"))
    {
      bool skipEfbCpu = Config::Get(Config::GFX_HACK_EFB_ACCESS_ENABLE);
      if (ImGui::Checkbox("Enable EFB Access from CPU", &skipEfbCpu))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_EFB_ACCESS_ENABLE, skipEfbCpu);
        Config::Save();
      }

        bool ignoreFormatChanges = Config::Get(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES);
        if (ImGui::Checkbox("Emulate Format Changes", &ignoreFormatChanges))
        {
          Config::SetBaseOrCurrent(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES, ignoreFormatChanges);
          Config::Save();
        }
        ImGui::TextWrapped("Emulates EFB format changes in software. Required for some games to render correctly.");

        bool storeEfbCopies = Config::Get(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM);
        if (ImGui::Checkbox("Store EFB Copies to Texture Only", &storeEfbCopies))
        {
          Config::SetBaseOrCurrent(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM, storeEfbCopies);
          Config::Save();
        }
        ImGui::TextWrapped("Stores EFB copies in GPU texture objects instead of RAM. Improves performance but breaks some games.");

      bool deferEfbCopies = Config::Get(Config::GFX_HACK_DEFER_EFB_COPIES);
      if (ImGui::Checkbox("Defer EFB Copies to RAM", &deferEfbCopies))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_DEFER_EFB_COPIES, deferEfbCopies);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Texture Cache"))
      {
        int accuracy = Config::Get(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES);

        int slider_index = 0;
        if (accuracy == 512)
          slider_index = 1;
        else if (accuracy == 128)
          slider_index = 2;

        const char* accuracy_labels[] = {"Safe (0)", "Normal (512)", "Fast (128)"};
        if (ImGui::Combo("Texture Cache Accuracy", &slider_index, accuracy_labels, 3))
        {
          int new_accuracy = 0;
          if (slider_index == 1)
            new_accuracy = 512;
          else if (slider_index == 2)
            new_accuracy = 128;

          Config::SetBaseOrCurrent(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES, new_accuracy);
          Config::Save();
        }
        ImGui::TextWrapped("Adjusts how often the texture cache is invalidated. Safe provides most compatibility.");

      bool gpuTextureDecoding = Config::Get(Config::GFX_ENABLE_GPU_TEXTURE_DECODING);
      if (ImGui::Checkbox("GPU Texture Decoding", &gpuTextureDecoding))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENABLE_GPU_TEXTURE_DECODING, gpuTextureDecoding);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("External Frame Buffer (XFB)"))
    {
      bool storeXfbCopies = Config::Get(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM);
      if (ImGui::Checkbox("Store XFB Copies to Texture Only", &storeXfbCopies))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM, storeXfbCopies);
        Config::Save();
      }

      bool immediateXfb = Config::Get(Config::GFX_HACK_IMMEDIATE_XFB);
      if (ImGui::Checkbox("Immediately Present XFB", &immediateXfb))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_IMMEDIATE_XFB, immediateXfb);
        Config::Save();
      }

      bool skipDuplicateXfbs = Config::Get(Config::GFX_HACK_SKIP_DUPLICATE_XFBS);
      if (ImGui::Checkbox("Skip Presenting Duplicate Frames", &skipDuplicateXfbs))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_SKIP_DUPLICATE_XFBS, skipDuplicateXfbs);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Other"))
      {
        bool fastDepthCalculation = Config::Get(Config::GFX_FAST_DEPTH_CALC);
        if (ImGui::Checkbox("Fast Depth Calculation", &fastDepthCalculation))
        {
          Config::SetBaseOrCurrent(Config::GFX_FAST_DEPTH_CALC, fastDepthCalculation);
          Config::Save();
        }
        ImGui::TextWrapped("Uses a less accurate algorithm to calculate depth values.");

        bool enableBoundingBox = Config::Get(Config::GFX_HACK_BBOX_ENABLE);
        if (ImGui::Checkbox("Enable Bounding Box", &enableBoundingBox))
        {
          Config::SetBaseOrCurrent(Config::GFX_HACK_BBOX_ENABLE, enableBoundingBox);
          Config::Save();
        }
        ImGui::TextWrapped("Enables bounding box emulation. Required for some games.");

      bool vertexRounding = Config::Get(Config::GFX_HACK_VERTEX_ROUNDING);
      if (ImGui::Checkbox("Vertex Rounding", &vertexRounding))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_VERTEX_ROUNDING, vertexRounding);
        Config::Save();
      }

      bool saveTextureCacheState = Config::Get(Config::GFX_SAVE_TEXTURE_CACHE_TO_STATE);
      if (ImGui::Checkbox("Save Texture Cache to State", &saveTextureCacheState))
      {
        Config::SetBaseOrCurrent(Config::GFX_SAVE_TEXTURE_CACHE_TO_STATE, saveTextureCacheState);
        Config::Save();
      }

        bool viSkip = Config::Get(Config::GFX_HACK_VI_SKIP);
        if (ImGui::Checkbox("VBI Skip", &viSkip))
        {
          Config::SetBaseOrCurrent(Config::GFX_HACK_VI_SKIP, viSkip);
          Config::Save();
        }

      ImGui::TreePop();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Advanced"))
  {
    if (ImGui::TreeNode("Performance Statistics"))
    {
      bool showFps = Config::Get(Config::GFX_SHOW_FPS);
      if (ImGui::Checkbox("Show FPS", &showFps))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_FPS, showFps);
        Config::Save();
      }

        bool showFTimes = Config::Get(Config::GFX_SHOW_FTIMES);
        if (ImGui::Checkbox("Show Frame Times", &showFTimes))
        {
          Config::SetBaseOrCurrent(Config::GFX_SHOW_FTIMES, showFTimes);
          Config::Save();
        }

      bool showVps = Config::Get(Config::GFX_SHOW_VPS);
      if (ImGui::Checkbox("Show VPS", &showVps))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_VPS, showVps);
        Config::Save();
      }

        bool showVTimes = Config::Get(Config::GFX_SHOW_VTIMES);
        if (ImGui::Checkbox("Show VI Times", &showVTimes))
        {
          Config::SetBaseOrCurrent(Config::GFX_SHOW_VTIMES, showVTimes);
          Config::Save();
        }

      bool showSpeed = Config::Get(Config::GFX_SHOW_SPEED);
      if (ImGui::Checkbox("Show Speed", &showSpeed))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_SPEED, showSpeed);
        Config::Save();
      }

      bool showSpeedColors = Config::Get(Config::GFX_SHOW_SPEED_COLORS);
      if (ImGui::Checkbox("Show Speed Colors", &showSpeedColors))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_SPEED_COLORS, showSpeedColors);
        Config::Save();
      }

      bool overlayStats = Config::Get(Config::GFX_OVERLAY_STATS);
      if (ImGui::Checkbox("Show Rendering Statistics", &overlayStats))
      {
        Config::SetBaseOrCurrent(Config::GFX_OVERLAY_STATS, overlayStats);
        Config::Save();
      }

        bool projStats = Config::Get(Config::GFX_OVERLAY_PROJ_STATS);
        if (ImGui::Checkbox("Show Projection Statistics", &projStats))
        {
          Config::SetBaseOrCurrent(Config::GFX_OVERLAY_PROJ_STATS, projStats);
          Config::Save();
        }

        bool scissorStats = Config::Get(Config::GFX_OVERLAY_SCISSOR_STATS);
        if (ImGui::Checkbox("Show Scissor Statistics", &scissorStats))
        {
          Config::SetBaseOrCurrent(Config::GFX_OVERLAY_SCISSOR_STATS, scissorStats);
          Config::Save();
        }

      bool logRenderTime = Config::Get(Config::GFX_LOG_RENDER_TIME_TO_FILE);
      if (ImGui::Checkbox("Log Render Time to File", &logRenderTime))
      {
        Config::SetBaseOrCurrent(Config::GFX_LOG_RENDER_TIME_TO_FILE, logRenderTime);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Debugging"))
    {
      bool enableWireframe = Config::Get(Config::GFX_ENABLE_WIREFRAME);
      if (ImGui::Checkbox("Enable Wireframe", &enableWireframe))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENABLE_WIREFRAME, enableWireframe);
        Config::Save();
      }

      bool showProjStats = Config::Get(Config::GFX_OVERLAY_PROJ_STATS);
      if (ImGui::Checkbox("Show Projection Statistics", &showProjStats))
      {
        Config::SetBaseOrCurrent(Config::GFX_OVERLAY_PROJ_STATS, showProjStats);
        Config::Save();
      }

      bool enableFormatOverlay = Config::Get(Config::GFX_TEXFMT_OVERLAY_ENABLE);
      if (ImGui::Checkbox("Texture Format Overlay", &enableFormatOverlay))
      {
        Config::SetBaseOrCurrent(Config::GFX_TEXFMT_OVERLAY_ENABLE, enableFormatOverlay);
        Config::Save();
      }

      bool enableApiValidation = Config::Get(Config::GFX_ENABLE_VALIDATION_LAYER);
      if (ImGui::Checkbox("Enable API Validation Layers", &enableApiValidation))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENABLE_VALIDATION_LAYER, enableApiValidation);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Utility"))
    {
      bool loadCustomTextures = Config::Get(Config::GFX_HIRES_TEXTURES);
      if (ImGui::Checkbox("Load Custom Textures", &loadCustomTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_HIRES_TEXTURES, loadCustomTextures);
        Config::Save();
      }

      bool prefetchCustomTextures = Config::Get(Config::GFX_CACHE_HIRES_TEXTURES);
      if (ImGui::Checkbox("Prefetch Custom Textures", &prefetchCustomTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_CACHE_HIRES_TEXTURES, prefetchCustomTextures);
        Config::Save();
      }

      bool disableVramCopies = Config::Get(Config::GFX_HACK_DISABLE_COPY_TO_VRAM);
      if (ImGui::Checkbox("Disable EFB VRAM Copies", &disableVramCopies))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_DISABLE_COPY_TO_VRAM, disableVramCopies);
        Config::Save();
      }

      bool enableGraphicsMods = Config::Get(Config::GFX_MODS_ENABLE);
      if (ImGui::Checkbox("Enable Graphics Mods", &enableGraphicsMods))
      {
        Config::SetBaseOrCurrent(Config::GFX_MODS_ENABLE, enableGraphicsMods);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Texture Dumping"))
    {
      bool dumpTextures = Config::Get(Config::GFX_DUMP_TEXTURES);
      if (ImGui::Checkbox("Enable", &dumpTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_DUMP_TEXTURES, dumpTextures);
        Config::Save();
      }

      bool dumpBaseTextures = Config::Get(Config::GFX_DUMP_BASE_TEXTURES);
      if (ImGui::Checkbox("Dump Base Textures", &dumpBaseTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_DUMP_BASE_TEXTURES, dumpBaseTextures);
        Config::Save();
      }

      bool dumpMipTextures = Config::Get(Config::GFX_DUMP_MIP_TEXTURES);
      if (ImGui::Checkbox("Dump Mip Maps", &dumpMipTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_DUMP_MIP_TEXTURES, dumpMipTextures);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Frame Dumping"))
    {
      const char* resolution_items[] = {"Window Resolution",
                                        "Aspect Ratio Corrected Internal Resolution",
                                        "Raw Internal Resolution"};
      auto resolution_type = Config::Get(Config::GFX_FRAME_DUMPS_RESOLUTION_TYPE);
      int resolution_idx = static_cast<int>(resolution_type);
      if (ImGui::Combo("Resolution Type", &resolution_idx, resolution_items,
                       IM_ARRAYSIZE(resolution_items)))
      {
        Config::SetBaseOrCurrent(Config::GFX_FRAME_DUMPS_RESOLUTION_TYPE,
                                 static_cast<FrameDumpResolutionType>(resolution_idx));
        Config::Save();
      }

      int pngCompression = Config::Get(Config::GFX_PNG_COMPRESSION_LEVEL);
      if (ImGui::SliderInt("PNG Compression Level", &pngCompression, 0, 9))
      {
        Config::SetBaseOrCurrent(Config::GFX_PNG_COMPRESSION_LEVEL, pngCompression);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Misc"))
    {
      bool enableProgScan = Config::Get(Config::SYSCONF_PROGRESSIVE_SCAN);
      if (ImGui::Checkbox("Enable Progressive Scan", &enableProgScan))
      {
        Config::SetBaseOrCurrent(Config::SYSCONF_PROGRESSIVE_SCAN, enableProgScan);
        Config::Save();
      }

#ifndef WINRT_XBOX  // This currently only works on the Vulkan backend
        bool backendMultithreading = Config::Get(Config::GFX_BACKEND_MULTITHREADING);
        if (ImGui::Checkbox("Backend Multithreading", &backendMultithreading))
        {
          Config::SetBaseOrCurrent(Config::GFX_BACKEND_MULTITHREADING, backendMultithreading);
          Config::Save();
        }
        ImGui::TextWrapped("Enables multithreaded command submission. Currently only works with Vulkan backend.");
#endif

        int commandBufferInterval = Config::Get(Config::GFX_COMMAND_BUFFER_EXECUTE_INTERVAL);
        if (ImGui::SliderInt("Command Buffer Execute Interval", &commandBufferInterval, 0, 1000))
        {
          Config::SetBaseOrCurrent(Config::GFX_COMMAND_BUFFER_EXECUTE_INTERVAL, commandBufferInterval);
          Config::Save();
        }
        ImGui::TextWrapped("Submits command buffers every N draw calls. Lower values may reduce latency.");

        int shaderCompilerThreads = Config::Get(Config::GFX_SHADER_COMPILER_THREADS);
        if (ImGui::SliderInt("Shader Compiler Threads", &shaderCompilerThreads, 1, 8))
        {
          Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILER_THREADS, shaderCompilerThreads);
          Config::Save();
        }

        int shaderPrecompilerThreads = Config::Get(Config::GFX_SHADER_PRECOMPILER_THREADS);
        if (ImGui::SliderInt("Shader Precompiler Threads", &shaderPrecompilerThreads, -1, 8))
        {
          Config::SetBaseOrCurrent(Config::GFX_SHADER_PRECOMPILER_THREADS, shaderPrecompilerThreads);
          Config::Save();
        }
        ImGui::TextWrapped("-1 = Automatic, 0 = Disabled, >0 = Specific number of threads");

      bool preferVsForLinePoint = Config::Get(Config::GFX_PREFER_VS_FOR_LINE_POINT_EXPANSION);
      if (ImGui::Checkbox("Prefer VS for Point/Line Expansion", &preferVsForLinePoint))
      {
        Config::SetBaseOrCurrent(Config::GFX_PREFER_VS_FOR_LINE_POINT_EXPANSION,
                                 preferVsForLinePoint);
        Config::Save();
      }

      bool cpuCull = Config::Get(Config::GFX_CPU_CULL);
      if (ImGui::Checkbox("Cull Vertices on the CPU", &cpuCull))
      {
        Config::SetBaseOrCurrent(Config::GFX_CPU_CULL, cpuCull);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Experimental"))
    {
      bool deferEfbAccess = Config::Get(Config::GFX_HACK_EFB_DEFER_INVALIDATION);
      if (ImGui::Checkbox("Defer EFB Cache Invalidation", &deferEfbAccess))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_EFB_DEFER_INVALIDATION, deferEfbAccess);
        Config::Save();
      }

      bool manualTextureSampling = Config::Get(Config::GFX_HACK_FAST_TEXTURE_SAMPLING);
      if (ImGui::Checkbox("Fast Texture Sampling", &manualTextureSampling))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_FAST_TEXTURE_SAMPLING, manualTextureSampling);
        Config::Save();
      }

      ImGui::TreePop();
    }

    ImGui::TreePop();
  }
}

void CreateAudioTab(UIState* state)
{
  if (ImGui::CollapsingHeader("DSP Options"))
  {
    const char* dsp_items[] = {"HLE", "LLE Recompiler", "LLE Interpreter"};
    int dsp_idx =
        Config::Get(Config::MAIN_DSP_HLE) ? 0 : (Config::Get(Config::MAIN_DSP_JIT) ? 1 : 2);
    if (ImGui::Combo("DSP Emulation Engine", &dsp_idx, dsp_items, IM_ARRAYSIZE(dsp_items)))
    {
      Config::SetBaseOrCurrent(Config::MAIN_DSP_HLE, dsp_idx == 0);
      Config::SetBaseOrCurrent(Config::MAIN_DSP_JIT, dsp_idx == 1);
      Config::Save();
    }
    ImGui::TextWrapped("Selects how the Digital Signal Processor (DSP) is emulated. Determines how "
                       "the audio is processed and what system features are available.");
  }

  if (ImGui::CollapsingHeader("Volume"))
  {
    int volume = Config::Get(Config::MAIN_AUDIO_MUTED) ? 0 : Config::Get(Config::MAIN_AUDIO_VOLUME);
    if (ImGui::SliderInt("Volume", &volume, 0, 100))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_MUTED, volume == 0);
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_VOLUME, volume);
      Config::Save();
      AudioCommon::UpdateSoundStream(Core::System::GetInstance());
    }
  }

  if (ImGui::CollapsingHeader("Backend Settings"))
  {
    std::vector<std::string> backends = AudioCommon::GetSoundBackends();
    std::string current_backend = Config::Get(Config::MAIN_AUDIO_BACKEND);
    int backend_idx = 0;
    for (size_t i = 0; i < backends.size(); ++i)
    {
      if (backends[i] == current_backend)
      {
        backend_idx = static_cast<int>(i);
        break;
      }
    }

    if (ImGui::Combo(
            "Audio Backend", &backend_idx,
            [](void* data, int idx, const char** out_text) {
              auto* backends = static_cast<std::vector<std::string>*>(data);
              *out_text = backends->at(idx).c_str();
              return true;
            },
            &backends, static_cast<int>(backends.size())))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_BACKEND, backends[backend_idx]);
      Config::Save();
    }

#ifdef _WIN32
    if (backends[backend_idx] == BACKEND_WASAPI)
    {
      std::vector<std::string> devices = WASAPIStream::GetAvailableDevices();
      std::string current_device = Config::Get(Config::MAIN_WASAPI_DEVICE);
      int device_idx = 0;
      for (size_t i = 0; i < devices.size(); ++i)
      {
        if (devices[i] == current_device)
        {
          device_idx = static_cast<int>(i);
          break;
        }
      }

      if (ImGui::Combo(
              "Output Device", &device_idx,
              [](void* data, int idx, const char** out_text) {
                auto* devices = static_cast<std::vector<std::string>*>(data);
                *out_text = devices->at(idx).c_str();
                return true;
              },
              &devices, static_cast<int>(devices.size())))
      {
        Config::SetBaseOrCurrent(Config::MAIN_WASAPI_DEVICE, devices[device_idx]);
        Config::Save();
      }
    }
#endif

    if (AudioCommon::SupportsLatencyControl(backends[backend_idx]))
    {
      int latency = Config::Get(Config::MAIN_AUDIO_LATENCY);
      if (ImGui::SliderInt("Latency", &latency, 0, 200))
      {
        Config::SetBaseOrCurrent(Config::MAIN_AUDIO_LATENCY, latency);
        Config::Save();
      }
      ImGui::TextWrapped(
          "Sets the audio latency in milliseconds. Higher values may reduce audio crackling.");
    }
  }
#ifndef WINRT_XBOX  // TODO: Can't figure out how this one works will look into more if it's
                    // requested or is required
  if (ImGui::CollapsingHeader("Dolby Pro Logic II"))
  {
    bool dpl2 = Config::Get(Config::MAIN_DPL2_DECODER);
    if (ImGui::Checkbox("Enable Dolby Pro Logic II", &dpl2))
    {
      Config::SetBaseOrCurrent(Config::MAIN_DPL2_DECODER, dpl2);
      Config::Save();
    }
    ImGui::TextWrapped(
        "Enables Dolby Pro Logic II emulation using 5.1 surround. Certain backends only.");
    if (dpl2)
    {
      const char* quality_items[] = {"Lowest (Latency ~10 ms)", "Low (Latency ~20 ms)",
                                     "High (Latency ~40 ms)", "Highest (Latency ~80 ms)"};
      int quality_idx = static_cast<int>(Config::Get(Config::MAIN_DPL2_QUALITY));
      if (ImGui::Combo("Decoding Quality", &quality_idx, quality_items,
                       IM_ARRAYSIZE(quality_items)))
      {
        Config::SetBaseOrCurrent(Config::MAIN_DPL2_QUALITY,
                                 static_cast<AudioCommon::DPL2Quality>(quality_idx));
        Config::Save();
      }
      ImGui::TextWrapped("Adjusts the quality setting of the Dolby Pro Logic II decoder. Higher "
                         "presets increases audio latency.");
    }
  }
#endif

  if (ImGui::CollapsingHeader("Audio Playback Settings"))
  {
    int buffer_size = Config::Get(Config::MAIN_AUDIO_BUFFER_SIZE);
    if (ImGui::SliderInt("Audio Buffer Size", &buffer_size, 5, 100))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_BUFFER_SIZE, buffer_size);
      Config::Save();
    }
    ImGui::TextWrapped("Sets the size of the audio buffer in milliseconds. Higher values may "
                       "reduce audio crackling.");

    bool fill_gaps = Config::Get(Config::MAIN_AUDIO_FILL_GAPS);
    if (ImGui::Checkbox("Fill Gaps", &fill_gaps))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_FILL_GAPS, fill_gaps);
      Config::Save();
    }
    ImGui::TextWrapped("Fills gaps in audio playback to prevent audio crackling.");

    bool mute_on_speed_limit = Config::Get(Config::MAIN_AUDIO_MUTE_ON_DISABLED_SPEED_LIMIT);
    if (ImGui::Checkbox("Mute on Speed Limit", &mute_on_speed_limit))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_MUTE_ON_DISABLED_SPEED_LIMIT,
                               mute_on_speed_limit);
      Config::Save();
    }
    ImGui::TextWrapped("Mutes audio when emulation speed is above 100%.");
  }
}

void CreateControlsTab(UIState* state)
{
  auto devices = g_controller_interface.GetAllDeviceStrings();

  if (ImGui::BeginTabBar("controlsbar"))
  {
    if (ImGui::BeginTabItem("GameCube"))
    {
      if (ImGui::BeginTable("gc_ports_tbl", 2, ImGuiTableFlags_SizingStretchProp))
      {
        ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; i++)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          CreateGCPort(i, devices);
          ImGui::TableNextColumn();
          if (ImGui::Button(fmt::format("Map##gc{}", i).c_str(), ImVec2(-1, 0)))
          {
            state->showMappingWindow = true;
            state->mappingWindowPort = i;
            state->mappingWindowIsWii = false;
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Wii"))
    {
      if (ImGui::BeginTable("wii_ports_tbl", 2, ImGuiTableFlags_SizingStretchProp))
      {
        ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; i++)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          CreateWiiPort(i, devices);
          ImGui::TableNextColumn();
          if (ImGui::Button(fmt::format("Map##wii{}", i).c_str(), ImVec2(-1, 0)))
          {
            state->showMappingWindow = true;
            state->mappingWindowPort = i;
            state->mappingWindowIsWii = true;
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Alternate Input Sources"))
    {
      bool dsu_enabled = Config::Get(ciface::DualShockUDPClient::Settings::SERVERS_ENABLED);
      if (ImGui::Checkbox("Enable DSU", &dsu_enabled))
      {
        Config::SetBaseOrCurrent(ciface::DualShockUDPClient::Settings::SERVERS_ENABLED,
                                 dsu_enabled);
        Config::Save();
        if (dsu_enabled)
          g_controller_interface.RefreshDevices();
      }
      ImGui::Separator();
      std::string servers = Config::Get(ciface::DualShockUDPClient::Settings::SERVERS);
      std::vector<std::string> entries;
      for (size_t p = 0, n; p < servers.size(); p = n + 1)
      {
        n = servers.find(';', p);
        if (n == std::string::npos)
          break;
        auto e = servers.substr(p, n - p);
        if (!e.empty())
          entries.push_back(e);
      }
      static int selected_dsu = -1;
      for (int i = 0; i < (int)entries.size(); ++i)
      {
        if (ImGui::Selectable(entries[i].c_str(), selected_dsu == i))
          selected_dsu = i;
      }
      ImGui::Separator();
      static char dsu_addr[64] = "";
      static int dsu_port = ciface::DualShockUDPClient::DEFAULT_SERVER_PORT;
      ImGui::Text("Address");
      ImGui::SameLine();
      if (ImGui::Button("Edit IP"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      ImGui::InputText("##dsu_addr", dsu_addr, sizeof(dsu_addr));
      ImGui::InputInt("Port", &dsu_port);
      if (ImGui::Button("Add Server"))
      {
        auto entry = fmt::format("DS4:{}:{};", dsu_addr, dsu_port);
        Config::SetBaseOrCurrent(ciface::DualShockUDPClient::Settings::SERVERS, servers + entry);
        Config::Save();
        servers = Config::Get(ciface::DualShockUDPClient::Settings::SERVERS);
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove Selected") && selected_dsu >= 0)
      {
        std::string rebuilt;
        for (int i = 0; i < (int)entries.size(); ++i)
        {
          if (i == selected_dsu)
            continue;
          rebuilt += entries[i] + ";";
        }
        Config::SetBaseOrCurrent(ciface::DualShockUDPClient::Settings::SERVERS, rebuilt);
        Config::Save();
        selected_dsu = -1;
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  if (state->showMappingWindow)
    ImGuiMappingWindow::Draw(state->mappingWindowPort, state);
}

void CreateGameCubeTab(UIState* state)
{
  if (ImGui::TreeNode("IPL Settings"))
  {
    const char* language_items[] = {"English", "German", "French", "Spanish", "Italian", "Dutch"};
    auto lang = Config::Get(Config::MAIN_GC_LANGUAGE);

    for (int i = 0; i < 6; i++)
    {
      ImGui::PushID(i);
      if (ImGui::RadioButton(language_items[i], i == lang))
      {
        Config::SetBaseOrCurrent(Config::MAIN_GC_LANGUAGE, i);
        Config::Save();
      }
      ImGui::PopID();
    }

    bool have_menu = false;
    for (const std::string dir : {"USA", "JAP", "EUR"})
    {
      const auto path = "/" + dir + "/" + GC_IPL;
      if (File::Exists(File::GetUserPath(D_GCUSER_IDX) + path) ||
          File::Exists(File::GetSysDirectory() + GC_SYS_DIR + path))
      {
        have_menu = true;
        break;
      }
    }

    bool skip_ipl = Config::Get(Config::MAIN_SKIP_IPL);
    if (ImGui::Checkbox("Skip Main Menu", &skip_ipl))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SKIP_IPL, skip_ipl);
      Config::Save();
    }
    if (!have_menu)
    {
      ImGui::TextWrapped("No GameCube BIOS found. Put IPL ROMs in User/GC/<region>/IPL.bin to enable IPL functionality.");
    }

    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Load GameCube Main Menu"))
  {
    auto has_ipl = [](DiscIO::Region region) {
      std::string region_dir;
      switch (region)
      {
      case DiscIO::Region::NTSC_J:
        region_dir = "JAP";
        break;
      case DiscIO::Region::NTSC_U:
        region_dir = "USA";
        break;
      case DiscIO::Region::PAL:
        region_dir = "EUR";
        break;
      default:
        return false;
      }
      return File::Exists(File::GetUserPath(D_GCUSER_IDX) + "/" + region_dir + "/" + GC_IPL) ||
             File::Exists(File::GetSysDirectory() + GC_SYS_DIR + "/" + region_dir + "/" + GC_IPL);
    };
    auto BootGameCubeIPL = [state](DiscIO::Region region) {
      state->controlsDisabled = true;
      state->showSettingsWindow = false;
      state->pending_boot_params = std::make_unique<BootParameters>(BootParameters::IPL(region));
    };

    bool any = false;
    if (has_ipl(DiscIO::Region::NTSC_J))
    {
      any = true;
      if (ImGui::Button("NTSC-J"))
        BootGameCubeIPL(DiscIO::Region::NTSC_J);
    }
    else
    {
      ImGui::BeginDisabled();
      ImGui::Button("NTSC-J");
      ImGui::EndDisabled();
    }
    if (has_ipl(DiscIO::Region::NTSC_U))
    {
      any = true;
      if (ImGui::Button("NTSC-U"))
        BootGameCubeIPL(DiscIO::Region::NTSC_U);
    }
    else
    {
      ImGui::BeginDisabled();
      ImGui::Button("NTSC-U");
      ImGui::EndDisabled();
    }
    if (has_ipl(DiscIO::Region::PAL))
    {
      any = true;
      if (ImGui::Button("PAL"))
        BootGameCubeIPL(DiscIO::Region::PAL);
    }
    else
    {
      ImGui::BeginDisabled();
      ImGui::Button("PAL");
      ImGui::EndDisabled();
    }
    if (!any)
      ImGui::TextWrapped("Put IPL ROMs in User/GC/<region>/IPL.bin");
    ImGui::TreePop();
  }
  auto slot1 = Config::Get(Config::MAIN_SLOT_A);
  if (ImGui::TreeNode("Slot A"))
  {
    if (ImGui::RadioButton("<Nothing>", slot1 == ExpansionInterface::EXIDeviceType::None))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::None);
      Config::Save();
    }

    if (ImGui::RadioButton("Dummy", slot1 == ExpansionInterface::EXIDeviceType::Dummy))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::Dummy);
      Config::Save();
    }

    if (ImGui::RadioButton("Memory Card", slot1 == ExpansionInterface::EXIDeviceType::MemoryCard))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::MemoryCard);
      Config::Save();
    }

    if (ImGui::RadioButton("GCI Folder",
                           slot1 == ExpansionInterface::EXIDeviceType::MemoryCardFolder))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A,
                               ExpansionInterface::EXIDeviceType::MemoryCardFolder);
      Config::Save();
    }

    if (ImGui::RadioButton("USB Gecko", slot1 == ExpansionInterface::EXIDeviceType::Gecko))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::Gecko);
      Config::Save();
    }

    if (ImGui::RadioButton("Advance Game Port", slot1 == ExpansionInterface::EXIDeviceType::AGP))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::AGP);
      Config::Save();
    }

    ImGui::TreePop();
  }

  // Todo: This really shouldn't be copy+pasted and could be cleaned up.
  auto slot2 = Config::Get(Config::MAIN_SLOT_B);
  if (ImGui::TreeNode("Slot B"))
  {
    if (ImGui::RadioButton("<Nothing>", slot2 == ExpansionInterface::EXIDeviceType::None))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::None);
      Config::Save();
    }

    if (ImGui::RadioButton("Dummy", slot2 == ExpansionInterface::EXIDeviceType::Dummy))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::Dummy);
      Config::Save();
    }

    if (ImGui::RadioButton("Memory Card", slot2 == ExpansionInterface::EXIDeviceType::MemoryCard))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::MemoryCard);
      Config::Save();
    }

    if (ImGui::RadioButton("GCI Folder",
                           slot2 == ExpansionInterface::EXIDeviceType::MemoryCardFolder))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B,
                               ExpansionInterface::EXIDeviceType::MemoryCardFolder);
      Config::Save();
    }

    if (ImGui::RadioButton("USB Gecko", slot2 == ExpansionInterface::EXIDeviceType::Gecko))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::Gecko);
      Config::Save();
    }

    if (ImGui::RadioButton("Advance Game Port", slot2 == ExpansionInterface::EXIDeviceType::AGP))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::AGP);
      Config::Save();
    }

    ImGui::TreePop();
  }

  auto sp1 = Config::Get(Config::MAIN_SERIAL_PORT_1);
  if (ImGui::TreeNode("SP1"))
  {
    if (ImGui::RadioButton("<Nothing>", sp1 == ExpansionInterface::EXIDeviceType::None))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1, ExpansionInterface::EXIDeviceType::None);
      Config::Save();
    }

    if (ImGui::RadioButton("Dummy", sp1 == ExpansionInterface::EXIDeviceType::Dummy))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::Dummy);
      Config::Save();
    }

    if (ImGui::RadioButton("Broadband Adapter (TAP)",
                           sp1 == ExpansionInterface::EXIDeviceType::Ethernet))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::Ethernet);
      Config::Save();
    }

    if (ImGui::RadioButton("Broadband Adapter (XLink Kai)",
                           sp1 == ExpansionInterface::EXIDeviceType::EthernetXLink))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::EthernetXLink);
      Config::Save();
    }

    if (ImGui::RadioButton("Broadband Adapter (HLE)",
                           sp1 == ExpansionInterface::EXIDeviceType::EthernetBuiltIn))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::EthernetBuiltIn);
      Config::Save();
    }

    ImGui::TreePop();
  }
}

void CreateWiiTab(UIState* state)
{
  static std::atomic<bool> import_in_progress{false};
  static std::atomic<bool> import_complete{false};
  static std::atomic<bool> import_canceled{false};
  static std::thread import_thread;
  static std::string import_status;
  static bool show_import_progress_modal = false;
  static bool show_import_result_modal = false;
  static bool import_success = false;
  static std::chrono::steady_clock::time_point import_begin_tp{};
  static std::atomic<bool> check_in_progress{false};
  static bool show_check_result_modal = false;
  static WiiUtils::NANDCheckResult last_check_result{};

  static WiiUtils::UpdateResult last_res = WiiUtils::UpdateResult::Cancelled;
  if (PrimaryButton("Load Wii System Menu") && !state->controlsDisabled)
  {
    state->controlsDisabled = true;
    state->showSettingsWindow = false;
    state->pending_boot_params =
        std::make_unique<BootParameters>(BootParameters::NANDTitle{Titles::SYSTEM_MENU});
  }
  ImGui::TextWrapped("Boots the Wii System Menu from the NAND.");

  bool pal60 = Config::Get(Config::SYSCONF_PAL60);
  if (ImGui::Checkbox("Enable PAL60", &pal60))
  {
    Config::SetBaseOrCurrent(Config::SYSCONF_PAL60, pal60);
    Config::Save();
  }
  ImGui::TextWrapped("Enables PAL60 mode (480p) instead of standard PAL (576i) for PAL games.");

  bool wiiScreensaver = Config::Get(Config::SYSCONF_SCREENSAVER);
  if (ImGui::Checkbox("Enable Wii Screensaver", &wiiScreensaver))
  {
    Config::SetBaseOrCurrent(Config::SYSCONF_SCREENSAVER, wiiScreensaver);
    Config::Save();
  }
  ImGui::TextWrapped("Enables the Wii system screensaver after a period of inactivity.");

  bool enable_wiilink = Config::Get(Config::MAIN_WII_WIILINK_ENABLE);
  if (ImGui::Checkbox("Enable WiiConnect24 via WiiLink", &enable_wiilink))
  {
    Config::SetBaseOrCurrent(Config::MAIN_WII_WIILINK_ENABLE, enable_wiilink);
    Config::Save();
  }
  ImGui::TextWrapped("Enables WiiConnect24 features via WiiLink, such as the Wii Mail service.");

  const char* language_items[] = {"Japanese",
                                  "English",
                                  "German",
                                  "French",
                                  "Spanish",
                                  "Italian",
                                  "Dutch",
                                  "Simplified Chinese",
                                  "Traditional Chinese",
                                  "Korean"};
  auto lang = Config::Get(Config::SYSCONF_LANGUAGE);

  if (ImGui::TreeNode("System Language"))
  {
    for (u32 i = 0; i < 10; i++)
    {
      ImGui::PushID(i);
      if (ImGui::RadioButton(language_items[i], i == lang))
      {
        Config::SetBaseOrCurrent(Config::SYSCONF_LANGUAGE, i);
        Config::Save();
      }
      ImGui::PopID();
    }

    ImGui::TreePop();
  }
  const char* sound_items[] = {"Mono", "Stereo", "Surround"};
  auto sound = Config::Get(Config::SYSCONF_SOUND_MODE);

  if (ImGui::TreeNode("Sound"))
  {
    for (u32 i = 0; i < 3; i++)
    {
      ImGui::PushID(i);
      if (ImGui::RadioButton(sound_items[i], i == sound))
      {
        Config::SetBaseOrCurrent(Config::SYSCONF_SOUND_MODE, i);
        Config::Save();
      }
      ImGui::PopID();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("SD Card Settings"))
  {
    bool insert_sd_card = Config::Get(Config::MAIN_WII_SD_CARD);
    if (ImGui::Checkbox("Insert SD Card", &insert_sd_card))
    {
      Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD, insert_sd_card);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Inserts a virtual SD card into the emulated Wii.\n"
                       "If unsure, leave this checked.");
    }

    bool allow_sd_writes = Config::Get(Config::MAIN_ALLOW_SD_WRITES);
    if (ImGui::Checkbox("Allow Writes to SD Card", &allow_sd_writes))
    {
      Config::SetBaseOrCurrent(Config::MAIN_ALLOW_SD_WRITES, allow_sd_writes);
      Config::Save();
    }

    std::string sd_card_path = Config::Get(Config::MAIN_WII_SD_CARD_IMAGE_PATH);
    char sd_card_path_buf[256];
    strncpy(sd_card_path_buf, sd_card_path.c_str(), sizeof(sd_card_path_buf));
    ImGui::InputText("SD Card Path", sd_card_path_buf, sizeof(sd_card_path_buf),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse##SDCard"))
    {
      state->controlsDisabled = true;
      UWP::OpenDiscPicker();
      state->controlsDisabled = false;
    }

    bool sync_sd_folder = Config::Get(Config::MAIN_WII_SD_CARD_ENABLE_FOLDER_SYNC);
    if (ImGui::Checkbox("Automatically Sync with Folder", &sync_sd_folder))
    {
      Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD_ENABLE_FOLDER_SYNC, sync_sd_folder);
      Config::Save();
    }
    ImGui::TextWrapped(
        "Synchronizes the SD Card with the SD Sync Folder when starting and ending emulation.");

    std::string sd_sync_folder = Config::Get(Config::MAIN_WII_SD_CARD_SYNC_FOLDER_PATH);
    char sd_sync_folder_buf[256];
    strncpy(sd_sync_folder_buf, sd_sync_folder.c_str(), sizeof(sd_sync_folder_buf));
    ImGui::InputText("SD Sync Folder", sd_sync_folder_buf, sizeof(sd_sync_folder_buf),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse##SDSync"))
    {
      state->controlsDisabled = true;
      UWP::OpenGameFolderPicker([state](std::string path) {
        if (path != "")
        {
          Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD_SYNC_FOLDER_PATH, path);
          Config::Save();
        }
        state->controlsDisabled = false;
      });
    }

    const char* sd_size_items[] = {"128 MB", "256 MB", "512 MB", "1 GB", "2 GB",
                                   "4 GB",   "8 GB",   "16 GB",  "32 GB"};
    u64 sd_size = Config::Get(Config::MAIN_WII_SD_CARD_FILESIZE);
    int sd_size_index = 0;
    for (size_t i = 0; i < IM_ARRAYSIZE(sd_size_items); ++i)
    {
      if (sd_size == (128 * 1024 * 1024) * (1ULL << i))
      {
        sd_size_index = static_cast<int>(i);
        break;
      }
    }
    if (ImGui::Combo("SD Card File Size", &sd_size_index, sd_size_items,
                     IM_ARRAYSIZE(sd_size_items)))
    {
      Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD_FILESIZE,
                               (128 * 1024 * 1024) * (1ULL << sd_size_index));
      Config::Save();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Wii Remote Settings"))
  {
    bool wiimote_motor = Config::Get(Config::SYSCONF_WIIMOTE_MOTOR);
    if (ImGui::Checkbox("Enable Rumble", &wiimote_motor))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_WIIMOTE_MOTOR, wiimote_motor);
      Config::Save();
    }

    const char* sensor_position_items[] = {"Top", "Bottom"};
    int sensor_position = Config::Get(Config::SYSCONF_SENSOR_BAR_POSITION);
    if (ImGui::Combo("Sensor Bar Position", &sensor_position, sensor_position_items,
                     IM_ARRAYSIZE(sensor_position_items)))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_SENSOR_BAR_POSITION, sensor_position);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Sets the position of the sensor bar relative to the screen.\n"
                       "If unsure, select Top.");
    }

    int ir_sensitivity = Config::Get(Config::SYSCONF_SENSOR_BAR_SENSITIVITY);
    if (ImGui::SliderInt("IR Sensitivity", &ir_sensitivity, 1, 5))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_SENSOR_BAR_SENSITIVITY, ir_sensitivity);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Adjusts the sensitivity of the Wii Remote's IR sensor.\n"
                       "Higher values increase sensitivity.\n"
                       "If unsure, leave this at 3.");
    }

    int speaker_volume = Config::Get(Config::SYSCONF_SPEAKER_VOLUME);
    if (ImGui::SliderInt("Speaker Volume", &speaker_volume, 0, 127))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_SPEAKER_VOLUME, speaker_volume);
      Config::Save();
    }

    ImGui::TreePop();
  }

  ImGui::Separator();
  if (ImGui::TreeNode("Manage NAND"))
  {
    if (ImGui::Button("Import BootMii NAND Backup...") && !state->controlsDisabled &&
        !import_in_progress.load())
    {
      state->controlsDisabled = true;
      show_import_progress_modal = true;
      import_status = "Waiting for file picker...";

      UWP::OpenNANDBinPicker([&](std::string path) {
        if (path.empty())
        {
          import_status = "Import cancelled.";
          import_canceled.store(true);
          return;
        }
        import_in_progress.store(true);
        import_complete.store(false);
        import_status = "Importing...";
        import_begin_tp = std::chrono::steady_clock::now();
        import_thread = std::thread([path]() {
          try
          {
            DiscIO::NANDImporter importer;
            importer.ImportNANDBin(
                path,
                [] {},
                []() {
                  // Ask user for keys via picker
                  std::promise<std::string> p;
                  auto f = p.get_future();
                  UWP::OpenBootMiiKeysPicker([&p](std::string keys) { p.set_value(keys); });
                  return f.get();
                });
            import_success = true;
          }
          catch (...)
          {
            import_success = false;
          }
          import_in_progress.store(false);
          import_complete.store(true);
        });
      });
    }

    if (ImGui::Button("Check NAND...") && !state->controlsDisabled && !check_in_progress.load())
    {
      check_in_progress.store(true);
      std::thread([] {
        IOS::HLE::Kernel ios;
        last_check_result = WiiUtils::CheckNAND(ios);
        check_in_progress.store(false);
        }).detach();
      show_check_result_modal = true;
    }

    if (ImGui::Button("Extract Certificates from NAND") && !state->controlsDisabled)
    {
      const bool ok = DiscIO::NANDImporter().ExtractCertificates();
      ImGui::OpenPopup(ok ? "Certificates Extracted" : "Extraction Failed");
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Certificates Extracted", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::TextWrapped("Successfully extracted certificates from NAND.");
      if (ImGui::Button("OK"))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Extraction Failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::TextWrapped("Failed to extract certificates from NAND.");
      if (ImGui::Button("OK"))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    ImGui::TreePop();
  }

  if (show_import_progress_modal)
  {
    ImGui::OpenPopup("Importing NAND backup...");
    show_import_progress_modal = false;
  }
  ImGui::SetNextWindowSize(ImVec2(420, 110), ImGuiCond_Always);
  if (ImGui::BeginPopupModal("Importing NAND backup...", nullptr, ImGuiWindowFlags_NoResize))
  {
    ImGui::TextWrapped("%s", import_status.c_str());
    float anim = fmodf(static_cast<float>(ImGui::GetTime()) * 0.5f, 1.0f);
    ImGui::ProgressBar(anim, ImVec2(-FLT_MIN, 0.0f));
    if (import_begin_tp.time_since_epoch().count() != 0)
    {
      auto secs = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - import_begin_tp)
                      .count();
      ImGui::Text("Elapsed: %llds", static_cast<long long>(secs));
    }
    if (import_canceled.load())
    {
      import_canceled.store(false);
      ImGui::CloseCurrentPopup();
      state->controlsDisabled = false;
      import_begin_tp = {};
      ImGui::OpenPopup("Import Cancelled");
    }
    if (import_complete.load())
    {
      if (import_thread.joinable())
        import_thread.join();
      state->controlsDisabled = false;
      ImGui::CloseCurrentPopup();
      import_begin_tp = {};
      show_import_result_modal = true;
    }
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::BeginPopupModal("Import Cancelled", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextWrapped("NAND import was cancelled.");
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (show_import_result_modal)
  {
    ImGui::OpenPopup(import_success ? "Import Complete" : "Import Failed");
    show_import_result_modal = false;
  }
  ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::BeginPopupModal("Import Complete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextWrapped("Successfully imported NAND backup.");
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::BeginPopupModal("Import Failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextWrapped("Failed to import NAND backup.");
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (show_check_result_modal)
  {
    ImGui::OpenPopup("NAND Check");
    show_check_result_modal = false;
  }
  ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::BeginPopupModal("NAND Check", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    if (!last_check_result.bad)
    {
      const bool overfull = last_check_result.used_clusters_user > IOS::HLE::FS::USER_CLUSTERS ||
                            last_check_result.used_clusters_system > IOS::HLE::FS::SYSTEM_CLUSTERS;
      if (overfull)
        ImGui::TextWrapped("Your NAND contains more data than allowed. Wii software may behave incorrectly or not allow saving.");
      else
        ImGui::TextWrapped("No issues have been detected.");
    }
    else
    {
      ImGui::TextWrapped("The emulated NAND is damaged. Attempt repair?");
      if (ImGui::Button("Repair"))
      {
        IOS::HLE::Kernel ios;
        const bool ok = WiiUtils::RepairNAND(ios);
        ImGui::OpenPopup(ok ? "NAND Repaired" : "Repair Failed");
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("NAND Repaired", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::TextWrapped("The NAND has been repaired.");
      if (ImGui::Button("OK"))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Repair Failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::TextWrapped("The NAND could not be repaired. It is recommended to back up your current data and start over with a fresh NAND.");
      if (ImGui::Button("OK"))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  static bool show_online_update_modal = false;
  if (ImGui::Button("Online System Update"))
    show_online_update_modal = true;
  if (show_online_update_modal)
  {
    ImGui::OpenPopup("Online System Update");
    show_online_update_modal = false;
  }
  if (ImGui::BeginPopupModal("Online System Update", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Select Region:");
    static int region_idx = 0;
    const char* region_items[] = {"JPN", "USA", "EUR", "KOR"};
    ImGui::Combo("Region", &region_idx, region_items, IM_ARRAYSIZE(region_items));
    if (ImGui::Button("OK"))
    {
      show_update_progress_modal = true;
      update_complete = false;
      update_progress = 0.0f;
      std::string region = region_items[region_idx];
      update_thread = std::thread([region]() {
        async_res = WiiUtils::DoOnlineUpdate(
            [](size_t done, size_t total, u64) {
              update_progress = total ? float(done) / float(total) : 0.0f;
              return true;
            },
            region);
        update_complete = true;
      });
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (show_update_progress_modal)
  {
    ImGui::OpenPopup("Updating...");
    show_update_progress_modal = false;
  }
  ImGui::SetNextWindowSize(ImVec2(600, 100), ImGuiCond_Once);
  if (ImGui::BeginPopupModal("Updating...", nullptr, ImGuiWindowFlags_NoResize))
  {
    ImGui::ProgressBar(update_progress.load(), ImVec2(-FLT_MIN, 0.0f));
    if (update_complete.load())
    {
      if (update_thread.joinable())
        update_thread.join();
      state->controlsDisabled = false;
      ImGui::CloseCurrentPopup();
      last_res = async_res;
      show_update_result_modal = true;
    }
    ImGui::EndPopup();
  }

  if (show_update_result_modal)
  {
    ImGui::OpenPopup("Update Result");
    show_update_result_modal = false;
  }
  ImGui::SetNextWindowSize(ImVec2(600, 150), ImGuiCond_Once);
  if (ImGui::BeginPopupModal("Update Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    const char* msg = "";
    switch (last_res)
    {
    case WiiUtils::UpdateResult::Succeeded:
      msg = "Update completed successfully.";
      break;
    case WiiUtils::UpdateResult::AlreadyUpToDate:
      msg = "Already up-to-date.";
      break;
    default:
      msg = "Update failed or cancelled.";
      break;
    }
    ImGui::TextWrapped("%s", msg);
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::Separator();
  if (ImGui::Button("Install WAD"))
  {
    state->controlsDisabled = true;
    UWP::OpenWADPicker();
    state->controlsDisabled = false;
  }
}

void CreateAdvancedTab(UIState* state)
{
  const Core::CPUThreadGuard guard(Core::System::GetInstance());

  if (ImGui::TreeNode("CPU Options"))
  {
#ifdef WINRT_XBOX
    const char* cpu_cores[] = {"Interpreter", "JIT64", "Cached Interpreter"};
    const PowerPC::CPUCore cpu_core_values[] = {PowerPC::CPUCore::Interpreter,
                                                PowerPC::CPUCore::JIT64,
                                                PowerPC::CPUCore::CachedInterpreter};
    const int cpu_cores_count = 3;
#else
    const char* cpu_cores[] = {"Interpreter", "JIT64", "JITARM64", "Cached Interpreter"};
    const PowerPC::CPUCore cpu_core_values[] = {PowerPC::CPUCore::Interpreter,
                                                PowerPC::CPUCore::JIT64, PowerPC::CPUCore::JITARM64,
                                                PowerPC::CPUCore::CachedInterpreter};
    const int cpu_cores_count = 4;
#endif
    PowerPC::CPUCore config_core = Config::Get(Config::MAIN_CPU_CORE);
    int combo_index = 0;
    for (int i = 0; i < cpu_cores_count; ++i)
    {
      if (cpu_core_values[i] == config_core)
      {
        combo_index = i;
        break;
      }
    }
    if (ImGui::Combo("CPU Emulation Engine", &combo_index, cpu_cores, cpu_cores_count))
    {
      Config::SetBaseOrCurrent(Config::MAIN_CPU_CORE, cpu_core_values[combo_index]);
      Config::Save();
    }

    bool mmuEnable = Config::Get(Config::MAIN_MMU);
    if (ImGui::Checkbox("Enable MMU", &mmuEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_MMU, mmuEnable);
      Config::Save();
    }
    ImGui::TextWrapped("Enables the Memory Management Unit, needed for some games. (ON = Compatible, "
                       "OFF = Fast)\n\nIf unsure, leave this unchecked.");

    bool pauseOnPanic = Config::Get(Config::MAIN_PAUSE_ON_PANIC);
    if (ImGui::Checkbox("Pause on Panic", &pauseOnPanic))
    {
      Config::SetBaseOrCurrent(Config::MAIN_PAUSE_ON_PANIC, pauseOnPanic);
      Config::Save();
    }
    ImGui::TextWrapped("Pauses the emulation if a Read/Write or Unknown Instruction panic "
                       "occurs.\nEnabling will affect performance.\nThe performance impact is the "
                       "same as having Enable MMU on.\n\nIf unsure, leave this unchecked.");

    bool accurateCPUCache = Config::Get(Config::MAIN_ACCURATE_CPU_CACHE);
    if (ImGui::Checkbox("Enable Write-Back Cache (slow)", &accurateCPUCache))
    {
      Config::SetBaseOrCurrent(Config::MAIN_ACCURATE_CPU_CACHE, accurateCPUCache);
      Config::Save();
    }
    ImGui::TextWrapped("Enables emulation of the CPU write-back cache.\nEnabling will have a "
                       "significant impact on performance.\nThis should be left disabled unless "
                       "absolutely needed.\n\nIf unsure, leave this unchecked.");

    if (ImGui::CollapsingHeader("Advanced CPU Options"))
    {
      bool jitFollowBranch = Config::Get(Config::MAIN_JIT_FOLLOW_BRANCH);
      if (ImGui::Checkbox("JIT Follow Branch", &jitFollowBranch))
      {
        Config::SetBaseOrCurrent(Config::MAIN_JIT_FOLLOW_BRANCH, jitFollowBranch);
        Config::Save();
      }
      ImGui::TextWrapped("Enables JIT to follow branches for optimization. Can improve performance "
                         "in some cases.\n\nIf unsure, leave this unchecked.");

      bool fastmem = Config::Get(Config::MAIN_FASTMEM);
      if (ImGui::Checkbox("Enable Fastmem", &fastmem))
      {
        Config::SetBaseOrCurrent(Config::MAIN_FASTMEM, fastmem);
        Config::Save();
      }
      ImGui::TextWrapped("Enables faster memory access by using virtual memory mapping. Can improve "
                         "performance significantly.\n\nIf unsure, leave this checked.");

      bool largeEntryPoints = Config::Get(Config::MAIN_LARGE_ENTRY_POINTS_MAP);
      if (ImGui::Checkbox("Large Entry Points Map", &largeEntryPoints))
      {
        Config::SetBaseOrCurrent(Config::MAIN_LARGE_ENTRY_POINTS_MAP, largeEntryPoints);
        Config::Save();
      }
      ImGui::TextWrapped("Uses a larger map for JIT entry points. Can improve performance for games "
                         "with many functions.\n\nIf unsure, leave this unchecked.");
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Timing"))
  {
    bool syncOnSkipIdle = Config::Get(Config::MAIN_SYNC_ON_SKIP_IDLE);
    if (ImGui::Checkbox("Sync on Skip Idle", &syncOnSkipIdle))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SYNC_ON_SKIP_IDLE, syncOnSkipIdle);
      Config::Save();
    }
    ImGui::TextWrapped("Synchronizes the GPU thread with the CPU thread when the CPU is idle.\nThis "
                       "helps prevent desynchronization between the CPU and GPU, which can cause "
                       "visual glitches or crashes.\n\nIf unsure, leave this checked.");

    bool emulateDiscSpeed = !Config::Get(Config::MAIN_FAST_DISC_SPEED);
    if (ImGui::Checkbox("Emulate Disc Speed", &emulateDiscSpeed))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FAST_DISC_SPEED, !emulateDiscSpeed);
      Config::Save();
    }
    ImGui::TextWrapped(
        "When checked, the emulator uses normal disc speed (ON = Stock, OFF = Fast).\nDisabling "
        "emulation accelerates the disc transfer rate, removing loading times but may cause issues "
        "with games that rely on precise timing.\n\nIf unsure, leave this checked.");

    bool correctTimeDrift = Config::Get(Config::MAIN_CORRECT_TIME_DRIFT);
    if (ImGui::Checkbox("Correct Time Drift", &correctTimeDrift))
    {
      Config::SetBaseOrCurrent(Config::MAIN_CORRECT_TIME_DRIFT, correctTimeDrift);
      Config::Save();
    }
    ImGui::TextWrapped("Allow the emulated console to run fast after stutters, pursuing accurate "
                       "overall elapsed time unless paused or speed-adjusted.\n\nThis may be useful "
                       "for internet play.\n\nIf unsure, leave this unchecked.");

    if (ImGui::CollapsingHeader("Advanced Timing Options"))
    {
      int timingVariance = Config::Get(Config::MAIN_TIMING_VARIANCE);
      if (ImGui::SliderInt("Timing Variance (ms)", &timingVariance, 0, 100))
      {
        Config::SetBaseOrCurrent(Config::MAIN_TIMING_VARIANCE, timingVariance);
        Config::Save();
      }
      ImGui::TextWrapped("Simulates timing variance in the emulated system. Higher values add more "
                         "randomness to timing.\n\nIf unsure, leave this at 0.");
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Clock Override"))
  {
    ImGui::Text("WARNING: Changing this from the default (1.0 = 100%) can and will break\ngames "
                "and cause glitches. Do so at your own risk. \nPlease do not report bugs that "
                "occur with a non-default clock. \nThis is not a magical performance slider!");

    bool overclockEnable = Config::Get(Config::MAIN_OVERCLOCK_ENABLE);
    if (ImGui::Checkbox("Enable Emulated CPU Clock Override", &overclockEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK_ENABLE, overclockEnable);
      Config::Save();
    }

    if (overclockEnable)
    {
      float clockOverride = Config::Get(Config::MAIN_OVERCLOCK);
      if (ImGui::SliderFloat("Emulated CPU Clock Speed Override", &clockOverride, 0.01f, 4.0f))
      {
        Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK, clockOverride);
        Config::Save();
      }

      int percent = static_cast<int>(std::round(clockOverride * 100.0f));
      int clock = static_cast<int>(std::round(clockOverride * 486.0f));
      ImGui::Text("%d%% (%d MHz)", percent, clock);
    }

    ImGui::TextWrapped(
        "Adjusts the emulated CPU's clock rate.\n\nOn games that have an unstable frame rate "
        "despite full emulation speed, higher values can improve their performance, requiring a "
        "powerful device. Lower values reduce the emulated console's performance, but improve the "
        "emulation speed.\n\nWARNING: Changing this from the default (100%) can and will break "
        "games and cause glitches. Do so at your own risk. Please do not report bugs that occur "
        "with a non-default clock.\n\nIf unsure, leave this unchecked.");

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("VBI Frequency Override"))
  {
    ImGui::Text("WARNING: Changing this from the default (1.0 = 100%) can and will break\ngames "
                "and cause glitches. Do so at your own risk. \nPlease do not report bugs that "
                "occur with a non-default frequency.");

    bool vbiOverclockEnable = Config::Get(Config::MAIN_VI_OVERCLOCK_ENABLE);
    if (ImGui::Checkbox("Enable VBI Frequency Override", &vbiOverclockEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_VI_OVERCLOCK_ENABLE, vbiOverclockEnable);
      Config::Save();
    }

    if (vbiOverclockEnable)
    {
      float vbiOverclock = Config::Get(Config::MAIN_VI_OVERCLOCK);
      if (ImGui::SliderFloat("VBI Frequency Override", &vbiOverclock, 0.01f, 5.0f))
      {
        Config::SetBaseOrCurrent(Config::MAIN_VI_OVERCLOCK, vbiOverclock);
        Config::Save();
      }

      int percent = static_cast<int>(std::round(vbiOverclock * 100.0f));
      float vps = 59.94f * vbiOverclock;
      ImGui::Text("%d%% (%.2f VPS)", percent, vps);
    }

    ImGui::TextWrapped(
        "Adjusts the VBI frequency. Also adjusts the emulated CPU's clock rate, to keep it "
        "relatively the same.\n\nMakes games run at a different frame rate, making the "
        "emulation less demanding when lowered, or improving smoothness when increased. This "
        "may affect gameplay speed, as it is often tied to the frame rate.\n\nWARNING: "
        "Changing this from the default (100%) can and will break games and cause glitches. "
        "Do so at your own risk. Please do not report bugs that occur with a non-default "
        "frequency.\n\nIf unsure, leave this unchecked.");

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Memory Override"))
  {
    bool ramOverrideEnable = Config::Get(Config::MAIN_RAM_OVERRIDE_ENABLE);
    if (ImGui::Checkbox("Enable Emulated Memory Size Override", &ramOverrideEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_RAM_OVERRIDE_ENABLE, ramOverrideEnable);
      Config::Save();
    }

    if (ramOverrideEnable)
    {
      u32 mem1Size = Config::Get(Config::MAIN_MEM1_SIZE) / 0x100000;
      if (ImGui::SliderInt("MEM1 Size (MB)", (int*)&mem1Size, 24, 64))
      {
        Config::SetBaseOrCurrent(Config::MAIN_MEM1_SIZE, mem1Size * 0x100000);
        Config::Save();
      }

      u32 mem2Size = Config::Get(Config::MAIN_MEM2_SIZE) / 0x100000;
      if (ImGui::SliderInt("MEM2 Size (MB)", (int*)&mem2Size, 64, 128))
      {
        Config::SetBaseOrCurrent(Config::MAIN_MEM2_SIZE, mem2Size * 0x100000);
        Config::Save();
      }
    }

    ImGui::TextWrapped(
        "Adjusts the amount of RAM in the emulated console.\n\n"
        "WARNING: Enabling this will completely break many games. Only a small number "
        "of games can benefit from this.");

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Custom RTC Options"))
  {
    bool customRTCEnable = Config::Get(Config::MAIN_CUSTOM_RTC_ENABLE);
    if (ImGui::Checkbox("Enable Custom RTC", &customRTCEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_CUSTOM_RTC_ENABLE, customRTCEnable);
      Config::Save();
    }

    if (customRTCEnable)
    {
      u64 customRTC = Config::Get(Config::MAIN_CUSTOM_RTC_VALUE);
      time_t current_time = customRTC;
      struct tm* timeinfo = gmtime(&current_time);

      // Create a date/time picker
      int year = timeinfo->tm_year + 1900;
      int month = timeinfo->tm_mon + 1;
      int day = timeinfo->tm_mday;
      int hour = timeinfo->tm_hour;
      int minute = timeinfo->tm_min;
      int second = timeinfo->tm_sec;

      bool date_changed = false;
      date_changed |= ImGui::InputInt("Year", &year, 1, 100);
      date_changed |= ImGui::InputInt("Month", &month, 1, 12);
      date_changed |= ImGui::InputInt("Day", &day, 1, 31);
      date_changed |= ImGui::InputInt("Hour", &hour, 1, 23);
      date_changed |= ImGui::InputInt("Minute", &minute, 1, 59);
      date_changed |= ImGui::InputInt("Second", &second, 1, 59);

      if (date_changed)
      {
        year = std::clamp(year, 2000, 2099);
        month = std::clamp(month, 1, 12);
        day = std::clamp(day, 1, 31);
        hour = std::clamp(hour, 0, 23);
        minute = std::clamp(minute, 0, 59);
        second = std::clamp(second, 0, 59);

        struct tm new_time = {};
        new_time.tm_year = year - 1900;
        new_time.tm_mon = month - 1;
        new_time.tm_mday = day;
        new_time.tm_hour = hour;
        new_time.tm_min = minute;
        new_time.tm_sec = second;
        new_time.tm_isdst = -1;

        time_t new_time_t = _mkgmtime(&new_time);
        if (new_time_t != -1)
        {
          Config::SetBaseOrCurrent(Config::MAIN_CUSTOM_RTC_VALUE, static_cast<u64>(new_time_t));
          Config::Save();
        }
      }
    }

    ImGui::TextWrapped("This setting allows you to set a custom real time clock (RTC) separate "
                       "from your current system time.\n\nIf unsure, leave this unchecked.");

    ImGui::TreePop();
  }
}

void CreatePathsTab(UIState* state)
{
  ImGui::Text("Game Folders");
  if (ImGui::BeginListBox("##folders"))
  {
    for (auto path : m_paths)
    {
      if (ImGui::Selectable(path.c_str()))
      {
        state->selectedPath = path;
      }
    }
    ImGui::EndListBox();
  }

  if (state->selectedPath == "")
  {
    ImGui::BeginDisabled();
  }

  if (ImGui::Button("Remove Path"))
  {
    if (state->selectedPath != "")
    {
      m_paths.erase(std::remove(m_paths.begin(), m_paths.end(), state->selectedPath),
                    m_paths.end());
      Config::SetIsoPaths(m_paths);
      Config::Save();
    }
  }

  if (state->selectedPath == "")
  {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::Button("Add Path"))
  {
    state->controlsDisabled = true;
    UWP::OpenGameFolderPicker([state](std::string path) {
      if (path != "")
      {
        if (!UWP::TestPathPermissions(path))
        {
          m_show_path_warning = true;
        }
        else
        {
          m_paths.emplace_back(path);
          Config::SetIsoPaths(m_paths);
          Config::Save();
        }
      }

      state->controlsDisabled = false;
    });
  }

  ImGui::Spacing();
  ImGui::Separator();

  if (ImGui::Button("Set Dolphin User Folder Location"))
  {
    state->controlsDisabled = true;
    UWP::OpenNewUserPicker([=](std::string path) {
      if (path != "")
      {
        if (!UWP::TestPathPermissions(path))
        {
          m_show_path_warning = true;
        }
      }

      state->controlsDisabled = false;
    });

    UICommon::Shutdown();
    UICommon::SetUserDirectory(UWP::GetUserLocation());
    UICommon::CreateDirectories();
    UICommon::Init();
  }

  if (ImGui::Button("Reset Dolphin User Folder Location"))
  {
    UWP::ResetUserLocation();

    UICommon::Shutdown();
    UICommon::SetUserDirectory(UWP::GetUserLocation());
    UICommon::CreateDirectories();
    UICommon::Init();
  }

  if (m_show_path_warning)
  {
    ImGui::OpenPopup("Warning");
    m_show_path_warning = false;
    ImGui::SetNextWindowSize(ImVec2(950 * m_frame_scale, 80 * m_frame_scale));
  }

  if (ImGui::BeginPopupModal("Warning"))
  {
    ImGui::TextWrapped("The folder path you have selected is not writable! Please check that you "
                       "have the correct permissions for the folder you have selected.");
    ImGui::Separator();
    if (ImGui::Button("OK"))
    {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextWrapped("Note: Please remember to do your USB filesystem setup, or paths to "
                     "your USB will not work properly!");
}

void CreateWiiPort(int index, std::vector<std::string> devices)
{
  if (ImGui::BeginChild(fmt::format("gc-wii-{}", index).c_str(), ImVec2(-1, 75 * m_frame_scale),
                        true))
  {
    auto controller = Wiimote::GetConfig()->GetController(index);
    auto default_device = controller->GetDefaultDevice().name;

    ImGui::Text("Wiimote Port %d", index + 1);

    if (ImGui::BeginCombo("Device", default_device.c_str()))
    {
      for (auto device : devices)
      {
        if (ImGui::Selectable(device.c_str(), strcmp(default_device.c_str(), device.c_str()) == 0))
        {
          controller->SetDefaultDevice(device);
          controller->UpdateReferences(g_controller_interface);
          Wiimote::GetConfig()->SaveConfig();
        }
      }

      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Profile", m_selected_wiimote_profile[index].c_str()))
    {
      for (auto profile : m_wiimote_profiles)
      {
        if (ImGui::Selectable(profile.c_str(), m_selected_wiimote_profile[index] == profile))
        {
          m_selected_wiimote_profile[index] = profile;

          if (m_selected_wiimote_profile[index] == "None")
          {
            Common::IniFile::Section sec;

            controller->LoadConfig(&sec);
            controller->SetDefaultDevice(default_device);
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index), WiimoteSource::None);
          }
          else if (m_selected_wiimote_profile[index] == "Wiimote + Nunchuk")
          {
            controller->LoadDefaults(g_controller_interface);

            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }
          else if (m_selected_wiimote_profile[index] == "Classic Controller")
          {
            Common::IniFile ini;
            ini.Load(File::GetSysDirectory() + PROFILES_DIR +
                     Wiimote::GetConfig()->GetProfileDirectoryName() + "/Classic.ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }
          else if (m_selected_wiimote_profile[index] == "Sideways Wiimote")
          {
            Common::IniFile ini;
            ini.Load(File::GetSysDirectory() + PROFILES_DIR +
                     Wiimote::GetConfig()->GetProfileDirectoryName() + "/Sideways.ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }
          else
          {
            Common::IniFile ini;
            ini.Load(File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR +
                     Wiimote::GetConfig()->GetProfileDirectoryName() + "/" + profile + ".ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }

          controller->UpdateReferences(g_controller_interface);
          Wiimote::GetConfig()->SaveConfig();
          Config::Save();
        }
      }

      ImGui::EndCombo();
    }
  }

  ImGui::EndChild();
}

void CreateAchievementsTab(UIState* state)
{
  const Core::CPUThreadGuard guard(Core::System::GetInstance());

#ifdef USE_RETRO_ACHIEVEMENTS
  static bool integration_enabled = Config::Get(Config::RA_ENABLED);
  static bool hardcore_enabled = Config::Get(Config::RA_HARDCORE_ENABLED);
  static bool unofficial_enabled = Config::Get(Config::RA_UNOFFICIAL_ENABLED);
  static bool encore_enabled = Config::Get(Config::RA_ENCORE_ENABLED);
  static bool spectator_enabled = Config::Get(Config::RA_SPECTATOR_ENABLED);
  static bool discord_presence_enabled = Config::Get(Config::RA_DISCORD_PRESENCE_ENABLED);
  static bool progress_enabled = Config::Get(Config::RA_PROGRESS_ENABLED);
  static char username[256] = "";
  static char password[256] = "";
  static bool show_password = false;
  static std::string login_error;

  const bool is_running = Core::GetState(Core::System::GetInstance()) != Core::State::Uninitialized;
  const bool logged_in = !Config::Get(Config::RA_API_TOKEN).empty();

  if (ImGui::Checkbox("Enable RetroAchievements.org Integration", &integration_enabled))
  {
    Config::SetBaseOrCurrent(Config::RA_ENABLED, integration_enabled);
    Config::Save();
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "Enable integration with RetroAchievements for earning achievements and competing in "
        "leaderboards.\n\n"
        "Must log in with a RetroAchievements account to use. Dolphin does not save your "
        "password locally and uses an API token to maintain login.");
  }

  if (integration_enabled)
  {
    if (!logged_in)
    {
      ImGui::Text("Username");
      ImGui::SameLine();
      if (ImGui::Button("Edit Username"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      if (ImGui::InputText("##username", username, sizeof(username),
                           ImGuiInputTextFlags_EnterReturnsTrue))
      {
        UWP::HideKeyboard();
        ImGui::SetKeyboardFocusHere(-1);
      }

      ImGui::Spacing();

      ImGui::Text("Password");
      ImGui::SameLine();
      if (ImGui::Button("Edit Password"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      ImGuiInputTextFlags pwd_flags =
          ImGuiInputTextFlags_EnterReturnsTrue | (show_password ? 0 : ImGuiInputTextFlags_Password);
      if (ImGui::InputText("##password", password, sizeof(password), pwd_flags))
      {
        UWP::HideKeyboard();
        show_password = !show_password;
        ImGui::SetKeyboardFocusHere(-1);
      }

      ImGui::BeginDisabled(is_running);
      if (ImGui::Button("Log In"))
      {
#ifdef _WIN32
        OutputDebugStringA(
            ("RetroAchievements: Login attempt for user: " + std::string(username) + "\n").c_str());
#endif

        if (std::string(username).empty())
        {
          login_error = "Username cannot be empty";
#ifdef _WIN32
          OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
        }
        else if (std::string(password).empty())
        {
          login_error = "Password cannot be empty";
#ifdef _WIN32
          OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
        }
        else
        {
          Config::SetBaseOrCurrent(Config::RA_USERNAME, username);

          try
          {
            std::string before_token = Config::Get(Config::RA_API_TOKEN);

            AchievementManager::GetInstance().Login(std::string(password));

            std::string after_token = Config::Get(Config::RA_API_TOKEN);

            if (after_token == before_token && after_token.empty())
            {
              login_error = "Authentication failed. Please check your credentials.";
#ifdef _WIN32
              OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
            }
            else
            {
              login_error.clear();
#ifdef _WIN32
              OutputDebugStringA("RetroAchievements: Login successful\n");
#endif
            }
          }
          catch (const std::exception& e)
          {
            login_error = std::string("Exception during login: ") + e.what();
#ifdef _WIN32
            OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
          }
        }
        memset(password, 0, sizeof(password));
      }
      ImGui::EndDisabled();

      if (!login_error.empty())
      {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
        ImGui::Text("%s", login_error.c_str());
        ImGui::PopStyleColor();
      }
    }
    else
    {
      auto& instance = AchievementManager::GetInstance();
      const auto* user_info = rc_client_get_user_info(instance.GetClient());

      if (user_info)
      {
        ImGui::BeginGroup();

        // User name and points
        ImGui::Text("%s", user_info->display_name);
        ImGui::Text("%u points", user_info->score);

        // User badge/icon
        const AchievementManager::Badge& player_badge = instance.GetPlayerBadge();
        if (!player_badge.data.empty())
        {
          static std::shared_ptr<AbstractTexture> profile_texture;
          static std::vector<u8> last_badge_data;

          const std::vector<u8> badge_data(player_badge.data.data(),
                                           player_badge.data.data() + player_badge.data.size());

          if (last_badge_data != badge_data)
          {
            last_badge_data = badge_data;

            TextureConfig config(player_badge.width, player_badge.height, 1, 1, 1,
                                 AbstractTextureFormat::RGBA8, 0, AbstractTextureType::Texture_2D);

            profile_texture = g_gfx->CreateTexture(config, "RetroAchievements profile picture");
            if (profile_texture)
            {
              profile_texture->Load(0, player_badge.width, player_badge.height, player_badge.width,
                                    player_badge.data.data(),
                                    sizeof(u8) * 4 * player_badge.width * player_badge.height);
            }
          }

          if (profile_texture)
          {
            ImGui::Image((ImTextureID)(intptr_t)profile_texture.get(), ImVec2(64, 64));
          }
          else
          {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetCursorScreenPos().x + 64, ImGui::GetCursorScreenPos().y + 64),
                IM_COL32(50, 150, 255, 255));

            ImGui::Dummy(ImVec2(64, 64));
          }
        }

        ImGui::EndGroup();

        if (ImGui::Button("Log Out"))
        {
#ifdef _WIN32
          OutputDebugStringA("RetroAchievements: User logged out\n");
#endif
          AchievementManager::GetInstance().Logout();
          Config::SetBaseOrCurrent(Config::RA_API_TOKEN, "");
          Config::Save();
        }
      }
      else
      {
        if (ImGui::Button("Log Out"))
        {
#ifdef _WIN32
          OutputDebugStringA("RetroAchievements: User logged out\n");
#endif
          AchievementManager::GetInstance().Logout();
          Config::SetBaseOrCurrent(Config::RA_API_TOKEN, "");
          Config::Save();
        }
      }

      if (instance.IsGameLoaded())
      {
        ImGui::Separator();
        rc_client_user_game_summary_t game_summary;
        rc_client_get_user_game_summary(instance.GetClient(), &game_summary);

        ImGui::Text("%s", instance.GetGameDisplayName().data());
        ImGui::Text("Unlocked %u/%u achievements worth %u/%u points",
                    game_summary.num_unlocked_achievements, game_summary.num_core_achievements,
                    game_summary.points_unlocked, game_summary.points_core);

        float progress =
            game_summary.num_core_achievements > 0 ?
                (float)game_summary.num_unlocked_achievements / game_summary.num_core_achievements :
                0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 0),
                           (std::to_string(game_summary.num_unlocked_achievements) + "/" +
                            std::to_string(game_summary.num_core_achievements))
                               .c_str());

        const auto& rich_presence = instance.GetRichPresence();
        if (rich_presence[0] != '\0')
        {
          ImGui::TextWrapped("%s", rich_presence.data());
        }
      }
    }
    ImGui::Separator();
    ImGui::Text("Function Settings");

    ImGui::BeginDisabled(!logged_in || (is_running && !hardcore_enabled));
    if (ImGui::Checkbox("Enable Hardcore Mode", &hardcore_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_HARDCORE_ENABLED, hardcore_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable Hardcore Mode on RetroAchievements.\n\n"
          "Hardcore Mode is intended to provide an experience as close to gaming on the original "
          "hardware as possible. RetroAchievements rankings are primarily oriented towards "
          "Hardcore "
          "points (Softcore points are tracked but not as heavily emphasized) and leaderboards "
          "require Hardcore Mode to be on.\n\n"
          "To ensure this experience, the following features will be disabled:\n"
          "- Loading states (saving states is allowed)\n"
          "- Emulator speeds below 100% (frame advance disabled, turbo allowed)\n"
          "- Cheats\n"
          "- Memory patches (file patches allowed)\n"
          "- Debug UI\n"
          "- Freelook\n\n"
          "This cannot be turned on while a game is playing.\n"
          "Close your current game before enabling.");
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!logged_in);
    if (ImGui::Checkbox("Enable Unofficial Achievements", &unofficial_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_UNOFFICIAL_ENABLED, unofficial_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable unlocking unofficial achievements as well as official achievements.\n\n"
          "Unofficial achievements may be optional or unfinished achievements that have not been "
          "deemed official by RetroAchievements and may be useful for testing or simply for "
          "fun.\n\n"
          "Setting takes effect on next game load.");
    }

    if (ImGui::Checkbox("Enable Encore Mode", &encore_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_ENCORE_ENABLED, encore_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable unlocking achievements in Encore Mode.\n\n"
          "Encore Mode re-enables achievements the player has already unlocked on the site so that "
          "the player will be notified if they meet the unlock conditions again, useful for custom "
          "speedrun criteria or simply for fun.\n\n"
          "Setting takes effect on next game load.");
    }

    if (ImGui::Checkbox("Enable Spectator Mode", &spectator_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_SPECTATOR_ENABLED, spectator_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable unlocking achievements in Spectator Mode.\n\n"
          "While in Spectator Mode, achievements and leaderboards will be processed and displayed "
          "on screen, but will not be submitted to the server.\n\n"
          "If this is on at game launch, it will not be turned off until game close, because a "
          "RetroAchievements session will not be created.\n\n"
          "If this is off at game launch, it can be toggled freely while the game is running.");
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Display Settings");

#ifndef WINRT_XBOX  // We cannot use Discord RPC on Xbox as you cannot launch the Discord Win32 app.
#ifdef USE_DISCORD_PRESENCE
    ImGui::BeginDisabled(!Config::Get(Config::MAIN_USE_DISCORD_PRESENCE));
    if (ImGui::Checkbox("Enable Discord Presence", &discord_presence_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_DISCORD_PRESENCE_ENABLED, discord_presence_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Use RetroAchievements rich presence in your Discord status.\n\n"
                        "Show Current Game on Discord must be enabled.");
    }
    ImGui::EndDisabled();
#endif
#endif

    if (ImGui::Checkbox("Enable Progress Notifications", &progress_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_PROGRESS_ENABLED, progress_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable progress notifications on achievements.\n\n"
          "Displays a brief popup message whenever the player makes progress on an achievement "
          "that tracks an accumulated value, such as 60 out of 120 stars.");
    }
  }
#else
  ImGui::TextWrapped("RetroAchievements support is not enabled in this build.");
#endif
}

void DrawAchievementsWindow(UIState* state)
{
#ifdef USE_RETRO_ACHIEVEMENTS
  auto& instance = AchievementManager::GetInstance();
  if (!instance.IsGameLoaded() || !Config::Get(Config::RA_ENABLED))
    return;

  if (!ImGui::Begin("Achievements", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::End();
    return;
  }

  auto* client = instance.GetClient();
  if (!client)
  {
    ImGui::End();
    return;
  }

  rc_client_achievement_list_t* achievement_list = rc_client_create_achievement_list(
      client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE | RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL,
      RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);

  if (achievement_list)
  {
    for (u32 bucket_idx = 0; bucket_idx < achievement_list->num_buckets; bucket_idx++)
    {
      auto& bucket = achievement_list->buckets[bucket_idx];
      if (ImGui::CollapsingHeader(bucket.label))
      {
        for (u32 ach_idx = 0; ach_idx < bucket.num_achievements; ach_idx++)
        {
          auto* achievement = bucket.achievements[ach_idx];

          const auto& badge = instance.GetAchievementBadge(achievement->id, !achievement->unlocked);
          ImGui::PushID(achievement->id);

          if (state->achievement_badges.find(achievement->id) == state->achievement_badges.end())
          {
            TextureConfig config(badge.width, badge.height, 1, 1, 1, AbstractTextureFormat::RGBA8,
                                 0, AbstractTextureType::Texture_2D);

            auto tex = g_gfx->CreateTexture(config);
            if (tex)
            {
              tex->Load(0, badge.width, badge.height, badge.width, badge.data.data(),
                        badge.width * badge.height * sizeof(u32));
              state->achievement_badges[achievement->id] = std::move(tex);
            }
          }

          ImVec4 border_color;
          if ((achievement->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) != 0)
            border_color = ImVec4(1.0f, 0.843f, 0.0f, 1.0f);
          else if ((achievement->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE) != 0)
            border_color = ImVec4(0.0f, 0.478f, 1.0f, 1.0f);
          else
            border_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

          const float badge_size = 64.0f;
          const float border_thickness = 4.0f;
          ImVec2 pos = ImGui::GetCursorScreenPos();
          ImGui::GetWindowDrawList()->AddRectFilled(
              ImVec2(pos.x, pos.y),
              ImVec2(pos.x + badge_size + border_thickness * 2,
                     pos.y + badge_size + border_thickness * 2),
              ImGui::ColorConvertFloat4ToU32(border_color), 0.0f, 0);

          ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + border_thickness,
                                     ImGui::GetCursorPos().y + border_thickness));
          ImGui::Image((ImTextureID)(intptr_t)state->achievement_badges[achievement->id].get(),
                       ImVec2(badge_size, badge_size));

          ImGui::SameLine();
          ImGui::BeginGroup();

          ImGui::TextWrapped("%s", achievement->title);
          ImGui::TextWrapped("%s", achievement->description);
          ImGui::Text("%d points", achievement->points);
          if (achievement->unlocked)
          {
            if (achievement->unlock_time != 0)
            {
              time_t time = achievement->unlock_time;
              struct tm* timeinfo = localtime(&time);
              char buffer[80];
              strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
              ImGui::Text("Unlocked at %s", buffer);
            }
            else
            {
              ImGui::Text("Unlocked");
            }
          }
          else
          {
            ImGui::Text("Locked");
          }

          if (achievement->measured_percent > 0.000)
          {
            ImGui::ProgressBar(achievement->unlocked ? 1.0f :
                                                       achievement->measured_percent / 100.0f,
                               ImVec2(-1, 0), achievement->measured_progress);
          }

          ImGui::EndGroup();
          ImGui::PopID();
          ImGui::Separator();
        }
      }
    }
    rc_client_destroy_achievement_list(achievement_list);
  }

  ImGui::End();
#endif
}

void CreateGCPort(int index, std::vector<std::string> devices)
{
  if (ImGui::BeginChild(fmt::format("gc-port-{}", index).c_str(), ImVec2(-1, 75 * m_frame_scale),
                        true))
  {
    auto controller = Pad::GetConfig()->GetController(index);
    auto default_device = controller->GetDefaultDevice().name;

    ImGui::Text("GameCube Port %d", index + 1);

    if (ImGui::BeginCombo("Device", default_device.c_str()))
    {
      for (auto device : devices)
      {
        if (ImGui::Selectable(device.c_str(), strcmp(default_device.c_str(), device.c_str()) == 0))
        {
          controller->SetDefaultDevice(device);
          controller->UpdateReferences(g_controller_interface);
          Pad::GetConfig()->SaveConfig();
        }
      }

      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Profile", m_selected_gc_profile[index].c_str()))
    {
      for (auto profile : m_gc_profiles)
      {
        if (ImGui::Selectable(profile.c_str(), m_selected_gc_profile[index] == profile))
        {
          m_selected_gc_profile[index] = profile;

          if (m_selected_gc_profile[index] == "None")
          {
            // Loading an empty inifile section clears everything.
            Common::IniFile::Section sec;

            controller->LoadConfig(&sec);
            controller->SetDefaultDevice(default_device);
            Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(index),
                                     SerialInterface::SIDevices::SIDEVICE_NONE);
          }
          else if (m_selected_gc_profile[index] == "Default")
          {
            controller->LoadDefaults(g_controller_interface);
            Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(index),
                                     SerialInterface::SIDevices::SIDEVICE_GC_CONTROLLER);
          }
          else
          {
            Common::IniFile ini;
            ini.Load(File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR +
                     Pad::GetConfig()->GetProfileDirectoryName() + "/" + profile + ".ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(index),
                                     SerialInterface::SIDevices::SIDEVICE_GC_CONTROLLER);
          }

          controller->UpdateReferences(g_controller_interface);
          Pad::GetConfig()->SaveConfig();
          Config::Save();
        }
      }

      ImGui::EndCombo();
    }
  }

  ImGui::EndChild();
}

FrontendResult ImGuiFrontend::CreateMainPage()
{
  float posX = 30 * m_frame_scale;
  float posY = (345.0f / 2) * m_frame_scale;
  auto extraFlags = m_games.size() < 5 ? ImGuiWindowFlags_None :
                                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

  ImGui::SetNextWindowPos(ImVec2(posX, posY));
  std::shared_ptr<UICommon::GameFile> carousel_selection;
  if (ImGui::Begin("Dolphin Emulator", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground |
                       extraFlags))
  {
    carousel_selection = CreateGameCarousel();
  }
  ImGui::End();
  if (carousel_selection != nullptr)
    return FrontendResult(carousel_selection);

  const u64 current_time_us = Common::Timer::NowUs();
  const u64 time_diff_us = current_time_us - m_imgui_last_frame_time;
  const float time_diff_secs = static_cast<float>(time_diff_us / 1000000.0);
  m_imgui_last_frame_time = current_time_us;

  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = time_diff_secs;

  return FrontendResult();  // keep running
}

FrontendResult ImGuiFrontend::CreateListPage()
{
  ImGui::SetNextWindowSize(ImVec2(540 * m_frame_scale, 425 * m_frame_scale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (540 / 2) * m_frame_scale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (425 / 2) * m_frame_scale));

  std::shared_ptr<UICommon::GameFile> list_selection;
  if (ImGui::Begin("Dolphin Emulator", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings))
  {
    list_selection = CreateGameList();
  }
  ImGui::End();
  if (list_selection != nullptr)
    return FrontendResult(list_selection);

  return FrontendResult();
}

std::shared_ptr<UICommon::GameFile> ImGuiFrontend::CreateGameList()
{
  if (ImGui::Button("Search Game"))
  {
    UWP::ShowKeyboard();
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SameLine();

  ImGui::PushItemWidth(-1);
  ImGui::InputText("##gamesearch", m_list_search_buf, 32);
  ImGui::PopItemWidth();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::BeginListBox("##Games List", ImVec2(-1, -1)))
  {
    size_t search = strlen(m_list_search_buf);
    std::vector<std::shared_ptr<UICommon::GameFile>> games;
    if (search > 0)
    {
      std::string search_phrase = std::string(m_list_search_buf);
      if (search_phrase != m_prev_list_search)
      {
        m_list_search_results.clear();
        for (auto& game : m_games)
        {
          auto& name = game->GetName(m_title_database);
          auto it = std::search(name.begin(), name.end(), search_phrase.begin(),
                                search_phrase.end(), [](unsigned char ch1, unsigned char ch2) {
                                  return std::toupper(ch1) == std::toupper(ch2);
                                });

          if (it != name.end())
          {
            m_list_search_results.push_back(game);
          }
        }

        m_prev_list_search = m_list_search_buf;
      }

      games = m_list_search_results;
    }
    else
    {
      games = m_games;
    }

    long timeSinceInit = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::high_resolution_clock::now() - m_time_since_init)
                             .count();
    for (auto& game : games)
    {
      if (ImGui::Selectable(
              fmt::format("{}##{}", game->GetName(m_title_database).c_str(), game->GetFilePath())
                  .c_str()) &&
          timeSinceInit > 1500)
      {
        ImGui::EndListBox();
        return game;
      }
    }

    ImGui::EndListBox();
  }

  return nullptr;
}

std::shared_ptr<UICommon::GameFile> ImGuiFrontend::CreateGameCarousel()
{
  if (m_last_category != m_ccat)
  {
    m_last_category = m_ccat;
    FilterGamesForCategory();
    m_selectedGameIdx = 0;
    m_target_scroll_offset = 0.0f;
    m_carousel_scroll_offset = 0.0f;
  }

  if (m_displayed_games.empty())
    return nullptr;

  static auto last_time = std::chrono::high_resolution_clock::now();
  auto current_time = std::chrono::high_resolution_clock::now();
  float delta_time = std::chrono::duration<float>(current_time - last_time).count();
  last_time = current_time;

  UpdateCarouselAnimation(delta_time);

  long timeSinceInit = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::high_resolution_clock::now() - m_time_since_init)
                           .count();

  if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) && timeSinceInit > 1500)
  {
    return m_displayed_games[m_selectedGameIdx];
  }

  DrawCarouselBackground();

  ImVec2 screen_size = ImGui::GetIO().DisplaySize;
  float base_x = screen_size.x * 0.5f;
  float base_y = screen_size.y * 0.52f;

  if (m_selectedGameIdx < 0) m_selectedGameIdx = 0;
  if (m_selectedGameIdx >= static_cast<int>(m_displayed_games.size()))
    m_selectedGameIdx = static_cast<int>(m_displayed_games.size()) - 1;

  int half_visible = 2;

  for (int d = half_visible; d >= 1; --d)
  {
    for (int side = -1; side <= 1; side += 2)
    {
      int i = m_selectedGameIdx + side * d;
      int wrapped_index = i;

      if (m_displayed_games.size() > m_carousel_visible_items)
      {
        while (wrapped_index < 0)
          wrapped_index += static_cast<int>(m_displayed_games.size());
        while (wrapped_index >= static_cast<int>(m_displayed_games.size()))
          wrapped_index -= static_cast<int>(m_displayed_games.size());
      }
      else
      {
        if (wrapped_index < 0 ||
            wrapped_index >= static_cast<int>(m_displayed_games.size()))
          continue;
      }

      DrawCarouselItem(m_displayed_games[wrapped_index], i, m_selectedGameIdx,
                       base_x, base_y);
    }
  }

  if (!m_displayed_games.empty())
  {
    DrawCarouselItem(m_displayed_games[m_selectedGameIdx], m_selectedGameIdx,
                     m_selectedGameIdx, base_x, base_y);
  }

  if (m_show_game_info && m_selectedGameIdx < static_cast<int>(m_displayed_games.size()))
  {
    DrawGameInfo(m_displayed_games[m_selectedGameIdx]);
  }

  return nullptr;
}

void ImGuiFrontend::UpdateCarouselAnimation(float delta_time)
{
  if (!m_carousel_smooth_scrolling)
  {
    m_carousel_scroll_offset = m_target_scroll_offset;
    return;
  }

  float difference = m_target_scroll_offset - m_carousel_scroll_offset;
  if (std::abs(difference) > 0.01f)
  {
    m_carousel_scroll_offset += difference * m_carousel_animation_speed * delta_time;
  }
  else
  {
    m_carousel_scroll_offset = m_target_scroll_offset;
  }

  m_game_selection_timer += delta_time;
  
  if (m_game_selection_timer >= 3.0f)
  {
    m_game_info_fade_alpha = std::min(1.0f, m_game_info_fade_alpha + delta_time * 3.0f);
  }
  else
  {
    m_game_info_fade_alpha = 0.0f;
  }
}

void ImGuiFrontend::DrawCarouselBackground()
{
  // no-op for now
}

float ImGuiFrontend::GetCarouselItemPosition(int index, int center_index)
{
  const float delta = static_cast<float>(index - center_index);
  return delta * m_carousel_item_spacing;
}

float ImGuiFrontend::GetCarouselItemScale(int index, int center_index)
{
  const float distance = std::abs(static_cast<float>(index - center_index));
  const float t = std::max(0.0f, 1.0f - 0.3f * distance);
  return m_carousel_unselected_scale +
         (m_carousel_selected_scale - m_carousel_unselected_scale) * t;
}

float ImGuiFrontend::GetCarouselItemAlpha(int index, int center_index)
{
  const float distance = std::abs(static_cast<float>(index - center_index));
  const float alpha = 1.0f - (0.25f * distance);
  return std::max(alpha, 0.4f);
}

void ImGuiFrontend::DrawCarouselItem(std::shared_ptr<UICommon::GameFile> game, int index, int center_index, float base_x, float base_y)
{
  if (!game) return;

  float position_offset = GetCarouselItemPosition(index, center_index);
  float scale = GetCarouselItemScale(index, center_index);
  float alpha = GetCarouselItemAlpha(index, center_index);

  float item_x = base_x + position_offset + m_carousel_scroll_offset;
  float item_y = base_y;

  float cover_width = 146.0f * m_frame_scale * scale;
  float cover_height = 204.0f * m_frame_scale * scale;

  ImVec2 screen_size = ImGui::GetIO().DisplaySize;
  float item_left = item_x - cover_width * 0.5f;
  float item_right = item_x + cover_width * 0.5f;
  
  if (item_right < -100.0f || item_left > screen_size.x + 100.0f)
    return;

  ImGui::SetCursorScreenPos(ImVec2(item_x - cover_width * 0.5f, item_y - cover_height * 0.5f));

  ImGui::PushID(game->GetFilePath().c_str());

  if (m_carousel_show_shadows && index == center_index)
  {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 shadow_min = ImVec2(item_x - cover_width * 0.5f + m_carousel_shadow_offset, 
                               item_y - cover_height * 0.5f + m_carousel_shadow_offset);
    ImVec2 shadow_max = ImVec2(shadow_min.x + cover_width, shadow_min.y + cover_height);
    
    shadow_min.x = std::max(shadow_min.x, 0.0f);
    shadow_min.y = std::max(shadow_min.y, 0.0f);
    shadow_max.x = std::min(shadow_max.x, screen_size.x);
    shadow_max.y = std::min(shadow_max.y, screen_size.y);
    
    if (shadow_min.x < shadow_max.x && shadow_min.y < shadow_max.y)
    {
      draw_list->AddRectFilled(shadow_min, shadow_max, IM_COL32(0, 0, 0, 100), 10.0f);
    }
  }

  AbstractTexture* handle = GetHandleForGame(game);
  
  if (handle)
  {
    ImVec4 tint_color = ImVec4(1.0f, 1.0f, 1.0f, alpha);
    ImVec4 border_color = (index == center_index) ? 
                          ImVec4(1.0f, 1.0f, 1.0f, alpha) : 
                          ImVec4(0.5f, 0.5f, 0.5f, alpha * 0.5f);

    ImGui::Image((ImTextureID)handle,
                 ImVec2(cover_width, cover_height),
                 ImVec2(0, 0), ImVec2(1, 1),
                 tint_color, border_color);

    if (m_carousel_show_reflections && index == center_index)
    {
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      
      float reflection_y = item_y + cover_height * 0.5f + 10.0f;
      float reflection_height = cover_height * 0.6f;
      
      ImVec2 reflection_min = ImVec2(item_x - cover_width * 0.5f, reflection_y);
      ImVec2 reflection_max = ImVec2(reflection_min.x + cover_width, reflection_min.y + reflection_height);
      
      ImU32 reflection_color = IM_COL32(255, 255, 255, static_cast<int>(m_carousel_reflection_alpha * alpha * 255));
      
      draw_list->AddImage((ImTextureID)handle, reflection_min, reflection_max,
                         ImVec2(0, 1), ImVec2(1, 0), reflection_color);
      
      draw_list->AddRectFilledMultiColor(reflection_min, reflection_max,
                                        IM_COL32(0, 0, 0, static_cast<int>((1.0f - m_carousel_reflection_alpha) * 255)),
                                        IM_COL32(0, 0, 0, static_cast<int>((1.0f - m_carousel_reflection_alpha) * 255)),
                                        IM_COL32(0, 0, 0, 255),
                                        IM_COL32(0, 0, 0, 255));
    }

    if (index == center_index)
    {
      std::string game_name = game->GetName(m_title_database);
      ImVec2 text_size = ImGui::CalcTextSize(game_name.c_str());
      float text_x = item_x - text_size.x * 0.5f;
      float text_y = item_y + cover_height * 0.5f + (m_carousel_show_reflections ? 70.0f : 18.0f);
      
      ImGui::SetCursorScreenPos(ImVec2(text_x, text_y));
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", game_name.c_str());
    }
  }
  else
  {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 rect_min = ImVec2(item_x - cover_width * 0.5f, item_y - cover_height * 0.5f);
    ImVec2 rect_max = ImVec2(rect_min.x + cover_width, rect_min.y + cover_height);
    
    draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(50, 50, 50, static_cast<int>(alpha * 255)), 5.0f);
    draw_list->AddRect(rect_min, rect_max, IM_COL32(100, 100, 100, static_cast<int>(alpha * 255)), 5.0f, 0, 2.0f);
    
    std::string game_name = game->GetName(m_title_database);
    ImVec2 text_size = ImGui::CalcTextSize(game_name.c_str());
    ImVec2 text_pos = ImVec2(item_x - text_size.x * 0.5f, item_y - text_size.y * 0.5f);
    
    ImGui::SetCursorScreenPos(text_pos);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", game_name.c_str());
  }

  ImGui::PopID();
}

void ImGuiFrontend::DrawGameInfo(std::shared_ptr<UICommon::GameFile> game)
{
  if (!game) return;

  ImVec2 screen_size = ImGui::GetIO().DisplaySize;
  float panel_width = 400.0f * m_frame_scale;
  float panel_height = 300.0f * m_frame_scale;
  float panel_x = screen_size.x - panel_width - 50.0f * m_frame_scale;
  float panel_y = screen_size.y * 0.2f;

  ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y));
  ImGui::SetNextWindowSize(ImVec2(panel_width, panel_height));
  
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_game_info_fade_alpha);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  
  if (ImGui::Begin("Game Info", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoCollapse))
  {
    // Game title
    ImGui::PushFont(nullptr); // Use default font but larger
    ImGui::TextWrapped("%s", game->GetName(m_title_database).c_str());
    ImGui::PopFont();
    
    ImGui::Separator();
    ImGui::Spacing();

    // Game details
    ImGui::Text("Game ID: %s", game->GetGameID().c_str());
    ImGui::Text("Region: %s", game->GetCountry() == DiscIO::Country::USA ? "NTSC-U" :
                              game->GetCountry() == DiscIO::Country::Japan ? "NTSC-J" :
                              game->GetCountry() == DiscIO::Country::Europe ? "PAL" : "Unknown");
    
    // Platform
    std::string platform = "Unknown";
    if (game->GetPlatform() == DiscIO::Platform::GameCubeDisc)
      platform = "GameCube";
    else if (game->GetPlatform() == DiscIO::Platform::WiiDisc)
      platform = "Nintendo Wii";
    else if (game->GetPlatform() == DiscIO::Platform::WiiWAD)
      platform = "WiiWare/VC";
    
    ImGui::Text("Platform: %s", platform.c_str());
    
    // File size
    u64 file_size = game->GetFileSize();
    std::string size_str;
    if (file_size > 1024 * 1024 * 1024)
      size_str = fmt::format("{:.1f} GB", file_size / (1024.0 * 1024.0 * 1024.0));
    else if (file_size > 1024 * 1024)
      size_str = fmt::format("{:.1f} MB", file_size / (1024.0 * 1024.0));
    else
      size_str = fmt::format("{:.1f} KB", file_size / 1024.0);
    
    ImGui::Text("Size: %s", size_str.c_str());
    
#ifdef USE_RETRO_ACHIEVEMENTS
    // Add RetroAchievements info if user is logged in
    auto& achievement_manager = AchievementManager::GetInstance();
    if (achievement_manager.HasAPIToken())
    {
      // Check if this is the currently loaded game
      if (achievement_manager.IsGameLoaded())
      {
        rc_client_t* client = achievement_manager.GetClient();
        if (client && rc_client_has_achievements(client))
        {
          rc_client_achievement_list_t* achievement_list = rc_client_create_achievement_list(
              client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
              RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
              
          if (achievement_list)
          {
            int total_achievements = 0;
            int unlocked_achievements = 0;
            
            for (u32 bx = 0; bx < achievement_list->num_buckets; bx++)
            {
              auto& bucket = achievement_list->buckets[bx];
              for (u32 achievement = 0; achievement < bucket.num_achievements; achievement++)
              {
                total_achievements++;
                
                const rc_client_achievement_t* achievement_info = bucket.achievements[achievement];
                if (achievement_info && achievement_info->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED)
                  unlocked_achievements++;
              }
            }
            
            if (total_achievements > 0)
            {
              ImGui::Text("Achievements: %d/%d", unlocked_achievements, total_achievements);
            }
            else
            {
              ImGui::TextDisabled("Achievements: No achievements available");
            }
            
            rc_client_destroy_achievement_list(achievement_list);
          }
          else
          {
            ImGui::TextDisabled("Achievements: Error loading achievement data");
          }
        }
        else
        {
          ImGui::TextDisabled("Achievements: No achievements available");
        }
      }
    }
#endif  // USE_RETRO_ACHIEVEMENTS

  }
  ImGui::End();

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(1);
}

void ImGuiFrontend::SmoothScrollToGame(int target_index)
{
  if (target_index != m_selectedGameIdx)
  {
    m_selectedGameIdx = target_index;
    m_target_scroll_offset = 0.0f; // Reset scroll offset when changing games
    m_game_info_fade_alpha = 0.0f; // Keep game info hidden initially
    m_game_selection_timer = 0.0f; // Reset timer when game selection changes
  }
}

#ifdef USE_RETRO_ACHIEVEMENTS
std::pair<int, int> ImGuiFrontend::GetAchievementCountsForGame(const std::string& file_path)
{
  auto& achievement_manager = AchievementManager::GetInstance();
  
  // Check if user is logged in and has API token
  if (!achievement_manager.HasAPIToken())
    return {0, 0};
    
  rc_client_t* client = achievement_manager.GetClient();
  if (!client)
    return {0, 0};
    
  auto* current_game_info = rc_client_get_game_info(client);
  if (current_game_info && current_game_info->id != 0 && achievement_manager.IsGameLoaded())
  {
    // For the currently loaded game, we can get achievement data directly
    if (rc_client_has_achievements(client))
    {
      rc_client_achievement_list_t* achievement_list = rc_client_create_achievement_list(
          client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
          RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
          
      if (achievement_list)
      {
        int total_achievements = 0;
        int unlocked_achievements = 0;
        
        for (u32 bx = 0; bx < achievement_list->num_buckets; bx++)
        {
          auto& bucket = achievement_list->buckets[bx];
          for (u32 achievement = 0; achievement < bucket.num_achievements; achievement++)
          {
            total_achievements++;
            
            const rc_client_achievement_t* achievement_info = bucket.achievements[achievement];
            if (achievement_info && achievement_info->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED)
              unlocked_achievements++;
          }
        }
        
        rc_client_destroy_achievement_list(achievement_list);
        return {unlocked_achievements, total_achievements};
      }
    }
  }
  
  if (auto game_file = std::make_shared<UICommon::GameFile>(file_path))
  {
    std::string game_id = game_file->GetGameID();
    if (!game_id.empty())
    {
      std::lock_guard<std::mutex> lock(g_cache_mutex);
      auto it = g_game_achievement_cache.find(game_id);
      if (it != g_game_achievement_cache.end() && it->second && it->second->loaded)
      {
        if (it->second->hasAchievements)
        {
          // Return total count only (no unlock data for browsing)
          return {0, static_cast<int>(it->second->achievements.size())};
        }
        else
        {
          return {0, 0};
        }
      }
      else if (it == g_game_achievement_cache.end() || !it->second || !it->second->loading)
      {
        if (it == g_game_achievement_cache.end())
          g_game_achievement_cache[game_id] = std::make_unique<GameAchievementData>();
          
        std::thread(LoadAchievementDataForGame, file_path, game_id).detach();
      }
    }
  }
  
  return {0, 0};
}
#else
std::pair<int, int> ImGuiFrontend::GetAchievementCountsForGame(const std::string& file_path)
{
  return {0, 0};
}
#endif  // USE_RETRO_ACHIEVEMENTS

AbstractTexture* ImGuiFrontend::GetHandleForGame(std::shared_ptr<UICommon::GameFile> game)
{
  std::string game_id = game->GetGameID();
  auto result = m_cover_textures.find(game_id);
  if (m_cover_textures.find(game_id) == m_cover_textures.end())
  {
    std::shared_ptr<AbstractTexture> texture = CreateCoverTexture(game);
    if (texture == nullptr)
    {
      AbstractTexture* missing = GetOrCreateMissingTex();
      m_cover_textures.emplace(game_id, missing);
      return missing;
    }
    else
    {
      auto pair = m_cover_textures.emplace(game_id, std::move(texture));
      return pair.first->second.get();
    }
  }

  return result->second.get();
}

std::shared_ptr<AbstractTexture> CreateTextureFromPath(std::string path, bool is_theme_asset)
{
  std::string file_data;
  if (!File::ReadFileToString(path, file_data))
    return {};

  std::vector<unsigned char> buffer = {file_data.begin(), file_data.end()};
  if (buffer.empty())
    return {};

  int width, height, channels;
  unsigned char* image_data = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()),
                                                    &width, &height, &channels, 4);
  if (!image_data)
    return {};

  TextureConfig tex_config(width, height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0,
                           AbstractTextureType::Texture_2D);

  std::string texture_name = is_theme_asset ? "theme:" + path : "cover:" + path;
  std::shared_ptr<AbstractTexture> tex = g_gfx->CreateTexture(tex_config, texture_name);
  if (!tex)
  {
    stbi_image_free(image_data);
    PanicAlertFmt("Failed to create ImGui texture");
    return {};
  }

  tex->Load(0, width, height, width, image_data, sizeof(u32) * width * height);
  stbi_image_free(image_data);

  return std::move(tex);
}

std::shared_ptr<AbstractTexture>
ImGuiFrontend::CreateCoverTexture(std::shared_ptr<UICommon::GameFile> game)
{
  if (!File::Exists(File::GetUserPath(D_COVERCACHE_IDX) + game->GetGameTDBID() + ".png"))
  {
    game->DownloadDefaultCover();
  }

  return std::move(
      CreateTextureFromPath(File::GetUserPath(D_COVERCACHE_IDX) + game->GetGameTDBID() + ".png"));
}

AbstractTexture* ImGuiFrontend::GetOrCreateMissingTex()
{
  if (m_missing_tex != nullptr)
    return m_missing_tex.get();

  auto missing_tex = CreateTextureFromPath("Assets/missing.png");
  m_missing_tex = std::move(missing_tex);

  return m_missing_tex.get();
}

void ImGuiFrontend::LoadGameList()
{
  m_paths.clear();
  m_games.clear();
  m_displayed_games.clear();
  m_paths = Config::GetIsoPaths();
  
  const bool recursive_scan = Config::Get(Config::MAIN_RECURSIVE_ISO_PATHS);
  std::vector<std::string> all_game_paths = UICommon::FindAllGamePaths(m_paths, recursive_scan);
  
#ifdef WINRT_XBOX
  auto localCachePath = winrt::to_string(
      winrt::Windows::Storage::ApplicationData::Current().LocalCacheFolder().Path());
  std::vector<std::string> local_cache_paths = {localCachePath};
  std::vector<std::string> local_game_paths = UICommon::FindAllGamePaths(local_cache_paths, recursive_scan);
  all_game_paths.insert(all_game_paths.end(), local_game_paths.begin(), local_game_paths.end());
#endif
  
  for (const auto& game_path : all_game_paths)
  {
    auto game = std::make_shared<UICommon::GameFile>(game_path);
    if (game && game->IsValid())
      m_games.emplace_back(std::move(game));
  }
  
  FilterGamesForCategory();
  if (m_selectedGameIdx >= m_displayed_games.size() || m_selectedGameIdx < 0)
    m_selectedGameIdx = 0;
}

void ImGuiFrontend::FilterGamesForCategory()
{
  m_displayed_games.clear();

  switch (m_ccat)
  {
  case CAll:
    m_displayed_games.insert(m_displayed_games.begin(), m_games.begin(), m_games.end());
    break;
  case CWii:
    for (auto& game : m_games)
    {
      if (game->GetPlatform() != DiscIO::Platform::WiiDisc)
        continue;

      m_displayed_games.push_back(game);
    }
    break;
  case CGC:
    for (auto& game : m_games)
    {
      if (game->GetPlatform() != DiscIO::Platform::GameCubeDisc)
        continue;

      m_displayed_games.push_back(game);
    }
    break;
  case COther:
    for (auto& game : m_games)
    {
      if (game->GetPlatform() == DiscIO::Platform::WiiDisc ||
          game->GetPlatform() == DiscIO::Platform::GameCubeDisc)
        continue;

      m_displayed_games.push_back(game);
    }
    break;
  }
}

void ImGuiFrontend::LoadThemes()
{
  std::string selected_theme = Config::Get(Config::FRONTEND_SELECTED_THEME);

  m_themes.clear();
  m_selected_theme = nullptr;

  RecurseForThemes("Sys/FrontendThemes/");
  RecurseForThemes(File::GetUserPath(D_THEMES_IDX));

  if (!m_selected_theme)
  {
    m_selected_theme = &m_themes["Flipper 2.2 - Beached"];
  }
}

void ImGuiFrontend::RecurseForThemes(std::string path)
{
  std::string selected_theme = Config::Get(Config::FRONTEND_SELECTED_THEME);

  for (auto folder : std::filesystem::directory_iterator(path))
  {
    if (!folder.is_directory())
    {
      continue;
    }

    FrontendTheme theme{};
    if (theme.TryLoad(folder.path().string()))
    {
      m_themes.emplace(theme.GetName(), theme);

      if (theme.GetName() == selected_theme)
      {
        m_selected_theme = &m_themes[theme.GetName()];
      }
    }
  }
}

void ImGuiFrontend::RecurseFolderForGames(std::string path)
{
  try
  {
    for (auto file : std::filesystem::directory_iterator(path))
    {
      if (file.is_directory())
      {
        RecurseFolderForGames(file.path().string());
        continue;
      }

      if (!file.is_regular_file())
        continue;

      std::filesystem::path normalised =
          std::filesystem::path(file.path().string()).make_preferred();
      std::string game_path = normalised.string();
      std::replace(game_path.begin(), game_path.end(), '\\', '/');
      auto game = new UICommon::GameFile(game_path);

      if (game && game->IsValid())
        m_games.emplace_back(std::move(game));
    }
  }
  catch (std::exception)
  {
    // This folder can't be opened.
  }
}

void ImGuiFrontend::AddGameFolder(std::string path)
{
  m_paths.push_back(path);
  Config::SetIsoPaths(m_paths);
}

bool ImGuiFrontend::TryInput(std::string expression, std::shared_ptr<ciface::Core::Device> device)
{
  auto* input = device->FindInput(expression);
  if (input == nullptr)
    return false;

  return input->GetState() > 0.5f;
}

void DrawSettingsMenu(UIState* state, float frame_scale)
{
  if (ImGui::BeginListBox("##tabs", ImVec2(100 * frame_scale, -1)))
  {
    if (ImGui::Selectable("General", state->selectedTab == General))
    {
      state->selectedTab = General;
    }
    if (ImGui::Selectable("Interface", state->selectedTab == Interface))
    {
      state->selectedTab = Interface;
    }
    if (ImGui::Selectable("Graphics", state->selectedTab == Graphics))
    {
      state->selectedTab = Graphics;
    }
    if (ImGui::Selectable("Audio", state->selectedTab == Audio))
    {
      state->selectedTab = Audio;
    }
    if (ImGui::Selectable("Controls", state->selectedTab == Controls))
    {
      state->selectedTab = Controls;
    }
    if (ImGui::Selectable("GameCube", state->selectedTab == GC))
    {
      state->selectedTab = GC;
    }
    if (ImGui::Selectable("Wii", state->selectedTab == Wii))
    {
      state->selectedTab = Wii;
    }
    if (ImGui::Selectable("Advanced", state->selectedTab == Advanced))
    {
      state->selectedTab = Advanced;
    }
    if (ImGui::Selectable("Paths", state->selectedTab == Paths))
    {
      state->selectedTab = Paths;
    }
    if (ImGui::Selectable("Achievements", state->selectedTab == Achievements))
    {
      state->selectedTab = Achievements;
    }
    if (ImGui::Selectable("About", state->selectedTab == About))
    {
      state->selectedTab = About;
    }

    ImGui::EndListBox();
  }

  ImGui::SameLine();
  if (ImGui::BeginChild("##tabview", ImVec2(-1, -1), true))
  {
    switch (state->selectedTab)
    {
    case General:
      CreateGeneralTab(state);
      break;
    case Interface:
      CreateInterfaceTab(state);
      break;
    case Graphics:
      CreateGraphicsTab(state);
      break;
    case Audio:
      CreateAudioTab(state);
      break;
    case Controls:
      CreateControlsTab(state);
      break;
    case GC:
      CreateGameCubeTab(state);
      break;
    case Wii:
      CreateWiiTab(state);
      break;
    case Paths:
      CreatePathsTab(state);
      break;
    case Advanced:
      CreateAdvancedTab(state);
      break;
    case Achievements:
      CreateAchievementsTab(state);
      break;
    case About:
      ImGui::TextWrapped(
          "Dolphin Emulator on UWP - Version 1.1.9.1 Beta (Based on Dolphin 2506-340)\n\n"
          "This is a fork of Dolphin Emulator introducing Xbox support with a big picture "
          "frontend\n\n"
          "Credits:\n\n"
          "Dolphin: for their amazing work on Dolphin Emulator\n"
          "SirMangler: for their amazing work on porting Dolphin for UWP\n"
          "worleydl: For his oct2024-rebase branch (it helped so much)\n"
          "SirManglers Ko-Fi: https://ko-fi.com/sirmangler\n"
          "Sterns Ko-Fi: https://ko-fi.com/stern\n\n"
          "Dolphin Emulator is licensed under GPLv2+ and is not associated with Nintendo.");
      break;
    }

    ImGui::EndChild();
  }
}

std::shared_ptr<AbstractTexture> FrontendTheme::GetBackground(ThemeBG cat)
{
  return m_textures[cat];
}

bool FrontendTheme::TryLoad(std::string path)
{
  const auto SetThemeOrBackup = [=](ThemeBG target, ThemeBG backup, std::string basePath) {

    std::string pngPath = basePath + ".png";
    std::string jpgPath = basePath + ".jpg";
    std::string jpegPath = basePath + ".jpeg";

    if (File::Exists(pngPath))
    {
      auto bg = CreateTextureFromPath(pngPath, true);
      m_textures[target] = std::move(bg);
    }
    else if (File::Exists(jpgPath))
    {
      auto bg = CreateTextureFromPath(jpgPath, true);
      m_textures[target] = std::move(bg);
    }
    else if (File::Exists(jpegPath))
    {
      auto bg = CreateTextureFromPath(jpegPath, true);
      m_textures[target] = std::move(bg);
    }

    if (!m_textures[target])
    {
      if (m_textures[backup])
      {
        m_textures[target] = m_textures[backup];
      }
    }
  };

  bool hasCarouselBg = File::Exists(path + "\\carousel_background_all.png") ||
                       File::Exists(path + "\\carousel_background_all.jpg") ||
                       File::Exists(path + "\\carousel_background_all.jpeg");

  bool hasMenuBg = File::Exists(path + "\\menu_background.png") ||
                   File::Exists(path + "\\menu_background.jpg") ||
                   File::Exists(path + "\\menu_background.jpeg");

  bool hasListUi = File::Exists(path + "\\list_ui.png") || File::Exists(path + "\\list_ui.jpg") ||
                   File::Exists(path + "\\list_ui.jpeg");

  bool hasCarouselUi = File::Exists(path + "\\carousel_ui.png") ||
                       File::Exists(path + "\\carousel_ui.jpg") ||
                       File::Exists(path + "\\carousel_ui.jpeg");

  if (!hasCarouselBg || !hasMenuBg || !hasListUi || !hasCarouselUi)
  {
    return false;
  }

  std::shared_ptr<AbstractTexture> all_bg;
  if (File::Exists(path + "\\carousel_background_all.png"))
    all_bg = CreateTextureFromPath(path + "\\carousel_background_all.png", true);
  else if (File::Exists(path + "\\carousel_background_all.jpg"))
    all_bg = CreateTextureFromPath(path + "\\carousel_background_all.jpg", true);
  else if (File::Exists(path + "\\carousel_background_all.jpeg"))
    all_bg = CreateTextureFromPath(path + "\\carousel_background_all.jpeg", true);

  if (!all_bg)
    return false;

  m_textures[ThemeBG::BG_All] = all_bg;

  std::shared_ptr<AbstractTexture> menu_bg;
  if (File::Exists(path + "\\menu_background.png"))
    menu_bg = CreateTextureFromPath(path + "\\menu_background.png", true);
  else if (File::Exists(path + "\\menu_background.jpg"))
    menu_bg = CreateTextureFromPath(path + "\\menu_background.jpg", true);
  else if (File::Exists(path + "\\menu_background.jpeg"))
    menu_bg = CreateTextureFromPath(path + "\\menu_background.jpeg", true);

  if (!menu_bg)
    return false;

  m_textures[ThemeBG::BG_Menu] = menu_bg;

  std::shared_ptr<AbstractTexture> list_ui_bg;
  if (File::Exists(path + "\\list_ui.png"))
    list_ui_bg = CreateTextureFromPath(path + "\\list_ui.png", true);
  else if (File::Exists(path + "\\list_ui.jpg"))
    list_ui_bg = CreateTextureFromPath(path + "\\list_ui.jpg", true);
  else if (File::Exists(path + "\\list_ui.jpeg"))
    list_ui_bg = CreateTextureFromPath(path + "\\list_ui.jpeg", true);

  if (!list_ui_bg)
    return false;

  m_textures[ThemeBG::BG_List_UI] = list_ui_bg;

  std::shared_ptr<AbstractTexture> carousel_ui_bg;
  if (File::Exists(path + "\\carousel_ui.png"))
    carousel_ui_bg = CreateTextureFromPath(path + "\\carousel_ui.png", true);
  else if (File::Exists(path + "\\carousel_ui.jpg"))
    carousel_ui_bg = CreateTextureFromPath(path + "\\carousel_ui.jpg", true);
  else if (File::Exists(path + "\\carousel_ui.jpeg"))
    carousel_ui_bg = CreateTextureFromPath(path + "\\carousel_ui.jpeg", true);

  if (!carousel_ui_bg)
    return false;

  m_textures[ThemeBG::BG_Carousel_UI] = carousel_ui_bg;

  SetThemeOrBackup(BG_Wii, BG_All, path + "\\carousel_background_wii");
  SetThemeOrBackup(BG_GC, BG_All, path + "\\carousel_background_gamecube");
  SetThemeOrBackup(BG_Other, BG_All, path + "\\carousel_background_other");
  SetThemeOrBackup(BG_List, BG_Menu, path + "\\menu_background_list");
  SetThemeOrBackup(BG_Netplay, BG_Menu, path + "\\menu_background_netplay");

  m_name = std::filesystem::path(path).filename().string();

  return true;
}



void DrawPropertiesDialog(UIState* state, float frame_scale)
{
  if (!state->propertiesGame)
    return;

  ImVec2 window_size = ImVec2(600 * frame_scale, 450 * frame_scale);
  ImVec2 window_pos = ImVec2(ImGui::GetIO().DisplaySize.x / 2 - window_size.x / 2,
                             ImGui::GetIO().DisplaySize.y / 2 - window_size.y / 2);

  ImGui::SetNextWindowPos(window_pos);
  ImGui::SetNextWindowSize(window_size);

  std::string window_title = "Properties: " + state->propertiesGame->GetLongName();

  if (ImGui::Begin(window_title.c_str(), &state->showPropertiesWindow,
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
  {
    if (ImGui::BeginTabBar("PropertiesTabs"))
    {
      if (ImGui::BeginTabItem("Info"))
      {
        state->selectedPropertiesTab = Info;
        CreatePropertiesInfoTab(state);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Game Config"))
      {
        state->selectedPropertiesTab = GameConfig;
        CreatePropertiesGameConfigTab(state);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Patches"))
      {
        state->selectedPropertiesTab = Patches;
        CreatePropertiesPatchesTab(state);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("AR Codes"))
      {
        state->selectedPropertiesTab = ARCodes;
        CreatePropertiesARCodesTab(state);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Gecko Codes"))
      {
        state->selectedPropertiesTab = GeckoCodes;
        CreatePropertiesGeckoCodesTab(state);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Graphics Mods"))
      {
        state->selectedPropertiesTab = GraphicsMods;
        CreatePropertiesGraphicsModsTab(state);
        ImGui::EndTabItem();
      }

#ifdef USE_RETRO_ACHIEVEMENTS
      if (ImGui::BeginTabItem("Achievements"))
      {
        state->selectedPropertiesTab = PropertiesAchievements;
        CreatePropertiesAchievementsTab(state);
        ImGui::EndTabItem();
      }
#endif  // USE_RETRO_ACHIEVEMENTS

      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}

void CreatePropertiesInfoTab(UIState* state)
{
  if (!state->propertiesGame)
    return;

  auto game = state->propertiesGame;

  ImGui::BeginGroup();
  ImGui::Text("File Details");
  ImGui::Separator();

  ImGui::Text("File Name: %s", game->GetFileName().c_str());
  ImGui::Text("File Path: %s", game->GetFilePath().c_str());

  // File size
  u64 file_size = game->GetFileSize();
  std::string size_str;
  if (file_size > 1024 * 1024 * 1024)
    size_str = fmt::format("{:.2f} GB", file_size / (1024.0 * 1024.0 * 1024.0));
  else if (file_size > 1024 * 1024)
    size_str = fmt::format("{:.2f} MB", file_size / (1024.0 * 1024.0));
  else
    size_str = fmt::format("{:.2f} KB", file_size / 1024.0);

  ImGui::Text("File Size: %s", size_str.c_str());
  ImGui::EndGroup();

  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::BeginGroup();
  ImGui::Text("Game Details");
  ImGui::Separator();

  ImGui::Text("Internal Name: %s", game->GetInternalName().c_str());
  ImGui::Text("Game ID: %s", game->GetGameID().c_str());

  const u64 title_id = game->GetTitleID();
  if (title_id != 0)
    ImGui::Text("Title ID: %016llx", static_cast<unsigned long long>(title_id));

  const u16 revision = game->GetRevision();
  if (revision != 0)
    ImGui::Text("Revision: %u", static_cast<unsigned>(revision));

  // Region
  std::string region = "Unknown";
  switch (game->GetCountry())
  {
  case DiscIO::Country::Europe:
    region = "Europe (PAL)";
    break;
  case DiscIO::Country::Japan:
    region = "Japan (NTSC-J)";
    break;
  case DiscIO::Country::USA:
    region = "USA (NTSC-U)";
    break;
  case DiscIO::Country::Australia:
    region = "Australia (PAL)";
    break;
  case DiscIO::Country::France:
    region = "France (PAL)";
    break;
  case DiscIO::Country::Germany:
    region = "Germany (PAL)";
    break;
  case DiscIO::Country::Italy:
    region = "Italy (PAL)";
    break;
  case DiscIO::Country::Korea:
    region = "Korea (NTSC-K)";
    break;
  case DiscIO::Country::Netherlands:
    region = "Netherlands (PAL)";
    break;
  case DiscIO::Country::Russia:
    region = "Russia (PAL)";
    break;
  case DiscIO::Country::Spain:
    region = "Spain (PAL)";
    break;
  case DiscIO::Country::Taiwan:
    region = "Taiwan (NTSC-T)";
    break;
  default:
    region = "Unknown";
    break;
  }
  ImGui::Text("Region: %s", region.c_str());

  // Platform
  std::string platform = "Unknown";
  switch (game->GetPlatform())
  {
  case DiscIO::Platform::GameCubeDisc:
    platform = "GameCube Disc";
    break;
  case DiscIO::Platform::WiiDisc:
    platform = "Wii Disc";
    break;
  case DiscIO::Platform::WiiWAD:
    platform = "WiiWare/Virtual Console";
    break;
  case DiscIO::Platform::ELFOrDOL:
    platform = "ELF/DOL";
    break;
  default:
    platform = "Unknown";
    break;
  }
  ImGui::Text("Platform: %s", platform.c_str());

  ImGui::EndGroup();
}

void CreatePropertiesGameConfigTab(UIState* state)
{
  if (!state->propertiesGame)
    return;

  const std::string game_id = state->propertiesGame->GetGameID();
  const u16 revision = state->propertiesGame->GetRevision();
  
  ImGui::Text("Game-specific configuration for: %s", game_id.c_str());
  ImGui::Separator();

  static std::unique_ptr<Config::Layer> game_config_layer;
  if (!game_config_layer)
  {
    game_config_layer = std::make_unique<Config::Layer>(
        ConfigLoaders::GenerateLocalGameConfigLoader(game_id, revision));
  }

  if (ImGui::CollapsingHeader("Core Settings", ImGuiTreeNodeFlags_DefaultOpen))
  {
    bool use_jit = game_config_layer->Get(Config::MAIN_CPU_CORE) == PowerPC::CPUCore::JIT64;
    if (ImGui::Checkbox("Enable JIT (recommended)", &use_jit))
    {
      Config::SetBaseOrCurrent(Config::MAIN_CPU_CORE, 
                              use_jit ? PowerPC::CPUCore::JIT64 : PowerPC::CPUCore::Interpreter);
    }

    bool dual_core = game_config_layer->Get(Config::MAIN_CPU_THREAD);
    if (ImGui::Checkbox("Enable Dual Core (speedup)", &dual_core))
    {
      Config::SetBaseOrCurrent(Config::MAIN_CPU_THREAD, dual_core);
    }

    bool mmu = game_config_layer->Get(Config::MAIN_MMU);
    if (ImGui::Checkbox("Enable MMU (required for some games)", &mmu))
    {
      Config::SetBaseOrCurrent(Config::MAIN_MMU, mmu);
    }

    bool sync_gpu = game_config_layer->Get(Config::MAIN_SYNC_GPU);
    if (ImGui::Checkbox("Synchronize GPU Thread", &sync_gpu))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SYNC_GPU, sync_gpu);
    }

    int cpu_clock_override = static_cast<int>(game_config_layer->Get(Config::MAIN_OVERCLOCK) * 100.0f);
    if (ImGui::SliderInt("CPU Clock Override %%", &cpu_clock_override, 1, 300))
    {
      Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK, cpu_clock_override / 100.0f);
      Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK_ENABLE, cpu_clock_override != 100);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##cpu"))
    {
      Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK, 1.0f);
      Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK_ENABLE, false);
    }
  }

  if (ImGui::CollapsingHeader("Audio Settings"))
  {
    bool use_dsp_hle = game_config_layer->Get(Config::MAIN_DSP_HLE);
    if (ImGui::Checkbox("DSP HLE Emulation (fast)", &use_dsp_hle))
    {
      Config::SetBaseOrCurrent(Config::MAIN_DSP_HLE, use_dsp_hle);
    }

    int audio_latency = game_config_layer->Get(Config::MAIN_AUDIO_LATENCY);
    if (ImGui::SliderInt("Audio Latency", &audio_latency, 0, 300))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_LATENCY, audio_latency);
    }
  }

  if (ImGui::CollapsingHeader("Game-specific Hacks"))
  {
    bool fprf = game_config_layer->Get(Config::MAIN_FPRF);
    if (ImGui::Checkbox("Enable FPRF", &fprf))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FPRF, fprf);
    }

    bool accurate_nans = game_config_layer->Get(Config::MAIN_ACCURATE_NANS);
    if (ImGui::Checkbox("Accurate NaN Handling", &accurate_nans))
    {
      Config::SetBaseOrCurrent(Config::MAIN_ACCURATE_NANS, accurate_nans);
    }
  }

  ImGui::Separator();
  if (PrimaryButton("Save Game Config"))
  {
    std::string ini_path = File::GetUserPath(D_GAMESETTINGS_IDX) + game_id + ".ini";
    Config::Save();
    ImGui::OpenPopup("Config Saved");
  }
  ImGui::SameLine();
  if (DangerButton("Reset to Defaults"))
  {
    ImGui::OpenPopup("Reset Confirm");
  }

  // Popup modals
  if (ImGui::BeginPopupModal("Config Saved", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Game configuration saved to %s.ini", game_id.c_str());
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Reset Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Are you sure you want to reset all settings to defaults?");
    if (ImGui::Button("Yes"))
    {
      std::string ini_path = File::GetUserPath(D_GAMESETTINGS_IDX) + game_id + ".ini";
      if (File::Exists(ini_path))
      {
        File::Delete(ini_path);
        Config::OnConfigChanged();
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void CreatePropertiesPatchesTab(UIState* state)
{
  if (!state->propertiesGame)
    return;

  const std::string game_id = state->propertiesGame->GetGameID();
  
  ImGui::Text("Patches for: %s", game_id.c_str());
  ImGui::Separator();

  std::vector<PatchEngine::Patch> patches;
  
  Common::IniFile global_ini;
  Common::IniFile local_ini;
  
  std::string global_patches_path = File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + game_id + ".ini";
  std::string local_patches_path = File::GetUserPath(D_GAMESETTINGS_IDX) + game_id + ".ini";
  
  global_ini.Load(global_patches_path);
  local_ini.Load(local_patches_path);
  
  PatchEngine::LoadPatchSection("OnFrame_Enabled", &patches, global_ini, local_ini);
  
  static char search_buffer[256] = "";
  ImGui::SetNextItemWidth(200.0f);
  ImGui::InputText("Search patches", search_buffer, sizeof(search_buffer));
  ImGui::SameLine();
  if (ImGui::Button("Clear"))
  {
    search_buffer[0] = '\0';
  }

  bool patches_enabled = Config::Get(Config::MAIN_ENABLE_CHEATS);
  if (ImGui::Checkbox("Enable Patches", &patches_enabled))
  {
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_CHEATS, patches_enabled);
  }

  ImGui::Separator();

  if (ImGui::BeginChild("PatchesList", ImVec2(0, 300), true))
  {
    for (size_t i = 0; i < patches.size(); ++i)
    {
      auto& patch = patches[i];
      
      if (strlen(search_buffer) > 0)
      {
        std::string patch_lower = patch.name;
        std::string search_lower = search_buffer;
        std::transform(patch_lower.begin(), patch_lower.end(), patch_lower.begin(), ::tolower);
        std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
        
        if (patch_lower.find(search_lower) == std::string::npos)
          continue;
      }

      ImGui::PushID(static_cast<int>(i));
      
      bool enabled = patch.enabled;
      if (ImGui::Checkbox("", &enabled))
      {
        patch.enabled = enabled;
        PatchEngine::SavePatchSection(&local_ini, patches);
        local_ini.Save(local_patches_path);
      }
      ImGui::SameLine();
      
      if (ImGui::TreeNode(patch.name.c_str()))
      {
        ImGui::Text("Patch entries (%zu):", patch.entries.size());
        
        if (ImGui::BeginTable("PatchEntries", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
          ImGui::TableSetupColumn("Type");
          ImGui::TableSetupColumn("Address");
          ImGui::TableSetupColumn("Value");
          ImGui::TableHeadersRow();
          
          for (const auto& entry : patch.entries)
          {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            
            switch (entry.type)
            {
              case PatchEngine::PatchType::Patch8Bit:
                ImGui::Text("8-bit");
                break;
              case PatchEngine::PatchType::Patch16Bit:
                ImGui::Text("16-bit");
                break;
              case PatchEngine::PatchType::Patch32Bit:
                ImGui::Text("32-bit");
                break;
              default:
                ImGui::Text("Unknown");
                break;
            }
            
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", entry.address);
            
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", entry.value);
          }
          
          ImGui::EndTable();
        }
        
        ImGui::Separator();
        if (ImGui::Button("Copy Patch Data"))
        {
          std::string patch_text = "Patch: " + patch.name + "\n";
          patch_text += "Entries:\n";
          
          for (const auto& entry : patch.entries)
          {
            patch_text += fmt::format("0x{:08X} = 0x{:08X}\n", entry.address, entry.value);
          }
          
          ImGui::SetClipboardText(patch_text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit Patch"))
        {
          ImGui::OpenPopup("Edit Patch");
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
        {
          patches.erase(patches.begin() + i);
          PatchEngine::SavePatchSection(&local_ini, patches);
          local_ini.Save(local_patches_path);
          ImGui::TreePop();
          ImGui::PopID();
          break;
        }
        
        ImGui::TreePop();
      }
      
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  
  if (ImGui::Button("Enable All"))
  {
    for (auto& patch : patches)
      patch.enabled = true;
    PatchEngine::SavePatchSection(&local_ini, patches);
    local_ini.Save(local_patches_path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Disable All"))
  {
    for (auto& patch : patches)
      patch.enabled = false;
    PatchEngine::SavePatchSection(&local_ini, patches);
    local_ini.Save(local_patches_path);
  }
  ImGui::SameLine();
  if (PrimaryButton("Add Custom Patch"))
  {
    ImGui::OpenPopup("Add Patch");
  }
  ImGui::SameLine();
  if (SecondaryButton("Reload from Disk"))
  {
    // Reload patches from the game's INI files
    patches.clear();
    global_ini.Load(global_patches_path);
    local_ini.Load(local_patches_path);
    PatchEngine::LoadPatchSection("OnFrame_Enabled", &patches, global_ini, local_ini);
  }

  // Add patch modal
  if (ImGui::BeginPopupModal("Add Patch", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char patch_name[256] = "";
    static char patch_entries[1024] = "";
    
    ImGui::Text("Add Custom Patch");
    ImGui::Separator();
    
    ImGui::InputText("Patch Name", patch_name, sizeof(patch_name));
    ImGui::InputTextMultiline("Patch Entries", patch_entries, sizeof(patch_entries), ImVec2(400, 200));
    
    ImGui::TextWrapped("Format: address=value (e.g., 0x80123456=0x12345678)");
    
    if (ImGui::Button("Add"))
    {
      if (strlen(patch_name) > 0 && strlen(patch_entries) > 0)
      {
        PatchEngine::Patch new_patch;
        new_patch.name = patch_name;
        new_patch.enabled = false;
        new_patch.user_defined = true;
        
        std::istringstream iss(patch_entries);
        std::string line;
        while (std::getline(iss, line))
        {
          size_t eq_pos = line.find('=');
          if (eq_pos != std::string::npos)
          {
            try
            {
              std::string addr_str = line.substr(0, eq_pos);
              std::string val_str = line.substr(eq_pos + 1);
              
              PatchEngine::PatchEntry entry;
              entry.address = std::stoul(addr_str, nullptr, 0);
              entry.value = std::stoul(val_str, nullptr, 0);
              entry.type = PatchEngine::PatchType::Patch32Bit;
              
              new_patch.entries.push_back(entry);
            }
            catch (const std::exception&)
            {
              // Skip invalid lines
            }
          }
        }
        
        if (!new_patch.entries.empty())
        {
          patches.push_back(new_patch);
          PatchEngine::SavePatchSection(&local_ini, patches);
          local_ini.Save(local_patches_path);
        }
        
        patch_name[0] = '\0';
        patch_entries[0] = '\0';
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Edit Patch", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char edit_name[256] = "";
    
    ImGui::Text("Edit Patch");
    ImGui::Separator();
    
    ImGui::InputText("Patch Name", edit_name, sizeof(edit_name));
    
    if (ImGui::Button("Save"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void CreatePropertiesARCodesTab(UIState* state)
{
  if (!state->propertiesGame)
    return;

  const std::string game_id = state->propertiesGame->GetGameID();
  
  ImGui::Text("Action Replay Codes for: %s", game_id.c_str());
  ImGui::Separator();

  // Load AR codes for this game
  Common::IniFile global_ar_ini;
  Common::IniFile local_ar_ini;
  
  std::string global_ar_path = File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + game_id + ".ini";
  std::string local_ar_path = File::GetUserPath(D_GAMESETTINGS_IDX) + game_id + ".ini";
  
  global_ar_ini.Load(global_ar_path);
  local_ar_ini.Load(local_ar_path);
  
  std::vector<ActionReplay::ARCode> ar_codes = ActionReplay::LoadCodes(global_ar_ini, local_ar_ini);
  
  static char ar_search[256] = "";
  ImGui::SetNextItemWidth(200.0f);
  ImGui::InputText("Search AR codes", ar_search, sizeof(ar_search));
  ImGui::SameLine();
  static bool show_enabled_only = false;
  ImGui::Checkbox("Show enabled only", &show_enabled_only);

  ImGui::Separator();

  if (ImGui::BeginChild("ARCodesList", ImVec2(0, 350), true))
  {
    for (size_t i = 0; i < ar_codes.size(); ++i)
    {
      auto& ar_code = ar_codes[i];
      
      if (strlen(ar_search) > 0)
      {
        std::string name_lower = ar_code.name;
        std::string search_lower = ar_search;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
        
        if (name_lower.find(search_lower) == std::string::npos)
          continue;
      }
      
      if (show_enabled_only && !ar_code.enabled)
        continue;

      ImGui::PushID(static_cast<int>(i));
      
      bool enabled = ar_code.enabled;
      if (ImGui::Checkbox("", &enabled))
      {
        ar_code.enabled = enabled;
        ActionReplay::SaveCodes(&local_ar_ini, ar_codes);
        local_ar_ini.Save(local_ar_path);
      }
      ImGui::SameLine();
      
      bool node_open = ImGui::TreeNode(ar_code.name.c_str());
      
      if (ar_code.enabled)
      {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ACTIVE]");
      }
      
      if (node_open)
      {
        ImGui::Text("Code entries:");
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        
        for (const auto& entry : ar_code.ops)
        {
          ImGui::Text("%08X %08X", entry.cmd_addr, entry.value);
        }
        
        ImGui::PopFont();
        
        ImGui::Separator();
        if (ImGui::Button("Copy Code"))
        {
          std::string code_text;
          for (const auto& entry : ar_code.ops)
          {
            code_text += fmt::format("{:08X} {:08X}\n", entry.cmd_addr, entry.value);
          }
          ImGui::SetClipboardText(code_text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit Code"))
        {
          ImGui::OpenPopup("Edit AR Code");
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
          ar_codes.erase(ar_codes.begin() + i);
          ActionReplay::SaveCodes(&local_ar_ini, ar_codes);
          local_ar_ini.Save(local_ar_path);
          ImGui::TreePop();
          ImGui::PopID();
          break;
        }
        
        ImGui::TreePop();
      }
      
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  
  if (ImGui::Button("Add New AR Code"))
  {
    ImGui::OpenPopup("Add AR Code");
  }
  ImGui::SameLine();
  if (ImGui::Button("Enable All"))
  {
    for (auto& code : ar_codes)
      code.enabled = true;
    ActionReplay::SaveCodes(&local_ar_ini, ar_codes);
    local_ar_ini.Save(local_ar_path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Disable All"))
  {
    for (auto& code : ar_codes)
      code.enabled = false;
    ActionReplay::SaveCodes(&local_ar_ini, ar_codes);
    local_ar_ini.Save(local_ar_path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Load from File"))
  {
    ImGui::OpenPopup("Load AR Codes");
  }

  if (ImGui::BeginPopupModal("Add AR Code", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char new_name[256] = "";
    static char new_code[1024] = "";
    
    ImGui::Text("Add New Action Replay Code");
    ImGui::Separator();
    
    ImGui::InputText("Code Name", new_name, sizeof(new_name));
    ImGui::InputTextMultiline("AR Code", new_code, sizeof(new_code), ImVec2(400, 150));
    
    ImGui::Text("Format: XXXXXXXX YYYYYYYY (address value pairs)");
    
    if (ImGui::Button("Add"))
    {
      if (strlen(new_name) > 0 && strlen(new_code) > 0)
      {
        ActionReplay::ARCode new_ar_code;
        new_ar_code.name = new_name;
        new_ar_code.enabled = false;
        new_ar_code.user_defined = true;
        
        std::istringstream iss(new_code);
        std::string line;
        while (std::getline(iss, line))
        {
          if (line.length() >= 17) // "XXXXXXXX YYYYYYYY"
          {
            ActionReplay::AREntry entry;
            std::string addr_str = line.substr(0, 8);
            std::string val_str = line.substr(9, 8);
            
            try
            {
              entry.cmd_addr = std::stoul(addr_str, nullptr, 16);
              entry.value = std::stoul(val_str, nullptr, 16);
              new_ar_code.ops.push_back(entry);
            }
            catch (const std::exception&)
            {
              // Skip invalid lines
            }
          }
        }
        
        if (!new_ar_code.ops.empty())
        {
          ar_codes.push_back(new_ar_code);
          ActionReplay::SaveCodes(&local_ar_ini, ar_codes);
          local_ar_ini.Save(local_ar_path);
        }
        
        new_name[0] = '\0';
        new_code[0] = '\0';
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Load AR Codes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Load Action Replay codes from file");
    ImGui::Separator();
    
    static char file_path[512] = "";
    ImGui::InputText("File path", file_path, sizeof(file_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse"))
    {
      // File dialog would go here
    }
    
    if (ImGui::Button("Load"))
    {
      if (strlen(file_path) > 0)
      {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void CreatePropertiesGeckoCodesTab(UIState* state)
{
  if (!state->propertiesGame)
    return;

  const std::string game_id = state->propertiesGame->GetGameID();
  
  ImGui::Text("Gecko Codes for: %s", game_id.c_str());
  ImGui::Separator();

  Common::IniFile global_gecko_ini;
  Common::IniFile local_gecko_ini;
  
  std::string global_gecko_path = File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + game_id + ".ini";
  std::string local_gecko_path = File::GetUserPath(D_GAMESETTINGS_IDX) + game_id + ".ini";
  
  global_gecko_ini.Load(global_gecko_path);
  local_gecko_ini.Load(local_gecko_path);
  
  std::vector<Gecko::GeckoCode> gecko_codes = Gecko::LoadCodes(global_gecko_ini, local_gecko_ini);
  
  static char gecko_search[256] = "";
  ImGui::SetNextItemWidth(200.0f);
  ImGui::InputText("Search codes", gecko_search, sizeof(gecko_search));
  ImGui::SameLine();
  
  bool codes_enabled = Config::Get(Config::MAIN_ENABLE_CHEATS);
  if (ImGui::Checkbox("Enable Gecko Codes", &codes_enabled))
  {
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_CHEATS, codes_enabled);
  }

  ImGui::Separator();

  if (ImGui::BeginChild("GeckoCodesList", ImVec2(0, 400), true))
  {
    for (size_t i = 0; i < gecko_codes.size(); ++i)
    {
      auto& gecko_code = gecko_codes[i];
      
      if (strlen(gecko_search) > 0)
      {
        std::string combined = gecko_code.name + " " + gecko_code.creator;
        for (const auto& note : gecko_code.notes)
        {
          combined += " " + note;
        }
        std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
        std::string search_lower = gecko_search;
        std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
        
        if (combined.find(search_lower) == std::string::npos)
          continue;
      }

      ImGui::PushID(static_cast<int>(i));
      
      bool enabled = gecko_code.enabled;
      if (ImGui::Checkbox("", &enabled))
      {
        gecko_code.enabled = enabled;
        Gecko::SaveCodes(local_gecko_ini, gecko_codes);
        local_gecko_ini.Save(local_gecko_path);
      }
      ImGui::SameLine();
      
      ImVec4 name_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
      if (!gecko_code.creator.empty())
      {
        if (gecko_code.creator.find("Official") != std::string::npos)
          name_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        else
          name_color = ImVec4(0.0f, 0.8f, 1.0f, 1.0f);
      }
      
      ImGui::PushStyleColor(ImGuiCol_Text, name_color);
      bool node_open = ImGui::TreeNode(gecko_code.name.c_str());
      ImGui::PopStyleColor();
      
      if (gecko_code.enabled)
      {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ON]");
      }
      
      if (node_open)
      {
        if (!gecko_code.notes.empty())
        {
          ImGui::Text("Notes:");
          for (const auto& note : gecko_code.notes)
          {
            ImGui::TextWrapped("- %s", note.c_str());
          }
        }
        
        if (!gecko_code.creator.empty())
        {
          ImGui::Text("Creator: %s", gecko_code.creator.c_str());
        }
        
        ImGui::Separator();
        ImGui::Text("Gecko Code:");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
        if (ImGui::BeginChild("CodeDisplay", ImVec2(0, 150), true))
        {
          ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
          
          for (const auto& code : gecko_code.codes)
          {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%08X", code.address);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), " %08X", code.data);
          }
          
          ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        
        ImGui::Separator();
        if (ImGui::Button("Copy Code"))
        {
          std::string code_text;
          for (const auto& code : gecko_code.codes)
          {
            code_text += fmt::format("{:08X} {:08X}\n", code.address, code.data);
          }
          ImGui::SetClipboardText(code_text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit"))
        {
          ImGui::OpenPopup("Edit Gecko Code");
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
          gecko_codes.erase(gecko_codes.begin() + i);
          Gecko::SaveCodes(local_gecko_ini, gecko_codes);
          local_gecko_ini.Save(local_gecko_path);
          ImGui::TreePop();
          ImGui::PopID();
          break;
        }
        
        ImGui::TreePop();
      }
      
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  
  if (ImGui::Button("Add New Code"))
  {
    ImGui::OpenPopup("Add Gecko Code");
  }
  ImGui::SameLine();
  if (ImGui::Button("Download Codes"))
  {
    ImGui::OpenPopup("Download Codes");
  }
  ImGui::SameLine();
  if (ImGui::Button("Enable All"))
  {
    for (auto& code : gecko_codes)
      code.enabled = true;
    Gecko::SaveCodes(local_gecko_ini, gecko_codes);
    local_gecko_ini.Save(local_gecko_path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Disable All"))
  {
    for (auto& code : gecko_codes)
      code.enabled = false;
    Gecko::SaveCodes(local_gecko_ini, gecko_codes);
    local_gecko_ini.Save(local_gecko_path);
  }

  if (ImGui::BeginPopupModal("Add Gecko Code", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char new_name[256] = "";
    static char new_notes[512] = "";
    static char new_creator[128] = "";
    static char new_code[1024] = "";
    
    ImGui::Text("Add New Gecko Code");
    ImGui::Separator();
    
    ImGui::InputText("Name", new_name, sizeof(new_name));
    ImGui::InputTextMultiline("Notes", new_notes, sizeof(new_notes), ImVec2(500, 60));
    ImGui::InputText("Creator", new_creator, sizeof(new_creator));
    ImGui::InputTextMultiline("Gecko Code", new_code, sizeof(new_code), ImVec2(500, 200));
    
    ImGui::TextWrapped("Format: XXXXXXXX YYYYYYYY (8-digit address followed by 8-digit value)");
    
    if (ImGui::Button("Add"))
    {
      if (strlen(new_name) > 0 && strlen(new_code) > 0)
      {
        Gecko::GeckoCode new_gecko_code;
        new_gecko_code.name = new_name;
        if (strlen(new_notes) > 0)
        {
          new_gecko_code.notes.push_back(new_notes);
        }
        new_gecko_code.creator = strlen(new_creator) > 0 ? new_creator : "User";
        new_gecko_code.enabled = false;
        new_gecko_code.user_defined = true;
        
        std::istringstream iss(new_code);
        std::string line;
        while (std::getline(iss, line))
        {
          if (line.length() >= 17) // "XXXXXXXX YYYYYYYY"
          {
            Gecko::GeckoCode::Code code;
            std::string addr_str = line.substr(0, 8);
            std::string data_str = line.substr(9, 8);
            
            try
            {
              code.address = std::stoul(addr_str, nullptr, 16);
              code.data = std::stoul(data_str, nullptr, 16);
              code.original_line = line;
              new_gecko_code.codes.push_back(code);
            }
            catch (const std::exception&)
            {
              // Skip invalid lines
            }
          }
        }
        
        if (!new_gecko_code.codes.empty())
        {
          gecko_codes.push_back(new_gecko_code);
          Gecko::SaveCodes(local_gecko_ini, gecko_codes);
          local_gecko_ini.Save(local_gecko_path);
        }
        
        new_name[0] = '\0';
        new_notes[0] = '\0';
        new_creator[0] = '\0';
        new_code[0] = '\0';
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Download Codes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Download Gecko Codes from Database");
    ImGui::Separator();
    
    ImGui::Text("This feature would connect to the Gecko code database");
    ImGui::Text("and download codes for: %s", game_id.c_str());
    ImGui::Separator();
    
    if (ImGui::Button("Open Gecko Codes Website"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void CreatePropertiesGraphicsModsTab(UIState* state)
{
  if (!state->propertiesGame)
    return;

  const std::string game_id = state->propertiesGame->GetGameID();
  ImGui::Text("Graphics Modifications for: %s", game_id.c_str());
  ImGui::Separator();

  std::string graphics_mod_path = File::GetUserPath(D_GRAPHICSMOD_IDX) + game_id + "/";
  bool mods_dir_exists = File::IsDirectory(graphics_mod_path);
  
  if (!mods_dir_exists)
  {
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No graphics mods directory found for this game.");
    ImGui::Text("Graphics mods should be placed in:");
    ImGui::TextWrapped("%s", graphics_mod_path.c_str());
    
    if (ImGui::Button("Create Mods Directory"))
    {
      File::CreateDir(graphics_mod_path);
      mods_dir_exists = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Mods Folder"))
    {
      // Open file explorer to mods directory
    }
    return;
  }

  bool graphics_mods_enabled = Config::Get(Config::GFX_MODS_ENABLE);
  if (ImGui::Checkbox("Enable Graphics Mods", &graphics_mods_enabled))
  {
    Config::SetBaseOrCurrent(Config::GFX_MODS_ENABLE, graphics_mods_enabled);
  }

  ImGui::Separator();

  File::FSTEntry root = File::ScanDirectoryTree(graphics_mod_path, true);
  std::vector<std::string> mod_directories;
  
  for (const auto& entry : root.children)
  {
    if (entry.isDirectory)
    {
      mod_directories.push_back(entry.physicalName);
    }
  }
  
  if (mod_directories.empty())
  {
    ImGui::Text("No graphics mods found in:");
    ImGui::TextWrapped("%s", graphics_mod_path.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("To add graphics mods, create subdirectories in the mods folder above. "
                      "Each subdirectory should contain mod files like textures, models, or shaders.");
  }
  else
  {
    ImGui::Text("Available Graphics Mods:");
    
    if (ImGui::BeginChild("GraphicsModsList", ImVec2(0, 300), true))
    {
      for (const std::string& mod_dir : mod_directories)
      {
        if (mod_dir == "." || mod_dir == "..")
          continue;
          
        std::string full_mod_path = graphics_mod_path + mod_dir;
        if (!File::IsDirectory(full_mod_path))
          continue;

        ImGui::PushID(mod_dir.c_str());
        
        static bool mod_enabled = true; 
        if (ImGui::Checkbox("", &mod_enabled))
        {
          // Handle enabling/disabling the mod
        }
        ImGui::SameLine();
        
        if (ImGui::TreeNode(mod_dir.c_str()))
        {
          File::FSTEntry mod_root = File::ScanDirectoryTree(full_mod_path, false);
          std::vector<std::string> mod_files;
          
          for (const auto& entry : mod_root.children)
          {
            if (!entry.isDirectory)
            {
              mod_files.push_back(entry.physicalName);
            }
          }
          
          ImGui::Text("Files: %zu", mod_files.size());
          
          // Count different file types
          int texture_count = 0, model_count = 0, shader_count = 0;
          for (const std::string& file : mod_files)
          {
            std::string filename, ext;
            SplitPath(file, nullptr, &filename, &ext);
            if (ext == ".png" || ext == ".dds" || ext == ".tga")
              texture_count++;
            else if (ext == ".obj" || ext == ".dae" || ext == ".fbx")
              model_count++;
            else if (ext == ".glsl" || ext == ".hlsl" || ext == ".shader")
              shader_count++;
          }
          
          if (texture_count > 0)
            ImGui::BulletText("Textures: %d", texture_count);
          if (model_count > 0)
            ImGui::BulletText("Models: %d", model_count);
          if (shader_count > 0)
            ImGui::BulletText("Shaders: %d", shader_count);
            
          ImGui::Separator();
          if (ImGui::Button("Open Mod Folder"))
          {
            // Open file explorer to this mod's directory
          }
          ImGui::SameLine();
          if (ImGui::Button("Delete Mod"))
          {
            ImGui::OpenPopup("Delete Mod Confirm");
          }
          
          if (ImGui::BeginPopupModal("Delete Mod Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
          {
            ImGui::Text("Are you sure you want to delete the mod '%s'?", mod_dir.c_str());
            ImGui::Text("This action cannot be undone.");
            
            if (ImGui::Button("Delete"))
            {
              File::DeleteDirRecursively(full_mod_path);
              ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
              ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
          }
          
          ImGui::TreePop();
        }
        
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
  }

  ImGui::Separator();
  
  if (ImGui::Button("Open Mods Folder"))
  {
    // Open file explorer to the graphics mods directory
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh Mods List"))
  {
    // Refresh the mod directories list
  }
  ImGui::SameLine();
  if (ImGui::Button("Install Mod"))
  {
    ImGui::OpenPopup("Install Graphics Mod");
  }
  ImGui::SameLine();
  if (ImGui::Button("Help"))
  {
    ImGui::OpenPopup("Graphics Mods Help");
  }

  if (ImGui::BeginPopupModal("Install Graphics Mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Install Graphics Mod");
    ImGui::Separator();
    
    static char mod_name[256] = "";
    static char mod_path[512] = "";
    
    ImGui::InputText("Mod Name", mod_name, sizeof(mod_name));
    ImGui::InputText("Mod Archive Path", mod_path, sizeof(mod_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse"))
    {
      // File dialog for selecting mod archive
    }
    
    ImGui::TextWrapped("Supported formats: ZIP, RAR, 7Z");
    
    if (ImGui::Button("Install"))
    {
      if (strlen(mod_name) > 0 && strlen(mod_path) > 0)
      {
        std::string target_path = graphics_mod_path + mod_name + "/";
        File::CreateDir(target_path);

        // todo: add archive extraction logic here
        
        mod_name[0] = '\0';
        mod_path[0] = '\0';
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Graphics Mods Help", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Graphics Mods Help");
    ImGui::Separator();
    
    ImGui::TextWrapped("Graphics mods allow you to enhance the visual quality of games by:");
    ImGui::BulletText("Replacing textures with higher resolution versions");
    ImGui::BulletText("Adding new models and geometry");
    ImGui::BulletText("Applying custom shaders and effects");
    
    ImGui::Separator();
    ImGui::Text("Installation:");
    ImGui::BulletText("Create a folder named after your mod in the graphics mods directory");
    ImGui::BulletText("Place mod files in the appropriate subdirectories");
    ImGui::BulletText("Enable graphics mods in the settings");
    
    ImGui::Separator();
    ImGui::Text("Mod Directory: %s", graphics_mod_path.c_str());
    
    if (ImGui::Button("Close"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

#ifdef USE_RETRO_ACHIEVEMENTS

void DrawSpinner(const char* label, float radius, float thickness, ImU32 color)
{
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return;

  ImGuiContext& g = *GImGui;
  const ImGuiStyle& style = g.Style;
  const ImGuiID id = window->GetID(label);

  ImVec2 pos = window->DC.CursorPos;
  ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

  const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  ImGui::ItemSize(bb, style.FramePadding.y);
  if (!ImGui::ItemAdd(bb, id))
    return;

  window->DrawList->PathClear();

  int num_segments = 30;
  int start = abs(ImSin(g.Time * 1.8f) * (num_segments - 5));

  const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
  const float a_max = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

  const ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

  for (int i = 0; i < num_segments; i++)
  {
    const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
    window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + g.Time * 8) * radius,
                                       centre.y + ImSin(a + g.Time * 8) * radius));
  }

  window->DrawList->PathStroke(color, false, thickness);
}

void CreatePropertiesAchievementsTab(UIState* state)
{
  if (!state->propertiesGame)
    return;

#ifdef _WIN32
  static bool debug_logged = false;
  if (!debug_logged)
  {
    OutputDebugStringA(("CreatePropertiesAchievementsTab called for: " + state->propertiesGame->GetFilePath() + "\n").c_str());
    debug_logged = true;
  }
#endif

  auto& achievement_manager = AchievementManager::GetInstance();
  
  // Check if user is logged in
  if (!achievement_manager.HasAPIToken())
  {
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Not logged in to RetroAchievements");
    ImGui::Separator();
    ImGui::TextWrapped("To view achievements for this game, please log in to RetroAchievements "
                      "in the General settings tab.");
    
    ImGui::Spacing();
    if (ImGui::Button("Open Settings"))
    {
      state->showSettingsWindow = true;
    }
    return;
  }

  // Show login status
  auto* user_info = rc_client_get_user_info(achievement_manager.GetClient());
  if (user_info)
  {
    ImGui::Text("Logged in as: %s", user_info->display_name);
    if (user_info->score > 0)
      ImGui::Text("RetroAchievements Score: %u", user_info->score);
  }
  else
  {
    ImGui::Text("Logged in to RetroAchievements");
  }
  
  ImGui::Separator();

  std::string game_id = state->propertiesGame->GetGameID();
  std::string file_path = state->propertiesGame->GetFilePath();
  
  rc_client_t* client = achievement_manager.GetClient();
  if (client && achievement_manager.IsGameLoaded())
  {
    auto* game_info = rc_client_get_game_info(client);
    if (game_info && game_info->id != 0 && rc_client_has_achievements(client))
    {
      // Get detailed achievement data for loaded game
      rc_client_achievement_list_t* achievement_list = rc_client_create_achievement_list(
          client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
          RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
          
      if (achievement_list)
      {
        int total_achievements = 0;
        int unlocked_achievements = 0;
        
        for (u32 bx = 0; bx < achievement_list->num_buckets; bx++)
        {
          auto& bucket = achievement_list->buckets[bx];
          for (u32 achievement = 0; achievement < bucket.num_achievements; achievement++)
          {
            total_achievements++;
            
            const rc_client_achievement_t* achievement_info = bucket.achievements[achievement];
            if (achievement_info && achievement_info->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED)
              unlocked_achievements++;
          }
        }
        
        if (total_achievements > 0)
        {
          int total_points = 0;
          int unlocked_points = 0;
          std::vector<const rc_client_achievement_t*> unlocked_list;
          std::vector<const rc_client_achievement_t*> locked_list;
          for (u32 bx = 0; bx < achievement_list->num_buckets; bx++)
          {
            auto& bucket = achievement_list->buckets[bx];
            for (u32 a = 0; a < bucket.num_achievements; a++)
            {
              const rc_client_achievement_t* ach = bucket.achievements[a];
              if (!ach)
                continue;
              total_points += static_cast<int>(ach->points);
              if (ach->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED)
              {
                unlocked_points += static_cast<int>(ach->points);
                unlocked_list.push_back(ach);
              }
              else
              {
                locked_list.push_back(ach);
              }
            }
          }

          const bool hardcore = rc_client_get_hardcore_enabled(client);
          ImGui::Text("%s%s", game_info->title, hardcore ? " (Hardcore Mode)" : "");
          ImGui::Text("You have unlocked %d of %d achievements, earning %d of %d possible points.",
                      unlocked_achievements, total_achievements, unlocked_points, total_points);
          float pct = total_achievements > 0 ? (static_cast<float>(unlocked_achievements) / total_achievements) : 0.0f;
          ImGui::ProgressBar(pct, ImVec2(-FLT_MIN, 0.0f));
          ImGui::Separator();

          auto draw_loaded_row = [&](const rc_client_achievement_t* achievement) {
            const bool is_unlocked = (achievement->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED);
            ImVec4 color = is_unlocked ? ImVec4(0.8f, 1.0f, 0.8f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

            // Icon
            const auto& badge = AchievementManager::GetInstance().GetAchievementBadge(achievement->id, !is_unlocked);
            static std::unordered_map<u32, std::unique_ptr<AbstractTexture>> s_badge_textures;
            AbstractTexture* icon_tex = nullptr;
            if (!badge.data.empty())
            {
              auto tex_it = s_badge_textures.find(achievement->id);
              if (tex_it == s_badge_textures.end() || !tex_it->second)
              {
                TextureConfig cfg(badge.width, badge.height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0, AbstractTextureType::Texture_2D);
                auto tex = g_gfx->CreateTexture(cfg, "RA Badge");
                if (tex)
                {
                  tex->Load(0, badge.width, badge.height, badge.width, badge.data.data(), sizeof(u8) * 4 * badge.width * badge.height);
                  icon_tex = tex.get();
                  s_badge_textures[achievement->id] = std::move(tex);
                }
              }
              else
              {
                icon_tex = tex_it->second.get();
              }
            }

            if (ImGui::BeginTable("ach_row", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX))
            {
              ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 28.0f);
              ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
              ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, 60.0f);
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              if (icon_tex)
                ImGui::Image((ImTextureID)(intptr_t)icon_tex, ImVec2(24, 24));
              ImGui::TableSetColumnIndex(1);
              ImGui::PushStyleColor(ImGuiCol_Text, is_unlocked ? IM_COL32(200,255,200,255) : IM_COL32(200,200,200,255));
              ImGui::TextUnformatted(achievement->title ? achievement->title : "");
              ImGui::PopStyleColor();
              ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170,170,170,255));
              if (achievement->description)
                ImGui::TextWrapped("%s", achievement->description);
              ImGui::PopStyleColor();
              ImGui::TableSetColumnIndex(2);
              ImGui::Text("%u pts", achievement->points);
              ImGui::EndTable();
            }
          };

          if (ImGui::CollapsingHeader(("Unlocked (" + std::to_string(unlocked_list.size()) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
          {
            if (ImGui::BeginChild("UnlockedAchievements", ImVec2(0, 220), true))
            {
              for (const auto* a : unlocked_list)
                draw_loaded_row(a);
            }
            ImGui::EndChild();
          }

          if (ImGui::CollapsingHeader(("Locked (" + std::to_string(locked_list.size()) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
          {
            if (ImGui::BeginChild("LockedAchievements", ImVec2(0, 220), true))
            {
              for (const auto* a : locked_list)
                draw_loaded_row(a);
            }
            ImGui::EndChild();
          }

          rc_client_destroy_achievement_list(achievement_list);
          return;
        }
        else
        {
          ImGui::Text("This game has no achievements available.");
        }
        
        rc_client_destroy_achievement_list(achievement_list);
      }
      else
      {
        ImGui::Text("Error loading achievement details.");
      }
    }
    else
    {
      ImGui::Text("This game has no achievements available.");
    }
  }
  else
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    
    auto it = g_game_achievement_cache.find(game_id);
    if (it == g_game_achievement_cache.end() || !it->second)
    {
      // Start loading achievement data
      g_game_achievement_cache[game_id] = std::make_unique<GameAchievementData>();
      
      std::thread(LoadAchievementDataForGame, file_path, game_id).detach();
      
      ImGui::Text("Loading achievement data...");
      DrawSpinner("##loading_achievements", 8.0f, 2.0f, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
    }
    else if (it->second->loading)
    {
      ImGui::Text("Loading achievement data...");
      DrawSpinner("##loading_achievements", 8.0f, 2.0f, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
    }
    else if (it->second->loaded)
    {
      if (it->second->hasAchievements)
      {
        ImGui::Text("Game: %s", it->second->gameTitle.c_str());
        ImGui::Text("Total Achievements: %zu", it->second->achievements.size());
        if (it->second->unlockedAchievementIds.empty())
          ImGui::Text("Note: Unlock status only shown for loaded games");
        ImGui::Separator();
        
        ImGui::Text("Achievement Details:");
        ImGui::Separator();
        
        if (ImGui::BeginChild("AchievementsList", ImVec2(0, 300), true))
        {
          for (const auto& achievement : it->second->achievements)
          {
            ImGui::PushID(achievement.id);
            
            const bool is_unlocked = it->second->unlockedAchievementIds.count(achievement.id) > 0;
            ImVec4 color = is_unlocked ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

            const auto& badge = AchievementManager::GetInstance().GetAchievementBadge(achievement.id, !is_unlocked);
            static std::unordered_map<u32, std::unique_ptr<AbstractTexture>> s_browse_badges;
            auto tex_it = s_browse_badges.find(achievement.id);
            if (tex_it == s_browse_badges.end() || !tex_it->second || badge.data.empty())
            {
              if (!badge.data.empty())
              {
                TextureConfig cfg(badge.width, badge.height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0, AbstractTextureType::Texture_2D);
                auto tex = g_gfx->CreateTexture(cfg, "RA Badge");
                if (tex)
                {
                  tex->Load(0, badge.width, badge.height, badge.width, badge.data.data(), sizeof(u8) * 4 * badge.width * badge.height);
                  s_browse_badges[achievement.id] = std::move(tex);
                }
              }
            }
            if (auto itex = s_browse_badges.find(achievement.id); itex != s_browse_badges.end())
            {
              ImGui::Image((ImTextureID)(intptr_t)itex->second.get(), ImVec2(20, 20));
              ImGui::SameLine();
            }
            else
            {
              ImGui::TextColored(color, is_unlocked ? "[✓]" : "[ ]");
              ImGui::SameLine();
            }
            
            if (ImGui::TreeNode(achievement.title))
            {
              ImGui::TextWrapped("Description: %s", achievement.description);
              ImGui::Text("Points: %u", achievement.points);
              
              if (achievement.author)
              {
                ImGui::Text("Author: %s", achievement.author);
              }
              
              // Show rarity if available
              if (achievement.rarity > 0.0f)
              {
                ImGui::Text("Rarity: %.2f%% of players", achievement.rarity);
              }
              
              // Show type
              const char* type_str = "Standard";
              switch (achievement.type)
              {
                case RC_ACHIEVEMENT_TYPE_PROGRESSION: type_str = "Progression"; break;
                case RC_ACHIEVEMENT_TYPE_WIN: type_str = "Win Condition"; break;
                case RC_ACHIEVEMENT_TYPE_MISSABLE: type_str = "Missable"; break;
              }
              ImGui::Text("Type: %s", type_str);
              
              ImGui::TreePop();
            }
            
            ImGui::PopID();
          }
        }
        ImGui::EndChild();
      }
      else
      {
        if (!it->second->errorMessage.empty())
        {
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Error: %s", it->second->errorMessage.c_str());
        }
        else
        {
          ImGui::Text("This game has no achievements available on RetroAchievements.");
        }
      }
    }
    else
    {
      if (!it->second->errorMessage.empty())
      {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Error: %s", it->second->errorMessage.c_str());
      }
      else
      {
        ImGui::Text("This game has no achievements available.");
      }
      
      if (ImGui::Button("Retry"))
      {
        g_game_achievement_cache.erase(game_id);
      }
    }
  }
  
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  
  // Links and actions
  if (ImGui::Button("Open RetroAchievements Website"))
  {
    // todo: open web browser to game's RA page
  }
  
  ImGui::SameLine();
  if (ImGui::Button("Refresh Achievement Data"))
  {
#ifdef _WIN32
    OutputDebugStringA(("Refresh Achievement Data clicked for game: " + game_id + "\n").c_str());
#endif
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    g_game_achievement_cache.erase(game_id);
  }
}
#endif  // USE_RETRO_ACHIEVEMENTS

}  // namespace ImGuiFrontend
