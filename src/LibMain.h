#pragma once

// 1) Biblioteca padrao
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 2) Deteccao de plataforma da CHOC
#include "choc/platform/choc_Platform.h"

// 3) CHOC
#include "choc/containers/choc_Value.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "choc/gui/choc_WebView.h"

// 4) GP SDK
#include "gigperformer/sdk/GPMidiMessages.h"
#include "gigperformer/sdk/GPUtils.h"
#include "gigperformer/sdk/GigPerformerAPI.h"
#include "gigperformer/sdk/types.h"

class LibMain : public gigperformer::sdk::GigPerformerAPI
{
  public:
    explicit LibMain(LibraryHandle handle) : GigPerformerAPI(handle) {}
    ~LibMain() override;

  protected:
    void Initialization() override;
    std::string GetProductDescription() override;

    // Item de menu em "Extensions" que abre a janela
    int GetMenuCount() override;
    std::string GetMenuName(int index) override;
    void InvokeMenu(int itemIndex) override;

    // Callbacks do Gig Performer
    void OnSongChanged(int oldIndex, int newIndex) override;
    void OnSongPartChanged(int oldIndex, int newIndex) override;
    void OnSetlistChanged(const std::string &newSetlistName) override;

  private:
    void openWindow();
    void destroyWindow();
    void pushState(int currentSongOverride, int currentPartOverride);
    std::string buildStateJson(int currentSongOverride, int currentPartOverride);

    std::unique_ptr<choc::ui::WebView> webview;
    std::unique_ptr<choc::ui::DesktopWindow> window;
    bool closedByUser = false;
    std::string setlistName;
};
