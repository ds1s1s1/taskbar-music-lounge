// ==WindhawkMod==
// @id              taskbar-music-lounge
// @name            Taskbar Music Lounge
// @description     Now-playing widget for the taskbar with media controls, a progress bar and Discord Rich Presence
// @version         0.2
// @author          Hashah2311, ds1s1s1
// @github          https://github.com/ds1s1s1
// @include         explorer.exe
// @compilerOptions -lole32 -ldwmapi -lgdi32 -luser32 -lwindowsapp -lshcore -lgdiplus -lshell32 -lcomdlg32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Music Lounge

Based on Taskbar Music Lounge by Hashah2311 (<https://github.com/Hashah2311>),
with Discord Rich Presence, a progress bar and per-player colors added.

A now-playing widget that sits on the taskbar. It reads from the Windows media
controls (SMTC), so it works with most players: Spotify, MusicBee, foobar2000,
Apple Music, and media playing in Chrome, Edge or Firefox.

## Features
- Album art, title and artist, with previous / play-pause / next buttons
- Long titles scroll instead of getting cut off
- Progress bar for the current track
- Accent color based on the player (Spotify, MusicBee, Apple Music, ...)
- Scroll over the widget to change the volume
- Drag it sideways to move it (set "Horizontal offset" to keep the position)
- Hides in fullscreen apps and after a while of nothing playing

## Discord status
The mod can also show the current song on your Discord profile.

1. Create an application at <https://discord.com/developers/applications>.
   Its name is what shows up after "Listening to".
2. Copy the Application ID from the General Information page.
3. Optional: upload an image under Rich Presence > Art Assets and put its name
   in "Discord large image key".
4. Turn on "Enable Discord presence" and paste the ID into "Discord Client ID".

Discord has to be running on the same PC. Album art isn't sent, since SMTC
doesn't give a URL that Discord can load.

If MusicBee doesn't show up, enable its Windows media controls integration in
MusicBee's preferences.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- PanelWidth: 360
  $name: Panel width
  $description: Width of the widget in pixels.
- PanelHeight: 44
  $name: Panel height
  $description: Height of the widget in pixels (usually match your taskbar height area).
- FontSize: 12
  $name: Font size
  $description: Size of the title / artist text in pixels.
- ButtonScale: "1.15"
  $name: Button scale
  $description: Scale factor for the playback control icons (0.5 - 4.0).
- OffsetX: 380
  $name: Horizontal offset
  $description: Distance in pixels from the left edge of the taskbar. You can also drag the widget.
- OffsetY: 0
  $name: Vertical offset
  $description: Fine-tune the vertical position in pixels.
- AutoTheme: true
  $name: Follow system theme
  $description: Automatically pick text color and background tint from the Windows light / dark theme.
- TextColor: "FFFFFF"
  $name: Manual text color (hex RRGGBB)
  $description: Used only when "Follow system theme" is off.
- BgOpacity: 140
  $name: Background opacity
  $description: Opacity of the acrylic background tint (0 - 255). Used only when "Follow system theme" is off.
- HideFullscreen: true
  $name: Hide in games / fullscreen
  $description: Hide the widget while a fullscreen app, game or presentation is active.
- IdleTimeout: 15
  $name: Idle hide timeout (seconds)
  $description: Hide the widget after this many seconds while nothing is playing. Set to 0 to always show.
- PreferredPlayer: ""
  $name: Preferred player
  $description: Optional. A text fragment of an app to always prefer (e.g. "spotify" or "musicbee"). Leave empty to follow whatever is currently playing.
- DiscordEnabled: false
  $name: Enable Discord presence
  $description: Show the currently playing track on your Discord profile (requires the Client ID below and a running Discord app).
- DiscordClientId: ""
  $name: Discord Client ID
  $description: The Application (Client) ID from your Discord Developer Portal application. See the mod description for setup steps.
- DiscordActivityType: listening
  $name: Discord activity label
  $description: How the status reads on Discord.
  $options:
  - listening: Listening to
  - playing: Playing
- DiscordLargeImage: ""
  $name: Discord large image key
  $description: Optional. The name of an uploaded Rich Presence art asset to show next to the track.
- DiscordShowWhenPaused: false
  $name: Keep Discord status while paused
  $description: If off, your Discord status is cleared when playback is paused or stopped.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shcore.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstdint>
#include <timeapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace Gdiplus;
using namespace std;
using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

const WCHAR* FONT_NAME = L"Segoe UI Variable Display";

constexpr UINT kAnimationIntervalMs = 16;
constexpr int kPanelPadding = 8;
constexpr int kAlbumCornerRadius = 12;
constexpr int kRepeatedTextGap = 60;
constexpr DWORD kDefaultAccent = 0xFF1DB954;  // fallback accent color

typedef enum _WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 } WINDOWCOMPOSITIONATTRIB;
typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_INVALID_STATE = 5
} ACCENT_STATE;
typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;
typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attribute;
    PVOID Data;
    SIZE_T SizeOfData;
} WINDOWCOMPOSITIONATTRIBDATA;
typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

enum ZBID {
    ZBID_DEFAULT = 0,
    ZBID_DESKTOP = 1,
    ZBID_UIACCESS = 2,
    ZBID_IMMERSIVE_IHM = 3,
    ZBID_IMMERSIVE_NOTIFICATION = 4,
    ZBID_IMMERSIVE_APPCHROME = 5,
    ZBID_IMMERSIVE_MOGO = 6,
    ZBID_IMMERSIVE_EDGY = 7,
    ZBID_IMMERSIVE_INACTIVEMOBODY = 8,
    ZBID_IMMERSIVE_INACTIVEDOCK = 9,
    ZBID_IMMERSIVE_ACTIVEMOBODY = 10,
    ZBID_IMMERSIVE_ACTIVEDOCK = 11,
    ZBID_IMMERSIVE_BACKGROUND = 12,
    ZBID_IMMERSIVE_SEARCH = 13,
    ZBID_GENUINE_WINDOWS = 14,
    ZBID_IMMERSIVE_RESTRICTED = 15,
    ZBID_SYSTEM_TOOLS = 16,
    ZBID_LOCK = 17,
    ZBID_ABOVELOCK_UX = 18,
};

typedef HWND(WINAPI* pCreateWindowInBand)(
    DWORD dwExStyle,
    LPCWSTR lpClassName,
    LPCWSTR lpWindowName,
    DWORD dwStyle,
    int x,
    int y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam,
    DWORD dwBand
);

struct ModSettings {
    int width = 360;
    int height = 44;
    int fontSize = 12;
    double buttonScale = 1.15;
    bool hideFullscreen = true;
    int idleTimeout = 15;
    int offsetX = 380;
    int offsetY = 0;
    bool autoTheme = true;
    DWORD manualTextColor = 0xFFFFFFFF;
    int bgOpacity = 140;
    wstring preferredPlayer;
} g_Settings;

HWND g_hMediaWindow = NULL;
bool g_Running = true;
int g_HoverState = 0;
HWINEVENTHOOK g_TaskbarHook = nullptr;
UINT g_TaskbarCreatedMsg = RegisterWindowMessage(L"TaskbarCreated");

int g_IdleSecondsCounter = 0;
bool g_IsHiddenByIdle = false;

struct MediaState {
    wstring title = L"Waiting for media...";
    wstring artist = L"";
    wstring player = L"";
    bool isPlaying = false;
    bool hasMedia = false;
    Bitmap* albumArt = nullptr;
    Color primaryColor = Color(0, 0, 0);
    Color accentColor = Color(kDefaultAccent);
    double progress = 0.0;
    bool hasProgress = false;
    mutex lock;
} g_MediaState;

int g_ScrollOffset = 0;
int g_TextWidth = 0;
bool g_IsScrolling = false;

float g_ScrollOffsetF = 0.0f;
float g_ScrollWaitF = 45.0f;
LARGE_INTEGER g_LastTime { 0 }, g_Freq { 0 };

bool g_IsDragging = false;
POINT g_DragStartPos { 0, 0 };
int g_DragStartOffsetX = 0;

GlobalSystemMediaTransportControlsSessionManager g_SessionManager = nullptr;

// ---------------------------------------------------------------------------
// Player identification (friendly name + accent color per app)
// ---------------------------------------------------------------------------

struct PlayerInfo {
    wstring name;
    DWORD color;  // ARGB
};

static wstring ToLower(wstring s) {
    for (auto& ch : s) ch = towlower(ch);
    return s;
}

static wstring SessionSourceLower(GlobalSystemMediaTransportControlsSession const& session) {
    if (!session) return L"";
    try {
        auto sourceId = session.SourceAppUserModelId();
        if (sourceId.empty()) return L"";
        return ToLower(wstring(sourceId.c_str()));
    } catch (...) {
        return L"";
    }
}

static PlayerInfo IdentifyPlayer(wstring const& srcLower) {
    auto has = [&](const wchar_t* needle) { return srcLower.find(needle) != wstring::npos; };

    if (has(L"spotify"))                           return { L"Spotify",      0xFF1DB954 };
    if (has(L"musicbee"))                          return { L"MusicBee",     0xFFF5A623 };
    if (has(L"itunes") || has(L"apple"))           return { L"Apple Music",  0xFFFC3C44 };
    if (has(L"foobar"))                            return { L"foobar2000",   0xFF2E7DD1 };
    if (has(L"vlc"))                               return { L"VLC",          0xFFFF8800 };
    if (has(L"aimp"))                              return { L"AIMP",         0xFF1E88E5 };
    if (has(L"deezer"))                            return { L"Deezer",       0xFFA238FF };
    if (has(L"tidal"))                             return { L"TIDAL",        0xFF000000 };
    if (has(L"chrome"))                            return { L"Chrome",       0xFF4285F4 };
    if (has(L"msedge") || has(L"edge"))            return { L"Edge",         0xFF0C7BD6 };
    if (has(L"firefox") || has(L"mozilla"))        return { L"Firefox",      0xFFFF6611 };
    if (has(L"zune") || has(L"media.player"))      return { L"Media Player", 0xFF5B8DEF };

    return { L"Media", kDefaultAccent };
}

// ===========================================================================
// Discord Rich Presence bridge (named-pipe IPC, no external dependency)
// ===========================================================================

namespace Discord {

std::atomic<bool> g_enabled{ false };
std::atomic<bool> g_showWhenPaused{ false };
std::atomic<bool> g_settingsDirty{ false };
std::atomic<int>  g_activityType{ 2 };  // 0 = Playing, 2 = Listening
std::mutex        g_cfgMutex;
std::string       g_clientId;       // ASCII digits
std::string       g_largeImageKey;  // asset name

HANDLE g_pipe = INVALID_HANDLE_VALUE;
std::thread* g_thread = nullptr;

// Desired presence, written by the media poll, read by the Discord thread.
struct Desired {
    std::mutex m;
    bool hasMedia = false;
    bool isPlaying = false;
    std::wstring title, artist, player;
    long long startMs = 0, endMs = 0;
    uint64_t version = 0;  // bumps only on meaningful change
} g_desired;

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring TrimW(std::wstring s, size_t maxLen) {
    if (s.size() > maxLen) s.resize(maxLen);
    return s;
}

static std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    sprintf_s(b, "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o;
}

static std::string JsonStr(const std::wstring& w, size_t maxLen) {
    std::wstring t = TrimW(w, maxLen);
    // Discord requires fields to be at least 2 characters.
    if (t.size() == 1) t += L' ';
    return JsonEscape(WideToUtf8(t));
}

static bool WriteFrame(HANDLE pipe, int op, const std::string& payload) {
    if (pipe == INVALID_HANDLE_VALUE) return false;
    uint32_t len = (uint32_t)payload.size();
    std::string buf;
    buf.resize(8 + payload.size());
    memcpy(&buf[0], &op, 4);
    memcpy(&buf[4], &len, 4);
    if (!payload.empty()) memcpy(&buf[8], payload.data(), payload.size());

    DWORD written = 0;
    return WriteFile(pipe, buf.data(), (DWORD)buf.size(), &written, nullptr) &&
           written == buf.size();
}

// Consume anything Discord sent back. Returns false if the pipe is broken.
static bool DrainReads(HANDLE pipe) {
    DWORD avail = 0;
    while (true) {
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail == 0) return true;
        char tmp[1024];
        DWORD toRead = avail < sizeof(tmp) ? avail : (DWORD)sizeof(tmp);
        DWORD rd = 0;
        if (!ReadFile(pipe, tmp, toRead, &rd, nullptr) || rd == 0) return false;
    }
}

static HANDLE TryConnect(const std::string& clientId) {
    if (clientId.empty()) return INVALID_HANDLE_VALUE;
    for (int i = 0; i < 10; i++) {
        wchar_t path[64];
        swprintf_s(path, L"\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        std::string hs = "{\"v\":1,\"client_id\":\"" + clientId + "\"}";
        if (WriteFrame(h, 0 /*HANDSHAKE*/, hs)) {
            Sleep(50);
            DrainReads(h);
            Wh_Log(L"Discord: connected on discord-ipc-%d", i);
            return h;
        }
        CloseHandle(h);
    }
    return INVALID_HANDLE_VALUE;
}

static std::string NextNonce() {
    static std::atomic<unsigned> counter{ 0 };
    unsigned c = counter.fetch_add(1);
    char b[48];
    sprintf_s(b, "%lu-%u", (unsigned long)GetTickCount(), c);
    return b;
}

static std::string BuildActivity(const Desired& d, const std::string& largeImg, int type) {
    std::string details = JsonStr(d.title, 120);
    std::string state   = JsonStr(d.artist, 120);
    std::string largeTx = JsonStr(d.player, 120);

    std::string a = "{";
    a += "\"type\":" + std::to_string(type);
    if (!details.empty()) a += ",\"details\":\"" + details + "\"";
    if (!state.empty())   a += ",\"state\":\""   + state   + "\"";

    if (d.isPlaying && d.endMs > d.startMs && d.startMs > 0) {
        a += ",\"timestamps\":{\"start\":" + std::to_string(d.startMs) +
             ",\"end\":" + std::to_string(d.endMs) + "}";
    }

    std::string assets;
    if (!largeImg.empty())
        assets += "\"large_image\":\"" + JsonEscape(largeImg) + "\"";
    if (!largeTx.empty()) {
        if (!assets.empty()) assets += ",";
        assets += "\"large_text\":\"" + largeTx + "\"";
    }
    if (!assets.empty()) a += ",\"assets\":{" + assets + "}";

    a += "}";
    return a;
}

static bool SendActivity(HANDLE pipe, const std::string& activityJson) {
    std::string payload =
        "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + NextNonce() +
        "\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
        ",\"activity\":" + activityJson + "}}";
    return WriteFrame(pipe, 1 /*FRAME*/, payload);
}

static void ClosePipe() {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

void Thread() {
    uint64_t lastSentVersion = UINT64_MAX;
    bool lastWasCleared = false;
    DWORD lastConnectAttempt = 0;

    while (g_Running) {
        if (!g_enabled.load()) {
            if (g_pipe != INVALID_HANDLE_VALUE) { ClosePipe(); }
            Sleep(500);
            continue;
        }

        // Re-read config if settings changed (e.g. client id edited).
        if (g_settingsDirty.exchange(false)) {
            ClosePipe();
            lastSentVersion = UINT64_MAX;
            lastWasCleared = false;
        }

        std::string clientId, largeImg;
        {
            std::lock_guard<std::mutex> g(g_cfgMutex);
            clientId = g_clientId;
            largeImg = g_largeImageKey;
        }
        int type = g_activityType.load();
        bool showPaused = g_showWhenPaused.load();

        if (g_pipe == INVALID_HANDLE_VALUE) {
            DWORD now = GetTickCount();
            if (now - lastConnectAttempt < 10000) { Sleep(500); continue; }
            lastConnectAttempt = now;
            g_pipe = TryConnect(clientId);
            if (g_pipe == INVALID_HANDLE_VALUE) { Sleep(500); continue; }
            lastSentVersion = UINT64_MAX;
            lastWasCleared = false;
        }

        // Snapshot desired presence.
        Desired snap;
        {
            std::lock_guard<std::mutex> g(g_desired.m);
            snap.hasMedia = g_desired.hasMedia;
            snap.isPlaying = g_desired.isPlaying;
            snap.title = g_desired.title;
            snap.artist = g_desired.artist;
            snap.player = g_desired.player;
            snap.startMs = g_desired.startMs;
            snap.endMs = g_desired.endMs;
            snap.version = g_desired.version;
        }

        bool shouldShow = snap.hasMedia && (snap.isPlaying || showPaused);
        bool ok = true;

        if (shouldShow) {
            if (snap.version != lastSentVersion) {
                ok = SendActivity(g_pipe, BuildActivity(snap, largeImg, type));
                if (ok) { lastSentVersion = snap.version; lastWasCleared = false; }
            }
        } else {
            if (!lastWasCleared) {
                ok = SendActivity(g_pipe, "null");
                if (ok) { lastWasCleared = true; lastSentVersion = UINT64_MAX; }
            }
        }

        if (!ok || !DrainReads(g_pipe)) {
            ClosePipe();
            lastSentVersion = UINT64_MAX;
            lastWasCleared = false;
        }

        Sleep(1000);
    }

    if (g_pipe != INVALID_HANDLE_VALUE) {
        SendActivity(g_pipe, "null");
        ClosePipe();
    }
}

void Push(bool hasMedia, bool isPlaying, const std::wstring& title,
          const std::wstring& artist, const std::wstring& player,
          long long startMs, long long endMs) {
    std::lock_guard<std::mutex> g(g_desired.m);
    bool changed =
        g_desired.hasMedia != hasMedia ||
        g_desired.isPlaying != isPlaying ||
        g_desired.title != title ||
        g_desired.artist != artist ||
        g_desired.player != player;

    g_desired.hasMedia = hasMedia;
    g_desired.isPlaying = isPlaying;
    g_desired.title = title;
    g_desired.artist = artist;
    g_desired.player = player;
    g_desired.startMs = startMs;
    g_desired.endMs = endMs;
    if (changed) g_desired.version++;
}

}  // namespace Discord

// ---------------------------------------------------------------------------

static bool SessionIsPlaying(GlobalSystemMediaTransportControlsSession const& s) {
    if (!s) return false;
    try {
        auto pb = s.GetPlaybackInfo();
        return pb && pb.PlaybackStatus() ==
                         GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    } catch (...) {
        return false;
    }
}

static bool MatchesPreferred(GlobalSystemMediaTransportControlsSession const& s) {
    if (g_Settings.preferredPlayer.empty()) return false;
    return SessionSourceLower(s).find(ToLower(g_Settings.preferredPlayer)) != wstring::npos;
}

GlobalSystemMediaTransportControlsSession SelectPreferredSession() {
    if (!g_SessionManager) return nullptr;

    auto sessions = g_SessionManager.GetSessions();

    // 1) Preferred player that is currently playing.
    if (!g_Settings.preferredPlayer.empty()) {
        for (auto const& s : sessions)
            if (MatchesPreferred(s) && SessionIsPlaying(s)) return s;
    }

    // 2) The system "current" session, if it is playing.
    auto current = g_SessionManager.GetCurrentSession();
    if (current && SessionIsPlaying(current)) return current;

    // 3) Any session that is playing.
    for (auto const& s : sessions)
        if (SessionIsPlaying(s)) return s;

    // 4) Preferred player, even if paused.
    if (!g_Settings.preferredPlayer.empty()) {
        for (auto const& s : sessions)
            if (MatchesPreferred(s)) return s;
    }

    // 5) Fall back to whatever the system thinks is current, else the first.
    if (current) return current;
    for (auto const& s : sessions) return s;

    return nullptr;
}

Bitmap* StreamToBitmap(IRandomAccessStreamWithContentType const& stream) {
    if (!stream) return nullptr;

    IStream* nativeStream = nullptr;
    if (SUCCEEDED(CreateStreamOverRandomAccessStream(reinterpret_cast<IUnknown*>(winrt::get_abi(stream)), IID_PPV_ARGS(&nativeStream)))) {
        Bitmap* bmp = Bitmap::FromStream(nativeStream);
        nativeStream->Release();

        if (bmp && bmp->GetLastStatus() == Ok) return bmp;
        delete bmp;
    }

    return nullptr;
}

Color CalculateAverageColor(Bitmap* bmp) {
    if (!bmp) return Color(0, 0, 0);
    UINT width = bmp->GetWidth();
    UINT height = bmp->GetHeight();
    if (width == 0 || height == 0) return Color(0, 0, 0);

    BitmapData bitmapData;
    Rect rect(0, 0, width, height);
    if (bmp->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Ok) {
        return Color(0, 0, 0);
    }

    long r = 0, g = 0, b = 0;
    int count = 0;
    int stride = bitmapData.Stride;
    UINT8* scan0 = (UINT8*)bitmapData.Scan0;

    for (UINT y = 0; y < height; y += 5) {
        UINT8* row = scan0 + (y * stride);
        for (UINT x = 0; x < width; x += 5) {
            UINT8 b_val = row[x * 4];
            UINT8 g_val = row[x * 4 + 1];
            UINT8 r_val = row[x * 4 + 2];
            r += r_val;
            g += g_val;
            b += b_val;
            count++;
        }
    }
    bmp->UnlockBits(&bitmapData);

    if (count == 0) return Color(0, 0, 0);
    return Color(255, r / count, g / count, b / count);
}

void LoadSettings() {
    g_Settings.width = Wh_GetIntSetting(L"PanelWidth");
    g_Settings.height = Wh_GetIntSetting(L"PanelHeight");
    g_Settings.fontSize = Wh_GetIntSetting(L"FontSize");
    g_Settings.offsetX = Wh_GetIntSetting(L"OffsetX");
    g_Settings.offsetY = Wh_GetIntSetting(L"OffsetY");
    g_Settings.autoTheme = Wh_GetIntSetting(L"AutoTheme") != 0;

    PCWSTR scaleStr = Wh_GetStringSetting(L"ButtonScale");
    if (scaleStr) {
        g_Settings.buttonScale = _wtof(scaleStr);
        Wh_FreeStringSetting(scaleStr);
    } else {
        g_Settings.buttonScale = 1.15;
    }

    if (g_Settings.buttonScale < 0.5) g_Settings.buttonScale = 0.5;
    if (g_Settings.buttonScale > 4.0) g_Settings.buttonScale = 4.0;

    g_Settings.hideFullscreen = Wh_GetIntSetting(L"HideFullscreen") != 0;
    g_Settings.idleTimeout = Wh_GetIntSetting(L"IdleTimeout");

    PCWSTR textHex = Wh_GetStringSetting(L"TextColor");
    DWORD textRGB = 0xFFFFFF;
    if (textHex) {
        if (wcslen(textHex) > 0) textRGB = wcstoul(textHex, nullptr, 16);
        Wh_FreeStringSetting(textHex);
    }
    g_Settings.manualTextColor = 0xFF000000 | textRGB;

    g_Settings.bgOpacity = Wh_GetIntSetting(L"BgOpacity");
    if (g_Settings.bgOpacity < 0) g_Settings.bgOpacity = 0;
    if (g_Settings.bgOpacity > 255) g_Settings.bgOpacity = 255;

    PCWSTR pref = Wh_GetStringSetting(L"PreferredPlayer");
    g_Settings.preferredPlayer = pref ? pref : L"";
    if (pref) Wh_FreeStringSetting(pref);

    if (g_Settings.width < 100) g_Settings.width = 360;
    if (g_Settings.height < 24) g_Settings.height = 44;
    if (g_Settings.fontSize < 8) g_Settings.fontSize = 12;

    // ----- Discord settings -----
    Discord::g_enabled.store(Wh_GetIntSetting(L"DiscordEnabled") != 0);
    Discord::g_showWhenPaused.store(Wh_GetIntSetting(L"DiscordShowWhenPaused") != 0);

    {
        std::lock_guard<std::mutex> g(Discord::g_cfgMutex);

        PCWSTR cid = Wh_GetStringSetting(L"DiscordClientId");
        Discord::g_clientId.clear();
        if (cid) {
            for (PCWSTR p = cid; *p; ++p)
                if (*p >= L'0' && *p <= L'9') Discord::g_clientId += (char)*p;
            Wh_FreeStringSetting(cid);
        }

        PCWSTR img = Wh_GetStringSetting(L"DiscordLargeImage");
        Discord::g_largeImageKey = img ? Discord::WideToUtf8(img) : std::string();
        if (img) Wh_FreeStringSetting(img);
    }

    PCWSTR typeStr = Wh_GetStringSetting(L"DiscordActivityType");
    Discord::g_activityType.store((typeStr && wcscmp(typeStr, L"playing") == 0) ? 0 : 2);
    if (typeStr) Wh_FreeStringSetting(typeStr);

    Discord::g_settingsDirty.store(true);
}

static long long NowUnixMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

void UpdateMediaInfo() {
    try {
        if (!g_SessionManager) {
            g_SessionManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        }

        if (!g_SessionManager) return;

        auto session = SelectPreferredSession();

        if (session) {
            auto props = session.TryGetMediaPropertiesAsync().get();
            auto info = session.GetPlaybackInfo();

            wstring newTitle = props.Title().c_str();
            wstring newArtist = props.Artist().c_str();

            PlayerInfo pinfo = IdentifyPlayer(SessionSourceLower(session));

            // Timeline / progress.
            double progress = 0.0;
            bool hasProgress = false;
            long long startMs = 0, endMs = 0;
            try {
                auto tl = session.GetTimelineProperties();
                long long posTicks = tl.Position().count();
                long long startTicks = tl.StartTime().count();
                long long endTicks = tl.EndTime().count();
                long long durTicks = endTicks - startTicks;
                if (durTicks > 0) {
                    long long elapsed = posTicks - startTicks;
                    if (elapsed < 0) elapsed = 0;
                    if (elapsed > durTicks) elapsed = durTicks;
                    progress = (double)elapsed / (double)durTicks;
                    hasProgress = true;

                    long long now = NowUnixMs();
                    startMs = now - elapsed / 10000;      // ticks(100ns) -> ms
                    endMs = startMs + durTicks / 10000;
                }
            } catch (...) {
            }

            bool isPlaying =
                (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

            {
                lock_guard<mutex> guard(g_MediaState.lock);

                bool shouldReloadArt =
                    !g_MediaState.albumArt ||
                    g_MediaState.title != newTitle ||
                    g_MediaState.artist != newArtist;

                if (shouldReloadArt) {
                    if (g_MediaState.albumArt) {
                        delete g_MediaState.albumArt;
                        g_MediaState.albumArt = nullptr;
                    }

                    auto thumbRef = props.Thumbnail();
                    if (thumbRef) {
                        auto stream = thumbRef.OpenReadAsync().get();
                        g_MediaState.albumArt = StreamToBitmap(stream);
                        if (g_MediaState.albumArt) {
                            g_MediaState.primaryColor = CalculateAverageColor(g_MediaState.albumArt);
                        }
                    }
                }

                g_MediaState.title = newTitle.empty() ? pinfo.name : newTitle;
                g_MediaState.artist = newArtist;
                g_MediaState.player = pinfo.name;
                g_MediaState.accentColor = Color(pinfo.color);
                g_MediaState.isPlaying = isPlaying;
                g_MediaState.hasMedia = true;
                g_MediaState.progress = progress;
                g_MediaState.hasProgress = hasProgress;
            }

            Discord::Push(true, isPlaying,
                          newTitle.empty() ? pinfo.name : newTitle,
                          newArtist, pinfo.name, startMs, endMs);
        } else {
            lock_guard<mutex> guard(g_MediaState.lock);
            g_MediaState.hasMedia = false;
            g_MediaState.title = L"No Media";
            g_MediaState.artist = L"";
            g_MediaState.player = L"";
            g_MediaState.isPlaying = false;
            g_MediaState.hasProgress = false;

            if (g_MediaState.albumArt) {
                delete g_MediaState.albumArt;
                g_MediaState.albumArt = nullptr;
            }

            Discord::Push(false, false, L"", L"", L"", 0, 0);
        }
    } catch (...) {
        lock_guard<mutex> guard(g_MediaState.lock);
        g_MediaState.hasMedia = false;
        g_MediaState.title = L"No Media";
        g_MediaState.artist = L"";
        g_MediaState.player = L"";
        g_MediaState.isPlaying = false;
        g_MediaState.hasProgress = false;

        if (g_MediaState.albumArt) {
            delete g_MediaState.albumArt;
            g_MediaState.albumArt = nullptr;
        }

        Discord::Push(false, false, L"", L"", L"", 0, 0);
    }
}

void SendMediaCommand(int cmd) {
    try {
        if (!g_SessionManager) return;

        auto session = SelectPreferredSession();
        if (!session) return;

        if (cmd == 1) session.TrySkipPreviousAsync();
        else if (cmd == 2) session.TryTogglePlayPauseAsync();
        else if (cmd == 3) session.TrySkipNextAsync();
    } catch (...) {
    }
}

bool IsSystemLightMode() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"SystemUsesLightTheme",
            RRF_RT_DWORD,
            nullptr,
            &value,
            &size
        ) == ERROR_SUCCESS) {
        return value != 0;
    }

    return false;
}

DWORD GetCurrentTextColor() {
    if (g_Settings.autoTheme) return IsSystemLightMode() ? 0xFF000000 : 0xFFFFFFFF;
    return g_Settings.manualTextColor;
}

void UpdateAppearance(HWND hwnd) {
    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

    HMODULE hUser = GetModuleHandle(L"user32.dll");
    if (hUser) {
        auto SetComp = (pSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
        if (SetComp) {
            DWORD tint = 0;
            if (g_Settings.autoTheme) {
                tint = IsSystemLightMode() ? 0x40FFFFFF : 0x40000000;
            } else {
                tint = (g_Settings.bgOpacity << 24) | 0xFFFFFF;
            }

            ACCENT_POLICY policy = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 0, tint, 0 };
            WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(ACCENT_POLICY) };
            SetComp(hwnd, &data);
        }
    }
}

void AddRoundedRect(GraphicsPath& path, int x, int y, int w, int h, int r) {
    int d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;

    path.Reset();
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

void DrawMediaPanel(HDC hdc, int width, int height) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    graphics.Clear(Color(0, 0, 0, 0));

    Color mainColor{ GetCurrentTextColor() };

    MediaState state;
    {
        lock_guard<mutex> guard(g_MediaState.lock);
        state.title = g_MediaState.title;
        state.artist = g_MediaState.artist;
        state.player = g_MediaState.player;
        state.albumArt = g_MediaState.albumArt ? g_MediaState.albumArt->Clone() : nullptr;
        state.hasMedia = g_MediaState.hasMedia;
        state.isPlaying = g_MediaState.isPlaying;
        state.primaryColor = g_MediaState.primaryColor;
        state.accentColor = g_MediaState.accentColor;
        state.progress = g_MediaState.progress;
        state.hasProgress = g_MediaState.hasProgress;
    }

    Color accent = state.accentColor;

    if (!state.hasMedia) {
        if (state.albumArt) {
            delete state.albumArt;
            state.albumArt = nullptr;
        }
        return;
    }

    GraphicsPath panelPath;
    AddRoundedRect(panelPath, 0, 0, width, height, 12);

    if (state.primaryColor.GetAlpha() != 0) {
        Color c1(max(20, g_Settings.bgOpacity / 3), state.primaryColor.GetRed(), state.primaryColor.GetGreen(), state.primaryColor.GetBlue());
        Color c2(0, state.primaryColor.GetRed(), state.primaryColor.GetGreen(), state.primaryColor.GetBlue());
        LinearGradientBrush gradBrush(Rect(0, 0, width, height), c1, c2, LinearGradientModeHorizontal);
        graphics.FillPath(&gradBrush, &panelPath);
    }

    const int artSize = max(0, height - (kPanelPadding * 2));
    const int artX = kPanelPadding;
    const int artY = kPanelPadding;

    GraphicsPath albumPath;
    AddRoundedRect(albumPath, artX, artY, artSize, artSize, kAlbumCornerRadius);

    if (state.albumArt) {
        GraphicsState clipState = graphics.Save();
        graphics.SetClip(&albumPath);
        graphics.DrawImage(state.albumArt, artX, artY, artSize, artSize);
        graphics.Restore(clipState);

        Pen albumBorder(Color(48, 255, 255, 255), 1.0f);
        graphics.DrawPath(&albumBorder, &albumPath);

        delete state.albumArt;
        state.albumArt = nullptr;
    } else {
        SolidBrush placeBrush(Color(60, 128, 128, 128));
        graphics.FillPath(&placeBrush, &albumPath);
    }

    const double scale = g_Settings.buttonScale;
    const int startControlX = artX + artSize + static_cast<int>(14 * scale);
    const int controlY = height / 2;

    SolidBrush iconBrush(mainColor);
    SolidBrush hoverBrush(state.isPlaying ? accent : mainColor);
    SolidBrush activeBg(Color(
        38,
        state.isPlaying ? accent.GetRed() : mainColor.GetRed(),
        state.isPlaying ? accent.GetGreen() : mainColor.GetGreen(),
        state.isPlaying ? accent.GetBlue() : mainColor.GetBlue()
    ));

    const float circleR = 13.0f * static_cast<float>(scale);
    const float iconW = 8.0f * static_cast<float>(scale);
    const float iconH = 12.0f * static_cast<float>(scale);
    const float gap = 30.0f * static_cast<float>(scale);

    float pX = static_cast<float>(startControlX);
    if (g_HoverState == 1) {
        graphics.FillEllipse(&activeBg, pX - circleR, static_cast<float>(controlY) - circleR, circleR * 2, circleR * 2);
    }
    PointF prevPts[3] = {
        PointF(pX + iconW, static_cast<float>(controlY) - (iconH / 2)),
        PointF(pX + iconW, static_cast<float>(controlY) + (iconH / 2)),
        PointF(pX, static_cast<float>(controlY))
    };
    graphics.FillPolygon(g_HoverState == 1 ? &hoverBrush : &iconBrush, prevPts, 3);
    graphics.FillRectangle(g_HoverState == 1 ? &hoverBrush : &iconBrush, pX, static_cast<float>(controlY) - (iconH / 2), 2.0f * static_cast<float>(scale), iconH);

    float plX = pX + gap;
    float playCircleR = circleR + 1.5f * static_cast<float>(scale);
    if (g_HoverState == 2) {
        graphics.FillEllipse(&activeBg, plX - playCircleR, static_cast<float>(controlY) - playCircleR, playCircleR * 2, playCircleR * 2);
    }
    if (state.isPlaying) {
        float barW = 3.0f * static_cast<float>(scale);
        float barH = 14.0f * static_cast<float>(scale);
        graphics.FillRectangle(g_HoverState == 2 ? &hoverBrush : &iconBrush, plX - (barW + 1), static_cast<float>(controlY) - (barH / 2), barW, barH);
        graphics.FillRectangle(g_HoverState == 2 ? &hoverBrush : &iconBrush, plX + 1, static_cast<float>(controlY) - (barH / 2), barW, barH);
    } else {
        float playW = 10.0f * static_cast<float>(scale);
        float playH = 16.0f * static_cast<float>(scale);
        PointF playPts[3] = {
            PointF(plX - (playW / 2), static_cast<float>(controlY) - (playH / 2)),
            PointF(plX - (playW / 2), static_cast<float>(controlY) + (playH / 2)),
            PointF(plX + (playW / 2), static_cast<float>(controlY))
        };
        graphics.FillPolygon(g_HoverState == 2 ? &hoverBrush : &iconBrush, playPts, 3);
    }

    float nX = plX + gap;
    if (g_HoverState == 3) {
        graphics.FillEllipse(&activeBg, nX - circleR, static_cast<float>(controlY) - circleR, circleR * 2, circleR * 2);
    }
    PointF nextPts[3] = {
        PointF(nX - iconW, static_cast<float>(controlY) - (iconH / 2)),
        PointF(nX - iconW, static_cast<float>(controlY) + (iconH / 2)),
        PointF(nX, static_cast<float>(controlY))
    };
    graphics.FillPolygon(g_HoverState == 3 ? &hoverBrush : &iconBrush, nextPts, 3);
    graphics.FillRectangle(g_HoverState == 3 ? &hoverBrush : &iconBrush, nX, static_cast<float>(controlY) - (iconH / 2), 2.0f * static_cast<float>(scale), iconH);

    int textX = static_cast<int>(nX + (18 * scale));
    int textMaxW = max(0, width - textX - 12);

    wstring fullText = state.title;
    if (!state.artist.empty()) {
        fullText += L" • ";
        fullText += state.artist;
    }

    FontFamily fontFamily(FONT_NAME, nullptr);
    Font font(&fontFamily, static_cast<REAL>(g_Settings.fontSize), FontStyleBold, UnitPixel);
    SolidBrush textBrush(mainColor);

    RectF boundRect;
    graphics.MeasureString(fullText.c_str(), -1, &font, RectF(0, 0, 5000, 100), &boundRect);
    g_TextWidth = static_cast<int>(boundRect.Width);

    GraphicsState textState = graphics.Save();
    Region textClip(Rect(textX, 0, textMaxW, height));
    graphics.SetClip(&textClip);

    // Leave room for the progress bar at the bottom when it is shown.
    const float textBlockH = state.hasProgress ? (height - 6.0f) : (float)height;
    const float textY = (textBlockH - boundRect.Height) / 2.0f;

    if (g_TextWidth > textMaxW && textMaxW > 0) {
        g_IsScrolling = true;
        float drawX = static_cast<float>(textX - g_ScrollOffset);
        graphics.DrawString(fullText.c_str(), -1, &font, PointF(drawX, textY), &textBrush);

        if (drawX + g_TextWidth < textX + textMaxW) {
            graphics.DrawString(
                fullText.c_str(),
                -1,
                &font,
                PointF(drawX + g_TextWidth + kRepeatedTextGap, textY),
                &textBrush
            );
        }
    } else {
        if (g_IsScrolling) {
            g_IsScrolling = false;
            g_ScrollOffset = 0;
            g_ScrollOffsetF = 0.0f;
            g_ScrollWaitF = 45.0f;
            g_LastTime.QuadPart = 0;
        }
        graphics.DrawString(fullText.c_str(), -1, &font, PointF(static_cast<float>(textX), textY), &textBrush);
    }

    graphics.Restore(textState);

    // Progress bar (thin seek line) along the bottom of the text area.
    if (state.hasProgress) {
        float by = (float)height - 5.0f;
        float bx0 = (float)textX;
        float bx1 = (float)(width - 12);
        if (bx1 > bx0) {
            float bw = bx1 - bx0;

            Pen trackPen(Color(45, mainColor.GetRed(), mainColor.GetGreen(), mainColor.GetBlue()), 2.0f);
            trackPen.SetStartCap(LineCapRound);
            trackPen.SetEndCap(LineCapRound);
            graphics.DrawLine(&trackPen, bx0, by, bx1, by);

            float p = (float)state.progress;
            if (p < 0.0f) p = 0.0f;
            if (p > 1.0f) p = 1.0f;
            if (p > 0.001f) {
                Pen fillPen(Color(230, accent.GetRed(), accent.GetGreen(), accent.GetBlue()), 2.0f);
                fillPen.SetStartCap(LineCapRound);
                fillPen.SetEndCap(LineCapRound);
                graphics.DrawLine(&fillPen, bx0, by, bx0 + bw * p, by);
            }
        }
    }
}

bool IsTaskbarWindow(HWND hwnd) {
    WCHAR cls[64];
    if (!hwnd) return false;
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    return wcscmp(cls, L"Shell_TrayWnd") == 0;
}

void CALLBACK TaskbarEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND hwnd,
    LONG,
    LONG,
    DWORD,
    DWORD
) {
    if (!IsTaskbarWindow(hwnd) || !g_hMediaWindow) return;
    PostMessage(g_hMediaWindow, WM_APP + 10, 0, 0);
}

void RegisterTaskbarHook(HWND hwnd) {
    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
    if (hTaskbar) {
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hTaskbar, &pid);
        if (tid != 0) {
            g_TaskbarHook = SetWinEventHook(
                EVENT_OBJECT_LOCATIONCHANGE,
                EVENT_OBJECT_LOCATIONCHANGE,
                nullptr,
                TaskbarEventProc,
                pid,
                tid,
                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
            );
        }
    }

    PostMessage(hwnd, WM_APP + 10, 0, 0);
}

#define IDT_POLL_MEDIA 1001
#define IDT_ANIMATION  1002
#define APP_WM_CLOSE   WM_APP

LRESULT CALLBACK MediaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            UpdateAppearance(hwnd);
            SetTimer(hwnd, IDT_POLL_MEDIA, 500, NULL);
            RegisterTaskbarHook(hwnd);
            timeBeginPeriod(1);
            QueryPerformanceFrequency(&g_Freq);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            return 0;

        case APP_WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            timeEndPeriod(1);
            if (g_TaskbarHook) {
                UnhookWinEvent(g_TaskbarHook);
                g_TaskbarHook = nullptr;
            }
            g_SessionManager = nullptr;
            PostQuitMessage(0);
            return 0;

        case WM_SETTINGCHANGE:
            UpdateAppearance(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case WM_TIMER:
            if (wParam == IDT_POLL_MEDIA) {
                UpdateMediaInfo();

                bool shouldHide = false;

                if (g_Settings.hideFullscreen) {
                    QUERY_USER_NOTIFICATION_STATE state;
                    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
                        if (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) {
                            shouldHide = true;
                        }
                    }
                }

                bool isPlaying = false;
                {
                    lock_guard<mutex> guard(g_MediaState.lock);
                    isPlaying = g_MediaState.isPlaying;
                }

                if (g_Settings.idleTimeout > 0) {
                    if (isPlaying) {
                        g_IdleSecondsCounter = 0;
                        g_IsHiddenByIdle = false;
                    } else {
                        g_IdleSecondsCounter++;
                        if (g_IdleSecondsCounter >= g_Settings.idleTimeout) {
                            g_IsHiddenByIdle = true;
                        }
                    }
                } else {
                    g_IsHiddenByIdle = false;
                }

                if (g_IsHiddenByIdle) shouldHide = true;

                if (shouldHide && IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else if (!shouldHide && !IsWindowVisible(hwnd)) {
                    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
                    if (hTaskbar && IsWindowVisible(hTaskbar)) {
                        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                    }
                }

                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam == IDT_ANIMATION) {
                if (g_IsScrolling) {
                    LARGE_INTEGER now;
                    QueryPerformanceCounter(&now);
                    if (g_LastTime.QuadPart == 0) g_LastTime = now;
                    float dt = (float)(now.QuadPart - g_LastTime.QuadPart) / (float)g_Freq.QuadPart;
                    g_LastTime = now;

                    if (dt > 0.1f) dt = 0.1f;

                    if (g_ScrollWaitF > 0.0f) {
                        g_ScrollWaitF -= dt * 60.0f;
                    } else {
                        g_ScrollOffsetF += 40.0f * dt;
                        if (g_ScrollOffsetF > g_TextWidth + kRepeatedTextGap) {
                            g_ScrollOffsetF = 0.0f;
                            g_ScrollWaitF = 45.0f;
                        }
                        g_ScrollOffset = (int)g_ScrollOffsetF;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else {
                    KillTimer(hwnd, IDT_ANIMATION);
                }
            }
            return 0;

        case WM_APP + 10: {
            HWND hTaskbar = FindWindow(TEXT("Shell_TrayWnd"), nullptr);
            if (!hTaskbar) break;

            if (!IsWindowVisible(hTaskbar)) {
                if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
                return 0;
            }

            if (!g_IsHiddenByIdle && !IsWindowVisible(hwnd)) {
                bool gameModeHide = false;
                if (g_Settings.hideFullscreen) {
                    QUERY_USER_NOTIFICATION_STATE state;
                    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
                        if (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) {
                            gameModeHide = true;
                        }
                    }
                }
                if (!gameModeHide) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            }

            RECT rc;
            GetWindowRect(hTaskbar, &rc);
            int taskbarWidth = rc.right - rc.left;
            int taskbarHeight = rc.bottom - rc.top;

            int x = rc.left + g_Settings.offsetX;
            int y;

            // For horizontal taskbars (top/bottom), apply a "floating" 6px margin from the bottom edge of the taskbar rect.
            // For vertical taskbars (left/right), keep the robust vertical centering.
            if (taskbarWidth > taskbarHeight) {
                y = rc.bottom - g_Settings.height - 6 + g_Settings.offsetY;
            } else {
                y = rc.top + (taskbarHeight / 2) - (g_Settings.height / 2) + g_Settings.offsetY;
            }
            RECT myRc;
            GetWindowRect(hwnd, &myRc);
            if (myRc.left != x || myRc.top != y ||
                (myRc.right - myRc.left) != g_Settings.width ||
                (myRc.bottom - myRc.top) != g_Settings.height) {
                SetWindowPos(
                    hwnd,
                    HWND_TOPMOST,
                    x,
                    y,
                    g_Settings.width,
                    g_Settings.height,
                    SWP_NOACTIVATE
                );
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (g_HoverState == 0) {
                g_IsDragging = true;
                GetCursorPos(&g_DragStartPos);
                g_DragStartOffsetX = g_Settings.offsetX;
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_IsDragging) {
                POINT pt;
                GetCursorPos(&pt);
                int dx = pt.x - g_DragStartPos.x;
                g_Settings.offsetX = g_DragStartOffsetX + dx;
                SendMessage(hwnd, WM_APP + 10, 0, 0);
                return 0;
            }

            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            int artSize = g_Settings.height - (kPanelPadding * 2);
            double scale = g_Settings.buttonScale;

            int startControlX = kPanelPadding + artSize + static_cast<int>(14 * scale);
            float gap = 30.0f * static_cast<float>(scale);
            float pX = static_cast<float>(startControlX);
            float plX = pX + gap;
            float nX = plX + gap;
            float radius = 13.0f * static_cast<float>(scale);

            int newState = 0;
            if (y > 8 && y < g_Settings.height - 8) {
                if (x >= pX - radius && x <= pX + radius) newState = 1;
                else if (x >= plX - (radius + 2) && x <= plX + (radius + 2)) newState = 2;
                else if (x >= nX - radius && x <= nX + radius) newState = 3;
            }

            if (newState != g_HoverState) {
                g_HoverState = newState;
                InvalidateRect(hwnd, NULL, FALSE);
            }

            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            g_HoverState = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_LBUTTONUP:
            if (g_IsDragging) {
                g_IsDragging = false;
                ReleaseCapture();
                return 0;
            }
            if (g_HoverState > 0) SendMediaCommand(g_HoverState);
            return 0;

        case WM_MOUSEWHEEL: {
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            keybd_event(zDelta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0, 0, 0);
            keybd_event(zDelta > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0, KEYEVENTF_KEYUP, 0);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

            DrawMediaPanel(memDC, rc.right, rc.bottom);

            if (g_IsScrolling) {
                SetTimer(hwnd, IDT_ANIMATION, kAnimationIntervalMs, NULL);
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        default:
            if (msg == g_TaskbarCreatedMsg) {
                if (g_TaskbarHook) {
                    UnhookWinEvent(g_TaskbarHook);
                    g_TaskbarHook = nullptr;
                }
                RegisterTaskbarHook(hwnd);
                return 0;
            }
            break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void MediaThread() {
    winrt::init_apartment();

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = MediaWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("WindhawkMusicLounge_GSMTC");
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    RegisterClass(&wc);

    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    pCreateWindowInBand CreateWindowInBand = nullptr;
    if (hUser32) {
        CreateWindowInBand = (pCreateWindowInBand)GetProcAddress(hUser32, "CreateWindowInBand");
    }

    if (CreateWindowInBand) {
        g_hMediaWindow = CreateWindowInBand(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            wc.lpszClassName,
            TEXT("MusicLounge"),
            WS_POPUP | WS_VISIBLE,
            0,
            0,
            g_Settings.width,
            g_Settings.height,
            NULL,
            NULL,
            wc.hInstance,
            NULL,
            ZBID_IMMERSIVE_NOTIFICATION
        );
        if (g_hMediaWindow) {
            Wh_Log(L"Created window in ZBID_IMMERSIVE_NOTIFICATION band");
        }
    }

    if (!g_hMediaWindow) {
        Wh_Log(L"Falling back to CreateWindowEx");
        g_hMediaWindow = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            wc.lpszClassName,
            TEXT("MusicLounge"),
            WS_POPUP | WS_VISIBLE,
            0,
            0,
            g_Settings.width,
            g_Settings.height,
            NULL,
            NULL,
            wc.hInstance,
            NULL
        );
    }

    SetLayeredWindowAttributes(g_hMediaWindow, 0, 255, LWA_ALPHA);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterClass(wc.lpszClassName, wc.hInstance);
    GdiplusShutdown(gdiplusToken);
    winrt::uninit_apartment();
}

std::thread* g_pMediaThread = nullptr;

BOOL WhTool_ModInit() {
    SetCurrentProcessExplicitAppUserModelID(L"taskbar-music-lounge");
    LoadSettings();
    g_Running = true;
    g_pMediaThread = new std::thread(MediaThread);
    Discord::g_thread = new std::thread(Discord::Thread);
    return TRUE;
}

void WhTool_ModUninit() {
    g_Running = false;
    if (g_hMediaWindow) SendMessage(g_hMediaWindow, APP_WM_CLOSE, 0, 0);
    if (g_pMediaThread) {
        if (g_pMediaThread->joinable()) g_pMediaThread->join();
        delete g_pMediaThread;
        g_pMediaThread = nullptr;
    }
    if (Discord::g_thread) {
        if (Discord::g_thread->joinable()) Discord::g_thread->join();
        delete Discord::g_thread;
        Discord::g_thread = nullptr;
    }
}

void WhTool_ModSettingsChanged() {
    LoadSettings();
    if (g_hMediaWindow) {
        SendMessage(g_hMediaWindow, WM_TIMER, IDT_POLL_MEDIA, 0);
        SendMessage(g_hMediaWindow, WM_SETTINGCHANGE, 0, 0);
    }
}

bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}

BOOL Wh_ModInit() {
    bool isService = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) {
        Wh_Log(L"CommandLineToArgvW failed");
        return FALSE;
    }

    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0) {
            isService = true;
            break;
        }
    }

    for (int i = 1; i < argc - 1; i++) {
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) {
                isCurrentToolModProcess = true;
            }
            break;
        }
    }

    LocalFree(argv);

    if (isService) {
        return FALSE;
    }

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex = CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            Wh_Log(L"Tool mod already running (%s)", WH_MOD_ID);
            ExitProcess(1);
        }

        if (!WhTool_ModInit()) {
            ExitProcess(1);
        }

        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);

        DWORD entryPointRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        void* entryPoint = (BYTE*)dosHeader + entryPointRVA;

        Wh_SetFunctionHook(entryPoint, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }

    if (isToolModProcess) {
        return FALSE;
    }

    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) {
        return;
    }

    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath, ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            Wh_Log(L"GetModuleFileName failed");
            return;
    }

    WCHAR commandLine[MAX_PATH + 2 + (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath, WH_MOD_ID);

    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule) {
        kernelModule = GetModuleHandle(L"kernel32.dll");
        if (!kernelModule) {
            Wh_Log(L"No kernelbase.dll/kernel32.dll");
            return;
        }
    }

    using CreateProcessInternalW_t = BOOL(WINAPI*)(
        HANDLE hUserToken,
        LPCWSTR lpApplicationName,
        LPWSTR lpCommandLine,
        LPSECURITY_ATTRIBUTES lpProcessAttributes,
        LPSECURITY_ATTRIBUTES lpThreadAttributes,
        WINBOOL bInheritHandles,
        DWORD dwCreationFlags,
        LPVOID lpEnvironment,
        LPCWSTR lpCurrentDirectory,
        LPSTARTUPINFOW lpStartupInfo,
        LPPROCESS_INFORMATION lpProcessInformation,
        PHANDLE hRestrictedUserToken
    );

    CreateProcessInternalW_t pCreateProcessInternalW =
        (CreateProcessInternalW_t)GetProcAddress(kernelModule, "CreateProcessInternalW");
    if (!pCreateProcessInternalW) {
        Wh_Log(L"No CreateProcessInternalW");
        return;
    }

    STARTUPINFO si {
        .cb = sizeof(STARTUPINFO),
        .dwFlags = STARTF_FORCEOFFFEEDBACK,
    };

    PROCESS_INFORMATION pi;
    if (!pCreateProcessInternalW(
            nullptr,
            currentProcessPath,
            commandLine,
            nullptr,
            nullptr,
            FALSE,
            NORMAL_PRIORITY_CLASS,
            nullptr,
            nullptr,
            &si,
            &pi,
            nullptr
        )) {
        Wh_Log(L"CreateProcess failed");
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModSettingsChanged();
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModUninit();
    ExitProcess(0);
}
