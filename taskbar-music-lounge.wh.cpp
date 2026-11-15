// ==WindhawkMod==
// @id              taskbar-music-lounge
// @name            Taskbar Music Lounge
// @description     Now-playing widget for the taskbar with media controls, a progress bar and Discord Rich Presence with cover art
// @version         0.3.1
// @author          Hashah2311, ds1s1s1
// @github          https://github.com/ds1s1s1
// @include         explorer.exe
// @compilerOptions -lole32 -ldwmapi -lgdi32 -luser32 -lwindowsapp -lshcore -lgdiplus -lshell32 -lcomdlg32 -lwinhttp -ladvapi32
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
- The picture changes as soon as the player's does, and video thumbnails are
  cropped to a square instead of squashed
- On sites that don't provide a picture, the previous video's picture isn't
  shown instead
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
   in "Discord large image key". It's shown when there's no song picture.
4. Turn on "Enable Discord presence" and paste the ID into "Discord Client ID".

Discord has to be running on the same PC. Discord only shows pictures from a
link, so with "Show the song or video picture on Discord" on, the cover of what
you're playing is uploaded to uguu.se (a temporary host that deletes files
after 3 hours) and that link is sent to Discord.

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
- DiscordShowCover: true
  $name: Show the song or video picture on Discord
  $description: Discord can only show pictures from a link, so the current cover is uploaded to uguu.se, a free temporary host that deletes files after 3 hours. Nothing is uploaded while Discord presence is off.
- DiscordLargeImage: ""
  $name: Discord large image key
  $description: Optional. Name of an art asset in your Discord app, shown when there is no picture to share.
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
#include <map>
#include <memory>
#include <timeapi.h>
#include <winhttp.h>

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
    shared_ptr<Bitmap> albumArt;
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

// Firefox and browsers built on it report a hash of their install folder
// instead of a name, and list that hash under TaskBarIDs. Returns the folder
// name ("Mozilla Firefox", "Zen Browser", ...) or "" for anything else.
static wstring GeckoBrowserFolder(wstring const& srcLower) {
    static wstring lastSrc, lastFolder;
    if (srcLower.size() != 16 || srcLower.find_first_not_of(L"0123456789abcdef") != wstring::npos) return L"";
    if (srcLower == lastSrc) return lastFolder;
    lastSrc = srcLower;
    lastFolder.clear();

    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        HKEY key;
        if (RegOpenKeyExW(root, L"Software\\Mozilla\\Firefox\\TaskBarIDs", 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        for (DWORD i = 0; lastFolder.empty(); i++) {
            wchar_t path[MAX_PATH], id[32];
            DWORD pathLen = MAX_PATH, idSize = sizeof(id) - sizeof(wchar_t), type = 0;
            LONG r = RegEnumValueW(key, i, path, &pathLen, nullptr, &type, reinterpret_cast<BYTE*>(id), &idSize);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS || type != REG_SZ) continue;
            id[idSize / sizeof(wchar_t)] = L'\0';
            if (ToLower(id) == srcLower) {
                wstring folder = path;
                size_t slash = folder.find_last_of(L"\\/");
                lastFolder = slash == wstring::npos ? folder : folder.substr(slash + 1);
            }
        }
        RegCloseKey(key);
        if (!lastFolder.empty()) break;
    }
    return lastFolder;
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

    wstring gecko = GeckoBrowserFolder(srcLower);
    if (ToLower(gecko).find(L"firefox") != wstring::npos) return { L"Firefox", 0xFFFF6611 };
    if (!gecko.empty())                                    return { gecko,      0xFFFF6611 };

    return { L"Media", kDefaultAccent };
}

// ===========================================================================
// Discord Rich Presence bridge (named-pipe IPC, no external dependency)
// ===========================================================================

namespace Discord {

std::atomic<bool> g_enabled{ false };
std::atomic<bool> g_showWhenPaused{ false };
std::atomic<bool> g_showCover{ true };
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
    uint64_t version = 0;            // bumps only on meaningful change
    uint64_t coverHash = 0;          // picture of the current track, 0 = none
    std::vector<uint8_t> coverJpeg;  // that picture as a square JPEG
    bool coverPending = false;       // the track just changed and its picture may not be in yet
} g_desired;

// Covers already uploaded, by picture hash. Only the Discord thread uses this.
struct UploadedCover {
    std::string url;
    ULONGLONG uploadedAt = 0;
};
std::map<uint64_t, UploadedCover> g_covers;

// uguu.se deletes files after 3 hours, so links are reused for a bit less than that
constexpr ULONGLONG kCoverLifetimeMs = 150ULL * 60 * 1000;

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

// Consume anything Discord sent back (appended to replies when given).
// Returns false if the pipe is broken.
static bool DrainReads(HANDLE pipe, std::string* replies = nullptr) {
    DWORD avail = 0;
    while (true) {
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail == 0) return true;
        char tmp[1024];
        DWORD toRead = avail < sizeof(tmp) ? avail : (DWORD)sizeof(tmp);
        DWORD rd = 0;
        if (!ReadFile(pipe, tmp, toRead, &rd, nullptr) || rd == 0) return false;
        if (replies) replies->append(tmp, rd);
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

// uguu.se answers {"success":true,"files":[{"url":"https:\/\/d.uguu.se\/abc.jpg",...}]}
static std::string ParseUploadUrl(const std::string& json) {
    size_t key = json.find("\"url\"");
    size_t colon = key == std::string::npos ? key : json.find(':', key);
    size_t start = colon == std::string::npos ? colon : json.find('"', colon);
    size_t end = start == std::string::npos ? start : json.find('"', start + 1);
    if (end == std::string::npos) return {};
    std::string url;
    for (size_t i = start + 1; i < end; i++) {
        if (json[i] == '\\' && i + 1 < end) i++;  // "\/" -> "/"
        url += json[i];
    }
    return url.rfind("https://", 0) == 0 && url.size() <= 250 ? url : std::string();
}

// Discord only shows pictures from a link, so the cover goes to uguu.se, a
// free host that deletes files after 3 hours. Returns the link, or "" on failure.
static std::string UploadCover(const std::vector<uint8_t>& jpeg) {
    std::string url;
    HINTERNET session = nullptr;
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
    session = WinHttpOpen(L"TaskbarMusicLounge", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
#endif
    if (!session) {
        session = WinHttpOpen(L"TaskbarMusicLounge", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) return url;
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);

    HINTERNET connect = WinHttpConnect(session, L"uguu.se", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"POST", L"/upload", nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    if (request) {
        char boundary[64];
        sprintf_s(boundary, "----TaskbarMusicLounge%08lx", (unsigned long)GetTickCount());
        std::string body = std::string("--") + boundary + "\r\n"
            "Content-Disposition: form-data; name=\"files[]\"; filename=\"cover.jpg\"\r\n"
            "Content-Type: image/jpeg\r\n\r\n";
        body.append(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
        body += std::string("\r\n--") + boundary + "--\r\n";

        wchar_t headers[128];
        swprintf_s(headers, L"Content-Type: multipart/form-data; boundary=%hs", boundary);

        if (WinHttpSendRequest(request, headers, (DWORD)-1L, &body[0], (DWORD)body.size(),
                               (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            std::string reply;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(request, &avail) && avail > 0 && reply.size() < 65536) {
                size_t old = reply.size();
                reply.resize(old + avail);
                DWORD read = 0;
                if (!WinHttpReadData(request, &reply[old], avail, &read)) break;
                reply.resize(old + read);
            }
            if (status == 200) url = ParseUploadUrl(reply);
            if (url.empty()) Wh_Log(L"Discord: cover upload failed (HTTP %lu)", status);
        } else {
            Wh_Log(L"Discord: cover upload failed (error %lu)", GetLastError());
        }
        WinHttpCloseHandle(request);
    }
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return url;
}

static void ForgetOldCovers(ULONGLONG now) {
    for (auto it = g_covers.begin(); it != g_covers.end();) {
        if (now - it->second.uploadedAt >= kCoverLifetimeMs) it = g_covers.erase(it);
        else ++it;
    }
}

static void ClosePipe() {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

void Thread() {
    uint64_t lastSentVersion = UINT64_MAX;
    std::string lastSentImage;
    bool lastWasCleared = false;
    DWORD lastConnectAttempt = 0;
    bool linksRejected = false;   // this Discord refused a picture link; stop sending them
    ULONGLONG retryUploadAt = 0;  // after a failed upload, the next one waits a minute
    ULONGLONG holdingSince = 0;   // when an update started waiting for its picture

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
            lastSentImage.clear();
            lastWasCleared = false;
            linksRejected = false;
        }

        std::string clientId, largeImg;
        {
            std::lock_guard<std::mutex> g(g_cfgMutex);
            clientId = g_clientId;
            largeImg = g_largeImageKey;
        }
        int type = g_activityType.load();
        bool showPaused = g_showWhenPaused.load();
        bool showCover = g_showCover.load() && !linksRejected;

        if (g_pipe == INVALID_HANDLE_VALUE) {
            DWORD now = GetTickCount();
            if (now - lastConnectAttempt < 10000) { Sleep(500); continue; }
            lastConnectAttempt = now;
            g_pipe = TryConnect(clientId);
            if (g_pipe == INVALID_HANDLE_VALUE) { Sleep(500); continue; }
            lastSentVersion = UINT64_MAX;
            lastSentImage.clear();
            lastWasCleared = false;
            linksRejected = false;  // Discord may have been updated in the meantime
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
            snap.coverHash = g_desired.coverHash;
            snap.coverPending = g_desired.coverPending;
        }

        bool shouldShow = snap.hasMedia && (snap.isPlaying || showPaused);

        // Large image: the track's own picture once it has a link, otherwise
        // the asset key from the settings.
        std::string image = largeImg;
        bool waitForCover = false;
        if (shouldShow && showCover) {
            ULONGLONG now = GetTickCount64();
            ForgetOldCovers(now);
            auto it = g_covers.find(snap.coverHash);
            if (snap.coverPending) {
                waitForCover = true;
            } else if (it != g_covers.end()) {
                image = it->second.url;
            } else if (snap.coverHash && now >= retryUploadAt) {
                std::vector<uint8_t> jpeg;
                {
                    std::lock_guard<std::mutex> g(g_desired.m);
                    if (g_desired.coverHash == snap.coverHash) jpeg = g_desired.coverJpeg;
                }
                std::string url = jpeg.empty() ? std::string() : UploadCover(jpeg);
                if (!url.empty()) {
                    g_covers[snap.coverHash] = { url, now };
                    image = url;
                } else if (!jpeg.empty()) {
                    retryUploadAt = GetTickCount64() + 60000;
                }
            }
        }

        bool ok = true;
        if (shouldShow) {
            bool changed = snap.version != lastSentVersion || image != lastSentImage;
            // A new track's title shows up a moment before its picture. Hold the
            // update for a few seconds so both change on Discord together.
            ULONGLONG now = GetTickCount64();
            if (!changed || !waitForCover) holdingSince = 0;
            else if (!holdingSince) holdingSince = now;
            bool hold = holdingSince && now - holdingSince < 5000;

            if (changed && !hold) {
                ok = SendActivity(g_pipe, BuildActivity(snap, image, type));
                if (ok) { lastSentVersion = snap.version; lastSentImage = image; lastWasCleared = false; }
            }
        } else {
            holdingSince = 0;
            if (!lastWasCleared) {
                ok = SendActivity(g_pipe, "null");
                if (ok) { lastWasCleared = true; lastSentVersion = UINT64_MAX; lastSentImage.clear(); }
            }
        }

        std::string replies;
        if (!ok || !DrainReads(g_pipe, &replies)) {
            ClosePipe();
            lastSentVersion = UINT64_MAX;
            lastSentImage.clear();
            lastWasCleared = false;
        } else if (replies.find("\"evt\":\"ERROR\"") != std::string::npos &&
                   lastSentImage.rfind("https://", 0) == 0) {
            // Older Discord builds only take asset keys. Keep the status, drop the picture.
            Wh_Log(L"Discord: picture link rejected, continuing without it");
            linksRejected = true;
            lastSentVersion = UINT64_MAX;
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

// Hand the current track's picture to the Discord thread (hash 0 = none).
void SetCover(uint64_t hash, std::vector<uint8_t> jpeg) {
    std::lock_guard<std::mutex> g(g_desired.m);
    g_desired.coverHash = hash;
    g_desired.coverJpeg = std::move(jpeg);
}

// True right after a track change, until that track's own picture is in.
void SetCoverPending(bool pending) {
    std::lock_guard<std::mutex> g(g_desired.m);
    g_desired.coverPending = pending;
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

// ---------------------------------------------------------------------------
// Album art
// ---------------------------------------------------------------------------

static const GUID kEncoderQuality = { 0x1d5be4b5, 0xfa4a, 0x452d, { 0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb } };

// Raw bytes of the session's thumbnail (usually a JPEG or PNG).
static bool ReadThumbnail(GlobalSystemMediaTransportControlsSessionMediaProperties const& props, vector<uint8_t>& out) {
    out.clear();
    try {
        auto ref = props.Thumbnail();
        if (!ref) return false;
        auto stream = ref.OpenReadAsync().get();
        if (!stream) return false;
        uint64_t size = stream.Size();
        if (size == 0 || size > 16 * 1024 * 1024) return false;
        DataReader reader(stream);
        uint32_t loaded = reader.LoadAsync(static_cast<uint32_t>(size)).get();
        out.resize(loaded);
        reader.ReadBytes(out);
    } catch (...) {
        out.clear();
    }
    return !out.empty();
}

static uint64_t HashBytes(const vector<uint8_t>& data) {
    uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (uint8_t b : data) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h ? h : 1;  // 0 means "no picture"
}

// The part of a video thumbnail inside its black bars. Square pictures are
// album covers and are left alone, and bars only count when both sides match,
// so a dark cover or a dark sky doesn't get cropped.
static Rect ContentRect(Bitmap* bmp) {
    INT w = (INT)bmp->GetWidth(), h = (INT)bmp->GetHeight();
    Rect full(0, 0, w, h);
    if (abs(w - h) * 20 <= max(w, h)) return full;
    BitmapData data;
    if (bmp->LockBits(&full, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) return full;

    auto dark = [&](INT x, INT y) {
        const BYTE* p = static_cast<const BYTE*>(data.Scan0) + y * data.Stride + x * 4;
        return p[0] < 24 && p[1] < 24 && p[2] < 24;
    };
    auto rowDark = [&](INT y) {
        for (INT x = 0; x < w; x += 4) if (!dark(x, y)) return false;
        return true;
    };
    auto colDark = [&](INT x) {
        for (INT y = 0; y < h; y += 4) if (!dark(x, y)) return false;
        return true;
    };

    INT top = 0, bottom = 0, left = 0, right = 0;
    while (top < h / 3 && rowDark(top)) top++;
    while (bottom < h / 3 && rowDark(h - 1 - bottom)) bottom++;
    while (left < w / 3 && colDark(left)) left++;
    while (right < w / 3 && colDark(w - 1 - right)) right++;
    bmp->UnlockBits(&data);

    // Allow a couple of pixels of difference for blurry JPEG edges
    if (abs(top - bottom) > 2 + h / 50) top = bottom = 0;
    if (abs(left - right) > 2 + w / 50) left = right = 0;
    return Rect(left, top, w - left - right, h - top - bottom);
}

// Decode a thumbnail into a standalone bitmap without black bars. GDI+ needs
// the source stream for as long as a bitmap made from it lives, so the pixels
// are copied out and the stream is released right away.
static Bitmap* DecodeArt(const vector<uint8_t>& bytes) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!mem) return nullptr;
    void* dst = GlobalLock(mem);
    if (!dst) {
        GlobalFree(mem);
        return nullptr;
    }
    memcpy(dst, bytes.data(), bytes.size());
    GlobalUnlock(mem);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
        GlobalFree(mem);
        return nullptr;
    }

    Bitmap* result = nullptr;
    Bitmap* decoded = Bitmap::FromStream(stream);
    if (decoded && decoded->GetLastStatus() == Ok && decoded->GetWidth() > 0 && decoded->GetHeight() > 0) {
        Rect content = ContentRect(decoded);
        result = new Bitmap(content.Width, content.Height, PixelFormat32bppPARGB);
        Graphics g(result);
        g.DrawImage(decoded, Rect(0, 0, content.Width, content.Height),
                    content.X, content.Y, content.Width, content.Height, UnitPixel);
    }
    delete decoded;
    stream->Release();
    return result;
}

static bool GetJpegEncoder(CLSID* clsid) {
    UINT count = 0, size = 0;
    if (GetImageEncodersSize(&count, &size) != Ok || size == 0) return false;
    vector<BYTE> buffer(size);
    auto* codecs = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, size, codecs) != Ok) return false;
    for (UINT i = 0; i < count; i++) {
        if (wcscmp(codecs[i].MimeType, L"image/jpeg") == 0) {
            *clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// The middle square of the art as a JPEG. Big pictures are scaled down to
// 512x512, the size Discord recommends; small ones aren't blown up past 256.
static vector<uint8_t> EncodeCoverJpeg(Bitmap* art) {
    vector<uint8_t> out;
    CLSID jpegClsid;
    if (!art || !GetJpegEncoder(&jpegClsid)) return out;
    INT w = (INT)art->GetWidth(), h = (INT)art->GetHeight();
    INT side = min(w, h);
    if (side <= 0) return out;
    INT size = side < 256 ? 256 : side > 512 ? 512 : side;

    Bitmap square(size, size, PixelFormat24bppRGB);
    {
        Graphics g(&square);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(art, Rect(0, 0, size, size), (w - side) / 2, (h - side) / 2, side, side, UnitPixel);
    }

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return out;
    ULONG quality = 90;
    EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = kEncoderQuality;
    params.Parameter[0].Type = EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;
    if (square.Save(stream, &jpegClsid, &params) == Ok) {
        HGLOBAL mem = nullptr;
        STATSTG stat;
        if (SUCCEEDED(GetHGlobalFromStream(stream, &mem)) && SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))) {
            if (const BYTE* p = static_cast<const BYTE*>(GlobalLock(mem))) {
                out.assign(p, p + stat.cbSize.QuadPart);
                GlobalUnlock(mem);
            }
        }
    }
    stream->Release();
    return out;
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
    Discord::g_showCover.store(Wh_GetIntSetting(L"DiscordShowCover") != 0);

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

// Album art state, only used on the media window's thread
uint64_t g_ArtHash = 0;            // hash of the thumbnail the player reports, 0 = none
shared_ptr<Bitmap> g_ArtBitmap;    // that thumbnail, decoded
Color g_ArtTint = Color(0, 0, 0);
ULONGLONG g_ArtChangedAt = 0;      // when the player last changed it
wstring g_ArtOwner;                // the track that was playing when it showed up
wstring g_ArtOwnerAlbum;           // that track's album ("" if it has none)
wstring g_ArtTrackKey;             // the track playing now (player + title + artist)
ULONGLONG g_ArtTrackSince = 0;     // when it started
bool g_ArtIsOwn = false;           // the reported picture belongs to the track playing now
bool g_ArtSettled = true;          // false while the track's own picture may still be coming
ULONGLONG g_ArtNextCheck = 0;
uint64_t g_CoverHashSent = 0;      // picture last handed to the Discord thread

// Players often publish the title first and the picture a moment later, so
// the thumbnail is checked on every poll for a few seconds after a track
// change, and every few seconds after that.
//
// Some players also keep reporting the previous picture when the new track
// has none (Firefox does this on sites without artwork). So a picture only
// counts for the track that was playing when it showed up, or for tracks of
// the same album. Anything else is hidden once the new track has had a few
// seconds to bring its own.
static void RefreshAlbumArt(GlobalSystemMediaTransportControlsSessionMediaProperties const& props,
                            const wstring& trackKey, const wstring& albumKey) {
    ULONGLONG now = GetTickCount64();
    if (trackKey != g_ArtTrackKey) {
        // A picture that changed right before the title is the new track's
        if (g_ArtHash && now - g_ArtChangedAt < 1500) {
            g_ArtOwner = trackKey;
            g_ArtOwnerAlbum = albumKey;
        }
        g_ArtTrackKey = trackKey;
        g_ArtTrackSince = now;
    }

    if (now - g_ArtTrackSince < 8000 || now >= g_ArtNextCheck) {
        g_ArtNextCheck = now + 4000;
        vector<uint8_t> bytes;
        uint64_t hash = ReadThumbnail(props, bytes) ? HashBytes(bytes) : 0;
        if (hash != g_ArtHash) {
            g_ArtHash = hash;
            g_ArtChangedAt = now;
            g_ArtOwner = trackKey;
            g_ArtOwnerAlbum = albumKey;
            g_ArtBitmap.reset(hash ? DecodeArt(bytes) : nullptr);
            g_ArtTint = g_ArtBitmap ? CalculateAverageColor(g_ArtBitmap.get()) : Color(0, 0, 0);
        }
    }

    g_ArtIsOwn = g_ArtHash && (g_ArtOwner == trackKey || (!albumKey.empty() && albumKey == g_ArtOwnerAlbum));
    g_ArtSettled = g_ArtIsOwn || now - g_ArtTrackSince >= 3000;

    // While the new track's picture may still be coming, the widget is left as it is
    lock_guard<mutex> guard(g_MediaState.lock);
    if (g_ArtIsOwn) {
        g_MediaState.albumArt = g_ArtBitmap;
        g_MediaState.primaryColor = g_ArtTint;
    } else if (g_ArtSettled) {
        g_MediaState.albumArt.reset();
        g_MediaState.primaryColor = Color(0, 0, 0);
    }
}

// Nothing is playing. The picture is hidden, but which track it belongs to is
// kept, in case the player comes back still reporting it for something else.
static void ClearAlbumArt() {
    g_ArtTrackKey.clear();
    g_ArtIsOwn = false;
    g_ArtSettled = true;
    lock_guard<mutex> guard(g_MediaState.lock);
    g_MediaState.albumArt.reset();
}

// Give the Discord thread the current picture while sharing it is turned on.
// Called before Discord::Push so a new title never goes out with the old picture.
static void SyncDiscordCover() {
    bool wanted = Discord::g_enabled.load() && Discord::g_showCover.load();
    uint64_t hash = wanted && g_ArtIsOwn ? g_ArtHash : 0;
    if (hash != g_CoverHashSent) {
        g_CoverHashSent = hash;
        vector<uint8_t> jpeg;
        if (hash) jpeg = EncodeCoverJpeg(g_ArtBitmap.get());
        Discord::SetCover(jpeg.empty() ? 0 : hash, std::move(jpeg));
    }
    Discord::SetCoverPending(wanted && !g_ArtSettled);
}

static void ShowNoMedia() {
    ClearAlbumArt();
    SyncDiscordCover();
    {
        lock_guard<mutex> guard(g_MediaState.lock);
        g_MediaState.hasMedia = false;
        g_MediaState.title = L"No Media";
        g_MediaState.artist = L"";
        g_MediaState.player = L"";
        g_MediaState.isPlaying = false;
        g_MediaState.hasProgress = false;
    }
    Discord::Push(false, false, L"", L"", L"", 0, 0);
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
            wstring album = props.AlbumTitle().c_str();

            wstring source = SessionSourceLower(session);
            PlayerInfo pinfo = IdentifyPlayer(source);

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

            RefreshAlbumArt(props, source + L'\x1f' + newTitle + L'\x1f' + newArtist,
                            album.empty() ? wstring() : source + L'\x1f' + album);
            SyncDiscordCover();

            {
                lock_guard<mutex> guard(g_MediaState.lock);
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
            ShowNoMedia();
        }
    } catch (...) {
        ShowNoMedia();
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
        state.albumArt = g_MediaState.albumArt;
        state.hasMedia = g_MediaState.hasMedia;
        state.isPlaying = g_MediaState.isPlaying;
        state.primaryColor = g_MediaState.primaryColor;
        state.accentColor = g_MediaState.accentColor;
        state.progress = g_MediaState.progress;
        state.hasProgress = g_MediaState.hasProgress;
    }

    Color accent = state.accentColor;

    if (!state.hasMedia) {
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
        // Video thumbnails are wide: show the middle square, like a cover, instead of squashing them
        INT iw = (INT)state.albumArt->GetWidth(), ih = (INT)state.albumArt->GetHeight();
        INT side = min(iw, ih);
        GraphicsState clipState = graphics.Save();
        graphics.SetClip(&albumPath);
        graphics.DrawImage(state.albumArt.get(), Rect(artX, artY, artSize, artSize),
                           (iw - side) / 2, (ih - side) / 2, side, side, UnitPixel);
        graphics.Restore(clipState);

        Pen albumBorder(Color(48, 255, 255, 255), 1.0f);
        graphics.DrawPath(&albumBorder, &albumPath);
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
    {
        // Bitmaps have to be gone before GDI+ shuts down
        lock_guard<mutex> guard(g_MediaState.lock);
        g_MediaState.albumArt.reset();
        g_ArtBitmap.reset();
    }
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
