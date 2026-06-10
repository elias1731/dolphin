#include "DolphinWinRT/Host.h"

// Define the UWP global flags
namespace UWP {
  Common::Flag g_shutdown_requested;
  Common::Flag g_tried_graceful_shutdown;
  Common::Flag g_needs_frontend_reset;  // Also add this if needed
}

// Define the NetPlay globals
std::shared_ptr<NetPlay::NetPlayClient> g_netplay_client;
std::shared_ptr<NetPlay::NetPlayServer> g_netplay_server;
std::shared_ptr<ImGuiFrontend::ImGuiNetPlay> g_netplay_dialog;