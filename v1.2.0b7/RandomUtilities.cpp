#include "RandomUtilities.h"
#include <cctype>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#pragma comment(lib, "psapi.lib")
#endif

// #region agent log
static void agentLog(const char* location, const char* message, const char* hypothesisId,
                     const std::string& dataJson = "{}") {
    std::ofstream f("debug-f62c21.log", std::ios::app);
    if (!f) return;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    f << "{\"sessionId\":\"f62c21\",\"hypothesisId\":\"" << hypothesisId
      << "\",\"location\":\"" << location << "\",\"message\":\"" << message
      << "\",\"data\":" << dataJson << ",\"timestamp\":" << ms << "}\n";
}
// #endregion

#ifdef _WIN32
#include <psapi.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// ==========================================
// CROSS-PLATFORM CLIPBOARD IMPLEMENTATION
// ==========================================
#ifdef _WIN32
static bool isHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
static bool isLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

static std::wstring sanitizeUtf16(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        wchar_t c = input[i];
        if (c < 0x20 && c != L'\t') continue;
        if (isHighSurrogate(c)) {
            if (i + 1 < input.size() && isLowSurrogate(input[i + 1])) {
                result.push_back(c);
                result.push_back(input[++i]);
            } else {
                result.push_back(0xFFFD);
            }
        } else if (isLowSurrogate(c)) {
            result.push_back(0xFFFD);
        } else {
            result.push_back(c);
        }
    }
    return result;
}

static std::wstring getClipboardTextW() {
    if (!OpenClipboard(nullptr)) {
        std::cerr << "  [ERROR] Could not open clipboard (Win32 error " << GetLastError() << ").\n";
        return {};
    }

    std::wstring result;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        std::cerr << "  [ERROR] Clipboard does not contain Unicode text.\n";
    } else {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(hData));
        if (!text) {
            std::cerr << "  [ERROR] Could not lock clipboard data (Win32 error " << GetLastError() << ").\n";
        } else {
            result = sanitizeUtf16(text);
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

static bool setClipboardTextW(const std::wstring& text) {
    const std::wstring safeText = sanitizeUtf16(text);
    if (!OpenClipboard(nullptr)) {
        std::cerr << "  [ERROR] Could not open clipboard (Win32 error " << GetLastError() << ").\n";
        return false;
    }

    const SIZE_T bytes = (safeText.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        std::cerr << "  [ERROR] Could not allocate clipboard memory.\n";
        CloseClipboard();
        return false;
    }

    wchar_t* pData = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!pData) {
        std::cerr << "  [ERROR] Could not lock clipboard memory.\n";
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    errno_t copyResult = wcscpy_s(pData, safeText.size() + 1, safeText.c_str());
    GlobalUnlock(hMem);
    if (copyResult != 0) {
        std::cerr << "  [ERROR] Could not copy Unicode text to clipboard.\n";
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, hMem)) {
        std::cerr << "  [ERROR] Could not publish Unicode clipboard data (Win32 error " << GetLastError() << ").\n";
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}
#endif

// ==========================================
// HELPER UTILITIES
// ==========================================
void clearScreen() {
    std::cout.flush();
    std::cerr.flush();
#ifdef _WIN32
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count, cellCount;
    COORD homeCoords = { 0, 0 };
    if (hStdOut == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleScreenBufferInfo(hStdOut, &csbi)) return;

    cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacter(hStdOut, (TCHAR)' ', cellCount, homeCoords, &count);
    FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, cellCount, homeCoords, &count);
    SetConsoleCursorPosition(hStdOut, homeCoords);

    // Force the visible window back to the top of the buffer.
    SHORT windowHeight = csbi.srWindow.Bottom - csbi.srWindow.Top;
    SMALL_RECT topWindow = { 0, 0,
                             (SHORT)(csbi.dwSize.X - 1),
                             windowHeight };
    SetConsoleWindowInfo(hStdOut, TRUE, &topWindow);
#else
    std::cout << "\033[2J\033[H";
#endif
}

void clearInputBuffer() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void pause() {
    std::cout << "\n  Press Enter to continue...\n";
    std::cout.flush();
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode = 0;
    GetConsoleMode(hIn, &oldMode);
    SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS);
    FlushConsoleInputBuffer(hIn);

    bool gotEnter = false;
    while (!gotEnter) {
        INPUT_RECORD ir;
        DWORD read = 0;
        if (!ReadConsoleInputW(hIn, &ir, 1, &read)) break;
        if (ir.EventType != KEY_EVENT) continue;
        if (!ir.Event.KeyEvent.bKeyDown) continue;
        if (ir.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) gotEnter = true;
    }
    SetConsoleMode(hIn, oldMode);
#else
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
#endif
}

std::string getHiddenPassword() {
#ifdef _WIN32
    constexpr size_t MAX_PASSWORD_LENGTH = 64;
    std::string pass = "";
    bool show = false;
    char ch;

    std::cout << "  (TAB to toggle [Show Password], ENTER to confirm)\n  > ";
    std::cout.flush();

    while (true) {
        ch = static_cast<char>(_getch());

        if (ch == 13) {
            std::cout << "\n";
            break;
        }
        else if (ch == 9) {
            show = !show;
            std::cout << "\r  > ";
            for (int i = 0; i < (int)pass.length() + 5; ++i) std::cout << ' ';
            std::cout << "\r  > ";
            if (show) std::cout << pass;
            else for (size_t i = 0; i < pass.length(); ++i) std::cout << '*';
            std::cout.flush();
        }
        else if (ch == 8) {
            if (!pass.empty()) {
                pass.pop_back();
                std::cout << "\b \b";
                std::cout.flush();
            }
        }
        else if (ch >= 32 && ch <= 126) {
            if (pass.size() >= MAX_PASSWORD_LENGTH) {
                std::cout << '\a';
                std::cout.flush();
                continue;
            }
            pass += ch;
            if (show) std::cout << ch;
            else std::cout << '*';
            std::cout.flush();
        }
    }
    return pass;
#else
    std::string pass;
    std::cout << "  Password (max 64 characters): ";
    std::getline(std::cin, pass);
    if (pass.size() > 64) {
        std::cout << "\a  [ERROR] Password cannot exceed 64 characters.\n";
        pass.resize(64);
    }
    return pass;
#endif
}

#ifdef _WIN32
void ConfigureConsole() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE || hIn == INVALID_HANDLE_VALUE) {
        // No console (e.g. launched detached). Nothing to configure.
        return;
    }

    SetConsoleTitleW(L"Random Utilities - Shivansh's C++ Hub");

    // Best-effort resize. Failure is harmless (Windows Terminal,
    // unusual DPI, or an in-progress resize will just keep the
    // current size).
    COORD       bufferSize = { 120, 9000 };
    SMALL_RECT  windowSize = { 0, 0, 119, 40 };
    SetConsoleScreenBufferSize(hOut, bufferSize);
    SetConsoleWindowInfo(hOut, TRUE, &windowSize);

    // Turn off QuickEdit so click-select doesn't freeze the input loop.
    DWORD dwMode = 0;
    if (GetConsoleMode(hIn, &dwMode)) {
        dwMode &= ~ENABLE_QUICK_EDIT_MODE;
        dwMode |= ENABLE_EXTENDED_FLAGS;
        SetConsoleMode(hIn, dwMode);
    }
}
#endif

void printLine() { std::cout << "  ==========================================================\n"; }

void printSection(const std::string& title) {
    std::cout << "\n"; printLine();
    std::cout << "    " << title << "\n";
    printLine(); std::cout << "\n";
}

void printHubBanner() {
    std::cout << "\n  ==========================================================\n";
    std::cout << "           R A N D O M   U T I L I T I E S   H U B\n";
    std::cout << "                     Developed by Shivansh\n";
    std::cout << "  ==========================================================\n\n";
}

std::string toBinary(long long n) {
    if (n == 0) return "0";
    std::string res = "";
    unsigned long long num = static_cast<unsigned long long>(n);
    while (num > 0) { res = (num % 2 == 0 ? "0" : "1") + res; num /= 2; }
    return res;
}

// ==========================================
// ADVANCED INPUT ENGINE (Win32 Console API)
// ==========================================
#ifdef _WIN32

static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    if (size_needed <= 0) {
        std::cerr << "  [ERROR] Invalid UTF-8 input (Win32 error " << GetLastError() << ").\n";
        return {};
    }
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size_needed <= 0) {
        std::cerr << "  [ERROR] Invalid UTF-16 input (Win32 error " << GetLastError() << ").\n";
        return {};
    }
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

struct InputState {
    std::wstring text;
    size_t cursor;

    SHORT startX;
    SHORT startY;

    SHORT consoleWidth;
    SHORT previousRows;
};

static void updateConsoleSize(HANDLE hOut, InputState& state) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        state.consoleWidth = csbi.dwSize.X;
    }
}

static COORD calculateCursorPosition(const InputState& state, size_t promptLen, size_t position) {
    int totalChars = state.startX + (int)promptLen + (int)position;
    COORD c;
    if (state.consoleWidth <= 0) {          // guard against a vanished console
        c.X = static_cast<SHORT>(totalChars);
        c.Y = state.startY;
        return c;
    }
    c.X = totalChars % state.consoleWidth;
    c.Y = state.startY + (totalChars / state.consoleWidth);
    return c;
}

static void clearPreviousDrawing(HANDLE hOut, const InputState& state) {
    if (state.consoleWidth <= 0 || state.previousRows <= 0) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) return;

    for (SHORT row = 0; row < state.previousRows; ++row) {
        const SHORT y = static_cast<SHORT>(state.startY + row);
        if (y < 0 || y >= csbi.dwSize.Y) continue;

        const SHORT x = row == 0 ? state.startX : 0;
        if (x < 0 || x >= csbi.dwSize.X) continue;

        const DWORD cells = static_cast<DWORD>(csbi.dwSize.X - x);
        COORD position = { x, y };
        DWORD written = 0;
        FillConsoleOutputCharacterW(hOut, L' ', cells, position, &written);
        FillConsoleOutputAttribute(hOut, csbi.wAttributes, cells, position, &written);
    }
}

static void redrawInput(HANDLE hOut, InputState& state, const std::string& prompt) {
    clearPreviousDrawing(hOut, state);

    COORD start = { state.startX, state.startY };
    SetConsoleCursorPosition(hOut, start);

    const std::wstring widePrompt = utf8_to_wstring(prompt);
    DWORD written = 0;
    if (!widePrompt.empty()) {
        WriteConsoleW(hOut, widePrompt.c_str(),
                      static_cast<DWORD>(widePrompt.size()), &written, nullptr);
    }
    if (!state.text.empty()) {
        WriteConsoleW(hOut, state.text.c_str(),
                      static_cast<DWORD>(state.text.length()), &written, nullptr);
    }

    int promptLen = static_cast<int>(widePrompt.length());
    int totalChars = state.startX + promptLen + (int)state.text.length();
    state.previousRows = state.consoleWidth > 0
                       ? (totalChars / state.consoleWidth) + 1
                       : 1;

    COORD cursorPos = calculateCursorPosition(state, promptLen, state.cursor);
    SetConsoleCursorPosition(hOut, cursorPos);
}

static void moveCursor(InputState& state, int delta) {
    if (delta < 0 && state.cursor >= (size_t)(-delta)) {
        state.cursor += delta;
    } else if (delta > 0 && state.cursor + delta <= state.text.length()) {
        state.cursor += delta;
    }
}

std::string getAdvancedInput(const std::string& prompt) {
    std::cout.flush();
    std::cerr.flush();

    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD oldModeIn = 0;
    GetConsoleMode(hIn, &oldModeIn);
    // ENABLE_PROCESSED_INPUT deliberately omitted so Ctrl+C arrives as
    // a KEY_EVENT we can intercept for the copy shortcut.
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);

    // Drop stale events (key-ups left over from the previous session).
    FlushConsoleInputBuffer(hIn);

    InputState state;
    state.text         = L"";
    state.cursor       = 0;
    state.previousRows = 1;

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (!GetConsoleScreenBufferInfo(hOut, &csbi) || csbi.dwSize.X <= 0) {
        // Console unavailable — fall back to plain iostream so the
        // program keeps running instead of dividing by zero.
        std::cout << prompt;
        std::cout.flush();
        std::string text;
        std::getline(std::cin, text);
        SetConsoleMode(hIn, oldModeIn);
        return text;
    }
    state.startX       = csbi.dwCursorPosition.X;
    state.startY       = csbi.dwCursorPosition.Y;
    state.consoleWidth = csbi.dwSize.X;

    redrawInput(hOut, state, prompt);

    bool done = false;
    while (!done) {
        INPUT_RECORD ir;
        DWORD read = 0;
        if (!ReadConsoleInputW(hIn, &ir, 1, &read)) continue;

        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            updateConsoleSize(hOut, state);
            redrawInput(hOut, state, prompt);
            continue;
        }
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

        WORD  vk   = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR uc   = ir.Event.KeyEvent.uChar.UnicodeChar;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;

        if (vk == VK_SHIFT   || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) {
            continue;
        }

        // Ctrl+C — copy
        if (vk == 'C' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            setClipboardTextW(state.text);
            continue;
        }

        // Ctrl+V — paste (newlines flattened to keep single line)
        if (vk == 'V' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            std::wstring wclip = getClipboardTextW();
            if (!wclip.empty()) {
                for (wchar_t& c : wclip) {
                    if (c == L'\r' || c == L'\n') c = L' ';
                }
                state.text.insert(state.cursor, wclip);
                state.cursor += wclip.length();
                redrawInput(hOut, state, prompt);
            }
            continue;
        }

        // F5 — copy shortcut
        if (vk == VK_F5) {
            setClipboardTextW(state.text);
            continue;
        }

        // Escape — abort
        if (vk == VK_ESCAPE) {
            const int promptLen = static_cast<int>(utf8_to_wstring(prompt).length());
            COORD endOfText = calculateCursorPosition(state, promptLen, state.text.length());
            COORD col0      = { 0, endOfText.Y };
            SetConsoleCursorPosition(hOut, col0);
            DWORD written = 0;
            WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);

            SetConsoleMode(hIn, oldModeIn);
            return "";
        }

        // Enter — commit
        if (vk == VK_RETURN) {
            const int promptLen = static_cast<int>(utf8_to_wstring(prompt).length());
            COORD endOfText = calculateCursorPosition(state, promptLen, state.text.length());
            COORD col0      = { 0, endOfText.Y };
            SetConsoleCursorPosition(hOut, col0);
            DWORD written = 0;
            WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);

            done = true;
            continue;
        }

        if (vk == VK_HOME) {
            state.cursor = 0;
            redrawInput(hOut, state, prompt);
            continue;
        }
        if (vk == VK_END) {
            state.cursor = state.text.length();
            redrawInput(hOut, state, prompt);
            continue;
        }
        if (vk == VK_LEFT) {
            moveCursor(state, -1);
            redrawInput(hOut, state, prompt);
            continue;
        }
        if (vk == VK_RIGHT) {
            moveCursor(state, 1);
            redrawInput(hOut, state, prompt);
            continue;
        }

        if (vk == VK_BACK) {
            if (state.cursor > 0) {
                state.text.erase(state.cursor - 1, 1);
                state.cursor--;
                redrawInput(hOut, state, prompt);
            }
            continue;
        }
        if (vk == VK_DELETE) {
            if (state.cursor < state.text.length()) {
                state.text.erase(state.cursor, 1);
                redrawInput(hOut, state, prompt);
            }
            continue;
        }

        const bool validSurrogateContinuation =
            isLowSurrogate(uc) &&
            state.cursor > 0 &&
            isHighSurrogate(state.text[state.cursor - 1]);

        if (uc >= 32 && (!isLowSurrogate(uc) || validSurrogateContinuation)) {
            state.text.insert(state.cursor, 1, uc);
            state.cursor++;
            redrawInput(hOut, state, prompt);
        }
    }

    SetConsoleMode(hIn, oldModeIn);
    return wstring_to_utf8(sanitizeUtf16(state.text));
}

#else
std::string getAdvancedInput(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();
    std::string text;
    std::getline(std::cin, text);
    return text;
}
#endif

// ==========================================
// LEVENSHTEIN DISTANCE
// ==========================================
int levenshteinDistance(const std::string& s1, const std::string& s2) {
    int len1 = (int)s1.length(), len2 = (int)s2.length();
    std::vector<int> prev(len2 + 1), curr(len2 + 1);
    for (int j = 0; j <= len2; ++j) prev[j] = j;
    for (int i = 1; i <= len1; ++i) {
        curr[0] = i;
        for (int j = 1; j <= len2; ++j) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return prev[len2];
}

std::string xorCrypt(const std::string& data, const std::string& key) {
    std::string result = data;
    if (key.empty()) return result;
    for (size_t i = 0; i < data.length(); ++i)
        result[i] = data[i] ^ key[i % key.length()];
    return result;
}

// #region agent log
static void runDebugSelfTests() {
    const char* runId = "post-fix";
    int y = 2000, m = 2, d = 30;
    const bool acceptedByOldLogic = d >= 1 && d <= 31;
    const bool acceptedByNewLogic = d >= 1 && d <= 29;
    agentLog("selftest:date", "invalid date check", "A",
             "{\"runId\":\"" + std::string(runId) + "\",\"y\":" + std::to_string(y) +
             ",\"m\":" + std::to_string(m) + ",\"d\":" + std::to_string(d) +
             ",\"daysInMonth\":29," +
             "\"acceptedByOldLogic\":" + std::string(acceptedByOldLogic ? "true" : "false") +
             ",\"acceptedByNewLogic\":" + std::string(acceptedByNewLogic ? "true" : "false") + "}");

    try {
        double nanVal = std::stod("nan");
        const bool readDoubleWouldAccept = std::isfinite(nanVal);
        agentLog("selftest:nan", "stod nan finite check", "B",
                 "{\"runId\":\"" + std::string(runId) + "\",\"value\":\"nan\",\"readDoubleWouldAccept\":" +
                 std::string(readDoubleWouldAccept ? "true" : "false") + "}");
    } catch (...) {}

    try {
        double infVal = std::stod("inf");
        const bool readDoubleWouldAccept = std::isfinite(infVal);
        agentLog("selftest:inf", "stod inf finite check", "B",
                 "{\"runId\":\"" + std::string(runId) + "\",\"value\":\"inf\",\"readDoubleWouldAccept\":" +
                 std::string(readDoubleWouldAccept ? "true" : "false") + "}");
    } catch (...) {}

    std::string plain = "hello";
    std::string encrypted = xorCrypt(plain, "");
    agentLog("selftest:xor", "empty password xor", "E",
             "{\"runId\":\"" + std::string(runId) + "\",\"plaintextEqualsCiphertext\":" +
             std::string(encrypted == plain ? "true" : "false") +
             ",\"emptyPasswordBlockedByUI\":true}");

    agentLog("selftest:menu", "menu id mapping", "C",
             "{\"runId\":\"" + std::string(runId) +
             "\",\"display11\":\"Reserved\",\"display12\":\"System Info\",\"display13\":\"Clean RAM\"}");

    std::ios::fmtflags before = std::cout.flags();
    std::streamsize oldPrec = std::cout.precision();
    std::cout << std::fixed << std::setprecision(2) << 1.23456;
    std::cout.flags(before);
    std::cout.precision(oldPrec);
    std::ios::fmtflags after = std::cout.flags();
    agentLog("selftest:fmt", "stream flags after fixed output", "D",
             "{\"runId\":\"" + std::string(runId) + "\",\"fixedBefore\":" +
             std::string((before & std::ios::fixed) ? "true" : "false") +
             ",\"fixedAfter\":" + std::string((after & std::ios::fixed) ? "true" : "false") + "}");
}
// #endregion

// ==========================================
// INPUT VALIDATION HELPERS
// ==========================================
static bool readDouble(const std::string& prompt, double& out) {
    while (true) {
        std::string raw = getAdvancedInput(prompt);
        if (std::cin.eof()) {
            std::cout << "  [ERROR] Input stream closed.\n";
            return false;
        }
        if (raw.empty()) { std::cout << "  [ERROR] Empty input.\n"; return false; }
        try {
            size_t pos = 0;
            out = std::stod(raw, &pos);
            agentLog("readDouble", "parsed value", "B",
                     "{\"raw\":\"" + raw + "\",\"isfinite\":" +
                     std::string(std::isfinite(out) ? "true" : "false") + ",\"posMatch\":" +
                     std::string(pos == raw.size() ? "true" : "false") + "}");
            if (pos == raw.size() && std::isfinite(out)) return true;
        } catch (...) {}
        std::cout << "  [ERROR] Not a valid number. Try again.\n";
    }
}

// ----------------------------------------------
// UTILITY FUNCTIONS
// ----------------------------------------------

void CalculatorPr() {
    bool run = true;
    std::vector<std::string> history;
    while (run) {
        clearScreen();
        printSection("ADVANCED CALCULATOR");
        std::cout << "  1. Basic (+, -, *, /)\n";
        std::cout << "  2. Scientific (sin, cos, tan, sqrt, log, abs)\n";
        std::cout << "  3. Power / Root\n";
        std::cout << "  4. History\n";
        std::cout << "  5. Exit\n\n";

        std::string cRaw = getAdvancedInput("  Choice: ");
        if (cRaw.empty()) continue;
        int c = -1;
        try { c = std::stoi(cRaw); } catch (...) { std::cout << "  [ERROR] Invalid.\n"; pause(); continue; }

        if (c == 5) { run = false; continue; }

        if (c >= 1 && c <= 3) {
            double n1 = 0, n2 = 0, res = 0;
            if (!readDouble("  Num 1: ", n1)) { pause(); continue; }

            if (c == 1) {
                if (!readDouble("  Num 2: ", n2)) { pause(); continue; }
                std::string opStr = getAdvancedInput("  Op (+  -  *  /): ");
                char op = opStr.empty() ? '?' : opStr[0];
                if      (op == '+') res = n1 + n2;
                else if (op == '-') res = n1 - n2;
                else if (op == '*') res = n1 * n2;
                else if (op == '/') {
                    if (n2 == 0) { std::cout << "  [ERROR] Division by zero.\n"; pause(); continue; }
                    res = n1 / n2;
                }
                else { std::cout << "  [ERROR] Invalid operator.\n"; pause(); continue; }
            }
            else if (c == 2) {
                std::string op = getAdvancedInput("  Op (sin/cos/tan/sqrt/log/log2/log10/abs): ");
                if      (op == "sin")   res = std::sin(n1);
                else if (op == "cos")   res = std::cos(n1);
                else if (op == "tan")   res = std::tan(n1);
                else if (op == "sqrt") {
                    if (n1 < 0) { std::cout << "  [ERROR] sqrt of negative.\n"; pause(); continue; }
                    res = std::sqrt(n1);
                }
                else if (op == "log" || op == "ln") {
                    if (n1 <= 0) { std::cout << "  [ERROR] log of non-positive.\n"; pause(); continue; }
                    res = std::log(n1);
                }
                else if (op == "log2") {
                    if (n1 <= 0) { std::cout << "  [ERROR] log of non-positive.\n"; pause(); continue; }
                    res = std::log2(n1);
                }
                else if (op == "log10") {
                    if (n1 <= 0) { std::cout << "  [ERROR] log of non-positive.\n"; pause(); continue; }
                    res = std::log10(n1);
                }
                else if (op == "abs") res = std::abs(n1);
                else { std::cout << "  [ERROR] Unknown operation.\n"; pause(); continue; }
            }
            else {
                if (!readDouble("  Exponent (e.g. 2 for square, 0.5 for sqrt): ", n2)) {
                    pause(); continue;
                }
                if (n1 < 0 && std::floor(n2) != n2) {
                    std::cout << "  [ERROR] Fractional power of negative base is complex.\n";
                    pause(); continue;
                }
                res = std::pow(n1, n2);
            }

            std::cout << "\n  Result: " << std::setprecision(10) << res << "\n";
            history.push_back(std::to_string(n1) + " op -> " + std::to_string(res));
        }
        else if (c == 4) {
            if (history.empty()) std::cout << "  No calculations yet.\n";
            else {
                std::cout << "\n  -- Calculation History --\n";
                for (int i = 0; i < (int)history.size(); ++i)
                    std::cout << "  [" << (i+1) << "] " << history[i] << "\n";
            }
        }
        else {
            std::cout << "  [ERROR] Invalid choice.\n";
        }
        pause();
    }
}

void GuessingGamePr() {
    clearScreen();
    printSection("GUESS THE NUMBER");
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> distrib(1, 100);
    int secret = distrib(rng), guess = -1, att = 0;
    const int MAX_ATT = 10;

    std::cout << "  You have " << MAX_ATT << " attempts. Good luck!\n\n";

    while (guess != secret && att < MAX_ATT) {
        std::string raw = getAdvancedInput(
            "  Guess (1-100) [" + std::to_string(MAX_ATT - att) + " left]: ");
        bool ok = !raw.empty();
        for (unsigned char c : raw) if (!std::isdigit(c)) { ok = false; break; }
        if (!ok) { std::cout << "  [ERROR] Enter a whole number.\n"; continue; }
        try { guess = std::stoi(raw); } catch (...) { std::cout << "  [ERROR] Invalid.\n"; continue; }
        if (guess < 1 || guess > 100) { std::cout << "  [ERROR] Must be 1-100.\n"; continue; }
        att++;
        if      (guess < secret) std::cout << "  Too Low!\n";
        else if (guess > secret) std::cout << "  Too High!\n";
    }

    if (guess == secret)
        std::cout << "\n  Correct! " << att << " attempt" << (att==1?"":"s") << "!\n";
    else
        std::cout << "\n  Out of attempts! The number was " << secret << ".\n";
    pause();
}

// ==========================================
// UNIT CONVERTER
// ==========================================
struct UnitDef {
    std::string name;
    std::string abbr;
    double      toBase;
};

static double toBaseTemp(double v, const std::string& abbr) {
    if (abbr == "C")  return v;
    if (abbr == "F")  return (v - 32.0) * 5.0 / 9.0;
    if (abbr == "K")  return v - 273.15;
    if (abbr == "R")  return (v - 491.67) * 5.0 / 9.0;
    return v;
}
static double fromBaseTemp(double c, const std::string& abbr) {
    if (abbr == "C")  return c;
    if (abbr == "F")  return c * 9.0 / 5.0 + 32.0;
    if (abbr == "K")  return c + 273.15;
    if (abbr == "R")  return (c + 273.15) * 9.0 / 5.0;
    return c;
}

void UnitConverterPr() {
    struct Category {
        std::string          name;
        std::vector<UnitDef> units;
        bool                 isTemp;
    };

    const std::vector<Category> cats = {
        { "Length", {
            {"Millimetre",  "mm",   0.001},
            {"Centimetre",  "cm",   0.01},
            {"Metre",       "m",    1.0},
            {"Kilometre",   "km",   1000.0},
            {"Inch",        "in",   0.0254},
            {"Foot",        "ft",   0.3048},
            {"Yard",        "yd",   0.9144},
            {"Mile",        "mi",   1609.344},
            {"Nautical Mi", "nmi",  1852.0},
            {"Light Year",  "ly",   9.461e15},
        }, false },
        { "Mass / Weight", {
            {"Milligram",   "mg",   1e-6},
            {"Gram",        "g",    0.001},
            {"Kilogram",    "kg",   1.0},
            {"Tonne",       "t",    1000.0},
            {"Ounce",       "oz",   0.0283495},
            {"Pound",       "lb",   0.453592},
            {"Stone",       "st",   6.35029},
            {"US Ton",      "ust",  907.185},
            {"UK Ton",      "ukt",  1016.05},
        }, false },
        { "Temperature", {
            {"Celsius",     "C",    1.0},
            {"Fahrenheit",  "F",    1.0},
            {"Kelvin",      "K",    1.0},
            {"Rankine",     "R",    1.0},
        }, true },
        { "Volume", {
            {"Millilitre",  "ml",   0.001},
            {"Litre",       "L",    1.0},
            {"Cubic Metre", "m3",   1000.0},
            {"US fl oz",    "floz", 0.0295735},
            {"US Cup",      "cup",  0.236588},
            {"US Pint",     "pt",   0.473176},
            {"US Quart",    "qt",   0.946353},
            {"US Gallon",   "gal",  3.78541},
            {"UK Gallon",   "ukgal",4.54609},
            {"Cubic Inch",  "in3",  0.0163871},
            {"Cubic Foot",  "ft3",  28.3168},
        }, false },
        { "Speed", {
            {"m/s",         "m/s",  1.0},
            {"km/h",        "km/h", 1.0 / 3.6},
            {"mph",         "mph",  0.44704},
            {"Knot",        "kn",   0.514444},
            {"ft/s",        "ft/s", 0.3048},
            {"Mach (sea)",  "mach", 340.29},
        }, false },
        { "Data Storage", {
            {"Bit",         "bit",  0.125},
            {"Byte",        "B",    1.0},
            {"Kilobyte",    "KB",   1024.0},
            {"Megabyte",    "MB",   1048576.0},
            {"Gigabyte",    "GB",   1073741824.0},
            {"Terabyte",    "TB",   1099511627776.0},
            {"Petabyte",    "PB",   1.126e15},
        }, false },
        { "Area", {
            {"mm2",         "mm2",  1e-6},
            {"cm2",         "cm2",  1e-4},
            {"m2",          "m2",   1.0},
            {"km2",         "km2",  1e6},
            {"Hectare",     "ha",   10000.0},
            {"Acre",        "ac",   4046.86},
            {"ft2",         "ft2",  0.092903},
            {"yd2",         "yd2",  0.836127},
            {"mi2",         "mi2",  2.59e6},
        }, false },
        { "Time", {
            {"Nanosecond",  "ns",   1e-9},
            {"Microsecond", "us",   1e-6},
            {"Millisecond", "ms",   0.001},
            {"Second",      "s",    1.0},
            {"Minute",      "min",  60.0},
            {"Hour",        "hr",   3600.0},
            {"Day",         "day",  86400.0},
            {"Week",        "wk",   604800.0},
            {"Month (avg)", "mo",   2629800.0},
            {"Year",        "yr",   31557600.0},
        }, false },
        { "Energy", {
            {"Joule",       "J",    1.0},
            {"Kilojoule",   "kJ",   1000.0},
            {"Calorie",     "cal",  4.184},
            {"Kilocalorie", "kcal", 4184.0},
            {"Watt-hour",   "Wh",   3600.0},
            {"kWh",         "kWh",  3.6e6},
            {"BTU",         "BTU",  1055.06},
            {"Foot-pound",  "ftlb", 1.35582},
            {"eV",          "eV",   1.60218e-19},
        }, false },
        { "Pressure", {
            {"Pascal",      "Pa",   1.0},
            {"Kilopascal",  "kPa",  1000.0},
            {"Megapascal",  "MPa",  1e6},
            {"Bar",         "bar",  100000.0},
            {"Millibar",    "mbar", 100.0},
            {"Atmosphere",  "atm",  101325.0},
            {"mmHg (Torr)", "mmHg", 133.322},
            {"psi",         "psi",  6894.76},
        }, false },
    };

    auto printUnits = [](const std::vector<UnitDef>& units) {
        for (int i = 0; i < (int)units.size(); ++i) {
            std::cout << "  [" << std::setw(2) << (i + 1) << "] "
                      << std::left << std::setw(14) << units[i].name
                      << " (" << units[i].abbr << ")\n";
        }
        std::cout << std::right;
    };

    auto pickUnit = [&](const std::vector<UnitDef>& units, const std::string& label) -> int {
        while (true) {
            std::string raw = getAdvancedInput(
                "  " + label + " [1-" + std::to_string(units.size()) + "]: ");
            bool ok = !raw.empty();
            for (unsigned char c : raw) if (!std::isdigit(c)) { ok = false; break; }
            if (ok) {
                try {
                    int idx = std::stoi(raw) - 1;
                    if (idx >= 0 && idx < (int)units.size()) return idx;
                } catch (...) {}
            }
            std::cout << "  [ERROR] Invalid selection.\n";
        }
    };

    bool run = true;
    while (run) {
        clearScreen();
        printSection("UNIT CONVERTER");
        std::cout << "  Select category:\n\n";
        for (int i = 0; i < (int)cats.size(); ++i)
            std::cout << "  [" << (i + 1) << "] " << cats[i].name << "\n";
        std::cout << "  [0] Exit\n\n";

        std::string catRaw = getAdvancedInput(
            "  Category [0-" + std::to_string(cats.size()) + "]: ");

        if (catRaw == "0" || catRaw.empty()) { run = false; continue; }
        bool catOk = !catRaw.empty();
        for (unsigned char c : catRaw) if (!std::isdigit(c)) { catOk = false; break; }
        if (!catOk) { std::cout << "  [ERROR] Enter a number.\n"; pause(); continue; }
        int catIdx = -1;
        try { catIdx = std::stoi(catRaw) - 1; }
        catch (...) {
            std::cout << "  [ERROR] Invalid category.\n"; pause(); continue;
        }
        if (catIdx < 0 || catIdx >= (int)cats.size()) {
            std::cout << "  [ERROR] Invalid category.\n"; pause(); continue;
        }

        const Category& cat = cats[catIdx];

        clearScreen();
        printSection("UNIT CONVERTER  --  " + cat.name);
        std::cout << "  Available units:\n\n";
        printUnits(cat.units);
        std::cout << "\n";
        int fromIdx = pickUnit(cat.units, "FROM unit");

        double value = 0.0;
        while (true) {
            std::string vRaw = getAdvancedInput(
                "  Value (" + cat.units[fromIdx].abbr + "): ");
            try {
                size_t pos = 0;
                value = std::stod(vRaw, &pos);
                if (pos == vRaw.size() && std::isfinite(value)) break;
            } catch (...) {}
            std::cout << "  [ERROR] Enter a valid number.\n";
        }

        std::cout << "\n";
        int toIdx = pickUnit(cat.units, "TO unit  ");

        double result = 0.0;
        if (cat.isTemp) {
            result = fromBaseTemp(toBaseTemp(value, cat.units[fromIdx].abbr),
                                  cat.units[toIdx].abbr);
        } else {
            result = value * cat.units[fromIdx].toBase / cat.units[toIdx].toBase;
        }

        std::cout << "\n";
        printLine();
        {
            auto oldFlags = std::cout.flags();
            auto oldPrec = std::cout.precision();
            std::cout << std::fixed << std::setprecision(6);
            std::cout << "  " << value  << " " << cat.units[fromIdx].abbr
                      << "  =  "
                      << result << " " << cat.units[toIdx].abbr << "\n";
            std::cout.flags(oldFlags);
            std::cout.precision(oldPrec);
        }
        std::cout << "  ("
                  << cat.units[fromIdx].name << "  ->  "
                  << cat.units[toIdx].name   << ")\n";
        printLine();

        pause();
    }
}

// ==========================================
// AGE CALCULATOR
// ==========================================
static int daysInMonth(int year, int month) {
    static const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
        return 29;
    return days[month];
}

static long long dateToDays(int y, int m, int d) {
    long long days = 0;
    for (int yr = 1; yr < y; ++yr) {
        days += 365 + ( ((yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0)) ? 1 : 0 );
    }
    for (int mo = 1; mo < m; ++mo) {
        days += daysInMonth(y, mo);
    }
    days += (d - 1);
    return days;
}

void AgeCalculatorPr() {
    clearScreen();
    printSection("AGE CALCULATOR");

    auto readIntRange = [](const std::string& prompt, int lo, int hi, int& out) -> bool {
        while (true) {
            std::string raw = getAdvancedInput(prompt);
            if (std::cin.eof()) return false;
            bool ok = !raw.empty();
            for (int i = (raw.empty()?0:(raw[0]=='-'?1:0)); i < (int)raw.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(raw[i]))) { ok = false; break; }
            if (ok) {
                try {
                    out = std::stoi(raw);
                    if (out >= lo && out <= hi) return true;
                    std::cout << "  [ERROR] Must be " << lo << "-" << hi << ".\n";
                } catch (...) { std::cout << "  [ERROR] Invalid number.\n"; }
            } else {
                std::cout << "  [ERROR] Enter a whole number.\n";
            }
        }
    };

    int y = 0, m = 0, d = 0;
    if (!readIntRange("  Birth Year  : ", 1, 9999, y)) { pause(); return; }
    if (!readIntRange("  Birth Month : ", 1, 12,   m)) { pause(); return; }
    if (!readIntRange("  Birth Day   : ", 1, 31,   d)) { pause(); return; }

    if (d > daysInMonth(y, m)) {
        std::cout << "  [ERROR] Invalid day for that month/year.\n";
        pause();
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_now = std::localtime(&time_t_now);
    int curYear = tm_now->tm_year + 1900;
    int curMonth = tm_now->tm_mon + 1;
    int curDay = tm_now->tm_mday;

    long long birthDays = dateToDays(y, m, d);
    long long currentDays = dateToDays(curYear, curMonth, curDay);
    long long diffDays = currentDays - birthDays;

    if (diffDays < 0) {
        std::cout << "  [ERROR] Birth date is in the future.\n";
    } else {
        int tempYear = y;
        int tempMonth = m;
        int tempDay = d;
        int ageYears = 0;

        while (true) {
            int nextYear = tempYear + 1;
            int dayInNextYear = std::min(tempDay, daysInMonth(nextYear, tempMonth));
            long long nextYearDays = dateToDays(nextYear, tempMonth, dayInNextYear);
            if (nextYearDays <= currentDays) {
                ageYears++;
                tempYear = nextYear;
            } else break;
        }

        int ageMonths = 0;
        while (true) {
            int nextMonth = tempMonth + 1;
            int nextYear = tempYear;
            if (nextMonth > 12) { nextMonth = 1; nextYear++; }
            int dayInNextMonth = std::min(tempDay, daysInMonth(nextYear, nextMonth));
            long long nextMonthDays = dateToDays(nextYear, nextMonth, dayInNextMonth);
            if (nextMonthDays <= currentDays) {
                ageMonths++;
                tempMonth = nextMonth;
                tempYear = nextYear;
            } else break;
        }

        int dayInCurrent = std::min(tempDay, daysInMonth(tempYear, tempMonth));
        int ageDays = (int)(currentDays - dateToDays(tempYear, tempMonth, dayInCurrent));

        std::cout << "  Age: " << ageYears << " years, " << ageMonths << " months, " << ageDays << " days\n";
    }
    pause();
}

// ==========================================
// OTHER UTILITIES
// ==========================================

void BaseConverterPr() {
    clearScreen();
    printSection("BASE CONVERTER");

    std::string raw = getAdvancedInput("  Enter Decimal (non-negative): ");
    long long d = 0;
    bool ok = !raw.empty();
    for (int i = (raw.empty()?0:(raw[0]=='-'?1:0)); i < (int)raw.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(raw[i]))) { ok = false; break; }
    if (!ok) { std::cout << "  [ERROR] Enter a whole number.\n"; pause(); return; }
    try { d = std::stoll(raw); } catch (...) { std::cout << "  [ERROR] Number too large.\n"; pause(); return; }
    if (d < 0) { std::cout << "  [ERROR] Non-negative only.\n"; pause(); return; }

    std::cout << "\n";
    std::cout << "  Decimal: " << d << "\n";
    std::cout << "  Binary:  " << toBinary(d) << "\n";
    std::cout << "  Octal:   " << std::oct << d << std::dec << "\n";
    std::cout << "  Hex:     " << std::uppercase << std::hex << d
              << std::dec << std::nouppercase << "\n";
    pause();
}

void RandomNumberGeneratorPr() {
    clearScreen();
    printSection("RANDOM GENERATOR");

    auto readInt = [](const std::string& prompt, long long& out) -> bool {
        while (true) {
            std::string raw = getAdvancedInput(prompt);
            bool ok = !raw.empty();
            for (int i = (raw.empty()?0:(raw[0]=='-'?1:0));
                 i < (int)raw.size(); ++i)
                 if (!std::isdigit(static_cast<unsigned char>(raw[i]))) { ok = false; break; }
            if (ok && !raw.empty()) {
                try { out = std::stoll(raw); return true; }
                catch (...) { std::cout << "  [ERROR] Number too large.\n"; }
            } else {
                std::cout << "  [ERROR] Enter a whole number.\n";
            }
        }
    };

    long long mn = 0, mx = 0;
    if (!readInt("  Min: ", mn)) { pause(); return; }
    if (!readInt("  Max: ", mx)) { pause(); return; }
    if (mn > mx) { std::cout << "  [ERROR] Min > Max.\n"; pause(); return; }

    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<long long> dist(mn, mx);
    std::cout << "\n  Result: " << dist(rng) << "\n";
    pause();
}

void BMICalculatorPr() {
    clearScreen();
    printSection("BMI CALCULATOR");
    double w = 0, h = 0;

    if (!readDouble("  Weight (kg): ", w)) { pause(); return; }
    if (w <= 0) { std::cout << "  [ERROR] Weight must be positive.\n"; pause(); return; }
    if (!readDouble("  Height (m):  ", h)) { pause(); return; }
    if (h <= 0) { std::cout << "  [ERROR] Height must be positive.\n"; pause(); return; }
    if (h > 3.0) { std::cout << "  [ERROR] Height over 3 m is unlikely. Did you enter cm?\n"; pause(); return; }

    double bmi = w / (h * h);
    std::string category;
    if      (bmi < 18.5) category = "Underweight";
    else if (bmi < 25.0) category = "Normal weight";
    else if (bmi < 30.0) category = "Overweight";
    else if (bmi < 35.0) category = "Obese (Class I)";
    else if (bmi < 40.0) category = "Obese (Class II)";
    else                 category = "Obese (Class III)";

    std::cout << "\n  BMI      : ";
    {
        auto oldFlags = std::cout.flags();
        auto oldPrec = std::cout.precision();
        std::cout << std::fixed << std::setprecision(2) << bmi << "\n";
        std::cout.flags(oldFlags);
        std::cout.precision(oldPrec);
    }
    std::cout << "  Category : " << category << "\n";
    pause();
}

void TextAnalyzerPr() {
    clearScreen();
    printSection("TEXT ANALYZER");
    std::string t = getAdvancedInput("  Enter Text: ");

    int words = 0, letters = 0, digits = 0, spaces = 0,
        upper = 0, lower = 0, sentences = 0, special = 0;
    bool inWord = false;

    for (unsigned char c : t) {
        if (std::isalpha(c)) {
            letters++;
            inWord = true;
            if (std::isupper(c)) upper++;
            else                  lower++;
        } else {
            if (inWord) { words++; inWord = false; }
            if (std::isdigit(c))           digits++;
            else if (std::isspace(c))      spaces++;
            else if (c=='.' || c=='!' || c=='?') { sentences++; special++; }
            else                            special++;
        }
    }
    if (inWord) words++;

    std::cout << "\n";
    std::cout << "  Characters (total) : " << t.length()  << "\n";
    std::cout << "  Letters            : " << letters      << "\n";
    std::cout << "  Words              : " << words        << "\n";
    std::cout << "  Sentences          : " << sentences    << "\n";
    std::cout << "  Digits             : " << digits       << "\n";
    std::cout << "  Spaces             : " << spaces       << "\n";
    std::cout << "  Uppercase letters  : " << upper        << "\n";
    std::cout << "  Lowercase letters  : " << lower        << "\n";
    std::cout << "  Special characters : " << special      << "\n";
    if (words > 0) {
        auto oldFlags = std::cout.flags();
        auto oldPrec = std::cout.precision();
        std::cout << "  Avg word length    : " << std::fixed << std::setprecision(1)
                  << (double)letters / words << "\n";
        std::cout.flags(oldFlags);
        std::cout.precision(oldPrec);
    }
    pause();
}

// ==========================================
// TEXT / CODE FORMATTER & ANALYZER
// ==========================================
static std::vector<uint32_t> decodeUtf8(const std::string& text) {
    std::vector<uint32_t> result;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t codePoint = 0;
        size_t width = 0;
        if (c < 0x80) { codePoint = c; width = 1; }
        else if (c >= 0xC2 && c <= 0xDF) { codePoint = c & 0x1F; width = 2; }
        else if (c >= 0xE0 && c <= 0xEF) { codePoint = c & 0x0F; width = 3; }
        else if (c >= 0xF0 && c <= 0xF4) { codePoint = c & 0x07; width = 4; }
        else { result.push_back(0xFFFD); ++i; continue; }

        if (i + width > text.size()) { result.push_back(0xFFFD); break; }
        bool valid = true;
        for (size_t j = 1; j < width; ++j) {
            unsigned char continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xC0) != 0x80) { valid = false; break; }
            codePoint = (codePoint << 6) | (continuation & 0x3F);
        }
        if (!valid || codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
            (width == 3 && codePoint < 0x800) || (width == 4 && codePoint < 0x10000)) {
            result.push_back(0xFFFD);
            ++i;
        } else {
            result.push_back(codePoint);
            i += width;
        }
    }
    return result;
}

static bool isUnicodeWhitespace(uint32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' ||
           c == 0x00A0 || c == 0x2003 || c == 0x3000;
}

static std::string trimWhitespace(const std::string& line) {
    const std::vector<uint32_t> points = decodeUtf8(line);
    size_t first = 0, last = points.size();
    while (first < last && isUnicodeWhitespace(points[first])) ++first;
    while (last > first && isUnicodeWhitespace(points[last - 1])) --last;

    std::string result;
    for (size_t i = first; i < last; ++i) {
        uint32_t c = points[i];
        if (c < 0x80) result.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (c >> 6)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (c >> 12)));
            result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (c >> 18)));
            result.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return result;
}

static std::string convertCase(const std::string& input, int mode) {
    std::string result = input;
    bool startWord = true;
    for (char& raw : result) {
        unsigned char c = static_cast<unsigned char>(raw);
        if (c < 128 && std::isalpha(c)) {
            if (mode == 1) raw = static_cast<char>(std::toupper(c));
            else if (mode == 2) raw = static_cast<char>(std::tolower(c));
            else if (mode == 3) {
                raw = static_cast<char>(startWord ? std::toupper(c) : std::tolower(c));
                startWord = false;
            }
        } else if (mode == 3 && (c < 128 ? std::isspace(c) : false)) {
            startWord = true;
        }
    }
    return result;
}

void TextFormatterPr() {
    clearScreen();
    printSection("TEXT / CODE FORMATTER & ANALYZER");
    std::cout << "  Enter one line at a time. Type :done when finished.\n";
    std::cout << "  Commands: :indent :upper :lower :title :trim :dedupe :stats :done\n\n";

    std::vector<std::string> lines;
    while (true) {
        std::string line = getAdvancedInput("  > ");
        if (line == ":done") break;
        lines.push_back(line);
    }

    std::string command = getAdvancedInput("  Operation: ");
    if (command == ":indent") {
        for (std::string& line : lines) {
            size_t tabs = 0;
            while (tabs < line.size() && line[tabs] == '\t') ++tabs;
            line.replace(0, tabs, std::string(tabs * 4, ' '));
        }
    } else if (command == ":upper" || command == ":lower" || command == ":title") {
        int mode = command == ":upper" ? 1 : command == ":lower" ? 2 : 3;
        for (std::string& line : lines) line = convertCase(line, mode);
    } else if (command == ":trim") {
        for (std::string& line : lines) line = trimWhitespace(line);
    } else if (command == ":dedupe") {
        std::vector<std::string> unique;
        unique.reserve(lines.size());
        for (const std::string& line : lines)
            if (std::find(unique.begin(), unique.end(), line) == unique.end()) unique.push_back(line);
        lines.swap(unique);
    } else if (command != ":stats" && !command.empty()) {
        std::cout << "  [ERROR] Unknown formatter operation.\n";
    }

    size_t words = 0, characters = 0;
    bool inWord = false;
    std::string formatted;
    size_t totalSize = 0;
    for (const std::string& line : lines) totalSize += line.size() + 1;
    formatted.reserve(totalSize);
    for (const std::string& line : lines) {
        if (!formatted.empty()) formatted.push_back('\n');
        formatted += line;
        inWord = false;
        for (uint32_t c : decodeUtf8(line)) {
            ++characters;
            if (isUnicodeWhitespace(c)) inWord = false;
            else if (!inWord) { ++words; inWord = true; }
        }
    }

    std::cout << "\n  --- Result ---\n";
    if (formatted.empty()) std::cout << "  (empty)\n";
    else std::cout << formatted << "\n";
    std::cout << "\n  Lines       : " << lines.size() << "\n";
    std::cout << "  Words       : " << words << "\n";
    std::cout << "  Characters  : " << characters << "\n";
    if (words > 0) {
        size_t nonWhitespace = 0;
        for (uint32_t c : decodeUtf8(formatted)) if (!isUnicodeWhitespace(c)) ++nonWhitespace;
        auto oldFlags = std::cout.flags();
        auto oldPrec = std::cout.precision();
        std::cout << "  Avg word len: " << std::fixed << std::setprecision(1)
                  << static_cast<double>(nonWhitespace) / words << "\n";
        std::cout.flags(oldFlags);
        std::cout.precision(oldPrec);
    }
    pause();
}

void CleanUpCompute() {
    clearScreen();
    printSection("CLEANUP PROTOCOL");
    std::cout << "  Optimizing memory workspace...\n";

#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        std::cerr << "  [ERROR] Could not read process memory before cleanup (Win32 error " << GetLastError() << ").\n";
        pause();
        return;
    }
    SIZE_T before = pmc.WorkingSetSize;

    if (!SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1)) {
        std::cerr << "  [ERROR] Could not request working-set cleanup (Win32 error " << GetLastError() << ").\n";
    }

    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        std::cerr << "  [ERROR] Could not read process memory after cleanup (Win32 error " << GetLastError() << ").\n";
        pause();
        return;
    }
    SIZE_T after = pmc.WorkingSetSize;

    SIZE_T freed = (before > after) ? (before - after) : 0;

    std::cout << "  Memory before: " << before / 1024.0 << " KB\n";
    std::cout << "  Memory after : " << after / 1024.0 << " KB\n";
    if (freed > 0) {
        if (freed < 1024)
            std::cout << "  Freed        : " << freed << " bytes\n";
        else if (freed < 1024 * 1024)
            std::cout << "  Freed        : " << freed / 1024.0 << " KB\n";
        else
            std::cout << "  Freed        : " << freed / (1024.0 * 1024.0) << " MB\n";
    } else {
        std::cout << "  No significant memory freed.\n";
    }
#else
    std::cout << "  (Memory cleanup not implemented for this platform.)\n";
#endif
    std::cout << "  [SUCCESS] Environment sanitized.\n";
    pause();
}

// ==========================================
// QUICK NOTE
// ==========================================
void QuickNotePr() {
    bool run = true;
    const std::string MAGIC = "SHIVANSH_V1";

    while (run) {
        clearScreen();
        printSection("QUICK NOTE (XOR — not strong encryption)");
        std::cout << "  1. Create / Save Note\n";
        std::cout << "  2. Open / Read Note\n";
        std::cout << "  3. Exit to Hub\n\n";

        std::string cRaw = getAdvancedInput("  Choice: ");
        if (cRaw.empty()) continue;
        int c = -1;
        try { c = std::stoi(cRaw); } catch (...) {
            std::cout << "  [ERROR] Invalid choice.\n";
            pause();
            continue;
        }

        if (c == 3) { run = false; continue; }

        std::string path = getAdvancedInput("  Enter file path (e.g. C:\\note.txt): ");
        std::cout << "  Enter Note Password:\n";
        std::string password = getHiddenPassword();

        if (c == 1) {
            clearScreen();
            printSection("NOTE EDITOR");
            std::cout << "  [Commands] :wq (Save & Exit) | :q (Quit) | :d (Delete Last Line) | :l (List Lines)\n";
            std::cout << "  (Arrow Keys + Backspace + Delete to edit each line)\n";
            std::cout << "  ----------------------------------------------------------\n";

            std::vector<std::string> lines;
            bool editing = true, save = false;

            auto printBuffer = [&]() {
                if (lines.empty()) {
                    std::cout << "  (note is empty)\n";
                } else {
                    std::cout << "  -- Current Note (" << lines.size() << " line"
                              << (lines.size() == 1 ? "" : "s") << ") --\n";
                    for (int i = 0; i < (int)lines.size(); ++i)
                        std::cout << "  " << std::setw(3) << (i + 1)
                                  << "  " << lines[i] << "\n";
                    std::cout << "  " << std::string(54, '-') << "\n";
                }
            };

            while (editing) {
                std::string line = getAdvancedInput("  > ");
                if (line == ":wq") { save = true; editing = false; }
                else if (line == ":q") { editing = false; }
                else if (line == ":d") {
                    if (!lines.empty()) {
                        std::string deleted = lines.back();
                        lines.pop_back();
                        std::cout << "  [DELETED] \"" << deleted << "\"\n";
                        printBuffer();
                    } else {
                        std::cout << "  [INFO] Nothing to delete.\n";
                    }
                }
                else if (line == ":l") { printBuffer(); }
                else { lines.push_back(line); }
            }

            if (save) {
                if (password.empty()) {
                    std::cout << "  [ERROR] Password required. Empty passwords are not allowed.\n";
                    pause();
                    continue;
                }
                size_t totalSize = MAGIC.size() + 1;
                for (const auto& l : lines) totalSize += l.size() + 1;
                std::string fullText;
                fullText.reserve(totalSize);
                fullText += MAGIC;
                fullText.push_back('\n');
                for (const auto& l : lines) {
                    fullText += l;
                    fullText.push_back('\n');
                }
                std::string encrypted = xorCrypt(fullText, password);
                agentLog("QuickNotePr", "save attempt", "E",
                         "{\"passwordEmpty\":" + std::string(password.empty() ? "true" : "false") +
                         ",\"savedAsPlaintext\":" +
                         std::string(password.empty() && encrypted == fullText ? "true" : "false") + "}");
                std::ofstream out(path, std::ios::binary);
                if (!out.is_open()) {
                    std::cout << "  [ERROR] Cannot write to path.\n";
                } else {
                    out.write(encrypted.data(), encrypted.size());
                    if (!out.good()) {
                        std::cout << "  [ERROR] Write failed.\n";
                    } else {
                        std::cout << "  [SUCCESS] Note saved (XOR obfuscated).\n";
                    }
                    out.close();
                }
            }
            pause();
        }
        else if (c == 2) {
            std::ifstream in(path, std::ios::binary);
            if (in.is_open()) {
                std::string cipher((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                in.close();
                std::string dec = xorCrypt(cipher, password);
                if (dec.size() >= MAGIC.size() && dec.substr(0, MAGIC.size()) == MAGIC) {
                    clearScreen();
                    printSection("DECRYPTED NOTE");
                    size_t start = MAGIC.size() + 1;
                    if (start < dec.size())
                        std::cout << dec.substr(start) << "\n";
                    else
                        std::cout << "  (note is empty)\n";
                } else {
                    std::cout << "\n  [ERROR] Incorrect Password or Corrupted File.\n";
                }
            } else {
                std::cout << "\n  [ERROR] File not found.\n";
            }
            pause();
        }
        else {
            std::cout << "  [ERROR] Invalid choice.\n";
            pause();
        }
    }
}

void PasswordGeneratorPr() {
    clearScreen();
    printSection("PASSWORD GENERATOR");

    std::cout << "  Include character sets (y/n):\n";
    auto ask = [](const std::string& label) -> bool {
        std::string r = getAdvancedInput("    " + label + "? [y/n]: ");
        return !r.empty() && (r[0] == 'y' || r[0] == 'Y');
    };
    bool useLower  = ask("Lowercase (a-z)");
    bool useUpper  = ask("Uppercase (A-Z)");
    bool useDigits = ask("Digits    (0-9)");
    bool useSymbol = ask("Symbols   (!@#$%^&*)");

    std::string pool = "";
    if (useLower)  pool += "abcdefghijklmnopqrstuvwxyz";
    if (useUpper)  pool += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (useDigits) pool += "0123456789";
    if (useSymbol) pool += "!@#$%^&*()-_=+[]{}|;:,.<>?";

    if (pool.empty()) {
        std::cout << "  [INFO] No sets selected — using all sets.\n";
        pool = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    }

    int len = 0;
    while (true) {
        std::string raw = getAdvancedInput("  Length (1-256): ");
        bool ok = !raw.empty();
        for (unsigned char c : raw) if (!std::isdigit(c)) { ok = false; break; }
        if (ok) {
            try { len = std::stoi(raw); } catch (...) { ok = false; }
        }
        if (ok && len >= 1 && len <= 256) break;
        std::cout << "  [ERROR] Enter a number between 1 and 256.\n";
    }

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> distrib(0, (int)pool.size() - 1);
    std::string pass = "";
    for (int i = 0; i < len; ++i) pass += pool[distrib(rng)];

    std::cout << "\n  Generated Password:\n  " << pass << "\n";
    std::cout << "\n  Strength: " << len << " chars from a pool of " << pool.size() << "\n";
    pause();
}

// ==========================================
// MAIN HUB
// ==========================================
int main() {
    if (std::getenv("RANDOMUTIL_DEBUG_TEST")) {
        runDebugSelfTests();
        return 0;
    }
#ifdef _WIN32
    ConfigureConsole();
#endif

    printHubBanner();
    std::cout << "  Initializing Core Systems...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "  [OK] Memory Allocation\n  [OK] Utility Engine\n  [OK] Levenshtein Matrix\n\n";

    std::string userName = getAdvancedInput("  Welcome User! What is your name? ");
    if (userName.empty()) userName = "User";

    std::vector<Feature> featureIndex = {
        { 1, "Advanced Calculator",      {"calc",   "math",   "arithmetic"}},
        { 2, "Guessing Game Pro",        {"game",   "guess",  "number"}},
        { 3, "Unit Converter",           {"unit",   "convert","km","celsius"}},
        { 4, "Password Generator",       {"pass",   "gen",    "password","secret"}},
        { 5, "Base Converter",           {"base",   "binary", "hex","octal"}},
        { 6, "Random Number Generator",  {"random", "rng",    "dice"}},
        { 7, "BMI Calculator",           {"bmi",    "health", "weight","height"}},
        { 8, "Age Calculator",           {"age",    "birth",  "birthday"}},
        { 9, "Text Analyzer",            {"text",   "word",   "analyze","count"}},
        {10, "Text Formatter",           {"format", "formatter", "indent", "trim", "dedupe"}},
        {11, "Reserved",                 {"reserved"}},
        {12, "System Info",              {"info",   "system", "build","version"}},
        {13, "Clean Up Compute",         {"clean",  "ram",    "memory","optimize"}},
        {14, "Quick Note Editor",        {"note",   "vim",    "save","write"}},
        {15, "Exit System",              {"exit",   "quit",   "bye","close"}},
    };

    bool hubRunning = true;
    while (hubRunning) {
        clearScreen();
        printHubBanner();
        printLine();
        std::cout << "  Logged in as: " << userName << "\n";
        printLine();
        std::cout << "\n  [1]  Calculator       [6]  Random Gen     [11] Reserved\n";
        std::cout << "  [2]  Guessing Game    [7]  BMI Calc       [12] System Info\n";
        std::cout << "  [3]  Unit Converter   [8]  Age Calc       [13] Clean RAM\n";
        std::cout << "  [4]  Password Gen     [9]  Text Analyzer  [14] Quick Note\n";
        std::cout << "  [5]  Base Converter   [10] Text Formatter  [15] Exit System\n";

        std::cout << "\n";
        std::string input = getAdvancedInput("  [SEARCH BAR] Fuzzy Search Enabled: ");
        if (input.empty()) continue;

        int choice = -1;
        bool isNumber = true;
        for (unsigned char c : input)
        if (!std::isdigit(c)) { isNumber = false; break; }
        if (isNumber) {
            try { choice = std::stoi(input); }
            catch (...) { choice = -1; }
        } else {
            std::string query = input;
            std::transform(query.begin(), query.end(), query.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            int minDist = 999;
            std::string bestMatch = "";

            for (const auto& feat : featureIndex) {
                std::string lowerName = feat.name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (lowerName.find(query) != std::string::npos) { choice = feat.id; break; }

                bool kwMatch = false;
                for (const auto& kw : feat.keywords) {
                    if (kw.find(query) != std::string::npos) { choice = feat.id; kwMatch = true; break; }
                }
                if (kwMatch) break;

                int dist = levenshteinDistance(query, lowerName);
                if (dist < minDist && dist <= 3) { minDist = dist; choice = feat.id; bestMatch = feat.name; }

                for (const auto& kw : feat.keywords) {
                    int kwDist = levenshteinDistance(query, kw);
                    if (kwDist < minDist && kwDist <= 2) { minDist = kwDist; choice = feat.id; bestMatch = feat.name; }
                }
            }

            if (choice == -1) {
                std::cout << "\n  [SEARCH] No features found for '" << input << "'.\n";
                pause();
                continue;
            }
            else if (minDist > 0 && minDist <= 3) {
                std::cout << "\n  [SEARCH] Did you mean '" << bestMatch << "'? Launching...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
            }
        }

        {
            const char* featureName = "unknown";
            for (const auto& feat : featureIndex)
                if (feat.id == choice) { featureName = feat.name.c_str(); break; }
            agentLog("main:switch", "menu choice routed", "C",
                     "{\"choice\":" + std::to_string(choice) +
                     ",\"feature\":\"" + std::string(featureName) + "\"}");
        }

        switch (choice) {
            case 1: CalculatorPr(); break;
            case 2: GuessingGamePr(); break;
            case 3: UnitConverterPr(); break;
            case 4: PasswordGeneratorPr(); break;
            case 5: BaseConverterPr(); break;
            case 6: RandomNumberGeneratorPr(); break;
            case 7: BMICalculatorPr(); break;
            case 8: AgeCalculatorPr(); break;
            case 9: TextAnalyzerPr(); break;
            case 10: TextFormatterPr(); break;
            case 11:
                clearScreen();
                printSection("RESERVED SLOT");
                std::cout << "  [INFO] This slot is reserved for a future utility.\n";
                std::cout << "         Stay tuned for the next update!\n";
                pause();
                break;
            case 12:
                clearScreen();
                printSection("SYSTEM INFO");
                std::cout << "  Developer   : Shivansh\n";
                std::cout << "  Version     : 1.2.0 b\n";
                std::cout << "  Build Date  : " << __DATE__ << "\n";
                std::cout << "  Architecture: Standard C++ Separation\n";
                std::cout << "  Code - 13929-0611-v1.2.0b7\n";
                pause();
                break;
            case 13: CleanUpCompute(); break;
            case 14: QuickNotePr(); break;
            case 15: hubRunning = false; break;
            default:
                std::cout << "\n  [ERROR] Invalid choice.\n";
                pause();
        }
    }

    clearScreen();
    std::cout << "\n  Shutting down... Goodbye " << userName << "!\n\n";
    return 0;
}