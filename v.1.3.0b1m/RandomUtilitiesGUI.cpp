// ============================================================================
//  RandomUtilitiesGUI.cpp  --  graphical hub
//
//  Tools run inside the main window by default. Each running tool is a
//  "session": a worker thread plus a text buffer that a window draws.
//  A session can be detached into a sub-window, moved back into the main
//  window, or started as a real console process ("Open in CLI").
// ============================================================================
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "RandomUtilities.h"

#ifdef _WIN32

#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <objidl.h>
#include <gdiplus.h>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

// ----------------------------------------------------------------------------
//  Classic console plumbing (--cli / fallback hub only)
// ----------------------------------------------------------------------------
bool EnsureConsoleStreams(bool attachParent) {
    if (GetConsoleWindow() != nullptr) return true;

    bool ok = false;
    if (attachParent) ok = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (!ok) ok = AllocConsole() != 0;
    if (!ok) return false;

    HANDLE hIn  = CreateFileW(L"CONIN$",  GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hIn  != INVALID_HANDLE_VALUE) SetStdHandle(STD_INPUT_HANDLE,  hIn);
    if (hOut != INVALID_HANDLE_VALUE) { SetStdHandle(STD_OUTPUT_HANDLE, hOut);
                                        SetStdHandle(STD_ERROR_HANDLE,  hOut); }

    FILE* f = nullptr;
    f = freopen("CONIN$",  "r", stdin);  (void)f;
    f = freopen("CONOUT$", "w", stdout); (void)f;
    f = freopen("CONOUT$", "w", stderr); (void)f;
    std::cin.clear(); std::cout.clear(); std::cerr.clear(); std::clog.clear();

    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
            SetConsoleMode(hOut, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    return true;
}

namespace {

using namespace Gdiplus;

// ----------------------------------------------------------------------------
//  Constants
// ----------------------------------------------------------------------------
constexpr float kW = 1000.f;
constexpr float kH = 700.f;
constexpr float kSubW = 920.f;
constexpr float kSubH = 620.f;

constexpr float kMargin  = 30.f;
constexpr int   kCols    = 4;
constexpr float kTileW   = 226.f;
constexpr float kTileH   = 70.f;
constexpr float kTileGap = 12.f;
constexpr float kGridX   = kMargin;
constexpr float kGridY   = 180.f;
constexpr float kRadius  = 8.f;

constexpr int   kTileCount = 21;
constexpr int   kExitSlot  = 21;

const char* kVersion = "1.3.0b1m";
float gScale = 1.f;

// ----------------------------------------------------------------------------
//  Tool table
// ----------------------------------------------------------------------------
struct Tool {
    int id;
    const wchar_t*           name;
    const char*              fullName;
    const wchar_t*           description;
    void                  (*fn)();
    std::vector<std::string> keys;
};

std::vector<Tool> makeTools() {
    std::vector<Tool> t;
    t.push_back({ 1, L"Calculator", "Advanced Calculator",
        L"Basic and scientific calculator.\n"
        L"Supported operations: +, -, *, /, sqrt, sin, cos, tan,\n"
        L"log, log2, log10, abs, and arbitrary powers.\n"
        L"Keeps a session history you can review.",
        CalculatorPr, {"calc","math","arithmetic"} });
    t.push_back({ 2, L"Guessing Game", "Guessing Game Pro",
        L"Guess a number between 1 and 100.\n"
        L"You get 10 attempts. After each guess the game tells\n"
        L"you whether the secret number is higher or lower.",
        GuessingGamePr, {"game","guess","number"} });
    t.push_back({ 3, L"Unit Converter", "Unit Converter",
        L"Convert between units in 15 categories and about 150 units:\n"
        L"Length, Mass, Volume, Speed, Temperature, Energy, Pressure,\n"
        L"Angle, Frequency, Power, Force, Data Storage, Area, Time,\n"
        L"and Data Rate.",
        UnitConverterPr, {"unit","convert","km","celsius"} });
    t.push_back({ 4, L"Password Gen", "Password Generator",
        L"Generate a random password from 1 to 256 characters.\n"
        L"Pick which character sets to include:\n"
        L"lowercase, uppercase, digits, symbols.",
        PasswordGeneratorPr, {"pass","gen","password","secret"} });
    t.push_back({ 5, L"Base Converter", "Base Converter",
        L"Convert a non-negative decimal number to binary, octal,\n"
        L"and hexadecimal in a single view.",
        BaseConverterPr, {"base","binary","hex","octal"} });
    t.push_back({ 6, L"Random Gen", "Random Number Generator",
        L"Generate a random integer between two bounds you choose.\n"
        L"Uses mt19937_64 seeded from std::random_device.",
        RandomNumberGeneratorPr, {"random","rng","dice"} });
    t.push_back({ 7, L"BMI Calc", "BMI Calculator",
        L"Body Mass Index from weight (kg) and height (m).\n"
        L"Reports the WHO category: Underweight, Normal, Overweight,\n"
        L"Obese Class I, II, or III.",
        BMICalculatorPr, {"bmi","health","weight","height"} });
    t.push_back({ 8, L"Age Calc", "Age Calculator",
        L"Exact age in years, months, and days from your birth date.\n"
        L"Leap years are handled correctly, including Feb 29 births.",
        AgeCalculatorPr, {"age","birth","birthday"} });
    t.push_back({ 9, L"Text Analyzer", "Text Analyzer",
        L"Count characters, letters, words, sentences, digits, spaces,\n"
        L"upper- and lowercase letters, special characters, and the\n"
        L"average word length.",
        TextAnalyzerPr, {"text","word","analyze","count"} });
    t.push_back({10, L"Text Formatter", "Text Formatter",
        L"Apply one of eight operations to a block of text:\n"
        L"indent, upper, lower, title, trim, dedupe, stats.\n"
        L"Enter one line at a time and terminate with :done.",
        TextFormatterPr, {"format","formatter","indent","trim","dedupe"} });
    t.push_back({11, L"Encoder / Decoder", "Encoder / Decoder",
        L"Encode or decode Base64, URL, and Hex.\n"
        L"Also supports ROT13 and arbitrary Caesar shifts.\n"
        L"Decoded binary is shown with \\xNN escaping.",
        EncoderDecoderPr, {"encode","decode","base64","url","rot13","hex"} });
    t.push_back({12, L"System Info", "System Info",
        L"Version, build date, developer, and architecture\n"
        L"information about this hub.",
        nullptr, {"info","system","build","version"} });
    t.push_back({13, L"Clean RAM", "Clean Up Compute",
        L"Request a working-set trim for this process\n"
        L"and report the memory freed in KB or MB.",
        CleanUpCompute, {"clean","ram","memory","optimize"} });
    t.push_back({14, L"Quick Note", "Quick Note Editor",
        L"In-place multi-line editor for .txt and .md files.\n"
        L"Optional password protection.\n"
        L"Commands: :wq save, :q quit, :d delete line, :pass password.",
        QuickNotePr, {"note","vim","save","write"} });
    t.push_back({15, L"Random Pick", "Random Picker",
        L"Coin flip, custom dice (sides x count),\n"
        L"or pick one item from a comma-separated list.",
        RandomPickerPr, {"pick","coin","dice","lottery","choose"} });
    t.push_back({16, L"Color Picker", "Color Converter",
        L"Convert between HEX, RGB, and HSL.\n"
        L"Shows a swatch preview using ANSI true-colour.",
        ColorConverterPr, {"color","colour","hex","rgb","hsl"} });
    t.push_back({17, L"Prime / Factor", "Prime / Factor Tools",
        L"Check primality, factorise an integer, find the next prime,\n"
        L"or list all primes up to a bound (max 1,000,000).",
        PrimeToolsPr, {"prime","factor","factorize","sieve"} });
    t.push_back({18, L"Roman Numerals", "Roman Numeral Converter",
        L"Convert an integer (1-3999) to a Roman numeral,\n"
        L"or parse a Roman numeral back to an integer.\n"
        L"Rejects invalid forms like IIII or VV.",
        RomanNumeralPr, {"roman","numeral","latin"} });
    t.push_back({19, L"File Hasher", "File Hasher",
        L"Compute CRC32 and SHA-256 hashes for any file,\n"
        L"along with the file size.",
        FileHasherPr, {"hash","sha","crc","checksum","integrity"} });
    t.push_back({20, L"File Mover", "Secure File Mover",
        L"Recursive move or copy with SHA-256 verification.\n"
        L"A source file is deleted only after the copy verifies.\n"
        L"Optionally filter by filename and include the source\n"
        L"folder name in the destination.",
        SecureFileMoverPr, {"move","copy","transfer","verify","sync","robocopy","move-sh"} });
    t.push_back({21, L"RAM Query", "RAM Query",
        L"14 modes for process and memory inspection:\n"
        L"top-N by RAM, system specs, health report, bloatware scan,\n"
        L"danger scan, process tree, pause / resume / kill,\n"
        L"compact dock, visual chart, and watch mode.",
        RAMQueryPr, {"ram","memory","process","cpu","task","monitor"} });
    return t;
}

// Tools that read raw keys or edit the screen in place need a real console.
// They always open in a console window, never in the embedded pane.
bool toolNeedsConsole(int) { return false; }   // every tool runs embedded; Open in CLI still works

// ----------------------------------------------------------------------------
//  Text helpers and fonts
// ----------------------------------------------------------------------------
const FontFamily* gSans = nullptr;
const FontFamily* gMono = nullptr;
std::map<long, Font*> gFonts;
std::map<long, Font*> gMonoFonts;

std::wstring widen(const char* s) {
    std::wstring w;
    if (!s) return w;
    for (; *s; ++s) w.push_back((wchar_t)(unsigned char)*s);
    return w;
}

std::wstring widen(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) w.push_back((wchar_t)(unsigned char)c);
    return w;
}

std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return widen(s);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string wToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

Font* font(float px, int style = FontStyleRegular) {
    long key = (style << 1) | ((long)(px * 4.f) << 8);
    auto it = gFonts.find(key);
    if (it != gFonts.end()) return it->second;
    Font* f = new Font(gSans, px, style, UnitPixel);
    gFonts[key] = f;
    return f;
}

Font* monoFont(float px, int style = FontStyleRegular) {
    long key = ((long)(px * 4.f) << 2) | style;
    auto it = gMonoFonts.find(key);
    if (it != gMonoFonts.end()) return it->second;
    Font* f = new Font(gMono, px, style, UnitPixel);
    gMonoFonts[key] = f;
    return f;
}

void drawText(Graphics& g, const std::wstring& s, Font* f, const RectF& r, const Color& c,
              StringAlignment h = StringAlignmentNear,
              StringAlignment v = StringAlignmentNear,
              bool wrap = false) {
    if (c.GetA() == 0 || s.empty()) return;
    StringFormat sf;
    sf.SetAlignment(h);
    sf.SetLineAlignment(v);
    sf.SetFormatFlags(wrap ? StringFormatFlagsLineLimit
                           : (StringFormatFlagsNoWrap | StringFormatFlagsNoClip));
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, r, &sf, &b);
}

void roundedPath(GraphicsPath& p, const RectF& r, float rad) {
    float d = rad * 2.f;
    p.AddArc(r.X,              r.Y,               d, d, 180.f, 90.f);
    p.AddArc(r.GetRight() - d, r.Y,               d, d, 270.f, 90.f);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d,   0.f, 90.f);
    p.AddArc(r.X,              r.GetBottom() - d, d, d,  90.f, 90.f);
    p.CloseFigure();
}

bool inRect(const RectF& r, float x, float y) {
    return x >= r.X && x <= r.GetRight() && y >= r.Y && y <= r.GetBottom();
}

// ----------------------------------------------------------------------------
//  Sessions: a running tool, its output buffer, and its input queue
//
//  Each tool runs on its own worker thread. The worker's std::cout, std::cin
//  and IConsoleHost calls are routed to that worker's Session through
//  thread-local pointers, so several tools can run at once.
// ----------------------------------------------------------------------------
// Colours in session output. kNoColor means "not set": the default text colour
// for foreground, transparent for background. Explicit colours are 0xAARRGGBB.
constexpr uint32_t kNoColor    = 0u;
constexpr uint32_t kDefaultFg  = 0xFFE2E2E2u;
constexpr uint32_t kCaretColor = 0xFF7FD0FFu;

// Style bits carried by each cell
constexpr uint8_t kStyleBold      = 1;   // SGR 1
constexpr uint8_t kStyleUnderline = 2;   // SGR 4
constexpr uint8_t kStyleCaret     = 4;   // ESC [ z : insertion point in front of this cell

uint32_t rgbOf(int r, int g, int b) {
    auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    return 0xFF000000u | ((uint32_t)clamp(r) << 16) | ((uint32_t)clamp(g) << 8) | (uint32_t)clamp(b);
}

// Palette index -> colour: 0-15 xterm defaults, 16-231 the 6x6x6 cube, 232-255 greys
uint32_t paletteColor(int n) {
    static const uint32_t base[16] = {
        0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
        0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF
    };
    n = std::max(0, std::min(n, 255));
    if (n < 16) return 0xFF000000u | base[n];
    if (n < 232) {
        static const int level[6] = { 0, 95, 135, 175, 215, 255 };
        int k = n - 16;
        return rgbOf(level[k / 36], level[(k / 6) % 6], level[k % 6]);
    }
    int v = 8 + (n - 232) * 10;
    return rgbOf(v, v, v);
}

struct Cell { wchar_t ch; uint32_t fg; uint32_t bg; uint8_t style; };
using Row = std::vector<Cell>;

struct SessionAborted {};

struct Session {
    int             toolId  = 0;
    int             toolIdx = -1;
    void          (*fn)()   = nullptr;
    std::wstring    title;

    // Shared between the worker thread and the window (guarded by mu)
    std::mutex                mu;
    std::condition_variable   cv;
    std::vector<Row>          lines;      // committed output lines
    Row                       pending;    // unfinished output line
    std::wstring              editBuf;    // what the user is typing
    std::deque<std::string>   inputQ;     // submitted lines (UTF-8)
    std::deque<int>           keyQ;       // keys pressed while no line is being read
    bool waiting  = false;
    bool masked   = false;
    bool finished = false;
    bool aborted  = false;

    // Output state (only touched under mu)
    uint32_t    curFg = kNoColor, curBg = kNoColor;
    uint8_t     curStyle  = 0;
    bool        caretNext = false;
    int         esc    = 0;               // ANSI parser: 0 text, 1 after ESC, 2 in CSI
    std::string csi;
    uint32_t    u8cp   = 0;               // UTF-8 decoder
    int         u8left = 0;

    // Worker thread only
    std::string inBuf;                    // leftover bytes for std::cin
    size_t      inPos = 0;

    void pushLineLocked(Row r) {
        lines.push_back(std::move(r));
        if (lines.size() > 2000) lines.erase(lines.begin(), lines.begin() + 500);
    }

    void commitLocked() {
        pushLineLocked(std::move(pending));
        pending.clear();
        caretNext = false;
    }

    void putLocked(uint32_t cp) {
        uint8_t st = curStyle;
        if (caretNext) { st |= kStyleCaret; caretNext = false; }
        if (cp > 0xFFFF) {                                // surrogate pair
            cp -= 0x10000;
            pending.push_back({ (wchar_t)(0xD800 + (cp >> 10)),   curFg, curBg, st });
            pending.push_back({ (wchar_t)(0xDC00 + (cp & 0x3FF)), curFg, curBg, st });
        } else {
            pending.push_back({ (wchar_t)cp, curFg, curBg, st });
        }
    }

    // SGR (ESC [ ... m): colours, bold, underline and reset. Others are ignored.
    void applySgrLocked(const std::string& p) {
        std::vector<int> v;
        int cur = 0; bool have = false;
        for (char ch : p) {
            if (ch >= '0' && ch <= '9') { cur = std::min(cur * 10 + (ch - '0'), 100000); have = true; }
            else if (ch == ';' || ch == ':') { v.push_back(have ? cur : 0); cur = 0; have = false; }
        }
        v.push_back(have ? cur : 0);

        for (size_t i = 0; i < v.size(); ++i) {
            const int a = v[i];
            if (a == 0)                    { curFg = kNoColor; curBg = kNoColor; curStyle = 0; }
            else if (a == 1)               curStyle = (uint8_t)(curStyle | kStyleBold);
            else if (a == 4)               curStyle = (uint8_t)(curStyle | kStyleUnderline);
            else if (a == 22)              curStyle = (uint8_t)(curStyle & ~kStyleBold);
            else if (a == 24)              curStyle = (uint8_t)(curStyle & ~kStyleUnderline);
            else if (a == 39)              curFg = kNoColor;
            else if (a == 49)              curBg = kNoColor;
            else if (a >= 30 && a <= 37)   curFg = paletteColor(a - 30);
            else if (a >= 90 && a <= 97)   curFg = paletteColor(a - 90 + 8);
            else if (a >= 40 && a <= 47)   curBg = paletteColor(a - 40);
            else if (a >= 100 && a <= 107) curBg = paletteColor(a - 100 + 8);
            else if ((a == 38 || a == 48) && i + 1 < v.size()) {
                uint32_t col = kNoColor;
                if (v[i + 1] == 5 && i + 2 < v.size()) {
                    col = paletteColor(v[i + 2]); i += 2;
                } else if (v[i + 1] == 2 && i + 4 < v.size()) {
                    col = rgbOf(v[i + 2], v[i + 3], v[i + 4]); i += 4;
                } else {
                    break;
                }
                if (a == 38) curFg = col; else curBg = col;
            }
        }
    }

    void csiLocked(unsigned char fin) {
        if (fin == 'm') applySgrLocked(csi);
        else if (fin == 'z' && csi.empty()) caretNext = true;     // caret marker
        else if (fin == 'J' && csi == "2") { lines.clear(); pending.clear(); }
    }

    void appendLocked(const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (esc == 1) {
                if (c == '[') { esc = 2; csi.clear(); } else esc = 0;
                continue;
            }
            if (esc == 2) {
                if (c >= 0x40 && c <= 0x7E) { csiLocked(c); esc = 0; }
                else if (csi.size() < 32)   csi.push_back(static_cast<char>(c));
                continue;
            }
            if (c == 0x1B) { u8left = 0; esc = 1; continue; }

            if (u8left > 0) {                             // continuation byte
                if ((c & 0xC0) == 0x80) {
                    u8cp = (u8cp << 6) | (c & 0x3F);
                    if (--u8left == 0) putLocked(u8cp);
                    continue;
                }
                u8left = 0;                               // broken sequence
                putLocked(0xFFFD);
            }
            if (c == '\r') continue;
            if (c == '\n') { commitLocked(); continue; }
            if (c < 0x80)  { putLocked(c); continue; }
            if ((c & 0xE0) == 0xC0)      { u8cp = c & 0x1F; u8left = 1; }
            else if ((c & 0xF0) == 0xE0) { u8cp = c & 0x0F; u8left = 2; }
            else if ((c & 0xF8) == 0xF0) { u8cp = c & 0x07; u8left = 3; }
            else                         putLocked(0xFFFD);
        }
    }

    // Worker thread
    void write(const std::string& s) {
        std::lock_guard<std::mutex> g(mu);
        appendLocked(s.data(), s.size());
    }

    void clearOutput() {
        std::lock_guard<std::mutex> g(mu);
        lines.clear();
        pending.clear();
        caretNext = false;
    }

    // Worker thread: blocks until the user submits a line
    std::string readInput(bool maskInput) {
        std::unique_lock<std::mutex> lk(mu);
        if (aborted) throw SessionAborted{};
        keyQ.clear();                                   // keys typed before the prompt are stale
        waiting = true;
        masked  = maskInput;
        editBuf.clear();
        cv.wait(lk, [this] { return !inputQ.empty() || aborted; });
        if (inputQ.empty()) { waiting = false; throw SessionAborted{}; }
        std::string line = std::move(inputQ.front());
        inputQ.pop_front();
        return line;
    }

    void pushKeyLocked(int code) {
        keyQ.push_back(code);
        if (keyQ.size() > 64) keyQ.pop_front();
        cv.notify_all();
    }

    // UI thread
    void key(wchar_t ch) {
        std::lock_guard<std::mutex> g(mu);
        if (!waiting) { pushKeyLocked(ch); return; }
        if (ch == 13) {                                    // Enter: echo and submit
            for (wchar_t c : editBuf) pending.push_back({ masked ? L'*' : c, kNoColor, kNoColor, 0 });
            inputQ.push_back(wToUtf8(editBuf));
            commitLocked();
            editBuf.clear();
            waiting = false;
            cv.notify_all();
        } else if (ch == 8) {                              // Backspace
            if (!editBuf.empty()) editBuf.pop_back();
        } else if (ch == 27) {                             // Esc clears the line
            editBuf.clear();
        } else if (ch >= 32 && editBuf.size() < 300) {
            editBuf.push_back(ch);
        }
    }

    // Keys that do not produce a character (arrows, F-keys, ...). They are
    // sent as 0x10000 + virtual-key code, so they never clash with characters.
    void rawKey(WPARAM vk) {
        const bool special = (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
                              vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
                              vk == VK_INSERT || vk == VK_DELETE || (vk >= VK_F1 && vk <= VK_F12));
        if (!special) return;
        std::lock_guard<std::mutex> g(mu);
        if (!waiting) pushKeyLocked(0x10000 + (int)vk);
    }

    bool hasKey() {
        std::lock_guard<std::mutex> g(mu);
        return !keyQ.empty();
    }

    // Worker thread: blocks until a key is pressed
    int readKey() {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [this] { return !keyQ.empty() || aborted; });
        if (keyQ.empty()) throw SessionAborted{};
        int k = keyQ.front();
        keyQ.pop_front();
        return k;
    }

    void abort() {
        std::lock_guard<std::mutex> g(mu);
        aborted = true;
        cv.notify_all();
    }

    void finish() {
        std::lock_guard<std::mutex> g(mu);
        if (!pending.empty()) commitLocked();
        waiting  = false;
        finished = true;
        cv.notify_all();
    }
};

thread_local Session* t_session = nullptr;

// Output dispatcher: session output when the calling thread owns a session,
// otherwise the original stream buffer.
class DispatchOut final : public std::streambuf {
public:
    std::streambuf* fallback = nullptr;

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        if (!t_session) return fallback ? fallback->sputc(ch) : traits_type::eof();
        t_session->write(std::string(1, ch));
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (!t_session) return fallback ? fallback->sputn(s, n) : 0;
        t_session->write(std::string(s, static_cast<size_t>(n)));
        return n;
    }
    int sync() override {
        if (t_session) return 0;
        return fallback ? fallback->pubsync() : 0;
    }
};

// Input dispatcher: each line typed in a session becomes one line for cin.
// The newline is kept so that cin.ignore() / clearInputBuffer() behave as in a console.
class DispatchIn final : public std::streambuf {
public:
    std::streambuf* fallback = nullptr;

protected:
    int_type underflow() override {
        if (!t_session) return fallback ? fallback->sgetc() : traits_type::eof();
        Session& s = *t_session;
        if (s.inPos >= s.inBuf.size()) {
            s.inBuf = s.readInput(false);
            s.inBuf.push_back('\n');
            s.inPos = 0;
        }
        return traits_type::to_int_type(s.inBuf[s.inPos]);
    }
    int_type uflow() override {
        if (!t_session) return fallback ? fallback->sbumpc() : traits_type::eof();
        int_type c = underflow();
        if (!traits_type::eq_int_type(c, traits_type::eof())) ++t_session->inPos;
        return c;
    }
    std::streamsize showmanyc() override { return 0; }
};

DispatchOut gCoutBuf;
DispatchOut gCerrBuf;
DispatchIn  gCinBuf;

// Console host for tools running inside a session
class GuiConsoleHost final : public IConsoleHost {
public:
    void clear() override {
        if (t_session) t_session->clearOutput();
    }
    std::string readLine(const std::string& prompt) override {
        if (!t_session) return std::string();
        t_session->write(prompt);
        return t_session->readInput(false);
    }
    std::string readPassword(const std::string& prompt) override {
        if (!t_session) return std::string();
        t_session->write(prompt);
        return t_session->readInput(true);
    }
    bool keyWaiting() override { return t_session && t_session->hasKey(); }
    int  readKey()     override { return t_session ? t_session->readKey() : -1; }

    void waitForEnter(const std::string& msg) override {
        if (!t_session) return;
        t_session->write(msg);
        (void)t_session->readInput(false);
    }
};
GuiConsoleHost gHost;

void sessionMain(std::shared_ptr<Session> s) {
    t_session = s.get();
    g_consoleHost = &gHost;
    try {
        if (s->fn) s->fn();
    } catch (const SessionAborted&) {
        // Closed by the user: nothing more to show
    } catch (const std::exception& e) {
        s->write(std::string("\n  [tool stopped: ") + e.what() + "]\n");
    } catch (...) {
        s->write("\n  [tool stopped unexpectedly]\n");
    }
    s->finish();
    g_consoleHost = nullptr;
    t_session = nullptr;
}

// ----------------------------------------------------------------------------
//  App state (main window)
// ----------------------------------------------------------------------------
struct Tile { RectF r; };

struct App {
    HWND    hwnd = nullptr;
    int     cw = (int)kW, ch = (int)kH;
    float   S  = 1.f;
    Bitmap* back = nullptr;

    std::vector<Tool> tools;
    std::vector<Tile> tiles;
    std::vector<char> match;

    std::wstring query;
    std::wstring hint;

    int   hover = -1;
    int   sel   = -1;
    float mx = -100, my = -100;
    bool  hand = false;

    int    launchIdx = -1;
    double launchAt  = 0;
    double t         = 0;
    int    runRequest = -1;

    bool  about  = false;
    float aboutT = 0;

    int    launched     = 0;
    double sessionStart = 0;

    // Tool embedded in the main window (null = hub is showing)
    std::shared_ptr<Session> session;
    std::wstring notice;
    bool exitDialog = false;   // the Exit [22] choice box is showing
};
App A;

std::shared_ptr<Session> spawnSession(int idx) {
    auto s = std::make_shared<Session>();
    const Tool& tl = A.tools[idx];
    s->toolId  = tl.id;
    s->toolIdx = idx;
    s->fn      = tl.fn;
    s->title   = widen(tl.fullName);
    std::thread([s]() { sessionMain(s); }).detach();
    return s;
}

// Starts this program again as "--tool N" in a brand-new console window.
// That process owns its console, so the window closes when the tool exits.
// Starts this program as "--tool N" in its own console window. Returns an
// empty string on success, otherwise the reason it failed.
std::wstring launchInCli(int toolId) {
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH))
        return L"the program file could not be found";

    std::wstring cmd = L"\"";
    cmd += exe;
    cmd += L"\" --tool " + std::to_wstring(toolId);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;   // zeroed = SW_HIDE, which hid the console
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe, buf.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS, nullptr, nullptr, &si, &pi))
        return L"CreateProcess failed (Windows error " + std::to_wstring(GetLastError()) + L")";

    // Let the new window take focus. Without this it can open behind the hub.
    AllowSetForegroundWindow(pi.dwProcessId);

    // A child that exits straight away has failed to run the tool.
    std::wstring problem;
    if (WaitForSingleObject(pi.hProcess, 300) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        problem = L"the console process exited at once (code " + std::to_wstring(code) + L")";
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return problem;
}

// Text for the pane after an "Open in CLI" request
std::wstring cliNotice(int toolId) {
    std::wstring problem = launchInCli(toolId);
    return problem.empty() ? std::wstring(L"Opened in a console window.")
                           : L"Could not open a console window: " + problem;
}

// ----------------------------------------------------------------------------
//  Sub-windows
// ----------------------------------------------------------------------------
struct Surface {
    HWND    hwnd = nullptr;
    Bitmap* back = nullptr;
    int     cw = 0, ch = 0;
    float   S  = 1.f;
    float   mx = -100, my = -100;
    bool    hand = false;
    std::shared_ptr<Session> session;
    std::wstring notice;

    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    ~Surface() { delete back; }
};

std::vector<std::unique_ptr<Surface>> gSubs;

void removeSub(Surface* p) {
    auto it = std::find_if(gSubs.begin(), gSubs.end(),
                           [p](const std::unique_ptr<Surface>& u) { return u.get() == p; });
    if (it != gSubs.end()) gSubs.erase(it);   // deletes p
}

void closeAllSubs() {
    while (!gSubs.empty() && gSubs.back()->hwnd) DestroyWindow(gSubs.back()->hwnd);
}

// Ends the message loop once no window is left (the program stays open while
// any sub-window still exists)
void maybeQuit() {
    if (!A.hwnd && gSubs.empty()) PostQuitMessage(0);
}

// Exit [22] -> "Exit this window": closes the hub and the tool running in it.
// Sub-windows keep running.
void exitThisWindow() {
    A.exitDialog = false;
    if (A.session) A.session->abort();
    A.session.reset();
    if (A.hwnd) DestroyWindow(A.hwnd);
}

// Exit [22] -> "Exit all windows": closes the hub, every sub-window and every tool.
void exitAllWindows() {
    A.exitDialog = false;
    if (A.session) A.session->abort();
    A.session.reset();
    closeAllSubs();
    if (A.hwnd) DestroyWindow(A.hwnd);
}

void openSubWindow(std::shared_ptr<Session> s) {
    auto surf = std::make_unique<Surface>();
    surf->session = std::move(s);
    Surface* raw = surf.get();
    std::wstring title = L"Random Utilities  -  " + raw->session->title;
    gSubs.push_back(std::move(surf));

    const int n = (int)gSubs.size();
    RECT want = { 0, 0, (LONG)(kSubW * gScale), (LONG)(kSubH * gScale) };
    AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW, FALSE);
    const int off = 40 + ((n - 1) % 6) * 32;

    HWND hw = CreateWindowExW(0, L"RandomUtilitiesSubWnd", title.c_str(), WS_OVERLAPPEDWINDOW,
                              off, off, want.right - want.left, want.bottom - want.top,
                              nullptr, nullptr, GetModuleHandleW(nullptr), raw);
    if (!hw) {
        if (raw->session) raw->session->abort();
        removeSub(raw);
        return;
    }
    ShowWindow(hw, SW_SHOW);
    UpdateWindow(hw);
    SetTimer(hw, 2, 30, nullptr);
}

// ----------------------------------------------------------------------------
//  Search
// ----------------------------------------------------------------------------
void runSearch() {
    A.match.assign(22, 0);
    A.hint.clear();
    if (A.query.empty()) { A.sel = -1; return; }

    std::string q;
    for (wchar_t wc : A.query) q.push_back(wc < 128 ? (char)wc : '?');
    for (char& c : q) c = (char)std::tolower((unsigned char)c);
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back()  == ' ') q.pop_back();
    if (q.empty()) { A.sel = -1; return; }

    bool numeric = true;
    for (unsigned char c : q) if (!std::isdigit(c)) { numeric = false; break; }
    if (numeric) {
        int n = q.size() <= 3 ? std::atoi(q.c_str()) : -1;
        if (n >= 1 && n <= 21) {
            A.match[n - 1] = 1;
            A.sel = n - 1;
            A.hint = L"Tool [" + std::to_wstring(n) + L"]";
        } else if (n == 22) {
            A.sel = kExitSlot;
            A.hint = L"Exit System";
        } else {
            A.hint = L"No tool numbered " + A.query;
        }
        return;
    }

    int best = -1, minDist = 999, count = 0;
    for (int i = 0; i < kTileCount; ++i) {
        const Tool& tl = A.tools[i];
        std::string ln = tl.fullName;
        for (char& c : ln) c = (char)std::tolower((unsigned char)c);
        bool sub = ln.find(q) != std::string::npos;
        for (const auto& kw : tl.keys) if (kw.find(q) != std::string::npos) sub = true;
        if (sub) { A.match[i] = 1; ++count; if (A.sel < 0) A.sel = i; continue; }
        int d = levenshteinDistance(q, ln);
        if (d < minDist && d <= 3) { minDist = d; best = i; }
        for (const auto& kw : tl.keys) {
            int kd = levenshteinDistance(q, kw);
            if (kd < minDist && kd <= 2) { minDist = kd; best = i; }
        }
    }
    if (A.sel >= 0) {
        A.hint = std::wstring(A.tools[A.sel].name);
        if (count > 1) A.hint += L"   (+" + std::to_wstring(count - 1) + L" more)";
    } else if (best >= 0) {
        A.match[best] = 1;
        A.sel = best;
        A.hint = L"Did you mean " + std::wstring(A.tools[best].name) + L"?  Press Enter";
    } else {
        A.hint = L"No features found for '" + A.query + L"'.";
    }
}

// ----------------------------------------------------------------------------
//  Hub layout and hit testing
// ----------------------------------------------------------------------------
RectF searchRect()  { return RectF(kMargin, 120.f, 600.f, 40.f); }
RectF exitRect()    { return RectF(kW - 120.f, 26.f, 90.f, 32.f); }
RectF scrollTrack() { return RectF(12.f, kGridY, 10.f, 6.f * kTileH + 5.f * kTileGap); }

void layoutTiles() {
    A.tiles.assign(kTileCount, Tile());
    for (int i = 0; i < kTileCount; ++i) {
        int c = i % kCols, r = i / kCols;
        A.tiles[i].r = RectF(kGridX + c * (kTileW + kTileGap),
                             kGridY + r * (kTileH + kTileGap),
                             kTileW, kTileH);
    }
}

int hitTile(float x, float y) {
    for (int i = 0; i < kTileCount; ++i)
        if (inRect(A.tiles[i].r, x, y)) return i;
    return -1;
}

// Buttons shown in the top-right corner of a tool view
enum BtnId { BTN_SUBWIN = 1, BTN_CLI, BTN_HUB, BTN_DOCK, BTN_CLOSE };

struct Btn { RectF r; const wchar_t* label; int id; };

std::vector<Btn> toolButtons(bool sub, float LW) {
    const float w = 156.f, h = 32.f, gap = 10.f, y = 18.f;
    const float x0 = LW - kMargin - 3.f * w - 2.f * gap;
    std::vector<Btn> b;
    if (sub) {
        b.push_back({ RectF(x0,                  y, w, h), L"Open in Graphics", BTN_DOCK });
        b.push_back({ RectF(x0 + (w + gap),      y, w, h), L"Open in CLI",      BTN_CLI });
        b.push_back({ RectF(x0 + 2 * (w + gap),  y, w, h), L"Close",            BTN_CLOSE });
    } else {
        b.push_back({ RectF(x0,                  y, w, h), L"Sub-window",       BTN_SUBWIN });
        b.push_back({ RectF(x0 + (w + gap),      y, w, h), L"Open in CLI",      BTN_CLI });
        b.push_back({ RectF(x0 + 2 * (w + gap),  y, w, h), L"\u2190 Hub",       BTN_HUB });
    }
    return b;
}

int hitBtn(const std::vector<Btn>& bs, float x, float y) {
    for (const auto& b : bs) if (inRect(b.r, x, y)) return b.id;
    return -1;
}

// ----------------------------------------------------------------------------
//  Actions
// ----------------------------------------------------------------------------
void launchTool(int idx) {
    if (idx < 0 || idx >= kTileCount) return;
    A.launchIdx = idx;
    A.launchAt  = A.t + 0.35;
    A.sel       = idx;
}

void leaveToolView() {
    if (A.session) A.session->abort();
    A.session.reset();
    A.notice.clear();
    A.query.clear();
    runSearch();
    A.hover = -1;
}

void runFromHub(int idx) {
    if (idx < 0 || idx >= kTileCount) return;
    const Tool& tl = A.tools[idx];
    if (toolNeedsConsole(tl.id)) {
        A.hint = cliNotice(tl.id);
        return;
    }
    A.session = spawnSession(idx);
    A.notice.clear();
    A.query.clear();
    ++A.launched;
}

void mainToolAction(int id) {
    if (!A.session) return;
    switch (id) {
    case BTN_SUBWIN: {
        std::shared_ptr<Session> s = A.session;
        A.session.reset();
        A.notice.clear();
        openSubWindow(s);
        break;
    }
    case BTN_CLI:
        A.notice = cliNotice(A.session->toolId);
        break;
    case BTN_HUB:
        leaveToolView();
        break;
    default:
        break;
    }
}

// Called from a sub-window's message handler. Any action that destroys the
// window returns immediately, because the Surface is freed during WM_DESTROY.
void subToolAction(Surface& s, int id) {
    switch (id) {
    case BTN_DOCK:
        if (!s.session) return;
        if (!A.hwnd) { s.notice = L"The main window has been closed."; return; }
        if (A.session) {
            s.notice = L"Main window is busy. Press \u2190 Hub there first.";
            return;
        }
        A.session = s.session;
        A.notice.clear();
        s.session.reset();
        {
            HWND hw = s.hwnd;
            DestroyWindow(hw);
        }
        ShowWindow(A.hwnd, SW_SHOW);
        SetForegroundWindow(A.hwnd);
        return;
    case BTN_CLI:
        if (!s.session) return;
        s.notice = cliNotice(s.session->toolId);
        return;
    case BTN_CLOSE:
        DestroyWindow(s.hwnd);
        return;
    default:
        return;
    }
}

void update(float dt) {
    A.t += dt;
    A.aboutT += ((A.about ? 1.f : 0.f) - A.aboutT) * (1.f - std::exp(-14.f * dt));

    if (A.launchIdx != -1 && A.t >= A.launchAt) {
        int idx = A.launchIdx;
        A.launchIdx = -1;
        if (A.tools[idx].fn == nullptr) {
            A.about = true;
        } else {
            A.runRequest = idx;
        }
    }
}

// ----------------------------------------------------------------------------
//  Colours
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
//  Branding: logo and window icons (file name is set in RandomUtilities.h)
// ----------------------------------------------------------------------------
std::unique_ptr<Bitmap> gLogo;          // null when the logo file was not found
HICON gBigIcon   = nullptr;
HICON gSmallIcon = nullptr;

std::wstring exeFolder() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
}

void loadLogo() {
    const std::wstring candidates[] = { exeFolder() + kLogoFileName, std::wstring(kLogoFileName) };
    for (const auto& path : candidates) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        auto bmp = std::make_unique<Bitmap>(path.c_str());
        if (bmp->GetLastStatus() == Ok && bmp->GetWidth() > 0 && bmp->GetHeight() > 0) {
            gLogo = std::move(bmp);
            return;
        }
    }
}

// Width of the logo when drawn at height h (0 if there is no logo)
float logoWidth(float h) {
    if (!gLogo || gLogo->GetHeight() == 0) return 0.f;
    return h * (float)gLogo->GetWidth() / (float)gLogo->GetHeight();
}

void drawLogo(Graphics& g, float x, float y, float h) {
    float w = logoWidth(h);
    if (w <= 0.f) return;
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(gLogo.get(), RectF(x, y, w, h));
}

// Square icon of the logo, centred on a transparent background
HICON makeIcon(int px) {
    if (!gLogo) return nullptr;
    float w = (float)gLogo->GetWidth(), h = (float)gLogo->GetHeight();
    if (w <= 0.f || h <= 0.f) return nullptr;

    Bitmap sq(px, px, PixelFormat32bppARGB);
    {
        Graphics g(&sq);
        g.Clear(Color(0, 0, 0, 0));
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        float s  = std::min((float)px / w, (float)px / h);
        float dw = w * s, dh = h * s;
        g.DrawImage(gLogo.get(), RectF(((float)px - dw) / 2.f, ((float)px - dh) / 2.f, dw, dh));
    }
    HICON icon = nullptr;
    sq.GetHICON(&icon);
    return icon;
}

const Color kBg       (255, 255, 255, 255);
const Color kInk      (255,  32,  32,  32);
const Color kInkSoft  (255, 120, 120, 120);
const Color kInkFaint (255, 170, 170, 170);
const Color kLine     (255, 190, 190, 190);
const Color kTileFill (255, 250, 250, 250);
const Color kTileHov  (255, 235, 244, 255);
const Color kTileBrd  (255, 140, 140, 140);
const Color kTileHovB (255,  60, 120, 220);
const Color kSearch   (255, 246, 246, 246);
const Color kAccent   (255,  60, 120, 220);

// ----------------------------------------------------------------------------
//  Hub drawing
// ----------------------------------------------------------------------------
void drawHeader(Graphics& g) {
    const float lw = logoWidth(kLogoHeightHub);
    if (lw > 0) drawLogo(g, kMargin, 14.f, kLogoHeightHub);
    const float tx = kMargin + (lw > 0 ? lw + 16.f : 0.f);

    drawText(g, L"Random Utilities", font(28.f, FontStyleBold),
             RectF(tx, 22.f, 600.f, 34.f), kInk);
    drawText(g, L"Developed by Shivansh", font(12.f),
             RectF(tx, 60.f, 500.f, 18.f), kInkSoft);
    drawText(g, L"Use System Info for info about the program",
             font(11.f, FontStyleItalic),
             RectF(tx, 80.f, 500.f, 16.f), kInkFaint);

    RectF r = exitRect();
    bool hot = inRect(r, A.mx, A.my) || A.sel == kExitSlot;
    GraphicsPath p; roundedPath(p, r, 6.f);
    SolidBrush fb(hot ? kTileHov : kTileFill);
    g.FillPath(&fb, &p);
    Pen pen(hot ? kTileHovB : kTileBrd, hot ? 1.6f : 1.0f);
    g.DrawPath(&pen, &p);
    drawText(g, L"Exit  [22]", font(12.f, FontStyleBold), r, kInk,
             StringAlignmentCenter, StringAlignmentCenter);
}

void drawSearch(Graphics& g) {
    RectF r = searchRect();
    GraphicsPath p; roundedPath(p, r, 8.f);
    SolidBrush fb(kSearch);
    g.FillPath(&fb, &p);
    Pen pen(!A.query.empty() ? kAccent : kLine, !A.query.empty() ? 1.6f : 1.0f);
    g.DrawPath(&pen, &p);

    Pen mp(kInkSoft, 1.6f);
    g.DrawEllipse(&mp, r.X + 14.f, r.Y + 12.f, 13.f, 13.f);
    g.DrawLine(&mp, r.X + 24.f, r.Y + 23.f, r.X + 30.f, r.Y + 29.f);

    Font* f = font(15.f);
    RectF tr(r.X + 42.f, r.Y, r.Width - 56.f, r.Height);
    if (A.query.empty()) {
        drawText(g, L"Search For A Function", f, tr, kInkFaint,
                 StringAlignmentNear, StringAlignmentCenter);
    } else {
        drawText(g, A.query, f, tr, kInk,
                 StringAlignmentNear, StringAlignmentCenter);
    }

    if (!A.hint.empty()) {
        drawText(g, A.hint, font(12.f),
                 RectF(r.GetRight() + 16.f, r.Y, kW - r.GetRight() - 40.f, r.Height),
                 kInkSoft, StringAlignmentNear, StringAlignmentCenter);
    }
}

void drawScrollBar(Graphics& g) {
    RectF track = scrollTrack();
    GraphicsPath pt; roundedPath(pt, track, 5.f);
    SolidBrush tb(Color(255, 240, 240, 240));
    g.FillPath(&tb, &pt);
    Pen tp(kLine, 1.f);
    g.DrawPath(&tp, &pt);

    RectF thumb(track.X + 1.f, track.Y + 1.f, track.Width - 2.f, 60.f);
    GraphicsPath pth; roundedPath(pth, thumb, 4.f);
    SolidBrush fb(Color(255, 200, 200, 200));
    g.FillPath(&fb, &pth);
    Pen fp(kTileBrd, 1.f);
    g.DrawPath(&fp, &pth);
}

void drawTile(Graphics& g, int i) {
    const Tool& tl = A.tools[i];
    RectF r = A.tiles[i].r;
    bool isHover = (A.hover == i);
    bool isSel   = (A.sel == i);
    bool dim     = !A.query.empty() && !A.match[i];

    GraphicsPath p; roundedPath(p, r, kRadius);
    SolidBrush fb((isHover || isSel) ? kTileHov : kTileFill);
    g.FillPath(&fb, &p);
    Pen pen((isHover || isSel) ? kTileHovB : kTileBrd,
            (isHover || isSel) ? 1.8f : 1.0f);
    g.DrawPath(&pen, &p);

    Color ink = dim ? Color(255, 190, 190, 190) : kInk;

    drawText(g, L"[" + std::to_wstring(tl.id) + L"]",
             font(10.f, FontStyleBold),
             RectF(r.X + 10.f, r.Y + 6.f, 40.f, 14.f), kInkFaint);

    drawText(g, std::wstring(tl.name), font(16.f, FontStyleBold),
             RectF(r.X + 6.f, r.Y + 24.f, r.Width - 12.f, 30.f),
             ink, StringAlignmentCenter, StringAlignmentCenter);
}

void drawTooltip(Graphics& g) {
    int idx = A.hover;
    if (idx < 0 || idx >= kTileCount) return;

    const Tool& tl = A.tools[idx];
    if (!tl.description || !*tl.description) return;

    const float tipW = 340.f;
    const float pad  = 12.f;

    std::wstring descW(tl.description);

    Font* f = font(12.f);
    RectF measured;
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsLineLimit);
    RectF probe(0.f, 0.f, tipW - 2.f * pad, 2000.f);
    g.MeasureString(descW.c_str(), -1, f, probe, &sf, &measured);
    float tipH = measured.Height + 2.f * pad + 8.f;
    if (tipH < 60.f) tipH = 60.f;

    RectF tile = A.tiles[idx].r;
    float tx = tile.GetRight() + 8.f;
    if (tx + tipW > kW - kMargin) tx = tile.X - tipW - 8.f;
    float ty = tile.Y;
    if (ty + tipH > kH - kMargin) ty = kH - kMargin - tipH;
    if (ty < kMargin) ty = kMargin;

    RectF r(tx, ty, tipW, tipH);
    GraphicsPath p; roundedPath(p, r, 8.f);
    SolidBrush fb(Color(255, 255, 255, 255));
    g.FillPath(&fb, &p);
    Pen pen(kAccent, 1.4f);
    g.DrawPath(&pen, &p);

    drawText(g, widen(tl.fullName), font(13.f, FontStyleBold),
             RectF(r.X + pad, r.Y + pad, r.Width - 2.f * pad, 18.f), kInk);

    drawText(g, descW, f,
             RectF(r.X + pad, r.Y + pad + 22.f, r.Width - 2.f * pad, r.Height - pad * 2.f - 22.f),
             kInkSoft, StringAlignmentNear, StringAlignmentNear, true);
}

void drawStatusBar(Graphics& g) {
    int secs = (int)(A.t - A.sessionStart);
    wchar_t buf[96];
    std::wstring verW = widen(kVersion);
    swprintf(buf, 96, L"v%ls   session %02d:%02d   launched %d",
             verW.c_str(), secs / 60, secs % 60, A.launched);
    drawText(g, std::wstring(buf), font(11.f),
             RectF(kMargin, kH - 26.f, 600.f, 16.f), kInkFaint);
}

void drawAbout(Graphics& g) {
    if (A.aboutT < 0.01f) return;
    float a = A.aboutT;

    SolidBrush dim(Color((BYTE)(140 * a), 0, 0, 0));
    g.FillRectangle(&dim, 0.f, 0.f, kW, kH);

    RectF p(kW / 2 - 260.f, kH / 2 - 150.f, 520.f, 300.f);
    GraphicsPath gp; roundedPath(gp, p, 12.f);
    SolidBrush bg(Color((BYTE)(255 * a), 255, 255, 255));
    g.FillPath(&bg, &gp);
    Pen pen(Color((BYTE)(255 * a), 40, 40, 40), 1.6f);
    g.DrawPath(&pen, &gp);

    const float lw = logoWidth(36.f);
    if (lw > 0) drawLogo(g, p.X + 24.f, p.Y + 16.f, 36.f);
    drawText(g, L"SYSTEM INFO", font(22.f, FontStyleBold),
             RectF(p.X + 24.f + (lw > 0 ? lw + 12.f : 0.f), p.Y + 20.f, p.Width - 48.f, 30.f),
             Color((BYTE)(255 * a), 20, 20, 20));

    Pen sep(Color((BYTE)(255 * a), 200, 200, 200), 1.f);
    g.DrawLine(&sep, p.X + 24.f, p.Y + 58.f, p.GetRight() - 24.f, p.Y + 58.f);

    std::wstring buildDate = widen(__DATE__);

    struct Row { std::wstring k; std::wstring v; };
    Row rows[] = {
        { L"Developer",    L"Shivansh" },
        { L"Version",      L"1.3.0" },
        { L"Build Date",   buildDate },
        { L"Architecture", L"Standard C++ Separation" },
        { L"Interface",    L"GDI+ window (tools open in the main window; Open in CLI for classic)" },
        { L"Code",         L"13929-0611-v1.3.0" },
    };
    float y = p.Y + 78.f;
    for (auto& row : rows) {
        drawText(g, row.k, font(13.f),
                 RectF(p.X + 24.f, y, 130.f, 22.f),
                 Color((BYTE)(255 * a), 110, 110, 110));
        drawText(g, row.v, font(13.f, FontStyleBold),
                 RectF(p.X + 160.f, y, p.Width - 184.f, 22.f),
                 Color((BYTE)(255 * a), 30, 30, 30));
        y += 28.f;
    }

    drawText(g, L"click or press any key to close", font(11.f, FontStyleItalic),
             RectF(p.X, p.GetBottom() - 30.f, p.Width, 18.f),
             Color((BYTE)(255 * a), 160, 160, 160),
             StringAlignmentCenter, StringAlignmentCenter);
}

void renderHub(Graphics& g) {
    drawHeader(g);
    drawSearch(g);
    drawScrollBar(g);
    for (int i = 0; i < kTileCount; ++i) drawTile(g, i);
    drawTooltip(g);
    drawStatusBar(g);
    drawAbout(g);
}

// ----------------------------------------------------------------------------
//  Tool view drawing (used by the main window and by sub-windows)
// ----------------------------------------------------------------------------
void drawButton(Graphics& g, const Btn& b, bool hot) {
    GraphicsPath p; roundedPath(p, b.r, 6.f);
    SolidBrush fb(hot ? kTileHov : kTileFill);
    g.FillPath(&fb, &p);
    Pen pen(hot ? kTileHovB : kTileBrd, hot ? 1.6f : 1.0f);
    g.DrawPath(&pen, &p);
    drawText(g, b.label, font(12.f, FontStyleBold), b.r, kInk,
             StringAlignmentCenter, StringAlignmentCenter);
}

// Width of one monospace cell at font f
float monoCharWidth(Graphics& g, Font* f) {
    std::wstring m(100, L'M');
    StringFormat sf(StringFormat::GenericTypographic());
    RectF box;
    g.MeasureString(m.c_str(), 100, f, PointF(0.f, 0.f), &sf, &box);
    return box.Width / 100.f;
}

// Draws one row of cells. Runs of equal style are drawn together. Backgrounds
// are filled cell by cell, underline is drawn under the run, and the caret is a
// thin bar in front of its cell, so the text itself is never changed.
void drawRow(Graphics& g, const Row& row, float x, float y, float charW, float lineH,
             Font* regular, Font* bold) {
    if (row.empty()) return;
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    const uint8_t mask = kStyleBold | kStyleUnderline | kStyleCaret;

    size_t i = 0;
    while (i < row.size()) {
        size_t j = i + 1;
        while (j < row.size() && row[j].fg == row[i].fg && row[j].bg == row[i].bg
               && (row[j].style & mask) == (row[i].style & mask)) ++j;

        const float rx = x + (float)i * charW;
        const float rw = (float)(j - i) * charW;
        const uint8_t st = row[i].style;
        const uint32_t fg = row[i].fg != kNoColor ? row[i].fg : kDefaultFg;

        if (row[i].bg != kNoColor) {
            SolidBrush bb(Color(row[i].bg));
            g.FillRectangle(&bb, rx, y, rw, lineH);
        }
        if (st & kStyleCaret) {
            SolidBrush cb{Color(kCaretColor)};
            g.FillRectangle(&cb, rx, y + 2.f, 2.f, lineH - 4.f);
        }

        std::wstring s;
        s.reserve(j - i);
        bool ink = false;
        for (size_t k = i; k < j; ++k) {
            s.push_back(row[k].ch);
            if (row[k].ch != L' ') ink = true;
        }
        if (ink) {
            SolidBrush fb{Color(fg)};
            g.DrawString(s.c_str(), (INT)s.size(), (st & kStyleBold) ? bold : regular,
                         PointF(rx, y), &sf, &fb);
        }
        if (st & kStyleUnderline) {
            Pen up{Color(fg), 1.f};
            g.DrawLine(&up, rx, y + lineH - 3.f, rx + rw, y + lineH - 3.f);
        }
        i = j;
    }
}

void renderToolView(Graphics& g, Session& s, bool sub, const std::wstring& notice,
                    float mx, float my, float LW, float LH) {
    const RectF pane(kMargin, 96.f, LW - 2.f * kMargin, LH - 96.f - 46.f);
    const float lineH = 17.f;
    Font* mf  = monoFont(13.f);
    Font* mfb = monoFont(13.f, FontStyleBold);
    const float charW = monoCharWidth(g, mf);
    const int rows = std::max(1, (int)((pane.Height - 16.f) / lineH));

    std::vector<Row> tail;
    Row live;
    bool waiting = false, finished = false;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        const size_t keep  = (size_t)(rows - 1);
        const size_t start = s.lines.size() > keep ? s.lines.size() - keep : 0;
        tail.assign(s.lines.begin() + start, s.lines.end());
        live     = s.pending;
        waiting  = s.waiting;
        finished = s.finished;
        if (waiting) {
            for (wchar_t c : s.editBuf) live.push_back({ s.masked ? L'*' : c, kNoColor, kNoColor, 0 });
            live.push_back({ L' ', kNoColor, kNoColor, kStyleCaret });   // caret after the typed text
        }
    }

    // Header: logo, title, status
    const float lw = logoWidth(kLogoHeightTool);
    if (lw > 0) drawLogo(g, kMargin, 12.f, kLogoHeightTool);
    const float tx = kMargin + (lw > 0 ? lw + 12.f : 0.f);

    drawText(g, s.title, font(22.f, FontStyleBold),
             RectF(tx, 16.f, std::max(120.f, LW - tx - kMargin - 500.f), 32.f), kInk);

    std::wstring status;
    if (!notice.empty())  status = notice;
    else if (finished)    status = L"Finished. Go back or open it again with the buttons above.";
    else if (waiting)     status = L"Waiting for your input. Type, then press Enter.";
    else                  status = L"Running...";
    drawText(g, status, font(12.f), RectF(tx, 56.f, LW - tx - kMargin, 18.f), kInkSoft);

    for (const auto& b : toolButtons(sub, LW)) drawButton(g, b, inRect(b.r, mx, my));

    // Terminal pane
    GraphicsPath pp; roundedPath(pp, pane, 8.f);
    SolidBrush pb(Color(255, 28, 28, 34));
    g.FillPath(&pb, &pp);
    Pen pbrd(kLine, 1.f);
    g.DrawPath(&pbrd, &pp);

    g.SetClip(pane);
    float y = pane.Y + 8.f;
    for (const Row& r : tail) {
        drawRow(g, r, pane.X + 12.f, y, charW, lineH, mf, mfb);
        y += lineH;
    }
    drawRow(g, live, pane.X + 12.f, y, charW, lineH, mf, mfb);
    g.ResetClip();

    drawText(g, L"Enter sends the line you are typing.  Esc clears it.",
             font(11.f, FontStyleItalic),
             RectF(kMargin, LH - 30.f, LW - 2.f * kMargin, 16.f), kInkFaint);
}

// ----------------------------------------------------------------------------
//  Backbuffers and presentation
// ----------------------------------------------------------------------------
void prepGraphics(Graphics& g, float S) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.ResetTransform();
    g.ScaleTransform(S, S);
}

void rebuildBack(HWND hwnd, Bitmap*& back, int& cw, int& ch, float& S, float LW, float LH) {
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    cw = w; ch = h;
    S = std::min((float)w / LW, (float)h / LH);
    delete back;
    back = new Bitmap(w, h, PixelFormat32bppPARGB);
}

void presentBack(HDC dc, Bitmap* back, int cw, int ch) {
    if (!back) return;
    Graphics sg(dc);
    sg.SetCompositingMode(CompositingModeSourceCopy);
    sg.DrawImage(back, 0, 0, cw, ch);
}

// ----------------------------------------------------------------------------
//  Exit choice box (Exit [22] and the hub's close button)
// ----------------------------------------------------------------------------
enum ExitChoice { EXIT_THIS = 1, EXIT_ALL, EXIT_CANCEL };

RectF exitBox() { return RectF(kW / 2 - 300.f, kH / 2 - 150.f, 600.f, 300.f); }

std::vector<Btn> exitButtons() {
    const RectF box = exitBox();
    const float w = 170.f, h = 36.f, gap = 14.f;
    const float x0 = box.X + (box.Width - (3.f * w + 2.f * gap)) / 2.f;
    const float y  = box.GetBottom() - 62.f;
    std::vector<Btn> b;
    b.push_back({ RectF(x0,                  y, w, h), L"Exit this window",  EXIT_THIS });
    b.push_back({ RectF(x0 + (w + gap),      y, w, h), L"Exit all windows", EXIT_ALL });
    b.push_back({ RectF(x0 + 2.f * (w + gap), y, w, h), L"Cancel",           EXIT_CANCEL });
    return b;
}

void drawExitDialog(Graphics& g) {
    SolidBrush dim(Color(150, 0, 0, 0));
    g.FillRectangle(&dim, 0.f, 0.f, kW, kH);

    const RectF box = exitBox();
    GraphicsPath gp; roundedPath(gp, box, 12.f);
    SolidBrush bg(kBg);
    g.FillPath(&bg, &gp);
    Pen pen(kTileBrd, 1.6f);
    g.DrawPath(&pen, &gp);

    const float pad = 28.f, tw = box.Width - 2.f * pad;
    drawText(g, L"Exit Random Utilities", font(20.f, FontStyleBold),
             RectF(box.X + pad, box.Y + 22.f, tw, 30.f), kInk);
    drawText(g, L"Choose what to close:", font(13.f),
             RectF(box.X + pad, box.Y + 62.f, tw, 20.f), kInkSoft);
    drawText(g, L"Exit this window closes the hub and the tool running in it. Sub-windows keep running, "
                 L"and the program stays open until they are closed.",
             font(12.f), RectF(box.X + pad, box.Y + 94.f, tw, 44.f), kInk,
             StringAlignmentNear, StringAlignmentNear, true);
    drawText(g, L"Exit all windows closes the hub, every sub-window and every running tool.",
             font(12.f), RectF(box.X + pad, box.Y + 146.f, tw, 22.f), kInk);

    for (const auto& b : exitButtons()) drawButton(g, b, inRect(b.r, A.mx, A.my));
}

void renderMain() {
    if (!A.back) return;
    Graphics g(A.back);
    g.ResetTransform();
    g.Clear(kBg);                          // fill the whole window, including any margin
    prepGraphics(g, A.S);
    if (A.session) renderToolView(g, *A.session, false, A.notice, A.mx, A.my, kW, kH);
    else           renderHub(g);
    if (A.exitDialog) drawExitDialog(g);
}

void renderSurface(Surface& s) {
    if (!s.back || !s.hwnd) return;
    {
        Graphics g(s.back);
        g.ResetTransform();
        g.Clear(kBg);
        prepGraphics(g, s.S);
        if (s.session) renderToolView(g, *s.session, true, s.notice, s.mx, s.my, kSubW, kSubH);
    }
    HDC dc = GetDC(s.hwnd);
    presentBack(dc, s.back, s.cw, s.ch);
    ReleaseDC(s.hwnd, dc);
}

// ----------------------------------------------------------------------------
//  Input (main window)
// ----------------------------------------------------------------------------
void onChar(wchar_t ch) {
    if (A.exitDialog) { if (ch == 27) A.exitDialog = false; return; }
    if (A.about) { A.about = false; return; }
    if (A.session) { A.session->key(ch); return; }
    if (A.launchIdx != -1) return;

    if (ch == 8) {
        if (!A.query.empty()) { A.query.pop_back(); runSearch(); }
        return;
    }
    if (ch == 27) {
        if (!A.query.empty()) { A.query.clear(); runSearch(); }
        return;
    }
    if (ch == 13) {
        if (A.sel == kExitSlot) { A.exitDialog = true; return; }
        if (A.sel >= 0 && A.sel < kTileCount) { launchTool(A.sel); return; }
        return;
    }
    if (ch >= 32 && A.query.size() < 60) {
        if (ch == L' ' && A.query.empty()) return;
        A.query.push_back(ch);
        runSearch();
    }
}

void onKeyDown(WPARAM vk) {
    if (A.exitDialog) return;
    if (A.session) { A.session->rawKey(vk); return; }
    if (A.about) { A.about = false; return; }
    if (A.launchIdx != -1) return;

    if (vk == VK_F1)     { A.about = true; return; }
    if (vk == VK_DELETE) { A.query.clear(); runSearch(); return; }
    if (vk == VK_ESCAPE) { A.query.clear(); runSearch(); return; }

    int s = A.sel;
    if (s == kExitSlot) s = kTileCount - 1;
    switch (vk) {
        case VK_LEFT:  s = (s < 0) ? 0 : std::max(0, s - 1); break;
        case VK_RIGHT: s = (s < 0) ? 0 : std::min(kTileCount - 1, s + 1); break;
        case VK_UP:    s = (s < 0) ? 0 : (s >= kCols ? s - kCols : s); break;
        case VK_DOWN:  s = (s < 0) ? 0 : (s + kCols < kTileCount ? s + kCols : s); break;
        case VK_HOME:  s = 0; break;
        case VK_END:   s = kTileCount - 1; break;
        case VK_TAB:   A.sel = kExitSlot; return;
        default: return;
    }
    A.sel = s;
}

void onMouseMove(float x, float y) {
    A.mx = x; A.my = y;
    if (A.session) {
        A.hover = -1;
        A.hand  = hitBtn(toolButtons(false, kW), x, y) >= 0;
        return;
    }
    int h = hitTile(x, y);
    A.hover = h;
    A.hand  = h >= 0 || inRect(exitRect(), x, y);
}

void onMouseDown(float x, float y) {
    if (A.about) { A.about = false; return; }
    if (A.exitDialog) {
        const int b = hitBtn(exitButtons(), x, y);
        if (b == EXIT_THIS)        exitThisWindow();
        else if (b == EXIT_ALL)    exitAllWindows();
        else if (b == EXIT_CANCEL) A.exitDialog = false;
        return;
    }
    if (A.session) {
        int b = hitBtn(toolButtons(false, kW), x, y);
        if (b >= 0) mainToolAction(b);
        return;
    }
    if (A.launchIdx != -1) return;
    if (inRect(exitRect(), x, y)) { A.exitDialog = true; return; }
    int h = hitTile(x, y);
    if (h >= 0) { launchTool(h); return; }
}

// ----------------------------------------------------------------------------
//  Window procedures
// ----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: {
        static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
        static LARGE_INTEGER last = [] { LARGE_INTEGER n; QueryPerformanceCounter(&n); return n; }();
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) return 0;
        update(dt);
        renderMain();
        HDC dc = GetDC(hwnd);
        presentBack(dc, A.back, A.cw, A.ch);
        ReleaseDC(hwnd, dc);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        presentBack(dc, A.back, A.cw, A.ch);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) rebuildBack(hwnd, A.back, A.cw, A.ch, A.S, kW, kH);
        return 0;
    case WM_CHAR:        onChar((wchar_t)wp); return 0;
    case WM_KEYDOWN:     onKeyDown(wp);        return 0;
    case WM_LBUTTONDOWN: onMouseDown((float)(short)LOWORD(lp) / A.S,
                                     (float)(short)HIWORD(lp) / A.S); return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        onMouseMove((float)(short)LOWORD(lp) / A.S,
                    (float)(short)HIWORD(lp) / A.S);
        return 0;
    }
    case WM_MOUSELEAVE: A.mx = A.my = -100; A.hover = -1; A.hand = false; return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, A.hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = (LONG)(640 * gScale);
        mmi->ptMinTrackSize.y = (LONG)(420 * gScale);
        return 0;
    }
    case WM_CLOSE:
        A.exitDialog = true;
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        A.hwnd = nullptr;
        maybeQuit();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK SubWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* s  = static_cast<Surface*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    Surface* s = reinterpret_cast<Surface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!s) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_TIMER:
        renderSurface(*s);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        presentBack(dc, s->back, s->cw, s->ch);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) rebuildBack(hwnd, s->back, s->cw, s->ch, s->S, kSubW, kSubH);
        return 0;
    case WM_KEYDOWN:
        if (s->session) s->session->rawKey(wp);
        return 0;
    case WM_CHAR:
        if (s->session) s->session->key((wchar_t)wp);
        return 0;
    case WM_MOUSEMOVE: {
        float x = (float)(short)LOWORD(lp) / s->S;
        float y = (float)(short)HIWORD(lp) / s->S;
        s->mx = x;
        s->my = y;
        s->hand = hitBtn(toolButtons(true, kSubW), x, y) >= 0;
        return 0;
    }
    case WM_LBUTTONDOWN: {
        float x = (float)(short)LOWORD(lp) / s->S;
        float y = (float)(short)HIWORD(lp) / s->S;
        int b = hitBtn(toolButtons(true, kSubW), x, y);
        if (b >= 0) subToolAction(*s, b);   // may free s: return at once
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, s->hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = (LONG)(640 * gScale);
        mmi->ptMinTrackSize.y = (LONG)(420 * gScale);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 2);
        if (s->session) s->session->abort();
        s->hwnd = nullptr;
        removeSub(s);
        maybeQuit();                       // frees s; nothing may touch it after this
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void setDpiAware() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    typedef BOOL (WINAPI *CtxFn)(HANDLE);
    CtxFn ctx = reinterpret_cast<CtxFn>(reinterpret_cast<void*>(
        GetProcAddress(u, "SetProcessDpiAwarenessContext")));
    if (ctx) { ctx((HANDLE)-4); return; }
    typedef BOOL (WINAPI *AwareFn)();
    AwareFn aw = reinterpret_cast<AwareFn>(reinterpret_cast<void*>(
        GetProcAddress(u, "SetProcessDPIAware")));
    if (aw) aw();
}

} // namespace

// ----------------------------------------------------------------------------
//  Entry points
// ----------------------------------------------------------------------------
bool RunGuiHub() {
    setDpiAware();

    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return false;
    loadLogo();
    gBigIcon   = makeIcon(256);
    gSmallIcon = makeIcon(32);

    gSans = new FontFamily(L"Segoe UI");
    if (!gSans->IsAvailable()) { delete gSans; gSans = FontFamily::GenericSansSerif()->Clone(); }
    gMono = new FontFamily(L"Consolas");
    if (!gMono->IsAvailable()) { delete gMono; gMono = FontFamily::GenericMonospace()->Clone(); }

    A.tools = makeTools();
    A.match.assign(22, 0);
    layoutTiles();

    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.lpszClassName = L"RandomUtilitiesSimpleWnd";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = gBigIcon ? gBigIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = gSmallIcon;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    if (!RegisterClassExW(&wc)) { GdiplusShutdown(token); return false; }

    WNDCLASSEXW sc = wc;
    sc.lpfnWndProc   = SubWndProc;
    sc.lpszClassName = L"RandomUtilitiesSubWnd";
    if (!RegisterClassExW(&sc)) { GdiplusShutdown(token); return false; }

    HDC sdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);
    gScale = dpi / 96.f;
    float scale = gScale;

    RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    DWORD style = WS_OVERLAPPEDWINDOW;     // resizable, with maximise
    RECT want = { 0, 0, (LONG)(kW * scale), (LONG)(kH * scale) };
    AdjustWindowRect(&want, style, FALSE);

    float fw = (float)(work.right - work.left - 16) / (float)(want.right - want.left);
    float fh = (float)(work.bottom - work.top - 16) / (float)(want.bottom - want.top);
    float fit = std::min(1.f, std::min(fw, fh));

    RECT cl = { 0, 0, (LONG)(kW * scale * fit), (LONG)(kH * scale * fit) };
    AdjustWindowRect(&cl, style, FALSE);
    int ww = cl.right - cl.left, wh = cl.bottom - cl.top;
    int wx = work.left + ((work.right - work.left) - ww) / 2;
    int wy = work.top  + ((work.bottom - work.top) - wh) / 2;

    A.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Random Utilities",
                             style, wx, wy, ww, wh,
                             nullptr, nullptr, hi, nullptr);
    if (!A.hwnd) { GdiplusShutdown(token); return false; }

    rebuildBack(A.hwnd, A.back, A.cw, A.ch, A.S, kW, kH);

    // Route std streams through the session dispatchers. Only the threads
    // that run a session are redirected; everything else keeps its stream.
    gCoutBuf.fallback = std::cout.rdbuf(&gCoutBuf);
    gCerrBuf.fallback = std::cerr.rdbuf(&gCerrBuf);
    gCinBuf.fallback  = std::cin.rdbuf(&gCinBuf);
    std::cin.exceptions(std::ios::badbit);   // an aborted session throws out of cin reads

    FreeConsole();

    ShowWindow(A.hwnd, SW_SHOW);
    UpdateWindow(A.hwnd);
    SetTimer(A.hwnd, 1, 30, nullptr);

    A.sessionStart = 0.0;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (A.runRequest >= 0) {
            int r = A.runRequest;
            A.runRequest = -1;
            runFromHub(r);
        }
    }

    std::cout.rdbuf(gCoutBuf.fallback);
    std::cerr.rdbuf(gCerrBuf.fallback);
    std::cin.rdbuf(gCinBuf.fallback);
    std::cin.exceptions(std::ios::goodbit);

    A.session.reset();
    delete A.back; A.back = nullptr;
    for (auto& kv : gFonts) delete kv.second;
    gFonts.clear();
    for (auto& kv : gMonoFonts) delete kv.second;
    gMonoFonts.clear();
    delete gSans; gSans = nullptr;
    delete gMono; gMono = nullptr;
    gLogo.reset();
    if (gBigIcon)   DestroyIcon(gBigIcon);
    if (gSmallIcon) DestroyIcon(gSmallIcon);
    gBigIcon = gSmallIcon = nullptr;
    GdiplusShutdown(token);
    return true;
}

// "RandomUtilities.exe --tool N": runs one tool in the console this process was
// started in. Used by "Open in CLI", so the console closes when the tool exits.
// Puts the console window in the middle of the primary monitor's work area
void centerConsoleWindow() {
    HWND h = GetConsoleWindow();
    if (!h) return;
    RECT wr = {};
    GetWindowRect(h, &wr);
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w  = wr.right - wr.left;
    int ht = wr.bottom - wr.top;
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top  + ((work.bottom - work.top) - ht) / 2;
    SetWindowPos(h, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

// "RandomUtilities.exe --tool N": runs one tool in the console this process was
// started in. Used by "Open in CLI" and by tools that need a real console.
int RunToolCli(int toolId) {
    std::vector<Tool> tools = makeTools();
    for (const Tool& tl : tools) {
        if (tl.id != toolId) continue;
        SetConsoleTitleW((L"Random Utilities  -  " + widen(tl.fullName)).c_str());
        centerConsoleWindow();
        if (HWND h = GetConsoleWindow()) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); }
        if (tl.fn) {
            try { tl.fn(); } catch (...) {}
        }
        std::cout.flush();
        return 0;
    }
    return 1;
}

#endif // _WIN32
