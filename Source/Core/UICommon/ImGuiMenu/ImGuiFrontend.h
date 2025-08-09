#pragma once

#include <memory>
#include "Core/Boot/Boot.h"
#include "Core/TitleDatabase.h"
#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerInterface/CoreDevice.h"
#include "VideoCommon/AbstractTexture.h"

namespace UICommon
{
class GameFile;
}

class WGDevice;

namespace ImGuiFrontend
{
enum SelectedTab
{
  General,
  Interface,
  Graphics,
  Audio,
  Controls,
  Paths,
  Wii,
  GC,
  Advanced,
  About,
  Achievements
};

enum PropertiesTab
{
  Info,
  GameConfig,
  Patches,
  ARCodes,
  GeckoCodes,
  GraphicsMods,
  PropertiesAchievements
};

enum ThemeBG
{
  BG_All = 1,
  BG_Wii,
  BG_GC,
  BG_Other,
  BG_Menu,
  BG_List,
  BG_Netplay,
  BG_List_UI,
  BG_Carousel_UI,
  BG_COUNT
};

enum CarouselCategory
{
  CAll = 1,
  CWii = 2,
  CGC = 3,
  COther = 4,
  CCount
};

class UIState
{
public:
  bool controlsDisabled = false;
  bool showSettingsWindow = false;
  bool showListView = false;
  bool showPropertiesWindow = false;
  bool menuPressed = false;
  std::string selectedPath;
  SelectedTab selectedTab = General;
  ThemeBG currentBG = BG_All;
  CarouselCategory carouselCat = CAll;
  std::map<uint32_t, std::shared_ptr<AbstractTexture>> achievement_badges;
  // Add mapping window state
  bool showMappingWindow = false;
  int mappingWindowPort = 0;
  bool mappingWindowIsWii = false;
  // Currently capturing input for a ControlReference
  ::ControlReference* capturingRef = nullptr;
  // Input detector used during capture
  std::unique_ptr<ciface::Core::InputDetector> mappingInputDetector;
  // Deferred boot parameters (Wii/GC system menu)
  std::unique_ptr<BootParameters> pending_boot_params;
  // Properties dialog state
  PropertiesTab selectedPropertiesTab = Info;
  std::shared_ptr<UICommon::GameFile> propertiesGame = nullptr;
  int wiimote_extension = 0;
  bool wiimote_motionplus = false;
  float wiimote_speaker_pan = 0.0f;
  float wiimote_battery = 1.0f;
  bool wiimote_upright = true;
  bool wiimote_sideways = false;
};

class FrontendResult
{
public:
  std::shared_ptr<UICommon::GameFile> game_result;
  bool netplay;
  std::unique_ptr<BootParameters> boot_params;

  FrontendResult()
  {
    game_result = nullptr;
    netplay = false;
    boot_params = nullptr;
  }

  FrontendResult(std::shared_ptr<UICommon::GameFile> game)
  {
    game_result = game;
    netplay = false;
    boot_params = nullptr;
  }

  FrontendResult(std::unique_ptr<BootParameters> bp)
      : game_result(nullptr), netplay(false), boot_params(std::move(bp))
  {
  }
};

class FrontendTheme
{
public:
  std::shared_ptr<AbstractTexture> GetBackground(ThemeBG cat);
  bool TryLoad(std::string path);
  std::string GetName() { return m_name; }

private:
  std::shared_ptr<AbstractTexture> m_textures[ThemeBG::BG_COUNT];
  std::string m_name;
};

class FrontendBackground
{
public:
  std::shared_ptr<AbstractTexture> GetTexture() { return m_texture; }
  bool TryLoad(std::string path);
  std::string GetName() { return m_name; }

private:
  std::shared_ptr<AbstractTexture> m_texture;
  std::string m_name;
};

class ImGuiFrontend
{
public:
  ImGuiFrontend();
  FrontendResult RunUntilSelection();

  Core::TitleDatabase m_title_database;
  std::mutex m_frontend_mutex;

private:
  void PopulateControls();
  void RefreshControls(bool updateGameSelection);

  FrontendResult RunMainLoop();
  FrontendResult CreateMainPage();
  FrontendResult CreateListPage();
  std::shared_ptr<UICommon::GameFile> CreateGameCarousel();
  std::shared_ptr<UICommon::GameFile> CreateGameList();

  void LoadGameList();
  void FilterGamesForCategory();
  void LoadThemes();
  void RecurseForThemes(std::string path);
  void LoadBackgrounds();
  void RecurseForBackgrounds(std::string path);
  void RecurseFolderForGames(std::string path);
  void AddGameFolder(std::string path);
  bool TryInput(std::string expression, std::shared_ptr<ciface::Core::Device> device);

#ifdef USE_RETRO_ACHIEVEMENTS
  void OnHardcoreChanged();
#endif  // USE_RETRO_ACHIEVEMENTS

  AbstractTexture* GetOrCreateMissingTex();
  AbstractTexture* GetHandleForGame(std::shared_ptr<UICommon::GameFile> game);
  std::shared_ptr<AbstractTexture> CreateCoverTexture(std::shared_ptr<UICommon::GameFile> game);
  
  void UpdateCarouselAnimation(float delta_time);
  void DrawCarouselItem(std::shared_ptr<UICommon::GameFile> game, int index, int center_index, float base_x, float base_y);
  void DrawGameInfo(std::shared_ptr<UICommon::GameFile> game);
  void DrawCarouselBackground();
  float GetCarouselItemPosition(int index, int center_index);
  float GetCarouselItemScale(int index, int center_index);
  float GetCarouselItemAlpha(int index, int center_index);
  void SmoothScrollToGame(int target_index);
  
  // RetroAchievements functions
  std::pair<int, int> GetAchievementCountsForGame(const std::string& file_path);

  std::vector<std::shared_ptr<ciface::Core::Device>> m_controllers;
  std::vector<std::shared_ptr<UICommon::GameFile>> m_games;
  std::unordered_map<std::string, std::shared_ptr<AbstractTexture>> m_cover_textures;
  u64 m_imgui_last_frame_time;

  std::unique_ptr<AbstractTexture> m_background_tex, m_background_list_tex;
  std::shared_ptr<AbstractTexture> m_missing_tex;

  bool m_direction_pressed = false;
  std::chrono::high_resolution_clock::time_point m_scroll_last =
      std::chrono::high_resolution_clock::now();

  std::chrono::high_resolution_clock::time_point m_time_since_init =
      std::chrono::high_resolution_clock::now();

  std::vector<std::shared_ptr<UICommon::GameFile>> m_displayed_games;
  CarouselCategory m_last_category =
      CarouselCategory::CCount;
  std::string m_prev_list_search;
  std::vector<std::shared_ptr<UICommon::GameFile>> m_list_search_results;
  char m_list_search_buf[32]{};

  float m_carousel_scroll_offset = 0.0f;
  float m_target_scroll_offset = 0.0f;
  float m_carousel_animation_speed = 8.0f;
  bool m_carousel_smooth_scrolling = true;
  int m_carousel_visible_items = 5;
  float m_carousel_item_spacing = 300.0f;
  float m_carousel_selected_scale = 1.06f;
  float m_carousel_unselected_scale = 0.80f;
  
  bool m_show_game_info = true;
  float m_game_info_fade_alpha = 1.0f;
  float m_game_selection_timer = 0.0f;
  std::string m_current_game_description;
  
  bool m_carousel_show_reflections = true;
  bool m_carousel_show_shadows = true;
  float m_carousel_reflection_alpha = 0.3f;
  float m_carousel_shadow_offset = 8.0f;

#ifdef USE_RETRO_ACHIEVEMENTS
  Config::ConfigChangedCallbackID m_config_changed_callback_id;
#endif  // USE_RETRO_ACHIEVEMENTS
};

void DrawSettingsMenu(UIState* state, float frame_scale);
void DrawPropertiesDialog(UIState* state, float frame_scale);
void CreatePropertiesInfoTab(UIState* state);
void CreatePropertiesGameConfigTab(UIState* state);
void CreatePropertiesPatchesTab(UIState* state);
void CreatePropertiesARCodesTab(UIState* state);
void CreatePropertiesGeckoCodesTab(UIState* state);
void CreatePropertiesGraphicsModsTab(UIState* state);
void CreatePropertiesAchievementsTab(UIState* state);
void CreateGeneralTab(UIState* state);
void CreateInterfaceTab(UIState* state);
void CreateGraphicsTab(UIState* state);
void CreateControlsTab(UIState* state);
void CreateGameCubeTab(UIState* state);
void CreateWiiTab(UIState* state);
void CreateAdvancedTab(UIState* state);
void CreatePathsTab(UIState* state);
void CreateAudioTab(UIState* state);
void CreateAchievementsTab(UIState* state);
void DrawAchievementsWindow(UIState* state);
void CreateWiiPort(int index, std::vector<std::string> devices);
void CreateGCPort(int index, std::vector<std::string> devices);

std::shared_ptr<AbstractTexture> CreateTextureFromPath(std::string path,
                                                       bool is_theme_asset = false);
}  // namespace ImGuiFrontend
