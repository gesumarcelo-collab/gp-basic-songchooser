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

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
constexpr int kNoOverride = -1000000;

std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (c < 0x20)
            {
                std::array<char, 8> buf{};
                std::snprintf(buf.data(), buf.size(), "\\u%04x", c);
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
        if (args.isArray() && args.size() >= 1)
            return parseIntView(args.at(0), fallback);
    }
    catch (...)
    {
    }
    return fallback;
}

std::pair<int, int> twoIntArgs(const choc::value::ValueView &args)
{
    int a = -1;
    int b = 0;
    try
    {
        if (args.isArray() && args.size() >= 1)
        {
            const auto first = args.at(0);
            if (first.isArray() && first.size() >= 2)
            {
                a = parseIntView(first.at(0), -1);
                b = parseIntView(first.at(1), 0);
            }
            else
            {
                a = parseIntView(first, -1);
                if (args.size() >= 2)
                    b = parseIntView(args.at(1), 0);
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
        if (args.isArray() && args.size() >= 1 && args.at(0).isString())
            return std::string(args.at(0).getString());
    }
    catch (...)
    {
    }
    return {};
}
} // namespace

LibMain::~LibMain()
{
    shuttingDown = true;
    destroyWindow();
}

void LibMain::Initialization()
{
    registerCallback("OnSongChanged");
    registerCallback("OnSongPartChanged");
    registerCallback("OnSetlistChanged");
}

void LibMain::OnClose()
{
    shuttingDown = true;
    unregisterCallback("OnSongChanged");
    unregisterCallback("OnSongPartChanged");
    unregisterCallback("OnSetlistChanged");
    destroyWindow();
}

std::string LibMain::GetProductDescription()
{
    return "<Library>"
           "<Product Name=\"Setlist Queue\" Version=\"1.1.1\" BuildDate=\"" __DATE__ "\"></Product>"
           "<Description>Janela flutuante com setlist, song parts, fila visual e cores por musica</Description>"
           "</Library>";
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

    std::string j = "{\"setlist\":\"" + jsonEscape(setlistName) +
                    "\",\"current\":" + std::to_string(current) +
                    ",\"currentPart\":" + std::to_string(currentPart) +
                    ",\"songs\":[";

    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            j += ',';

        j += "{\"i\":" + std::to_string(i) +
             ",\"n\":\"" + jsonEscape(getSongName(i)) + "\"" +
             ",\"parts\":[";

        int pCount = getSongpartCount(i);
        if (pCount < 0)
            pCount = 0;

        for (int p = 0; p < pCount; ++p)
        {
            if (p > 0)
                j += ',';
            j += "{\"i\":" + std::to_string(p) +
                 ",\"n\":\"" + jsonEscape(getSongpartName(i, p)) + "\"}";
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
        webview.reset();
        window.reset();
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
        webview = std::make_unique<choc::ui::WebView>();
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

    window = std::make_unique<choc::ui::DesktopWindow>(choc::ui::Bounds{120, 80, 460, 760});
    window->setWindowTitle("Setlist Queue");
    window->setResizable(true);
    window->setMinimumSize(280, 320);
    window->windowClosed = [this] {
closedByUser = true;
#ifdef _WIN32
if (window)
ShowWindow(static_cast(window->getWindowHandle()), SW_HIDE);
#endif
};
window->setContent(webview->getViewHandle());
webview->setHTML(std::string(reinterpret_cast<const char *>(kWebContent), kWebContentSize));
window->setVisible(true);
window->toFront();
#ifdef _WIN32
if (auto hwnd = static_cast(window->getWindowHandle()))
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
