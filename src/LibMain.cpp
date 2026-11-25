#include "LibMain.h"
#include "webcontent.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>

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
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
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

int intArgAt(const choc::value::ValueView &args, uint32_t index, int fallback)
{
    try
    {
        if (args.isArray() && args.size() > index)
        {
            const auto arg = args[index];
            if (arg.isInt32())
                return arg.getInt32();
            if (arg.isInt64())
                return static_cast<int>(arg.getInt64());
            if (arg.isFloat32())
                return static_cast<int>(arg.getFloat32());
            if (arg.isFloat64())
                return static_cast<int>(arg.getFloat64());
            if (arg.isString())
                return std::stoi(std::string(arg.getString()));
        }
    }
    catch (...)
    {
    }
    return fallback;
}

std::string stringArg(const choc::value::ValueView &args)
{
    try
    {
        if (args.isArray() && args.size() >= 1 && args[0].isString())
            return std::string(args[0].getString());
    }
    catch (...)
    {
    }
    return {};
}
} // namespace

LibMain::~LibMain()
{
    destroyWindow();
}

void LibMain::Initialization()
{
    registerCallback("OnSongChanged");
    registerCallback("OnSongPartChanged");
    registerCallback("OnSetlistChanged");
}

std::string LibMain::GetProductDescription()
{
    return "<Library>"
           "<Product Name=\"Setlist Queue\" Version=\"1.1.0\" BuildDate=\"" __DATE__ "\"></Product>"
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
    openWindow();
}

void LibMain::OnSongChanged(int, int newIndex)
{
    pushState(newIndex, kNoOverride);
}

void LibMain::OnSongPartChanged(int, int newPartIndex)
{
    pushState(kNoOverride, newPartIndex);
}

void LibMain::OnSetlistChanged(const std::string &newSetlistName)
{
    setlistName = newSetlistName;
    pushState(kNoOverride, kNoOverride);
}

std::string LibMain::buildStateJson(int currentSongOverride, int currentPartOverride)
{
    int count = getSongCount();
    if (count < 0)
        count = 0;
    const int current = (currentSongOverride != kNoOverride) ? currentSongOverride : getCurrentSongIndex();
    const int currentPart = (currentPartOverride != kNoOverride) ? currentPartOverride : getCurrentSongpartIndex();

    std::string j = "{\"setlist\":\"" + jsonEscape(setlistName) +
                    "\",\"current\":" + std::to_string(current) +
                    ",\"currentPart\":" + std::to_string(currentPart) +
                    ",\"songs\":[";
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            j += ',';
        j += "{\"i\":" + std::to_string(i) + ",\"n\":\"" + jsonEscape(getSongName(i)) + "\",\"parts\":[";
        int partCount = getSongpartCount(i);
        if (partCount < 0)
            partCount = 0;
        for (int p = 0; p < partCount; ++p)
        {
            if (p > 0)
                j += ',';
            j += "\"" + jsonEscape(getSongpartName(i, p)) + "\"";
        }
        j += "]}";
    }
    j += "]}";
    return j;
}

void LibMain::pushState(int currentSongOverride, int currentPartOverride)
{
    if (!webview || closedByUser)
        return;
    try
    {
        webview->evaluateJavascript("window.gpPush&&window.gpPush(" + buildStateJson(currentSongOverride, currentPartOverride) + ");");
    }
    catch (...)
    {
    }
}

void LibMain::destroyWindow()
{
    webview.reset();
    window.reset();
    closedByUser = false;
}

void LibMain::openWindow()
{
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
        return choc::value::createString(buildStateJson(kNoOverride, kNoOverride));
    });

    webview->bind("gp_selectSong", [this](const choc::value::ValueView &args) -> choc::value::Value {
        const int songIdx = intArgAt(args, 0, -1);
        const int partIdx = intArgAt(args, 1, 0);
        if (songIdx >= 0 && songIdx < getSongCount())
        {
            if (!inSetlistMode())
                switchToSetlistView();
            int maxParts = getSongpartCount(songIdx);
            int safePart = (partIdx >= 0 && partIdx < maxParts) ? partIdx : 0;
            switchToSong(songIdx, safePart);
        }
        return choc::value::createString("1");
    });

    webview->bind("gp_loadSettings", [](const choc::value::ValueView &) -> choc::value::Value {
        return choc::value::createString(readTextFile(settingsFile()));
    });

    webview->bind("gp_saveSettings", [](const choc::value::ValueView &args) -> choc::value::Value {
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
            ShowWindow(static_cast<HWND>(window->getWindowHandle()), SW_HIDE);
#endif
    };

    window->setContent(webview->getViewHandle());
    webview->setHTML(std::string(reinterpret_cast<const char *>(kWebContent), kWebContentSize));

    window->setVisible(true);
    window->toFront();

#ifdef _WIN32
    if (auto hwnd = static_cast<HWND>(window->getWindowHandle()))
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
