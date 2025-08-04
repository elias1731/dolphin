#include "ImGuiNetPlay.h"
#include "WinRTKeyboard.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <chrono>
#include <exception>
#include <chrono>
#include <thread>

#ifdef WINRT_XBOX
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/windows.graphics.display.core.h>
#include <winrt/windows.gaming.input.h>
#include <windows.applicationmodel.h>
#include <gamingdeviceinformation.h>

#include "DolphinWinRT/UWPUtils.h"

using winrt::Windows::UI::Core::CoreWindow;
using namespace winrt;
#endif

#include "Core/Core.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/SyncIdentifier.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/NetPlayClient.h"
#include "Core/NetPlayServer.h"
#include "Core/System.h"

#include "Common/WindowSystemInfo.h"
#include "Common/HttpRequest.h"
#include "Common/Version.h"

#include "UICommon/GameFile.h"
#include "UICommon/UICommon.h"
#include "UICommon/NetPlayIndex.h"

#include "VideoCommon/NetPlayChatUI.h"
#include "VideoCommon/OnScreenDisplay.h"

// Forward declarations
namespace NetPlay { class NetPlayClient; class NetPlayServer; }
namespace ImGuiFrontend { class ImGuiNetPlay; }

// Global declarations
extern std::shared_ptr<NetPlay::NetPlayClient> g_netplay_client;
extern std::shared_ptr<NetPlay::NetPlayServer> g_netplay_server;
extern std::shared_ptr<ImGuiFrontend::ImGuiNetPlay> g_netplay_dialog;

namespace ImGuiFrontend
{
NetPlayDrawResult result = NetPlayDrawResult::Continue;

NetPlay::SyncIdentifier m_current_game_identifier;
std::shared_ptr<const UICommon::GameFile> m_host_selected_game = nullptr;
bool m_traversal = false;
char m_nick_buf[32];
char m_host_buf[32];
char m_search_buf[32];
std::string m_warning_text;
bool m_prompt_warning = false;

std::string m_prev_search;
std::vector<std::shared_ptr<UICommon::GameFile>> m_search_results;
Common::Lazy<std::string> m_external_ip;

ImGuiNetPlay::ImGuiNetPlay(ImGuiFrontend* frontend, std::vector<std::shared_ptr<UICommon::GameFile>> games, float frame_scale)
    : m_frontend(frontend), m_games(games), m_frameScale(frame_scale)
{
  std::string nickname = Config::Get(Config::NETPLAY_NICKNAME);
  strcpy(m_nick_buf, nickname.data());

  std::string type = Config::Get(Config::NETPLAY_TRAVERSAL_CHOICE);
  std::string address = Config::Get(type == "traversal" ? Config::NETPLAY_HOST_CODE :
                                        Config::NETPLAY_ADDRESS);
  strcpy(m_host_buf, address.data());
  
  // Load settings
  m_strict_settings_sync = Config::Get(Config::NETPLAY_STRICT_SETTINGS_SYNC);
  m_sync_codes = Config::Get(Config::NETPLAY_SYNC_CODES);
  m_record_input = Config::Get(Config::NETPLAY_RECORD_INPUTS);
  m_enable_chunked_upload_limit = Config::Get(Config::NETPLAY_ENABLE_CHUNKED_UPLOAD_LIMIT);
  m_chunked_upload_limit = Config::Get(Config::NETPLAY_CHUNKED_UPLOAD_LIMIT);
  // Note: Some settings may not be available in this version
  m_sync_all_wii_saves = false; // Default value
  m_golf_mode_overlay = false; // Default value  
  m_hide_remote_gbas = false; // Default value
  
  // Set network mode based on config
  const std::string network_mode = Config::Get(Config::NETPLAY_NETWORK_MODE);
  if (network_mode == "fixeddelay")
    m_network_mode = 0;
  else if (network_mode == "hostinputauthority") 
    m_network_mode = 1;
  else if (network_mode == "golf")
    m_network_mode = 2;
    
  // Set save data mode - default to Load Only
  m_save_data_mode = 1;
  
  // Initialize browser status
  m_browser_status = "Ready - Click 'Refresh Now' to search for servers";
  
  // Start server browser refresh thread
  m_refresh_run = true;
  m_refresh_thread = std::thread([this] { RefreshLoop(); });
  
  // Initialize browser status
  m_browser_status = "Ready to search";
}

ImGuiNetPlay::~ImGuiNetPlay()
{
  // Clean up refresh thread
  m_refresh_run = false;
  m_refresh_event.Set();
  if (m_refresh_thread.joinable())
    m_refresh_thread.join();
}

void ImGuiNetPlay::DrawLobbyWindow()
{
  ImGui::SetNextWindowSize(ImVec2(740 * m_frameScale, 525 * m_frameScale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (740 / 2) * m_frameScale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (525 / 2) * m_frameScale));

  if (ImGui::Begin("NetPlay Lobby", nullptr, ImGuiWindowFlags_MenuBar))
  {
    // Menu bar
    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu("Data"))
      {
        const char* save_data_items[] = {"No Save Data", "Load Host's Save Data Only", "Load and Write Host's Save Data"};
        if (ImGui::Combo("Save Data Sync", &m_save_data_mode, save_data_items, 3))
        {
          // Config::SetBaseOrCurrent(Config::NETPLAY_SAVE_DATA_SYNCING_MODE, 
          //                          static_cast<Config::NetplaySaveDataSyncingMode>(m_save_data_mode));
          // Config::Save();
        }
        
        ImGui::Separator();
        
        if (ImGui::Checkbox("Use All Wii Save Data", &m_sync_all_wii_saves))
        {
          // Note: This setting may not be available in this version
          // Config::SetBaseOrCurrent(Config::NETPLAY_SYNC_ALL_WII_SAVES, m_sync_all_wii_saves);
          // Config::Save();
        }
        
        ImGui::Separator();
        
        if (ImGui::Checkbox("Sync AR/Gecko Codes", &m_sync_codes))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_SYNC_CODES, m_sync_codes);
          Config::Save();
        }
        
        if (ImGui::Checkbox("Strict Settings Sync", &m_strict_settings_sync))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_STRICT_SETTINGS_SYNC, m_strict_settings_sync);
          Config::Save();
        }
        
        ImGui::EndMenu();
      }
      
      if (ImGui::BeginMenu("Network"))
      {
        const char* network_modes[] = {"Fair Input Delay", "Host Input Authority", "Golf Mode"};
        if (ImGui::Combo("Network Mode", &m_network_mode, network_modes, 3))
        {
          const char* mode_strings[] = {"fixeddelay", "hostinputauthority", "golf"};
          Config::SetBaseOrCurrent(Config::NETPLAY_NETWORK_MODE, std::string(mode_strings[m_network_mode]));
          Config::Save();
          
          // Note: SetNetworkMode may not be available in this version
          // if (g_netplay_server)
          // {
          //   g_netplay_server->SetNetworkMode(static_cast<NetPlay::NetworkMode>(m_network_mode));
          // }
        }
        
        ImGui::EndMenu();
      }
      
      if (ImGui::BeginMenu("Checksum"))
      {
        if (ImGui::MenuItem("Current Game"))
        {
          if (g_netplay_server)
            g_netplay_server->ComputeGameDigest(m_current_game_identifier);
        }
        
        if (ImGui::MenuItem("SD Card"))
        {
          if (g_netplay_server)
            g_netplay_server->ComputeGameDigest(NetPlay::NetPlayClient::GetSDCardIdentifier());
        }
        
        ImGui::EndMenu();
      }
      
      if (ImGui::BeginMenu("Other"))
      {
        if (ImGui::Checkbox("Record Inputs", &m_record_input))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_RECORD_INPUTS, m_record_input);
          Config::Save();
        }
        
        if (ImGui::Checkbox("Show Golf Mode Overlay", &m_golf_mode_overlay))
        {
          // Note: This setting may not be available in this version
          // Config::SetBaseOrCurrent(Config::NETPLAY_GOLF_MODE_OVERLAY, m_golf_mode_overlay);
          // Config::Save();
        }
        
        if (ImGui::Checkbox("Hide Remote GBAs", &m_hide_remote_gbas))
        {
          // Note: This setting may not be available in this version  
          // Config::SetBaseOrCurrent(Config::NETPLAY_HIDE_REMOTE_GBAS, m_hide_remote_gbas);
          // Config::Save();
        }
        
        ImGui::EndMenu();
      }
      
      ImGui::EndMenuBar();
    }

    // Connection info
    if (g_netplay_server != nullptr)
    {
      std::string address;
      if (m_traversal)
      {
        const auto host_id = Common::g_TraversalClient->GetHostID();
        address = Common::g_TraversalClient->IsConnected() ?
                      std::string(host_id.begin(), host_id.end()) :
                      "Connecting..";
      }
      else
      {
        if (!m_external_ip->empty())
        {
          address = m_external_ip->c_str();
        }
        else
        {
          address = "Unknown";
        }
      }

      ImGui::Text(m_traversal ? "Lobby Code: %s" : "External IP: %s", address.c_str());
      
      // Copy button for connection info
      ImGui::SameLine();
      if (ImGui::Button("Copy"))
      {
        ImGui::SetClipboardText(address.c_str());
      }
    }

    auto game = g_netplay_dialog->FindGameFile(m_current_game_identifier);
    if (game)
    {
      ImGui::Text("Selected Game: %s", game->GetName(m_frontend->m_title_database).c_str());
    }

    ImGui::Spacing();
    
    // Create a two-column layout
    if (ImGui::BeginTable("lobby_layout", 2, ImGuiTableFlags_Resizable))
    {
      ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 300 * m_frameScale);
      ImGui::TableSetupColumn("Chat", ImGuiTableColumnFlags_WidthStretch);
      
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      
      // Players section
      ImGui::Text("Players:");
      DrawLobbyMenu();
      
      ImGui::TableNextColumn();
      
      // Chat section
      DrawChatSection();
      
      ImGui::EndTable();
    }

    if (g_netplay_server)
    {
      ImGui::Spacing();
      ImGui::Separator();

      if (ImGui::Button("Start Game"))
      {
        if (g_netplay_server->RequestStartGame())
        {
          ImGui::End();
          return;
        }
      }

      ImGui::SameLine();

      if (ImGui::Button("Exit Lobby"))
      {
        g_netplay_client->Stop();
        g_netplay_client = nullptr;
        g_netplay_server = nullptr;
      }
    }
    else
    {
      ImGui::Spacing();
      ImGui::Separator();
      
      if (ImGui::Button("Disconnect"))
      {
        g_netplay_client->Stop();
        g_netplay_client = nullptr;
      }
    }

    if (m_prompt_warning)
    {
      m_prompt_warning = false;
      ImGui::OpenPopup("Warning");
    }

    if (ImGui::BeginPopupModal("Warning"))
    {
      ImGui::Text(m_warning_text.c_str());
      ImGui::Separator();
      if (ImGui::Button("OK"))
      {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::Separator();
    ImGui::TextWrapped("Please note that Xbox NetPlay is still early and may not show all notifications for various things such as synchronising memory cards, and waiting for client's games to start (e.g Compile Shaders Before Starting).\nThis may appear as a black screen until the game boots.");

    ImGui::End();
  }
}

void ImGuiNetPlay::DrawSetup()
{
  constexpr auto Warning = [](const char* text) {
    m_warning_text = std::string(text);
    ImGui::OpenPopup("Warning");
  };

  ImGui::SetNextWindowSize(ImVec2(640 * m_frameScale, 525 * m_frameScale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (640 / 2) * m_frameScale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (525 / 2) * m_frameScale));

  if (ImGui::Begin("NetPlay Setup", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
  {
    std::string type = Config::Get(Config::NETPLAY_TRAVERSAL_CHOICE);
    m_traversal = type == "traversal";

    if (ImGui::BeginCombo("Connection Type", type.c_str()))
    {
      if (ImGui::Selectable("Direct Connection", type == "direct"))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_CHOICE, "direct");
        Config::Save();
      }

      if (ImGui::Selectable("Traversal Server", type == "traversal"))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_CHOICE, "traversal");
        Config::Save();
      }

      ImGui::EndCombo();
    }

    if (ImGui::Button("Edit Nickname"))
    {
      UWP::ShowKeyboard();
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SameLine();
    if (ImGui::InputText("##nickname", m_nick_buf, 32))
    {
      printf(m_nick_buf);
    }

    ImGui::Spacing();
    
    // Add server browser button
    if (ImGui::Button("Server Browser"))
    {
      m_show_browser = true;
      m_browser_status = "Searching for servers...";
      RefreshServerList(); // Trigger initial refresh
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Settings"))
    {
      m_show_settings = true;
    }

    ImGui::Spacing();

    ImGui::BeginTabBar("#connectionTabs");
    if (ImGui::BeginTabItem("Connect"))
    {
      if (ImGui::Button(m_traversal ? "Set Host Code " : "Set Host IP"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      ImGui::InputText("##address", m_host_buf, 32);

      // Port setting for direct connections
      if (!m_traversal)
      {
        int port = static_cast<int>(Config::Get(Config::NETPLAY_CONNECT_PORT));
        if (ImGui::InputInt("Port", &port))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_CONNECT_PORT, static_cast<u16>(port));
          Config::Save();
        }
      }

      if (ImGui::Button("Connect"))
      {
        size_t nick_len = strlen(m_nick_buf);
        size_t host_len = strlen(m_host_buf);
        if (nick_len == 0)
        {
          Warning("Please enter a valid nickname!");
        }
        else if (host_len == 0)
        {
          Warning("Please enter a valid IP address / host code!");
        }
        else
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_NICKNAME, std::string(m_nick_buf));
          Config::Save();

          Config::SetBaseOrCurrent(m_traversal ? Config::NETPLAY_HOST_CODE :
                                                 Config::NETPLAY_ADDRESS,
                                   std::string(m_host_buf));
          Config::Save();

          std::string host_ip = m_traversal ? Config::Get(Config::NETPLAY_HOST_CODE) :
                                              Config::Get(Config::NETPLAY_ADDRESS);
          u16 host_port = Config::Get(Config::NETPLAY_CONNECT_PORT);

          const std::string traversal_host = Config::Get(Config::NETPLAY_TRAVERSAL_SERVER);
          const u16 traversal_port = Config::Get(Config::NETPLAY_TRAVERSAL_PORT);
          const std::string nickname = Config::Get(Config::NETPLAY_NICKNAME);

          g_netplay_client = std::make_shared<NetPlay::NetPlayClient>(
              host_ip, host_port, this, nickname,
              NetPlay::NetTraversalConfig{m_traversal, traversal_host, traversal_port});
        }
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextWrapped("ALERT:\n\n"
         "All players must use the same Dolphin version.\n"
         "If enabled, SD cards must be identical between players.\n"
         "If DSP LLE is used, DSP ROMs must be identical between players.\n"
         "If a game is hanging on boot, it may not support Dual Core Netplay. Disable Dual Core.\n"
         "If connecting directly, the host must have the chosen UDP port open/forwarded!\n\n"
         "Wii Remote support in netplay is experimental and may not work correctly.\n"
         "Use at your own risk.");

      if (ImGui::BeginPopupModal("Warning"))
      {
        ImGui::Text(m_warning_text.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK"))
        {
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Host"))
    {
      if (ImGui::BeginListBox("Games List", ImVec2(-1, 200 * m_frameScale)))
      {
        size_t search = strlen(m_search_buf);
        std::vector<std::shared_ptr<UICommon::GameFile>> games;
        if (search > 0)
        {
          std::string search_phrase = std::string(m_search_buf);
          if (search_phrase != m_prev_search)
          {
            m_search_results.clear();
            for (auto& game : m_games)
            {
              std::string name = game->GetLongName();
              if (name == "")
                name = game->GetFileName();

              auto it = std::search(name.begin(), name.end(), search_phrase.begin(),
                                    search_phrase.end(),
                                    [](unsigned char ch1, unsigned char ch2) {
                                      return std::toupper(ch1) == std::toupper(ch2);
                                    });

              if (it != name.end())
              {
                m_search_results.push_back(game);
              }
            }

            m_prev_search = m_search_buf;
          }

          games = m_search_results;
        }
        else
        {
          games = m_games;
        }

        for (auto& game : games)
        {
          std::string name = game->GetLongName();
          if (name == "")
            name = game->GetFileName();

          if (ImGui::Selectable(
                  (name + "##" + game->GetFilePath()).c_str(),
                                m_host_selected_game == game))
          {
            m_host_selected_game = game;
          }
        }

        ImGui::EndListBox();
      }

      if (ImGui::Button("Search Game"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      ImGui::InputText("##gamesearch", m_search_buf, 32);

      ImGui::Spacing();

      bool strict_sync = Config::Get(Config::NETPLAY_STRICT_SETTINGS_SYNC);
      if (ImGui::Checkbox("Strict Settings Synchronisation", &strict_sync))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_STRICT_SETTINGS_SYNC, strict_sync);
        Config::Save();
      }

      bool upnp = Config::Get(Config::NETPLAY_USE_UPNP);
      if (ImGui::Checkbox("Use UPNP", &upnp))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_USE_UPNP, upnp);
        Config::Save();
      }

      if (!m_traversal)
      {
        int port = static_cast<int>(Config::Get(Config::NETPLAY_HOST_PORT));
        if (ImGui::InputInt("Host Port", &port))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_HOST_PORT, static_cast<u16>(port));
          Config::Save();
        }
      }
      else
      {
        int listen_port = static_cast<int>(Config::Get(Config::NETPLAY_LISTEN_PORT));
        if (ImGui::InputInt("Listen Port", &listen_port))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_LISTEN_PORT, static_cast<u16>(listen_port));
          Config::Save();
        }
      }
      
      // Server browser settings
      bool use_index = Config::Get(Config::NETPLAY_USE_INDEX);
      if (ImGui::Checkbox("Show in Server Browser", &use_index))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_USE_INDEX, use_index);
        Config::Save();
      }
      
      if (use_index)
      {
        std::string server_name = Config::Get(Config::NETPLAY_INDEX_NAME);
        char server_name_buf[64];
        strncpy(server_name_buf, server_name.c_str(), sizeof(server_name_buf));
        if (ImGui::InputText("Server Name", server_name_buf, sizeof(server_name_buf)))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_INDEX_NAME, std::string(server_name_buf));
          Config::Save();
        }
        
        std::string server_password = Config::Get(Config::NETPLAY_INDEX_PASSWORD);
        char server_password_buf[64];
        strncpy(server_password_buf, server_password.c_str(), sizeof(server_password_buf));
        if (ImGui::InputText("Password (optional)", server_password_buf, sizeof(server_password_buf), ImGuiInputTextFlags_Password))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_INDEX_PASSWORD, std::string(server_password_buf));
          Config::Save();
        }
      }
      
      // Chunked upload limit
      if (ImGui::Checkbox("Limit Chunked Upload Speed", &m_enable_chunked_upload_limit))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_ENABLE_CHUNKED_UPLOAD_LIMIT, m_enable_chunked_upload_limit);
        Config::Save();
      }
      
      if (m_enable_chunked_upload_limit)
      {
        if (ImGui::InputInt("Upload Limit (kbps)", &m_chunked_upload_limit, 100, 1000))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_CHUNKED_UPLOAD_LIMIT, static_cast<u32>(m_chunked_upload_limit));
          Config::Save();
        }
      }

      if (ImGui::Button("Host Lobby"))
      {
        size_t nick_len = strlen(m_nick_buf);
        if (nick_len == 0)
        {
          Warning("Please enter a valid nickname!");
        }
        else if (m_host_selected_game == nullptr)
        {
          Warning("Please select a game!");
        }
        else
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_NICKNAME, std::string(m_nick_buf));
          Config::Save();

          // Settings
          u16 host_port = Config::Get(Config::NETPLAY_HOST_PORT);
          const std::string traversal_choice = Config::Get(Config::NETPLAY_TRAVERSAL_CHOICE);
          const bool is_traversal = traversal_choice == "traversal";
          const bool use_upnp = Config::Get(Config::NETPLAY_USE_UPNP);

          const std::string traversal_host = Config::Get(Config::NETPLAY_TRAVERSAL_SERVER);
          const u16 traversal_port = Config::Get(Config::NETPLAY_TRAVERSAL_PORT);

          if (is_traversal)
            host_port = Config::Get(Config::NETPLAY_LISTEN_PORT);

          // Create Server
          g_netplay_server = std::make_shared<NetPlay::NetPlayServer>(
              host_port, use_upnp, this,
              NetPlay::NetTraversalConfig{is_traversal, traversal_host, traversal_port});

          if (!g_netplay_server->is_connected)
          {
            Warning("Could not create the netplay server. Is the port already in use?");
            g_netplay_server = nullptr;
          }
          else
          {
            g_netplay_server->ChangeGame(m_host_selected_game->GetSyncIdentifier(),
                m_host_selected_game->GetNetPlayName(m_frontend->m_title_database));

            std::string host_ip = "127.0.0.1";

            const std::string nickname = Config::Get(Config::NETPLAY_NICKNAME);
            const std::string network_mode = Config::Get(Config::NETPLAY_NETWORK_MODE);

            g_netplay_client = std::make_shared<NetPlay::NetPlayClient>(
                host_ip, host_port, this, nickname,
                NetPlay::NetTraversalConfig{false, traversal_host, traversal_port});

            m_external_ip = Common::Lazy<std::string>([]() -> std::string {
              Common::HttpRequest request;
              // ENet does not support IPv6, so IPv4 has to be used
              request.UseIPv4();
              Common::HttpRequest::Response response =
                  request.Get("https://ip.dolphin-emu.org/", {{"X-Is-Dolphin", "1"}});

              if (response.has_value())
                return std::string(response->begin(), response->end());
              return "";
            });
          }
        }
      }

      if (ImGui::BeginPopupModal("Warning"))
      {
        ImGui::Text(m_warning_text.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK"))
        {
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }

      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
  }
}

NetPlayDrawResult ImGuiNetPlay::Draw()
{
  if (g_netplay_client)
  {
    DrawLobbyWindow();
  }
  else
  {
    DrawSetup();
  }
  
  // Draw browser and settings windows when needed
  if (m_show_browser)
  {
    DrawBrowser();
  }
  
  if (m_show_settings)
  {
    DrawSettings();
  }

  return result;
}

void ImGuiNetPlay::BootGame(const std::string& filename,
                            std::unique_ptr<BootSessionData> boot_session_data) {
// Todo, make the host handle this.
#ifdef WINRT_XBOX
// Visual Studio / Microsoft Programming moment
#pragma warning(push)
#pragma warning(disable : 4265)
  CoreApplication::MainView().Dispatcher().RunAsync(
      winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
      [filename, data = std::move(boot_session_data)] {
        CoreWindow window = CoreWindow::GetForCurrentThread();
        void* abi = winrt::get_abi(window);

        WindowSystemInfo wsi;
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
            wsi.render_width = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();
            wsi.render_height = hdi.GetCurrentDisplayMode().ResolutionHeightInRawPixels();
            // Our UI is based on 1080p, and we're adding a modifier to zoom in by 80%
            wsi.render_surface_scale = ((float)wsi.render_width / 1920.0f) * 1.8f;
          }
        }

        std::unique_ptr<BootParameters> boot =
            BootParameters::GenerateFromFile(filename, std::move(*data));
        auto& system = Core::System::GetInstance();

        if (!BootManager::BootCore(system, std::move(boot), wsi))
        {
          fprintf(stderr, "Could not boot the specified file\n");
        }
    });
#pragma warning(pop)
#endif

  result = NetPlayDrawResult::BootGame;
};

void ImGuiNetPlay::DisplayMessage(std::string msg, int duration, float r, float g, float b)
{
  if (Config::Get(Config::GFX_SHOW_NETPLAY_MESSAGES) && Core::IsRunning(Core::System::GetInstance()))
    g_netplay_chat_ui->AppendChat(msg, {r, g, b});
}

void ImGuiNetPlay::Reset()
{
  result = NetPlayDrawResult::Continue;
}

void ImGuiNetPlay::StopGame()
{
  g_netplay_client->StopGame();
}

bool ImGuiNetPlay::IsHosting() const
{
  return g_netplay_server != nullptr;
}

void ImGuiNetPlay::Update()
{
}

void ImGuiNetPlay::AppendChat(const std::string& msg)
{
  m_chat_messages.push_back(msg);
  
  // Limit chat history to prevent memory issues
  if (m_chat_messages.size() > 100)
  {
    m_chat_messages.erase(m_chat_messages.begin());
  }
  
  DisplayMessage(msg, OSD::Duration::NORMAL, 1.0f, 1.0f, 0.0f);
}

void ImGuiNetPlay::OnMsgChangeGame(const NetPlay::SyncIdentifier& sync_identifier,
                                   const std::string& netplay_name)
{
  m_current_game_identifier = sync_identifier;
}

void ImGuiNetPlay::OnMsgChangeGBARom(int pad, const NetPlay::GBAConfig& config)
{
}

void ImGuiNetPlay::OnMsgStartGame()
{
  g_netplay_chat_ui =
      std::make_unique<NetPlayChatUI>([this](const std::string& message) {});

  auto game = FindGameFile(m_current_game_identifier);
  if (game)
  {
    g_netplay_client->StartGame(game->GetFilePath());
  }
  else
  {
    m_warning_text = "Could not find the selected game or the game dump is too different.";
    m_prompt_warning = true;
  }
}

void ImGuiNetPlay::OnMsgStopGame()
{
  g_netplay_client->StopGame();
}

void ImGuiNetPlay::OnMsgPowerButton()
{

}

void ImGuiNetPlay::OnPlayerConnect(const std::string& player)
{
  DisplayMessage("Player " + player + " has connected", OSD::Duration::NORMAL, 1.0f,
                 1.0f, 0.0f);
}

void ImGuiNetPlay::OnPlayerDisconnect(const std::string& player)
{
  DisplayMessage("Player " + player + " has disconnected", OSD::Duration::NORMAL, 1.0f,
    1.0f, 0.0f);
}

void ImGuiNetPlay::OnPadBufferChanged(u32 buffer)
{
}

void ImGuiNetPlay::OnHostInputAuthorityChanged(bool enabled)
{
}

void ImGuiNetPlay::OnDesync(u32 frame, const std::string& player)
{
  DisplayMessage("Possible desync detected.", OSD::Duration::VERY_LONG, 1.0f, 0.0f, 0.0f);
}

void ImGuiNetPlay::OnConnectionLost()
{
}

void ImGuiNetPlay::OnConnectionError(const std::string& message)
{
  m_warning_text = std::string(message);
  m_prompt_warning = true;

  g_netplay_client = nullptr;
}

void ImGuiNetPlay::OnTraversalError(Common::TraversalClient::FailureReason error)
{
  DisplayMessage("A traversal error has occurred", OSD::Duration::VERY_LONG, 1.0f, 0.0f, 0.0f);
}

void ImGuiNetPlay::OnTraversalStateChanged(Common::TraversalClient::State state)
{

}

void ImGuiNetPlay::OnGameStartAborted()
{
  if (Core::IsRunningOrStarting(Core::System::GetInstance()))
  {
#ifdef WINRT_XBOX
    // todo make the host manage this
    UWP::g_shutdown_requested.Set();
#endif
  }
}

void ImGuiNetPlay::OnGolferChanged(bool is_golfer,
                                   const std::string& golfer_name)
{
}

bool ImGuiNetPlay::IsRecording()
{
  return false;
}

std::shared_ptr<const UICommon::GameFile>
ImGuiNetPlay::FindGameFile(const NetPlay::SyncIdentifier& sync_identifier,
             NetPlay::SyncIdentifierComparison* found)
{
  NetPlay::SyncIdentifierComparison temp;
  if (!found)
    found = &temp;

  *found = NetPlay::SyncIdentifierComparison::DifferentGame;

  for (auto& game : m_games)
  {
    *found = std::min(*found, game->CompareSyncIdentifier(sync_identifier));
    if (*found == NetPlay::SyncIdentifierComparison::SameGame)
      return game;
  }
  
  return nullptr;
}

std::string ImGuiNetPlay::FindGBARomPath(const std::array<u8, 20>& hash, std::string_view title,
                           int device_number)
{
  return "";
}

void ImGuiNetPlay::ShowGameDigestDialog(const std::string& title)
{
}

void ImGuiNetPlay::SetGameDigestProgress(int pid, int progress)
{
}

void ImGuiNetPlay::SetGameDigestResult(int pid, const std::string& r)
{
}

void ImGuiNetPlay::AbortGameDigest()
{
}

void ImGuiNetPlay::OnIndexAdded(bool success, std::string error)
{
}

void ImGuiNetPlay::OnIndexRefreshFailed(std::string error)
{
}

void ImGuiNetPlay::ShowChunkedProgressDialog(const std::string& title, u64 data_size,
                                             const std::vector<int>& players)
{
}

void ImGuiNetPlay::HideChunkedProgressDialog()
{
}

void ImGuiNetPlay::SetChunkedProgress(int pid, u64 progress)
{
}

void ImGuiNetPlay::OnTtlDetermined(u8 ttl)
{

}

void ImGuiNetPlay::SetHostWiiSyncData(std::vector<u64> titles, std::string redirect_folder)
{
  if (g_netplay_client)
    g_netplay_client->SetWiiSyncData(nullptr, std::move(titles), std::move(redirect_folder));
}

void DrawLobbyMenu()
{
  if (g_netplay_client == nullptr)
    return;

  auto players = g_netplay_client->GetPlayers();
  
  if (ImGui::BeginTable("players", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
  {
    ImGui::TableSetupColumn("Player");
    ImGui::TableSetupColumn("Latency");
    ImGui::TableSetupColumn("Status");
    ImGui::TableSetupColumn("Actions");
    ImGui::TableHeadersRow();
    
    for (auto& player : players)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%s", player->name.c_str());
      
      ImGui::TableNextColumn();
      ImGui::Text("%d ms", player->ping);
      
      ImGui::TableNextColumn();
      // Show player status (Ready, Not Ready, etc.)
      ImGui::Text("Ready"); // TODO: Get actual player status
      
      ImGui::TableNextColumn();
      if (g_netplay_server && player->pid != g_netplay_client->GetLocalPlayerId())
      {
        if (ImGui::Button(("Kick##" + std::to_string(player->pid)).c_str()))
        {
          g_netplay_server->KickPlayer(player->pid);
        }
      }
    }

    ImGui::EndTable();
  }

  if (g_netplay_server != nullptr)
  {
    ImGui::Spacing();

    int pad_buffer = Config::Get(Config::NETPLAY_BUFFER_SIZE);
    if (ImGui::InputInt("Input Buffer Size", &pad_buffer, 1, 5))
    {
      pad_buffer = std::max(0, std::min(20, pad_buffer)); // Clamp between 0-20
      Config::SetBaseOrCurrent(Config::NETPLAY_BUFFER_SIZE, pad_buffer);
      Config::Save();
      g_netplay_server->AdjustPadBufferSize(static_cast<unsigned int>(pad_buffer));
    }
    
    ImGui::Spacing();
    ImGui::Text("Controller Assignments:");

    // GameCube controller assignments
    if (ImGui::BeginTable("gc-slots", 4, ImGuiTableFlags_Borders))
    {
      ImGui::TableSetupColumn("GC Port 1", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableSetupColumn("GC Port 2", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableSetupColumn("GC Port 3", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableSetupColumn("GC Port 4", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableHeadersRow();

      ImGui::TableNextRow();

      auto gc_mapping = g_netplay_server->GetPadMapping();
      for (uint32_t port = 0; port < 4; port++)
      {
        ImGui::TableNextColumn();
        std::string selected_player = "None";
        for (auto player : players)
        {
          if (gc_mapping[port] == player->pid)
          {
            selected_player = player->name;
            break;
          }
        }

        if (ImGui::BeginCombo(("##gcport" + std::to_string(port)).c_str(), selected_player.c_str()))
        {
          for (auto& player : players)
          {
            if (ImGui::Selectable(player->name.c_str(), gc_mapping[port] == player->pid))
            {
              gc_mapping[port] = player->pid;
              g_netplay_server->SetPadMapping(gc_mapping);
            }
          }

          if (ImGui::Selectable("None", gc_mapping[port] == 0))
          {
            gc_mapping[port] = 0;
            g_netplay_server->SetPadMapping(gc_mapping);
          }

          ImGui::EndCombo();
        }
      }

      ImGui::EndTable();
    }

    ImGui::Spacing();

    // Wii Remote assignments
    if (ImGui::BeginTable("wii-slots", 4, ImGuiTableFlags_Borders))
    {
      ImGui::TableSetupColumn("Wii Remote 1", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableSetupColumn("Wii Remote 2", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableSetupColumn("Wii Remote 3", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableSetupColumn("Wii Remote 4", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableHeadersRow();

      ImGui::TableNextRow();

      auto wii_mapping = g_netplay_server->GetWiimoteMapping();
      for (uint32_t port = 0; port < 4; port++)
      {
        ImGui::TableNextColumn();
        std::string selected_player = "None";
        for (auto player : players)
        {
          if (wii_mapping[port] == player->pid)
          {
            selected_player = player->name;
            break;
          }
        }

        if (ImGui::BeginCombo(("##wiiport" + std::to_string(port)).c_str(), selected_player.c_str()))
        {
          for (auto& player : players)
          {
            if (ImGui::Selectable(player->name.c_str(), wii_mapping[port] == player->pid))
            {
              wii_mapping[port] = player->pid;
              g_netplay_server->SetWiimoteMapping(wii_mapping);
            }
          }

          if (ImGui::Selectable("None", wii_mapping[port] == 0))
          {
            wii_mapping[port] = 0;
            g_netplay_server->SetWiimoteMapping(wii_mapping);
          }

          ImGui::EndCombo();
        }
      }

      ImGui::EndTable();
    }

    ImGui::Spacing();
    
    // Additional host controls
    if (ImGui::Button("Assign Controller Ports"))
    {
      // TODO: Open controller port assignment dialog
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Configure Pads"))
    {
      // TODO: Open pad configuration dialog
    }
  }
}

void ImGuiNetPlay::DrawChatSection()
{
  ImGui::Text("Chat:");
  
  // Chat messages display
  if (ImGui::BeginChild("ChatMessages", ImVec2(-1, 150.0f * m_frameScale), true))
  {
    for (const auto& message : m_chat_messages)
    {
      ImGui::TextWrapped("%s", message.c_str());
    }
    
    // Auto-scroll to bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
      ImGui::SetScrollHereY(1.0f);
    
    ImGui::EndChild();
  }
  
  // Chat input
  if (ImGui::InputText("##chatinput", m_chat_input_buf, sizeof(m_chat_input_buf), ImGuiInputTextFlags_EnterReturnsTrue))
  {
    if (strlen(m_chat_input_buf) > 0 && g_netplay_client)
    {
      g_netplay_client->SendChatMessage(std::string(m_chat_input_buf));
      m_chat_input_buf[0] = '\0'; // Clear input
    }
  }
  
  ImGui::SameLine();
  if (ImGui::Button("Send") && strlen(m_chat_input_buf) > 0 && g_netplay_client)
  {
    g_netplay_client->SendChatMessage(std::string(m_chat_input_buf));
    m_chat_input_buf[0] = '\0'; // Clear input
  }
}

void ImGuiNetPlay::DrawBrowser()
{
  ImGui::SetNextWindowSize(ImVec2(700 * m_frameScale, 450 * m_frameScale));
  
  if (ImGui::Begin("NetPlay Session Browser", &m_show_browser))
  {
    // Filters
    if (ImGui::CollapsingHeader("Filters"))
    {
      bool needs_refresh = false;
      
      // Region filter
      ImGui::Text("Region:");
      ImGui::SameLine();
      
      static std::vector<std::pair<std::string, std::string>> regions;
      static std::vector<const char*> region_names;
      static bool regions_initialized = false;
      
      if (!regions_initialized)
      {
        regions = NetPlayIndex::GetRegions();
        region_names.push_back("Any Region");
        for (const auto& region : regions)
        {
          region_names.push_back(region.second.c_str());
        }
        regions_initialized = true;
      }
      
      static int selected_region = 0;
      if (ImGui::Combo("##region", &selected_region, region_names.data(), static_cast<int>(region_names.size())))
      {
        if (selected_region == 0)
          m_server_region = "";
        else
          m_server_region = regions[selected_region - 1].first;
        needs_refresh = true;
      }
      
      // Name filter
      ImGui::Text("Name:");
      ImGui::SameLine();
      char name_filter_buf[128];
      strncpy(name_filter_buf, m_server_name_filter.c_str(), sizeof(name_filter_buf));
      if (ImGui::InputText("##namefilter", name_filter_buf, sizeof(name_filter_buf)))
      {
        m_server_name_filter = std::string(name_filter_buf);
        needs_refresh = true;
      }
      
      // Game ID filter
      ImGui::Text("Game ID:");
      ImGui::SameLine();
      char game_filter_buf[128];
      strncpy(game_filter_buf, m_server_game_filter.c_str(), sizeof(game_filter_buf));
      if (ImGui::InputText("##gamefilter", game_filter_buf, sizeof(game_filter_buf)))
      {
        m_server_game_filter = std::string(game_filter_buf);
        needs_refresh = true;
      }
      
      // Server type filter
      const char* server_types[] = {"Private and Public", "Public", "Private"};
      if (ImGui::Combo("Server Type", &m_server_type_filter, server_types, 3))
      {
        needs_refresh = true;
      }
      
      // Additional filters
      if (ImGui::Checkbox("Hide Incompatible Sessions", &m_hide_incompatible))
      {
        needs_refresh = true;
      }
      
      if (ImGui::Checkbox("Hide In-Game Sessions", &m_hide_ingame))
      {
        needs_refresh = true;
      }
      
      if (needs_refresh)
      {
        RefreshServerList();
      }
    }
    
    // Status line with more prominence
    ImGui::Separator();
    ImGui::Text("Status: %s", m_browser_status.c_str());
    
    // Quick refresh button right in the status area
    ImGui::SameLine();
    if (ImGui::Button("Refresh Now"))
    {
      // Direct synchronous refresh for immediate testing
      m_browser_status = "Direct refresh in progress...";
      
      try 
      {
        NetPlayIndex client;
        
        // Build filters
        std::map<std::string, std::string> filters;
        if (m_hide_incompatible)
          filters["version"] = Common::GetScmDescStr();
        if (!m_server_name_filter.empty())
          filters["name"] = m_server_name_filter;
        if (!m_server_game_filter.empty())
          filters["game"] = m_server_game_filter;
        if (!m_server_region.empty())
          filters["region"] = m_server_region;
        if (m_server_type_filter == 1)
          filters["password"] = "0";
        else if (m_server_type_filter == 2)
          filters["password"] = "1";
        if (m_hide_ingame)
          filters["in_game"] = "0";
        
        auto entries = client.List(filters);
        
        if (entries)
        {
          m_sessions = std::move(*entries);
          const int session_count = static_cast<int>(m_sessions.size());
          m_browser_status = "Direct: " + std::to_string(session_count) + " sessions found";
          m_selected_session_index = -1;
        }
        else
        {
          m_browser_status = "Direct error: " + client.GetLastError();
          m_sessions.clear();
          m_selected_session_index = -1;
        }
      }
      catch (const std::exception& e)
      {
        m_browser_status = "Direct exception: " + std::string(e.what());
        m_sessions.clear();
        m_selected_session_index = -1;
      }
    }
    ImGui::Separator();
    
    // Server list table
    if (ImGui::BeginTable("ServerList", 6, 
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | 
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable, 
        ImVec2(0.0f, 320.0f * m_frameScale)))
    {
      ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthFixed, 60 * m_frameScale);
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Password?", ImGuiTableColumnFlags_WidthFixed, 70 * m_frameScale);
      ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 60 * m_frameScale);
      ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 80 * m_frameScale);
      ImGui::TableHeadersRow();
      
      // Display sessions
      for (size_t i = 0; i < m_sessions.size(); i++)
      {
        const auto& session = m_sessions[i];
        
        // Check version compatibility
        const bool compatible = session.version == Common::GetScmDescStr();
        
        ImGui::TableNextRow();
        
        // Disable row if incompatible
        if (!compatible)
        {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }
        
        ImGui::TableNextColumn();
        if (ImGui::Selectable(session.region.c_str(), m_selected_session_index == static_cast<int>(i), 
            ImGuiSelectableFlags_SpanAllColumns | (compatible ? 0 : ImGuiSelectableFlags_Disabled)))
        {
          if (compatible)
            m_selected_session_index = static_cast<int>(i);
        }
        
        ImGui::TableNextColumn();
        ImGui::Text("%s", session.name.c_str());
        
        ImGui::TableNextColumn();
        ImGui::Text("%s", session.has_password ? "Yes" : "No");
        
        ImGui::TableNextColumn();
        ImGui::Text("%s", session.game_id.c_str());
        
        ImGui::TableNextColumn();
        ImGui::Text("%d", session.player_count);
        
        ImGui::TableNextColumn();
        ImGui::Text("%s", session.version.c_str());
        
        if (!compatible)
        {
          ImGui::PopStyleColor();
        }
      }
      
      if (m_sessions.empty())
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("No servers found");
        for (int i = 1; i < 6; i++)
        {
          ImGui::TableNextColumn();
          ImGui::Text("-");
        }
      }
      
      ImGui::EndTable();
    }
    
    // Bottom buttons
    ImGui::Spacing();
    
    if (ImGui::Button("Refresh"))
    {
      RefreshServerList();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Join Selected") && m_selected_session_index >= 0 && 
        m_selected_session_index < static_cast<int>(m_sessions.size()))
    {
      JoinSelectedServer();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      m_show_browser = false;
    }
    
    ImGui::End();
  }
  
  // Draw password dialog if needed
  if (m_show_password_dialog)
  {
    DrawPasswordDialog();
  }
}

void ImGuiNetPlay::DrawSettings()
{
  ImGui::SetNextWindowSize(ImVec2(400 * m_frameScale, 300 * m_frameScale));
  
  if (ImGui::Begin("NetPlay Settings", &m_show_settings))
  {
    if (ImGui::CollapsingHeader("Data Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
      const char* save_data_modes[] = {"No Save Data", "Load Host's Save Data Only", "Load and Write Host's Save Data"};
      if (ImGui::Combo("Save Data Sync", &m_save_data_mode, save_data_modes, 3))
      {
        // Note: Save data syncing mode config may not be available in this version
        // Config::SetBaseOrCurrent(Config::NETPLAY_SAVE_DATA_SYNCING_MODE, 
        //                          static_cast<Config::NetplaySaveDataSyncingMode>(m_save_data_mode));
        // Config::Save();
      }
      
      if (ImGui::Checkbox("Use All Wii Save Data", &m_sync_all_wii_saves))
      {
        // Note: This setting may not be available in this version
        // Config::SetBaseOrCurrent(Config::NETPLAY_SYNC_ALL_WII_SAVES, m_sync_all_wii_saves);
        // Config::Save();
      }
      
      if (ImGui::Checkbox("Sync AR/Gecko Codes", &m_sync_codes))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_SYNC_CODES, m_sync_codes);
        Config::Save();
      }
      
      if (ImGui::Checkbox("Strict Settings Sync", &m_strict_settings_sync))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_STRICT_SETTINGS_SYNC, m_strict_settings_sync);
        Config::Save();
      }
    }
    
    if (ImGui::CollapsingHeader("Network Settings"))
    {
      const char* network_modes[] = {"Fair Input Delay", "Host Input Authority", "Golf Mode"};
      if (ImGui::Combo("Network Mode", &m_network_mode, network_modes, 3))
      {
        const char* mode_strings[] = {"fixeddelay", "hostinputauthority", "golf"};
        Config::SetBaseOrCurrent(Config::NETPLAY_NETWORK_MODE, std::string(mode_strings[m_network_mode]));
        Config::Save();
      }
    }
    
    if (ImGui::CollapsingHeader("Other Settings"))
    {
      if (ImGui::Checkbox("Record Inputs", &m_record_input))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_RECORD_INPUTS, m_record_input);
        Config::Save();
      }
      
      if (ImGui::Checkbox("Show Golf Mode Overlay", &m_golf_mode_overlay))
      {
        // Note: This setting may not be available in this version
        // Config::SetBaseOrCurrent(Config::NETPLAY_GOLF_MODE_OVERLAY, m_golf_mode_overlay);
        // Config::Save();
      }
      
      if (ImGui::Checkbox("Hide Remote GBAs", &m_hide_remote_gbas))
      {
        // Note: This setting may not be available in this version
        // Config::SetBaseOrCurrent(Config::NETPLAY_HIDE_REMOTE_GBAS, m_hide_remote_gbas);
        // Config::Save();
      }
      
      if (ImGui::Checkbox("Limit Chunked Upload Speed", &m_enable_chunked_upload_limit))
      {
        Config::SetBaseOrCurrent(Config::NETPLAY_ENABLE_CHUNKED_UPLOAD_LIMIT, m_enable_chunked_upload_limit);
        Config::Save();
      }
      
      if (m_enable_chunked_upload_limit)
      {
        if (ImGui::InputInt("Upload Limit (kbps)", &m_chunked_upload_limit, 100, 1000))
        {
          Config::SetBaseOrCurrent(Config::NETPLAY_CHUNKED_UPLOAD_LIMIT, static_cast<u32>(m_chunked_upload_limit));
          Config::Save();
        }
      }
    }
    
    if (ImGui::Button("Close"))
    {
      m_show_settings = false;
    }
    
    ImGui::End();
  }
}

void ImGuiNetPlay::RefreshServerList()
{
  std::map<std::string, std::string> filters;

  if (m_hide_incompatible)
    filters["version"] = Common::GetScmDescStr();

  if (!m_server_name_filter.empty())
    filters["name"] = m_server_name_filter;

  if (!m_server_game_filter.empty())
    filters["game"] = m_server_game_filter;

  if (!m_server_region.empty())
    filters["region"] = m_server_region;

  if (m_server_type_filter == 1) // Public only
    filters["password"] = "0";
  else if (m_server_type_filter == 2) // Private only
    filters["password"] = "1";

  if (m_hide_ingame)
    filters["in_game"] = "0";

  // Update status immediately to show we're starting a refresh
  m_browser_status = "Starting refresh...";
  
  std::unique_lock<std::mutex> lock(m_refresh_filters_mutex);
  m_refresh_filters = std::move(filters);
  m_refresh_event.Set();
}

void ImGuiNetPlay::RefreshLoop()
{
  while (m_refresh_run)
  {
    m_refresh_event.Wait();
    
    if (!m_refresh_run)
      break;

    std::unique_lock<std::mutex> lock(m_refresh_filters_mutex);
    if (m_refresh_filters)
    {
      auto filters = std::move(*m_refresh_filters);
      m_refresh_filters.reset();
      lock.unlock();

      m_browser_status = "Refreshing...";

      try 
      {
        NetPlayIndex client;
        auto entries = client.List(filters);

        if (entries)
        {
          m_sessions = std::move(*entries);
          const int session_count = static_cast<int>(m_sessions.size());
          m_browser_status = (session_count == 1) ? 
            (std::to_string(session_count) + " session found") :
            (std::to_string(session_count) + " sessions found");
          m_selected_session_index = -1; // Reset selection
        }
        else
        {
          m_browser_status = "Error obtaining session list: " + client.GetLastError();
          m_sessions.clear();
          m_selected_session_index = -1;
        }
      }
      catch (const std::exception& e)
      {
        m_browser_status = "Network error: " + std::string(e.what());
        m_sessions.clear();
        m_selected_session_index = -1;
      }
    }
    
    // Small delay to prevent excessive refreshing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

void ImGuiNetPlay::JoinSelectedServer()
{
  if (m_selected_session_index < 0 || m_selected_session_index >= static_cast<int>(m_sessions.size()))
    return;

  const NetPlaySession& session = m_sessions[m_selected_session_index];

  if (session.has_password)
  {
    // Show password dialog
    m_password_session = session;
    m_show_password_dialog = true;
    m_password_input_buf[0] = '\0';
  }
  else
  {
    // Join directly
    Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_CHOICE, session.method);
    Config::SetBaseOrCurrent(Config::NETPLAY_CONNECT_PORT, static_cast<u16>(session.port));

    if (session.method == "traversal")
      Config::SetBaseOrCurrent(Config::NETPLAY_HOST_CODE, session.server_id);
    else
      Config::SetBaseOrCurrent(Config::NETPLAY_ADDRESS, session.server_id);

    Config::Save();

    // Update the connection UI
    strncpy(m_host_buf, session.server_id.c_str(), sizeof(m_host_buf));
    m_traversal = (session.method == "traversal");
    
    // Close browser and initiate connection
    m_show_browser = false;
  }
}

void ImGuiNetPlay::DrawPasswordDialog()
{
  ImGui::SetNextWindowSize(ImVec2(400 * m_frameScale, 150 * m_frameScale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (200) * m_frameScale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (75) * m_frameScale));

  if (ImGui::BeginPopupModal("Enter Password", &m_show_password_dialog, 
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
  {
    ImGui::Text("This session requires a password:");
    ImGui::Spacing();
    
    if (ImGui::InputText("Password", m_password_input_buf, sizeof(m_password_input_buf), 
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue))
    {
      // Password entered, try to join
      std::string password(m_password_input_buf);
      auto decrypted_id = m_password_session.DecryptID(password);
      
      if (decrypted_id)
      {
        // Valid password, join server
        Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_CHOICE, m_password_session.method);
        Config::SetBaseOrCurrent(Config::NETPLAY_CONNECT_PORT, static_cast<u16>(m_password_session.port));

        if (m_password_session.method == "traversal")
          Config::SetBaseOrCurrent(Config::NETPLAY_HOST_CODE, decrypted_id.value());
        else
          Config::SetBaseOrCurrent(Config::NETPLAY_ADDRESS, decrypted_id.value());

        Config::Save();

        // Update the connection UI
        strncpy(m_host_buf, decrypted_id.value().c_str(), sizeof(m_host_buf));
        m_traversal = (m_password_session.method == "traversal");
        
        // Close dialogs
        m_show_password_dialog = false;
        m_show_browser = false;
        ImGui::CloseCurrentPopup();
      }
      else
      {
        // Invalid password - show error
        m_warning_text = "Invalid password provided.";
        m_prompt_warning = true;
      }
    }
    
    ImGui::Spacing();
    
    if (ImGui::Button("OK"))
    {
      // Try to join with entered password
      std::string password(m_password_input_buf);
      auto decrypted_id = m_password_session.DecryptID(password);
      
      if (decrypted_id)
      {
        // Valid password, join server
        Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_CHOICE, m_password_session.method);
        Config::SetBaseOrCurrent(Config::NETPLAY_CONNECT_PORT, static_cast<u16>(m_password_session.port));

        if (m_password_session.method == "traversal")
          Config::SetBaseOrCurrent(Config::NETPLAY_HOST_CODE, decrypted_id.value());
        else
          Config::SetBaseOrCurrent(Config::NETPLAY_ADDRESS, decrypted_id.value());

        Config::Save();

        // Update the connection UI
        strncpy(m_host_buf, decrypted_id.value().c_str(), sizeof(m_host_buf));
        m_traversal = (m_password_session.method == "traversal");
        
        // Close dialogs
        m_show_password_dialog = false;
        m_show_browser = false;
        ImGui::CloseCurrentPopup();
      }
      else
      {
        // Invalid password - show error
        m_warning_text = "Invalid password provided.";
        m_prompt_warning = true;
      }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
      m_show_password_dialog = false;
      ImGui::CloseCurrentPopup();
    }
    
    ImGui::EndPopup();
  }
  
  if (m_show_password_dialog && !ImGui::IsPopupOpen("Enter Password"))
  {
    ImGui::OpenPopup("Enter Password");
  }
}
}  // namespace ImGuiFrontend
