#include "LibMain.h"
#include "webcontent.h"

#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
constexpr int kNoOverride = -1000000;
constexpr char kQuote = '"';
constexpr char kSlash = '\\';

using WebViewPtr = std::unique_ptr<choc::ui::WebView>;
using WindowPtr = std::unique_ptr<choc::ui::DesktopWindow>;
using CharBuf8 = std::array<char, 8>;
using IntPair = std::pair<int, int>;

LibMain *gInstance = nullptr;

#ifdef _WIN32
void pinCurrentDllInMemory()
{
    static bool pinned = false;
    if (pinned)
        return;

    HMODULE hm = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(&pinCurrentDllInMemory),
            &hm))
    {
        pinned = true;
    }
}
#endif

std::string qStr(const std::string &s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r.push_back(kQuote);
    r += s;
    r.push_back(kQuote);
    return r;
}

std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out.push_back(kSlash); out.push_back(kQuote); break;
        case '\\': out.push_back(kSlash); out.push_back(kSlash); break;
        case '\n': out.push_back(kSlash); out.push_back('n');    break;
        case '\r': out.push_back(kSlash); out.push_back('r');    break;
        case '\t': out.push_back(kSlash); out.push_back('t');    break;
        case '\b': out.push_back(kSlash); out.push_back('b');    break;
        case '\f': out.push_back(kSlash); out.push_back('f');    break;
        default:
            if (c < 0x20)
            {
                CharBuf8 buf{};
                std::snprintf(buf.data(), buf.size(), "%cu%04x", kSlash, c);
                out += buf.data();
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

std::filesystem::path settingsFile()
{
    std::filesystem::path dir;
    try
    {
#ifdef _WIN32
        if (const wchar_t *appData = _wgetenv(L"APPDATA"))
            dir = std::filesystem::path(appData);
#endif
        if (dir.empty())
            dir = std::filesystem::temp_directory_path();
        dir /= "SetlistQueue";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    catch (...)
    {
    }
    return dir / "settings.json";
}

std::string readTextFile(const std::filesystem::path &p)
{
    try
    {
        std::ifstream in(p, std::ios::binary);
        if (!in)
            return {};
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    catch (...)
    {
        return {};
    }
}

bool writeTextFile(const std::filesystem::path &p, const std::string &text)
{
    try
    {
        auto tmp = p;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        std::filesystem::rename(tmp, p);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int parseIntView(const choc::value::ValueView &v, int fallback)
{
    try
    {
        if (v.isInt32())
            return v.getInt32();
        if (v.isInt64())
            return static_cast<int>(v.getInt64());
        if (v.isFloat32())
            return static_cast<int>(v.getFloat32());
        if (v.isFloat64())
            return static_cast<int>(v.getFloat64());
        if (v.isString())
            return std::stoi(std::string(v.getString()));
    }
    catch (...)
    {
    }
    return fallback;
}

int intArg(const choc::value::ValueView &args, int fallback)
{
    try
    {
        if (args.isArray())
        {
            for (const auto &item : args)
                return parseIntView(item, fallback);
        }
    }
    catch (...)
    {
    }
    return fallback;
}

IntPair twoIntArgs(const choc::value::ValueView &args)
{
    int a = -1;
    int b = 0;
    try
    {
        if (args.isArray())
        {
            int count = 0;
            for (const auto &item : args)
            {
                if (count == 0 && item.isArray())
                {
                    int sub = 0;
                    for (const auto &subItem : item)
                    {
                        if (sub == 0)
                            a = parseIntView(subItem, -1);
                        else if (sub == 1)
                            b = parseIntView(subItem, 0);
                        ++sub;
                    }
                    break;
                }
                if (count == 0)
                    a = parseIntView(item, -1);
                else if (count == 1)
                    b = parseIntView(item, 0);
                ++count;
            }
        }
    }
    catch (...)
    {
    }
    return {a, b};
}

std::string stringArg(const choc::value::ValueView &args)
{
    try
    {
        if (args.isArray())
        {
            for (const auto &item : args)
            {
                if (item.isString())
                    return std::string(item.getString());
                break;
            }
        }
    }
    catch (...)
    {
    }
    return {};
}

extern "C" void gpScriptOpen(GPRuntimeEngine *)
{
    if (gInstance)
        gInstance->openWindow();
}

extern "C" void gpScriptNext(GPRuntimeEngine *)
{
    if (gInstance)
        gInstance->triggerStepCommand(1);
}

extern "C" void gpScriptPrev(GPRuntimeEngine *)
{
    if (gInstance)
        gInstance->triggerStepCommand(-1);
}

ExternalAPI_GPScriptFunctionDefinition gScriptFunctions[] = {
    {"Open", "", "", "Abre a janela Setlist Queue", gpScriptOpen},
    {"Next", "", "", "Avanca para a proxima musica da fila", gpScriptNext},
    {"Prev", "", "", "Volta para a musica anterior da fila", gpScriptPrev}
};
} // namespace

LibMain::~LibMain()
{
    shuttingDown = true;
    if (gInstance == this)
        gInstance = nullptr;
    destroyWindow();
}

void LibMain::Initialization()
{
    gInstance = this;
    registerCallback("OnSongChanged");
    registerCallback("OnSongPartChanged");
    registerCallback("OnSetlistChanged");
    registerCallback("OnWidgetValueChanged");

    listenForWidget("SetlistQueue", true);
    listenForWidget("SetlistQueueOpen", true);
    listenForWidget("SetlistQueueNext", true);
    listenForWidget("SetlistQueuePrev", true);
}

void LibMain::OnClose()
{
    shuttingDown = true;
    unregisterCallback("OnSongChanged");
    unregisterCallback("OnSongPartChanged");
    unregisterCallback("OnSetlistChanged");
    unregisterCallback("OnWidgetValueChanged");
    destroyWindow();
}

std::string LibMain::GetProductDescription()
{
    return std::string("<Library><Product Name=") + qStr("SetlistQueue") +
           " Version=" + qStr("1.3.1") +
           " BuildDate=" + qStr(__DATE__) +
           "></Product><Description>Setlist Queue com busca e widgets</Description></Library>";
}

int LibMain::GetMenuCount()
{
    return 1;
}

std::string LibMain::GetMenuName(int)
{
    return "Setlist Queue";
}

void LibMain::InvokeMenu(int)
{
    if (!shuttingDown)
        openWindow();
}

int LibMain::RequestGPScriptFunctionSignatureList(GPScript_AllowedLocations,
                                                  ExternalAPI_GPScriptFunctionDefinition **list)
{
    *list = gScriptFunctions;
    return static_cast<int>(sizeof(gScriptFunctions) / sizeof(gScriptFunctions));
}

void LibMain::triggerStepCommand(int delta)
{
    if (shuttingDown)
        return;

    if (webview && !closedByUser)
    {
        pendingStep.store(delta);
        return;
    }

    const int count = getSongCount();
    if (count <= 0)
        return;

    int cur = getCurrentSongIndex();
    int target = (cur < 0) ? (delta > 0 ? 0 : count - 1) : (cur + delta);
    if (target < 0)
        target = 0;
    if (target >= count)
        target = count - 1;

    if (!inSetlistMode())
        switchToSetlistView();
    switchToSong(target, 0);
}

void LibMain::OnWidgetValueChanged(const std::string &widgetName, double newValue)
{
    if (shuttingDown || newValue < 0.5)
        return;

    if (widgetName == "SetlistQueue" || widgetName == "SetlistQueueOpen")
    {
        openWindow();
    }
    else if (widgetName == "SetlistQueueNext")
    {
        triggerStepCommand(1);
    }
    else if (widgetName == "SetlistQueuePrev")
    {
        triggerStepCommand(-1);
    }
}

void LibMain::OnSongChanged(int, int newIndex)
{
    if (!shuttingDown)
        pushState(newIndex, kNoOverride);
}

void LibMain::OnSongPartChanged(int, int newIndex)
{
    if (!shuttingDown)
        pushState(kNoOverride, newIndex);
}

void LibMain::OnSetlistChanged(const std::string &newSetlistName)
{
    if (shuttingDown)
        return;
    setlistName = newSetlistName;
    pushState(kNoOverride, kNoOverride);
}

std::string LibMain::buildStateJson(int songOverride, int partOverride)
{
    int count = getSongCount();
    if (count < 0)
        count = 0;

    const int current = (songOverride != kNoOverride) ? songOverride : getCurrentSongIndex();
    const int currentPart = (partOverride != kNoOverride) ? partOverride : getCurrentSongpartIndex();
    const int step = pendingStep.exchange(0);

    std::string j = "{" + qStr("setlist") + ":" + qStr(jsonEscape(setlistName)) +
                    "," + qStr("current") + ":" + std::to_string(current) +
                    "," + qStr("currentPart") + ":" + std::to_string(currentPart) +
                    "," + qStr("step") + ":" + std::to_string(step) +
                    "," + qStr("songs") + ":[";

    for (int i = 0; i < count; ++i)
{
if (i > 0)
j += ',';
j += "{" + qStr("i") + ":" + std::to_string(i) +
"," + qStr("n") + ":" + qStr(jsonEscape(getSongName(i))) +
"," + qStr("parts") + ":[";
int pCount = getSongpartCount(i);
if (pCount < 0)
pCount = 0;
for (int p = 0; p < pCount; ++p)
{
if (p > 0)
j += ',';
j += "{" + qStr("i") + ":" + std::to_string(p) +
"," + qStr("n") + ":" + qStr(jsonEscape(getSongpartName(i, p))) + "}";
}
j += "]}";
}
j += "]}";
return j;
}
void LibMain::pushState(int songOverride, int partOverride)
{
if (shuttingDown || !webview || closedByUser)
return;
try
{
webview->evaluateJavascript("window.gpPush&&window.gpPush(" + buildStateJson(songOverride, partOverride) + ");");
}
catch (...)
{
}
}
void LibMain::destroyWindow()
{
try
{
if (window)
{
window->windowClosed = nullptr;
window->setVisible(false);
}
if (shuttingDown)
{
webview.release();
window.release();
}
else
{
webview.reset();
window.reset();
}
}
catch (...)
{
webview.release();
window.release();
}
closedByUser = false;
}
void LibMain::openWindow()
{
if (shuttingDown)
return;
#ifdef _WIN32
pinCurrentDllInMemory();
#endif
if (window && closedByUser)
destroyWindow();
if (window)
{
window->setVisible(true);
window->toFront();
return;
}
try
{
webview = WebViewPtr(new choc::ui::WebView());
}
catch (...)
{
webview.reset();
}
if (!webview || webview->getViewHandle() == nullptr)
{
#ifdef _WIN32
MessageBoxA(nullptr, "WebView2 Runtime nao encontrado no Windows. Instale o Microsoft Edge WebView2 Runtime.", "Setlist Queue", MB_OK | MB_ICONERROR);
#endif
webview.reset();
return;
}
webview->bind("gp_getState", [this](const choc::value::ValueView &) -> choc::value::Value {
if (shuttingDown)
return choc::value::createString("{}");
return choc::value::createString(buildStateJson(kNoOverride, kNoOverride));
});
webview->bind("gp_selectSong", [this](const choc::value::ValueView &args) -> choc::value::Value {
if (shuttingDown)
return choc::value::createString("0");
const int idx = intArg(args, -1);
if (idx >= 0 && idx < getSongCount())
{
if (!inSetlistMode())
switchToSetlistView();
switchToSong(idx, 0);
}
return choc::value::createString("1");
});
webview->bind("gp_selectSongPart", [this](const choc::value::ValueView &args) -> choc::value::Value {
if (shuttingDown)
return choc::value::createString("0");
const auto [songIdx, partIdx] = twoIntArgs(args);
if (songIdx >= 0 && songIdx < getSongCount())
{
if (!inSetlistMode())
switchToSetlistView();
const int maxParts = getSongpartCount(songIdx);
const int safePart = (partIdx >= 0 && partIdx < maxParts) ? partIdx : 0;
switchToSong(songIdx, safePart);
}
return choc::value::createString("1");
});
webview->bind("gp_loadSettings", [this](const choc::value::ValueView &) -> choc::value::Value {
if (shuttingDown)
return choc::value::createString("{}");
return choc::value::createString(readTextFile(settingsFile()));
});
webview->bind("gp_saveSettings", [this](const choc::value::ValueView &args) -> choc::value::Value {
if (shuttingDown)
return choc::value::createString("0");
const auto text = stringArg(args);
if (!text.empty())
writeTextFile(settingsFile(), text);
return choc::value::createString("1");
});
window = WindowPtr(new choc::ui::DesktopWindow(choc::ui::Bounds{120, 80, 460, 760}));
window->setWindowTitle("Setlist Queue");
window->setResizable(true);
window->setMinimumSize(280, 320);
window->windowClosed = [this] {
closedByUser = true;
#ifdef _WIN32
if (window)
ShowWindow((HWND) window->getWindowHandle(), SW_HIDE);
#endif
};
window->setContent(webview->getViewHandle());
const std::string html((const char *) kWebContent, kWebContentSize);
webview->setHTML(html);
window->setVisible(true);
window->toFront();
#ifdef _WIN32
if (HWND hwnd = (HWND) window->getWindowHandle())
SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
}
namespace gigperformer
{
namespace sdk
{
GigPerformerAPI *CreateGPExtension(LibraryHandle handle)
{
return new LibMain(handle);
}
} // namespace sdk
} // namespace gigperformer
