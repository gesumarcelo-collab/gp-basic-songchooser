// Inclua SEMPRE o LibMain.h primeiro: ele ja traz os headers padrao,
// choc_Platform.h e a CHOC na ordem correta, antes do GP SDK.
#include "LibMain.h"
#include "webcontent.h" // gerado pelo CMake a partir de ui/index.html

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

int intArg(const choc::value::ValueView &args, int fallback)
{
    try
    {
        if (args.size() >= 1 && args[0].isString())
            return std::stoi(std::string(args[0].getString()));
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
        if (args.size() >= 1 && args[0].isString())
            return std::string(args[0].getString());
    }
    catch (...)
    {
    }
    return {};
}
} // namespace

// ---------------------------------------------------------------------------

LibMain::~LibMain()
{
    destroyWindow();
}

void LibMain::Initialization()
{
    registerCallback("OnSongChanged");
    registerCallback("OnSetlistChanged");
}

std::string LibMain::GetProductDescription()
{
    return "Setlist Queue: janela flutuante com o setlist, fila visual reordenavel e personalizacao por musica";
}

int LibMain::GetMenuCount()
{
    return 1;
}

std::string LibMain::GetMenuName(int)
{
    return "Setlist Queue (Fila de Musicas)";
}

void LibMain::InvokeMenu(int)
{
    openWindow();
}

void LibMain::OnSongChanged(int, int newIndex)
{
    pushState(newIndex);
}

void LibMain::OnSetlistChanged(const std::string &newSetlistName)
{
    setlistName = newSetlistName;
    pushState(kNoOverride);
}

std::string LibMain::buildStateJson(int currentOverride)
{
    int count = getSongCount();
    if (count < 0)
        count = 0;
    const int current = (currentOverride != kNoOverride) ? currentOverride : getCurrentSongIndex();

    std::string j = "{\"setlist\":\"" + jsonEscape(setlistName) + "\",\"current\":" + std::to_string(current) +
                    ",\"songs\":[";
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            j += ',';
        j += "{\"i\":" + std::to_string(i) + ",\"n\":\"" + jsonEscape(getSongName(i)) + "\"}";
    }
    j += "]}";
    return j;
}

void LibMain::pushState(int currentOverride)
{
    if (!webview || closedByUser)
        return;
    try
    {
        webview->evaluateJavascript("window.gpPush&&window.gpPush(" + buildStateJson(currentOverride) + ");");
    }
    catch (...)
    {
    }
}

void LibMain::destroyWindow()
{
    // Ordem: primeiro o webview, depois a janela
    webview.reset();
    window.reset();
    closedByUser = false;
}

void LibMain::openWindow()
{
    if (window && closedByUser)
        destroyWindow(); // recria depois de o usuario ter fechado

    if (window)
    {
        window->setVisible(true);
        window->toFront();
        return;
    }

    // Criar o WebView pode falhar se o WebView2 Runtime nao estiver instalado.
    // Em vez de loadedOK() (que nao existe em todas as versoes da CHOC), checamos
    // se o handle da view e valido; e protegemos com try/catch.
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
        OutputDebugStringA("SetlistQueue: WebView2 indisponivel (instale o WebView2 Runtime)\n");
#endif
        webview.reset();
        return;
    }

    // --- Funcoes C++ chamaveis pelo JavaScript (retornam Promise no JS) ---
    webview->bind("gp_getState", [this](const choc::value::ValueView &) -> choc::value::Value {
        return choc::value::createString(buildStateJson(kNoOverride));
    });

    webview->bind("gp_selectSong", [this](const choc::value::ValueView &args) -> choc::value::Value {
        const int idx = intArg(args, -1);
        if (idx >= 0 && idx < getSongCount())
            switchToSong(idx, 0); // indice REAL no setlist, primeira parte
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
        closedByUser = true; // a destruicao real acontece depois, fora deste callback
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
    // Janela flutuante: fica sempre por cima do Gig Performer
    if (auto hwnd = static_cast<HWND>(window->getWindowHandle()))
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
}

// ---------------------------------------------------------------------------

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
