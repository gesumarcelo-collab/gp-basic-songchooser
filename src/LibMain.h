#pragma once

// ---------------------------------------------------------------------------
// ORDEM DOS INCLUDES IMPORTA:
//   1) biblioteca padrao (a CHOC usa std::function / std::unique_ptr etc.)
//   2) choc_Platform.h (define as macros de plataforma que choc_WebView.h verifica)
//   3) headers da CHOC
//   4) headers do GP SDK
// ---------------------------------------------------------------------------

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
    void OnSetlistChanged(const std::string &newSetlistName) override;

  private:
    void openWindow();
    void destroyWindow();
    void pushState(int currentOverride);
    std::string buildStateJson(int currentOverride);

    std::unique_ptr<choc::ui::WebView> webview;
    std::unique_ptr<choc::ui::DesktopWindow> window;
    bool closedByUser = false;
    std::string setlistName;
};
