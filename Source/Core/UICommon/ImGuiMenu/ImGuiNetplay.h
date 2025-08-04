#pragma once

#include "ImGuiFrontend.h"

#include <Core/NetPlayClient.h>
#include <Core/SyncIdentifier.h>
#include <UICommon/NetPlayIndex.h>
#include <Common/Event.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>

namespace ImGuiFrontend
{
enum NetPlayDrawResult
{
  Continue,
  BootGame,
  ExitNetplay
};

class ImGuiNetPlay : public NetPlay::NetPlayUI
{
  void BootGame(const std::string& filename,
                std::unique_ptr<BootSessionData> boot_session_data) override;

  void StopGame() override;
  bool IsHosting() const override;

  void Update() override;
  void AppendChat(const std::string& msg) override;

  void OnMsgChangeGame(const NetPlay::SyncIdentifier& sync_identifier,
                       const std::string& netplay_name) override;

  void OnMsgChangeGBARom(int pad, const NetPlay::GBAConfig& config) override;
  void OnMsgStartGame() override;
  void OnMsgStopGame() override;
  void OnMsgPowerButton() override;
  void OnPlayerConnect(const std::string& player) override;
  void OnPlayerDisconnect(const std::string& player) override;
  void OnPadBufferChanged(u32 buffer) override;
  void OnHostInputAuthorityChanged(bool enabled) override;
  void OnDesync(u32 frame, const std::string& player) override;
  void OnConnectionLost() override;
  void OnConnectionError(const std::string& message) override;
  void OnTraversalError(Common::TraversalClient::FailureReason error) override;
  void OnTraversalStateChanged(Common::TraversalClient::State state) override;
  void OnGameStartAborted() override;
  void OnGolferChanged(bool is_golfer, const std::string& golfer_name) override;

  bool IsRecording() override;
  std::shared_ptr<const UICommon::GameFile>
  FindGameFile(const NetPlay::SyncIdentifier& sync_identifier,
               NetPlay::SyncIdentifierComparison* found = nullptr) override;

  std::string FindGBARomPath(const std::array<u8, 20>& hash, std::string_view title,
                             int device_number) override;

  void ShowGameDigestDialog(const std::string& title) override;
  void SetGameDigestProgress(int pid, int progress) override;
  void SetGameDigestResult(int pid, const std::string& result) override;
  void AbortGameDigest() override;

  void OnIndexAdded(bool success, std::string error) override;
  void OnIndexRefreshFailed(std::string error) override;

  void ShowChunkedProgressDialog(const std::string& title, u64 data_size,
                                 const std::vector<int>& players) override;

  void HideChunkedProgressDialog() override;
  void SetChunkedProgress(int pid, u64 progress) override;

  void SetHostWiiSyncData(std::vector<u64> titles, std::string redirect_folder) override;

  void OnTtlDetermined(u8 ttl) override;

public:
  ImGuiNetPlay(ImGuiFrontend* frontend, std::vector<std::shared_ptr<UICommon::GameFile>> games, float frame_scale);
  ~ImGuiNetPlay();

  NetPlayDrawResult Draw();
  void DrawLobbyWindow();
  void DrawSetup();
  void DrawBrowser();
  void DrawSettings();
  void DrawChatSection();
  void DrawPasswordDialog();
  void RefreshServerList();
  void RefreshLoop();
  void JoinSelectedServer();
  void Reset();
  void DisplayMessage(std::string msg, int duration, float r, float g, float b);

private:
  ImGuiFrontend* m_frontend;
  std::vector<std::shared_ptr<UICommon::GameFile>> m_games;
  float m_frameScale;
  
  // UI state
  bool m_show_settings = false;
  bool m_show_browser = false;
  
  // Settings
  bool m_strict_settings_sync = false;
  bool m_sync_codes = true;
  bool m_record_input = false;
  bool m_enable_chunked_upload_limit = false;
  int m_chunked_upload_limit = 3000;
  int m_network_mode = 0; // 0=Fair Input Delay, 1=Host Input Authority, 2=Golf Mode
  int m_save_data_mode = 1; // 0=None, 1=Load Only, 2=Load & Write
  bool m_sync_all_wii_saves = false;
  bool m_golf_mode_overlay = false;
  bool m_hide_remote_gbas = false;
  
  // Browser state
  std::string m_server_region = "";
  std::string m_server_name_filter = "";
  std::string m_server_game_filter = "";
  bool m_hide_incompatible = false;  // Changed to false to show all servers by default
  bool m_hide_ingame = false;
  int m_server_type_filter = 0; // 0=All, 1=Public, 2=Private
  
  // Server browser data
  std::vector<NetPlaySession> m_sessions;
  std::thread m_refresh_thread;
  std::atomic<bool> m_refresh_run{false};
  std::mutex m_refresh_filters_mutex;
  std::optional<std::map<std::string, std::string>> m_refresh_filters;
  Common::Event m_refresh_event;
  int m_selected_session_index = -1;
  std::string m_browser_status = "";
  
  // Chat
  std::vector<std::string> m_chat_messages;
  char m_chat_input_buf[256] = "";
  
  // Password dialog state
  bool m_show_password_dialog = false;
  char m_password_input_buf[64] = "";
  NetPlaySession m_password_session;
  
  // External IP for hosting
  Common::Lazy<std::string> m_external_ip;
};

void DrawLobbyMenu();
}  // namespace
