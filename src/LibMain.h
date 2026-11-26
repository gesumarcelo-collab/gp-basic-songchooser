#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "choc/platform/choc_Platform.h"
#include "choc/containers/choc_Value.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "choc/gui/choc_WebView.h"

#include "gigperformer/sdk/GPMidiMessages.h"
#include "gigperformer/sdk/GPUtils.h"
#include "gigperformer/sdk/GigPerformerAPI.h"
#include "gigperformer/sdk/types.h"

class LibMain : public gigperformer::sdk::GigPerformerAPI
{
  public:
    explicit LibMain(LibraryHandle handle) : GigPerformerAPI(handle) {}
    ~LibMain() override;

    void openWindow();
    void triggerStepCommand(int delta);

  protected:
    void Initialization() override;
    void OnClose() override;
    std::string GetProductDescription() override;

    int GetMenuCount() override;
    std::string GetMenuName(int index) override;
    void InvokeMenu(int itemIndex) override;

    void OnSongChanged(int oldIndex, int newIndex) override;
    void OnSongPartChanged(int oldIndex, int newIndex) override;
    void OnSetlistChanged(const std::string &newSetlistName) override;
    void OnWidgetValueChanged(const std::string &widgetName, double newValue) override;

    int RequestGPScriptFunctionSignatureList(GPScript_AllowedLocations location,
                                             ExternalAPI_GPScriptFunctionDefinition **list) override;

  private:
    void destroyWindow();
    void pushState(int songOverride, int partOverride);
    std::string buildStateJson(int songOverride, int partOverride);

    std::unique_ptr<choc::ui::WebView> webview;
    std::unique_ptr<choc::ui::DesktopWindow> window;
    bool closedByUser = false;
    bool shuttingDown = false;
    std::string setlistName;
    std::atomic<int> pendingStep{0};
};
