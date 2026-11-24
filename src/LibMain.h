#pragma once

#include "gigperformer/sdk/GPMidiMessages.h"
#include "gigperformer/sdk/GPUtils.h"
#include "gigperformer/sdk/GigPerformerAPI.h"
#include "gigperformer/sdk/types.h"

#include "choc/gui/choc_DesktopWindow.h"
#include "choc/gui/choc_WebView.h"

#include <memory>
#include <string>

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