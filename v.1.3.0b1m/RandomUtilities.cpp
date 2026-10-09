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

constexpr char programVersion[] = "v.1.3.0b1m";
thread_local IConsoleHost* g_consoleHost = nullptr;

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
    if (g_consoleHost) { g_consoleHost->clear(); return; }
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
    if (g_consoleHost) {
        g_consoleHost->waitForEnter("\n  Press Enter to continue...\n");
        return;
    }
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
    if (g_consoleHost) {
        return g_consoleHost->readPassword("  Password: ");
    }
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

    // Switch the console to UTF-8 so multi-byte characters (degree sign,
    // multiplication sign, ≤, ≥, etc.) render correctly instead of
    // appearing as CP437 mojibake like ┬░.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
         DWORD outMode = 0;
         if (GetConsoleMode(hOut, &outMode)) {
               SetConsoleMode(hOut, outMode
                       | ENABLE_PROCESSED_OUTPUT
                       | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
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

// Core implementation.  navOut is set on exit:
//   0  = Enter was pressed (normal commit)
//  -1  = Up arrow was pressed
//   1  = Down arrow was pressed
//  -2  = Escape was pressed
static std::string getAdvancedInputImpl(const std::string& prompt,
                                         const std::wstring& preload,
                                         int& navOut) {
    navOut = 0;
    std::cout.flush();
    std::cerr.flush();

    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD oldModeIn = 0;
    GetConsoleMode(hIn, &oldModeIn);
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
    FlushConsoleInputBuffer(hIn);

    InputState state;
    state.text         = preload;
    state.cursor       = preload.size();
    state.previousRows = 1;

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (!GetConsoleScreenBufferInfo(hOut, &csbi) || csbi.dwSize.X <= 0) {
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

        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) continue;

        if (vk == 'C' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            setClipboardTextW(state.text); continue;
        }
        if (vk == 'V' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            std::wstring wclip = getClipboardTextW();
            if (!wclip.empty()) {
                for (wchar_t& c : wclip) if (c == L'\r' || c == L'\n') c = L' ';
                state.text.insert(state.cursor, wclip);
                state.cursor += wclip.length();
                redrawInput(hOut, state, prompt);
            }
            continue;
        }
        if (vk == VK_F5) { setClipboardTextW(state.text); continue; }

        // New: Up / Down propagate a navigation signal to the caller.
        if (vk == VK_UP) {
            const int promptLen = (int)utf8_to_wstring(prompt).length();
            COORD endOfText = calculateCursorPosition(state, promptLen, state.text.length());
            COORD col0 = { 0, endOfText.Y };
            SetConsoleCursorPosition(hOut, col0);
            DWORD written = 0; WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);
            SetConsoleMode(hIn, oldModeIn);
            navOut = -1;
            return "";
        }
        if (vk == VK_DOWN) {
            const int promptLen = (int)utf8_to_wstring(prompt).length();
            COORD endOfText = calculateCursorPosition(state, promptLen, state.text.length());
            COORD col0 = { 0, endOfText.Y };
            SetConsoleCursorPosition(hOut, col0);
            DWORD written = 0; WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);
            SetConsoleMode(hIn, oldModeIn);
            navOut = 1;
            return "";
        }

        if (vk == VK_ESCAPE) {
            const int promptLen = (int)utf8_to_wstring(prompt).length();
            COORD endOfText = calculateCursorPosition(state, promptLen, state.text.length());
            COORD col0 = { 0, endOfText.Y };
            SetConsoleCursorPosition(hOut, col0);
            DWORD written = 0; WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);
            SetConsoleMode(hIn, oldModeIn);
            navOut = -2;
            return "";
        }

        if (vk == VK_RETURN) {
            const int promptLen = (int)utf8_to_wstring(prompt).length();
            COORD endOfText = calculateCursorPosition(state, promptLen, state.text.length());
            COORD col0 = { 0, endOfText.Y };
            SetConsoleCursorPosition(hOut, col0);
            DWORD written = 0; WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);
            done = true;
            continue;
        }

        if (vk == VK_HOME) { state.cursor = 0;                     redrawInput(hOut, state, prompt); continue; }
        if (vk == VK_END)  { state.cursor = state.text.length();   redrawInput(hOut, state, prompt); continue; }
        if (vk == VK_LEFT) { moveCursor(state, -1);                redrawInput(hOut, state, prompt); continue; }
        if (vk == VK_RIGHT){ moveCursor(state,  1);                redrawInput(hOut, state, prompt); continue; }

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
            isLowSurrogate(uc) && state.cursor > 0 &&
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

std::string getAdvancedInput(const std::string& prompt) {
    if (g_consoleHost) return g_consoleHost->readLine(prompt);
    int nav = 0;
    return getAdvancedInputImpl(prompt, L"", nav);
}

std::string getAdvancedInputWithNav(const std::string& prompt,
                                     const std::string& preload,
                                     int& navOut) {
    if (g_consoleHost) {
        navOut = 0;
        return g_consoleHost->readLine(prompt + (preload.empty() ? "" : ("[" + preload + "] ")));
    }
    return getAdvancedInputImpl(prompt, utf8_to_wstring(preload), navOut);
}

#else
std::string getAdvancedInput(const std::string& prompt) {
    if (g_consoleHost) return g_consoleHost->readLine(prompt);
    std::cout << prompt;
    std::cout.flush();
    std::string text;
    std::getline(std::cin, text);
    return text;
}

std::string getAdvancedInputWithNav(const std::string& prompt,
                                     const std::string& preload,
                                     int& navOut) {
    navOut = 0;
    if (g_consoleHost) return g_consoleHost->readLine(prompt + (preload.empty() ? "" : ("[" + preload + "] ")));
    std::cout << prompt;
    if (!preload.empty()) std::cout << "[" << preload << "] ";
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
    while (true) {
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

        std::string again = getAdvancedInput("  Play again? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
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
    if (abbr == "Re") return v * 5.0 / 4.0;               // Réaumur
    if (abbr == "De") return 100.0 - v * 2.0 / 3.0;       // Delisle
    return v;
}
static double fromBaseTemp(double c, const std::string& abbr) {
    if (abbr == "C")  return c;
    if (abbr == "F")  return c * 9.0 / 5.0 + 32.0;
    if (abbr == "K")  return c + 273.15;
    if (abbr == "R")  return (c + 273.15) * 9.0 / 5.0;
    if (abbr == "Re") return c * 4.0 / 5.0;               // Réaumur
    if (abbr == "De") return (100.0 - c) * 3.0 / 2.0;     // Delisle
    return c;
}

void UnitConverterPr() {
    struct Category {
        std::string          name;
        std::vector<UnitDef> units;
        bool                 isTemp;
    };

        const std::vector<Category> cats = {

        // -------------------- Length --------------------
        { "Length", {
            {"Nanometre",     "nm",   1e-9},
            {"Micrometre",    "um",   1e-6},
            {"Millimetre",    "mm",   0.001},
            {"Centimetre",    "cm",   0.01},
            {"Metre",         "m",    1.0},
            {"Kilometre",     "km",   1000.0},
            {"Inch",          "in",   0.0254},
            {"Foot",          "ft",   0.3048},
            {"Yard",          "yd",   0.9144},
            {"Mile",          "mi",   1609.344},
            {"Nautical Mi",   "nmi",  1852.0},
            {"Fathom",        "ftm",  1.8288},
            {"Furlong",       "fur",  201.168},
            {"Astron. Unit",  "AU",   1.495978707e11},
            {"Light Year",    "ly",   9.4607304725808e15},
            {"Parsec",        "pc",   3.0856775814913673e16},
        }, false },

        // -------------------- Mass / Weight --------------------
        { "Mass / Weight", {
            {"Microgram",     "ug",   1e-9},
            {"Milligram",     "mg",   1e-6},
            {"Gram",          "g",    0.001},
            {"Kilogram",      "kg",   1.0},
            {"Tonne",         "t",    1000.0},
            {"Ounce",         "oz",   0.028349523125},
            {"Pound",         "lb",   0.45359237},
            {"Stone",         "st",   6.35029318},
            {"US Ton",        "ust",  907.18474},
            {"UK Ton",        "ukt",  1016.0469088},
            {"Carat",         "ct",   0.0002},
            {"Grain",         "gr",   6.479891e-5},
            {"Atomic Mass U", "u",    1.66053906660e-27},
        }, false },

        // -------------------- Temperature --------------------
        { "Temperature", {
            {"Celsius",       "C",    1.0},
            {"Fahrenheit",    "F",    1.0},
            {"Kelvin",        "K",    1.0},
            {"Rankine",       "R",    1.0},
            {"Reaumur",       "Re",   1.0},
            {"Delisle",       "De",   1.0},
        }, true },

        // -------------------- Volume --------------------
        { "Volume", {
            {"Millilitre",    "ml",    0.001},
            {"Litre",         "L",     1.0},
            {"Cubic Metre",   "m3",    1000.0},
            {"Teaspoon (US)", "tsp",   0.00492892159375},
            {"Tablespoon(US)","tbsp",  0.0147867647812},
            {"US fl oz",      "floz",  0.0295735295625},
            {"US Cup",        "cup",   0.2365882365},
            {"US Pint",       "pt",    0.473176473},
            {"US Quart",      "qt",    0.946352946},
            {"US Gallon",     "gal",   3.785411784},
            {"UK Gallon",     "ukgal", 4.54609},
            {"Barrel (oil)",  "bbl",   158.987294928},
            {"Bushel (US)",   "bu",    35.23907016688},
            {"Cubic Inch",    "in3",   0.016387064},
            {"Cubic Foot",    "ft3",   28.316846592},
        }, false },

        // -------------------- Speed --------------------
        { "Speed", {
            {"m/s",            "m/s",  1.0},
            {"km/h",           "km/h", 1.0 / 3.6},
            {"mph",            "mph",  0.44704},
            {"Knot",           "kn",   0.514444444444},
            {"ft/s",           "ft/s", 0.3048},
            {"Mach (sea)",     "mach", 340.29},
            {"Sound (air 20C)","snd",  343.0},
            {"Speed of Light", "c",    299792458.0},
        }, false },

        // -------------------- Data Storage --------------------
        { "Data Storage", {
            {"Bit",            "bit",  0.125},
            {"Nibble",         "nib",  0.5},
            {"Byte",           "B",    1.0},
            {"Word (16-bit)",  "word", 2.0},
            {"Kilobyte (1000)","KB",   1000.0},
            {"Kibibyte (1024)","KiB",  1024.0},
            {"Megabyte",       "MB",   1e6},
            {"Mebibyte",       "MiB",  1048576.0},
            {"Gigabyte",       "GB",   1e9},
            {"Gibibyte",       "GiB",  1073741824.0},
            {"Terabyte",       "TB",   1e12},
            {"Tebibyte",       "TiB",  1099511627776.0},
            {"Petabyte",       "PB",   1e15},
            {"Pebibyte",       "PiB",  1125899906842624.0},
            {"Exabyte",        "EB",   1e18},
        }, false },

        // -------------------- Area --------------------
        { "Area", {
            {"Square mm",     "mm2",  1e-6},
            {"Square cm",     "cm2",  1e-4},
            {"Square inch",   "in2",  6.4516e-4},
            {"Square foot",   "ft2",  0.09290304},
            {"Square yard",   "yd2",  0.83612736},
            {"Square metre",  "m2",   1.0},
            {"Acre",          "ac",   4046.8564224},
            {"Hectare",       "ha",   10000.0},
            {"Square km",     "km2",  1e6},
            {"Square mile",   "mi2",  2589988.110336},
            {"Barn",          "barn", 1e-28},
        }, false },

        // -------------------- Time --------------------
        { "Time", {
            {"Nanosecond",    "ns",   1e-9},
            {"Microsecond",   "us",   1e-6},
            {"Millisecond",   "ms",   0.001},
            {"Second",        "s",    1.0},
            {"Minute",        "min",  60.0},
            {"Hour",          "hr",   3600.0},
            {"Day",           "day",  86400.0},
            {"Week",          "wk",   604800.0},
            {"Fortnight",     "fn",   1209600.0},
            {"Month (avg)",   "mo",   2629800.0},
            {"Year (Julian)", "yr",   31557600.0},
            {"Decade",        "dec",  315576000.0},
            {"Century",       "cen",  3155760000.0},
            {"Millennium",    "mil",  31557600000.0},
        }, false },

        // -------------------- Energy --------------------
        { "Energy", {
            {"Joule",         "J",    1.0},
            {"Kilojoule",     "kJ",   1000.0},
            {"Calorie",       "cal",  4.184},
            {"Kilocalorie",   "kcal", 4184.0},
            {"Watt-hour",     "Wh",   3600.0},
            {"Kilowatt-hour", "kWh",  3.6e6},
            {"BTU",           "BTU",  1055.05585262},
            {"Foot-pound",    "ftlb", 1.35581794833},
            {"Therm",         "thm",  1.05505585262e8},
            {"Ton of TNT",    "tTNT", 4.184e9},
            {"Erg",           "erg",  1e-7},
            {"Electronvolt",  "eV",   1.602176634e-19},
        }, false },

        // -------------------- Pressure --------------------
        { "Pressure", {
            {"Pascal",         "Pa",   1.0},
            {"Kilopascal",     "kPa",  1000.0},
            {"Megapascal",     "MPa",  1e6},
            {"Bar",            "bar",  100000.0},
            {"Millibar",       "mbar", 100.0},
            {"Atmosphere",     "atm",  101325.0},
            {"Torr / mmHg",    "torr", 133.322387415},
            {"psi",            "psi",  6894.757293168},
            {"Technical Atm",  "at",   98066.5},
        }, false },

        // -------------------- Angle --------------------
        { "Angle", {
            {"Radian",         "rad",  1.0},
            {"Degree",         "deg",  0.0174532925199433},
            {"Gradian",        "grad", 0.015707963267949},
            {"Arcminute",      "arcm", 0.000290888208665722},
            {"Arcsecond",      "arcs", 4.84813681109536e-6},
            {"Turn (rev)",     "turn", 6.28318530717959},
            {"Mil (NATO)",     "mil",  0.00098174770424681},
        }, false },

        // -------------------- Frequency --------------------
        { "Frequency", {
            {"Hertz",          "Hz",   1.0},
            {"Kilohertz",      "kHz",  1000.0},
            {"Megahertz",      "MHz",  1e6},
            {"Gigahertz",      "GHz",  1e9},
            {"Terahertz",      "THz",  1e12},
            {"RPM",            "rpm",  1.0 / 60.0},
            {"Beats/min (BPM)","bpm",  1.0 / 60.0},
        }, false },

        // -------------------- Power --------------------
        { "Power", {
            {"Watt",             "W",    1.0},
            {"Kilowatt",         "kW",   1000.0},
            {"Megawatt",         "MW",   1e6},
            {"Gigawatt",         "GW",   1e9},
            {"Horsepower (mech)","hp",   745.699871582},
            {"Horsepower (metr)","PS",   735.49875},
            {"BTU / hour",       "BTU/h",0.29307107},
            {"Foot-pound / s",   "ftlb/s",1.35581794833},
        }, false },

        // -------------------- Force --------------------
        { "Force", {
            {"Newton",         "N",    1.0},
            {"Kilonewton",     "kN",   1000.0},
            {"Dyne",           "dyn",  1e-5},
            {"Pound-force",    "lbf",  4.4482216152605},
            {"Kilogram-force", "kgf",  9.80665},
            {"Kip",            "kip",  4448.2216152605},
        }, false },

        // -------------------- Data Rate --------------------
        { "Data Rate", {
            {"bit/s",          "bps",  1.0},
            {"kbit/s",         "kbps", 1000.0},
            {"Mbit/s",         "Mbps", 1e6},
            {"Gbit/s",         "Gbps", 1e9},
            {"Byte/s",         "B/s",  8.0},
            {"kB/s",           "kB/s", 8000.0},
            {"MB/s",           "MB/s", 8e6},
            {"GB/s",           "GB/s", 8e9},
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
    while (true) {
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
        if (!readIntRange("  Birth Year  : ", 1, 9999, y)) { pause(); break; }
        if (!readIntRange("  Birth Month : ", 1, 12,   m)) { pause(); break; }
        if (!readIntRange("  Birth Day   : ", 1, 31,   d)) { pause(); break; }

        if (d > daysInMonth(y, m)) {
            std::cout << "  [ERROR] Invalid day for that month/year.\n";
        } else {
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
                    if (nextYearDays <= currentDays) { ageYears++; tempYear = nextYear; }
                    else break;
                }

                int ageMonths = 0;
                while (true) {
                    int nextMonth = tempMonth + 1;
                    int nextYear = tempYear;
                    if (nextMonth > 12) { nextMonth = 1; nextYear++; }
                    int dayInNextMonth = std::min(tempDay, daysInMonth(nextYear, nextMonth));
                    long long nextMonthDays = dateToDays(nextYear, nextMonth, dayInNextMonth);
                    if (nextMonthDays <= currentDays) {
                        ageMonths++; tempMonth = nextMonth; tempYear = nextYear;
                    } else break;
                }

                int dayInCurrent = std::min(tempDay, daysInMonth(tempYear, tempMonth));
                int ageDays = (int)(currentDays - dateToDays(tempYear, tempMonth, dayInCurrent));

                std::cout << "  Age: " << ageYears << " years, " << ageMonths
                          << " months, " << ageDays << " days\n";
            }
        }
        pause();

        std::string again = getAdvancedInput("  Run again? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

// ==========================================
// OTHER UTILITIES
// ==========================================

void BaseConverterPr() {
    while (true) {
        clearScreen();
        printSection("BASE CONVERTER");

        std::string raw = getAdvancedInput("  Enter Decimal (non-negative): ");
        long long d = 0;
        bool ok = !raw.empty();
        for (int i = (raw.empty()?0:(raw[0]=='-'?1:0)); i < (int)raw.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(raw[i]))) { ok = false; break; }
        if (!ok) { std::cout << "  [ERROR] Enter a whole number.\n"; }
        else {
            try { d = std::stoll(raw); } catch (...) { ok = false; }
            if (!ok) { std::cout << "  [ERROR] Number too large.\n"; }
            else if (d < 0) { std::cout << "  [ERROR] Non-negative only.\n"; }
            else {
                std::cout << "\n";
                std::cout << "  Decimal: " << d << "\n";
                std::cout << "  Binary:  " << toBinary(d) << "\n";
                std::cout << "  Octal:   " << std::oct << d << std::dec << "\n";
                std::cout << "  Hex:     " << std::uppercase << std::hex << d
                          << std::dec << std::nouppercase << "\n";
            }
        }
        pause();

        std::string again = getAdvancedInput("  Convert another? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

void BMICalculatorPr() {
    while (true) {
        clearScreen();
        printSection("BMI CALCULATOR");
        double w = 0, h = 0;
        bool good = true;

        if (!readDouble("  Weight (kg): ", w)) { good = false; }
        else if (w <= 0) { std::cout << "  [ERROR] Weight must be positive.\n"; good = false; }
        else if (!readDouble("  Height (m):  ", h)) { good = false; }
        else if (h <= 0) { std::cout << "  [ERROR] Height must be positive.\n"; good = false; }
        else if (h > 3.0) { std::cout << "  [ERROR] Height over 3 m is unlikely. Did you enter cm?\n"; good = false; }

        if (good) {
            double bmi = w / (h * h);
            std::string category;
            if      (bmi < 18.5) category = "Underweight";
            else if (bmi < 25.0) category = "Normal weight";
            else if (bmi < 30.0) category = "Overweight";
            else if (bmi < 35.0) category = "Obese (Class I)";
            else if (bmi < 40.0) category = "Obese (Class II)";
            else                 category = "Obese (Class III)";

            std::cout << "\n  BMI      : ";
            auto oldFlags = std::cout.flags();
            auto oldPrec = std::cout.precision();
            std::cout << std::fixed << std::setprecision(2) << bmi << "\n";
            std::cout.flags(oldFlags);
            std::cout.precision(oldPrec);
            std::cout << "  Category : " << category << "\n";
        }
        pause();

        std::string again = getAdvancedInput("  Run again? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

void TextAnalyzerPr() {
    while (true) {
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

        std::string again = getAdvancedInput("  Analyze another? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
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
    while (true) {
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

        std::string again = getAdvancedInput("  Run again? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

void RandomNumberGeneratorPr() { 
    while (true) {
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
        if (!readInt("  Min: ", mn)) { pause(); break; }
        if (!readInt("  Max: ", mx)) { pause(); break; }
        if (mn > mx) { std::cout << "  [ERROR] Min > Max.\n"; }
        else {
            std::mt19937_64 rng(std::random_device{}());
            std::uniform_int_distribution<long long> dist(mn, mx);
            std::cout << "\n  Result: " << dist(rng) << "\n";
        }
        pause();

        std::string again = getAdvancedInput("  Roll again? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

void CleanUpCompute() {
    while (true) {
        clearScreen();
        printSection("CLEANUP PROTOCOL");
        std::cout << "  Optimizing memory workspace...\n";

#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            std::cerr << "  [ERROR] Could not read process memory before cleanup (Win32 error " << GetLastError() << ").\n";
        } else {
            SIZE_T before = pmc.WorkingSetSize;
            if (!SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1)) {
                std::cerr << "  [ERROR] Could not request working-set cleanup (Win32 error " << GetLastError() << ").\n";
            }
            if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                std::cerr << "  [ERROR] Could not read process memory after cleanup (Win32 error " << GetLastError() << ").\n";
            } else {
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
            }
        }
#else
        std::cout << "  (Memory cleanup not implemented for this platform.)\n";
#endif
        std::cout << "  [SUCCESS] Environment sanitized.\n";
        pause();

        std::string again = getAdvancedInput("  Run again? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

// ==========================================
// QUICK NOTE
// ==========================================\

// Key codes sent by the GUI session: characters arrive as their character code,
// other keys as 0x10000 + Windows virtual-key code (values written out so this
// compiles everywhere).
constexpr int kNoteKeyPgUp   = 0x10000 + 0x21;   // VK_PRIOR
constexpr int kNoteKeyPgDn   = 0x10000 + 0x22;   // VK_NEXT
constexpr int kNoteKeyEnd    = 0x10000 + 0x23;   // VK_END
constexpr int kNoteKeyHome   = 0x10000 + 0x24;   // VK_HOME
constexpr int kNoteKeyLeft   = 0x10000 + 0x25;   // VK_LEFT
constexpr int kNoteKeyUp     = 0x10000 + 0x26;   // VK_UP
constexpr int kNoteKeyRight  = 0x10000 + 0x27;   // VK_RIGHT
constexpr int kNoteKeyDown   = 0x10000 + 0x28;   // VK_DOWN
constexpr int kNoteKeyDelete = 0x10000 + 0x2E;   // VK_DELETE
constexpr int kNoteKeyF2     = 0x10000 + 0x71;   // VK_F2

// The pane draws the caret where ESC [ z appears, as a thin bar in front of the
// next character. The text is never changed, so nothing looks truncated.
static const char* kCaretMark = "\x1b[z";

// ---- Markdown rendering (styled with the ANSI codes the pane understands) ------

static std::string mdSgr(const char* code) { return std::string("\x1b[") + code + "m"; }

static size_t mdIndent(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i;
}

static bool mdIsFence(const std::string& s) {
    return s.compare(mdIndent(s), 3, "```") == 0;
}

// Inline spans: **bold**, *em* / _em_, `code`, [text](url)
static std::string mdInline(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s.compare(i, 2, "**") == 0) {
            size_t e = s.find("**", i + 2);
            if (e != std::string::npos && e > i + 2) {
                out += mdSgr("1") + s.substr(i + 2, e - i - 2) + mdSgr("22");
                i = e + 2;
                continue;
            }
        }
        if (s[i] == '`') {
            size_t e = s.find('`', i + 1);
            if (e != std::string::npos) {
                out += mdSgr("48;2;58;58;72") + s.substr(i + 1, e - i - 1) + mdSgr("49");
                i = e + 1;
                continue;
            }
        }
        if ((s[i] == '*' || s[i] == '_') && i + 1 < s.size() && s[i + 1] != ' ') {
            size_t e = s.find(s[i], i + 1);
            if (e != std::string::npos && e > i + 1) {
                out += mdSgr("4") + s.substr(i + 1, e - i - 1) + mdSgr("24");
                i = e + 1;
                continue;
            }
        }
        if (s[i] == '[') {
            size_t close = s.find(']', i + 1);
            if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(') {
                size_t pe = s.find(')', close + 1);
                if (pe != std::string::npos) {
                    out += mdSgr("4;38;2;110;190;255") + s.substr(i + 1, close - i - 1) + mdSgr("24;39");
                    i = pe + 1;
                    continue;
                }
            }
        }
        out += s[i];
        ++i;
    }
    return out;
}

// One source line of markdown -> one styled output line
static void mdPrintLine(const std::string& raw, bool& inCode) {
    if (mdIsFence(raw)) {
        inCode = !inCode;
        std::cout << mdSgr("38;2;120;120;135") << "  " << std::string(40, '-') << mdSgr("39") << "\n";
        return;
    }
    if (inCode) {
        std::cout << mdSgr("48;2;40;40;50") << "  " << raw << mdSgr("49") << "\n";
        return;
    }

    const size_t ind = mdIndent(raw);
    const std::string pad(ind, ' ');
    const std::string body = raw.substr(ind);
    if (body.empty()) { std::cout << "\n"; return; }

    // Horizontal rule: three or more of the same -, * or _
    if (body.size() >= 3 &&
        (body.find_first_not_of('-') == std::string::npos ||
         body.find_first_not_of('*') == std::string::npos ||
         body.find_first_not_of('_') == std::string::npos)) {
        std::cout << mdSgr("38;2;120;120;135") << "  " << std::string(40, '-') << mdSgr("39") << "\n";
        return;
    }

    // Headings: # to ######
    size_t h = 0;
    while (h < body.size() && body[h] == '#') ++h;
    if (h >= 1 && h <= 6 && h < body.size() && body[h] == ' ') {
        std::cout << pad << mdSgr("1;38;2;127;208;255") << body.substr(h + 1) << mdSgr("22;39") << "\n";
        return;
    }

    // Block quote
    if (body.size() >= 2 && body[0] == '>' && body[1] == ' ') {
        std::cout << pad << mdSgr("38;2;120;120;135") << "| " << mdSgr("39")
                  << mdInline(body.substr(2)) << "\n";
        return;
    }

    // Bullet list
    if (body.size() >= 2 && (body[0] == '-' || body[0] == '*' || body[0] == '+') && body[1] == ' ') {
        std::cout << pad << "  \xE2\x80\xA2 " << mdInline(body.substr(2)) << "\n";   // bullet
        return;
    }

    // Numbered list: digits, ". "
    size_t d = 0;
    while (d < body.size() && body[d] >= '0' && body[d] <= '9') ++d;
    if (d > 0 && d + 1 < body.size() && body[d] == '.' && body[d + 1] == ' ') {
        std::cout << pad << "  " << body.substr(0, d + 2) << mdInline(body.substr(d + 2)) << "\n";
        return;
    }

    std::cout << pad << mdInline(body) << "\n";
}

// Prints lines [top, top+count) of the note as markdown
static void mdPrintWindow(const std::vector<std::string>& lines, size_t top, size_t count) {
    bool inCode = false;
    for (size_t i = 0; i < top && i < lines.size(); ++i)   // code-fence state above the window
        if (mdIsFence(lines[i])) inCode = !inCode;
    const size_t end = std::min(lines.size(), top + count);
    for (size_t i = top; i < end; ++i) mdPrintLine(lines[i], inCode);
}

// Note editor for an embedded tool session. It has two views:
//   plain text  - editable, the same keys and commands as the console editor
//   markdown    - rendered preview, read-only (F2 or :md / :plain switch views)
// The pane cannot move the cursor, so the visible part is redrawn after each key.
// Returns true when the user chose :wq.
static bool hostEditNote(const std::string& path, std::vector<std::string>& lines, std::string& password) {
    if (lines.empty()) lines.push_back("");
    size_t cur = 0, col = 0;
    size_t top = 0;                       // first line shown in the markdown view
    bool preview = false;
    std::string status;
    bool save = false;
    const size_t kWindow      = 18;       // note lines shown in the plain view
    const size_t kPreviewRows = 24;       // rendered lines shown in the markdown view

    // Byte offset of the UTF-8 character that contains byte c
    auto charStart = [](const std::string& str, size_t c) {
        if (c > str.size()) c = str.size();
        while (c > 0 && c < str.size() && (static_cast<unsigned char>(str[c]) & 0xC0) == 0x80) --c;
        return c;
    };
    // Byte offset of the character after the one at byte c
    auto charNext = [](const std::string& str, size_t c) {
        if (c >= str.size()) return str.size();
        ++c;
        while (c < str.size() && (static_cast<unsigned char>(str[c]) & 0xC0) == 0x80) ++c;
        return c;
    };

    auto redraw = [&]() {
        clearScreen();
        if (preview) {
            printSection("NOTE PREVIEW (MARKDOWN)  --  " + path);
            std::cout << "  [F2 / Esc] Back to plain text   [Up/Down, PgUp/PgDn] Scroll   [Home/End] Top / bottom\n";
            std::cout << "  ----------------------------------------------------------\n\n";
            const size_t maxTop = lines.size() > kPreviewRows ? lines.size() - kPreviewRows : 0;
            if (top > maxTop) top = maxTop;
            mdPrintWindow(lines, top, kPreviewRows);
            const size_t shown = std::min(lines.size(), top + kPreviewRows);
            std::cout << "\n  Lines " << (top + 1) << "-" << shown << " of " << lines.size() << "\n";
        } else {
            printSection("NOTE EDITOR (PLAIN TEXT)  --  " + path);
            std::cout << "  [Arrows] Move  [Enter] Split  [Backspace] Join previous  [Del] Join next  [F2] Markdown view\n";
            std::cout << "  Commands on their own line:  :wq save & exit   :q quit   :d delete line   :pass password\n";
            std::cout << "                               :md markdown view   :plain plain text view\n";
            std::cout << "  ----------------------------------------------------------\n\n";
            size_t start = cur > kWindow / 2 ? cur - kWindow / 2 : 0;
            size_t end   = std::min(lines.size(), start + kWindow);
            if (end == lines.size() && lines.size() > kWindow && end - start < kWindow)
                start = lines.size() - kWindow;
            for (size_t i = start; i < end; ++i) {
                std::string num = std::to_string(i + 1);
                while (num.size() < 3) num = " " + num;
                std::cout << (i == cur ? "> " : "  ") << num << "  ";
                if (i == cur) {
                    const size_t caret = charStart(lines[i], col);
                    const std::string after = lines[i].substr(caret);
                    std::cout << lines[i].substr(0, caret) << kCaretMark
                              << (after.empty() ? std::string(" ") : after);
                } else {
                    std::cout << lines[i];
                }
                std::cout << "\n";
            }
            std::cout << "\n  Line " << (cur + 1) << " of " << lines.size() << "\n";
        }
        if (!status.empty()) std::cout << "  " << status << "\n";
        std::cout.flush();
    };

    redraw();
    while (true) {
        const int k = g_consoleHost->readKey();
        if (k < 0) break;                              // session closed
        status.clear();

        if (preview) {
            const size_t maxTop = lines.size() > kPreviewRows ? lines.size() - kPreviewRows : 0;
            if (k == 27 || k == kNoteKeyF2)      { preview = false; }
            else if (k == kNoteKeyUp)            { if (top > 0) --top; }
            else if (k == kNoteKeyDown)          { if (top < maxTop) ++top; }
            else if (k == kNoteKeyPgUp)          { top = top > kPreviewRows - 3 ? top - (kPreviewRows - 3) : 0; }
            else if (k == kNoteKeyPgDn)          { top = std::min(maxTop, top + (kPreviewRows - 3)); }
            else if (k == kNoteKeyHome)          { top = 0; }
            else if (k == kNoteKeyEnd)           { top = maxTop; }
            else continue;                             // nothing changed
            redraw();
            continue;
        }

        if (k == 27) break;                            // Esc: leave without saving
        if (k == kNoteKeyF2) { preview = true; top = 0; redraw(); continue; }

        if (k == kNoteKeyUp) {
            if (cur > 0) { --cur; col = charStart(lines[cur], col); }
        } else if (k == kNoteKeyDown) {
            if (cur + 1 < lines.size()) { ++cur; col = charStart(lines[cur], col); }
        } else if (k == 13) {                          // Enter: commands, or split the line
            const std::string cmd = lines[cur];
            if (cmd == ":wq") { save = true; break; }
            if (cmd == ":q")  break;
            if (cmd == ":md" || cmd == ":plain") {
                lines[cur].clear();
                col = 0;
                preview = (cmd == ":md");
                top = 0;
                redraw();
                continue;
            }
            if (cmd == ":pass") {
                lines[cur].clear();
                col = 0;
                status = "Type the new password below. Leave it empty to clear the password.";
                redraw();
                std::string np = getHiddenPassword();
                if (!np.empty()) { password = np;    status = "[OK] Password set. Save with :wq to write encrypted."; }
                else             { password.clear(); status = "[INFO] Password cleared. Save with :wq to write plain text."; }
                redraw();
                continue;
            }
            if (cmd == ":d") {
                if (lines.size() > 1) {
                    lines.erase(lines.begin() + cur);
                    if (cur >= lines.size()) cur = lines.size() - 1;
                } else {
                    lines[0].clear();
                }
                col = 0;
            } else {
                const std::string tail = lines[cur].substr(col);
                lines[cur].resize(col);
                lines.insert(lines.begin() + cur + 1, tail);
                ++cur;
                col = 0;
            }
        } else if (k == kNoteKeyLeft) {
            if (col > 0) col = charStart(lines[cur], col - 1);
            else if (cur > 0) { --cur; col = lines[cur].size(); }
        } else if (k == kNoteKeyRight) {
            if (col < lines[cur].size()) col = charNext(lines[cur], col);
            else if (cur + 1 < lines.size()) { ++cur; col = 0; }
        } else if (k == kNoteKeyHome) {
            col = 0;
        } else if (k == kNoteKeyEnd) {
            col = lines[cur].size();
        } else if (k == 8) {                           // Backspace
            if (col > 0) {
                const size_t from = charStart(lines[cur], col - 1);
                lines[cur].erase(from, col - from);
                col = from;
            } else if (cur > 0) {
                col = lines[cur - 1].size();
                lines[cur - 1] += lines[cur];
                lines.erase(lines.begin() + cur);
                --cur;
            }
        } else if (k == kNoteKeyDelete) {
            if (col < lines[cur].size()) {
                lines[cur].erase(col, charNext(lines[cur], col) - col);
            } else if (cur + 1 < lines.size()) {
                lines[cur] += lines[cur + 1];
                lines.erase(lines.begin() + cur + 1);
            }
        } else if (k >= 32 && k < 0x10000 && !(k >= 0xD800 && k <= 0xDFFF)) {   // printable (BMP)
            std::string enc;
            if (k < 0x80) {
                enc += (char)k;
            } else if (k < 0x800) {
                enc += (char)(0xC0 | (k >> 6));
                enc += (char)(0x80 | (k & 0x3F));
            } else {
                enc += (char)(0xE0 | (k >> 12));
                enc += (char)(0x80 | ((k >> 6) & 0x3F));
                enc += (char)(0x80 | (k & 0x3F));
            }
            lines[cur].insert(col, enc);
            col += enc.size();
        } else {
            continue;                                  // nothing changed
        }
        redraw();
    }
    return save;
}

void QuickNotePr() {
    bool run = true;
    const std::string MAGIC = "SHIVANSH_V1";

    while (run) {
        clearScreen();
        printSection("QUICK NOTE  (.txt / .md — plain or password-protected)");
        std::cout << "  1. Open / Create & Edit\n";
        std::cout << "  2. View (read-only)\n";
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
        if (c != 1 && c != 2) {
            std::cout << "  [ERROR] Invalid choice.\n";
            pause();
            continue;
        }

        std::string path = getAdvancedInput("  File path (.txt / .md): ");
        if (path.empty()) continue;

        // ------------------------------------------------------------
        // Load (if file exists) and auto-detect encryption
        // ------------------------------------------------------------
        std::vector<std::string> lines;
        std::string password;
        bool fileExists = false;

        auto splitLines = [](const std::string& body, std::vector<std::string>& out) {
            out.clear();
            size_t start = 0;
            while (start <= body.size()) {
                size_t nl = body.find('\n', start);
                if (nl == std::string::npos) {
                    if (start < body.size()) out.push_back(body.substr(start));
                    break;
                }
                out.push_back(body.substr(start, nl - start));
                start = nl + 1;
            }
        };

        {
            std::ifstream in(path, std::ios::binary);
            if (in.is_open()) {
                fileExists = true;
                std::string raw((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                in.close();

                // The MAGIC header is written in PLAINTEXT at the top of
                // encrypted files, so we can reliably tell encrypted files
                // apart from plain ones. The encrypted payload begins
                // immediately after MAGIC + '\n'.
                if (raw.size() >= MAGIC.size() &&
                    raw.compare(0, MAGIC.size(), MAGIC) == 0) {

                    std::cout << "  [INFO] This file is password-protected.\n";
                    std::cout << "  Enter Password:\n";
                    password = getHiddenPassword();

                    std::string cipher = (raw.size() > MAGIC.size() + 1)
                                       ? raw.substr(MAGIC.size() + 1)
                                       : std::string();
                    std::string dec = xorCrypt(cipher, password);

                    // The decrypted body begins with a second copy of
                    // MAGIC — that's how we verify the password.
                    if (dec.size() >= MAGIC.size() &&
                        dec.compare(0, MAGIC.size(), MAGIC) == 0) {
                        std::string body = (dec.size() > MAGIC.size() + 1)
                                         ? dec.substr(MAGIC.size() + 1)
                                         : std::string();
                        splitLines(body, lines);
                    } else {
                        std::cout << "  [ERROR] Wrong password.\n";
                        pause();
                        continue;
                    }
                } else {
                    // Plain text file — load verbatim, no scrambling
                    splitLines(raw, lines);
                }
            }
        }

        if (!fileExists && c == 2) {
            std::cout << "  [ERROR] File does not exist.\n";
            pause();
            continue;
        }

        // ------------------------------------------------------------
        // New file — ask whether to add a password
        // ------------------------------------------------------------
        if (!fileExists && c == 1) {
            std::string ans = getAdvancedInput("  Add a password? [y/n]: ");
            bool wantPw = !ans.empty() && (ans[0] == 'y' || ans[0] == 'Y');
            if (wantPw) {
                std::cout << "  Enter Password:\n";
                password = getHiddenPassword();
                if (password.empty())
                    std::cout << "  [INFO] Empty password — proceeding without protection.\n";
            }
        }

        // ------------------------------------------------------------
        // VIEW ONLY (option 2)
        // ------------------------------------------------------------
        if (c == 2) {
            clearScreen();
            printSection("VIEW: " + path);
            if (lines.empty()) std::cout << "  (empty)\n";
            else for (const auto& l : lines) std::cout << "  " << l << "\n";
            pause();
            continue;
        }

        // ------------------------------------------------------------
        // EDIT MODE (option 1)
        // ------------------------------------------------------------
        if (lines.empty()) lines.push_back("");

        if (g_consoleHost) {
            // Embedded in the hub: in-place editor drawn into the tool pane
            bool save = hostEditNote(path, lines, password);

            if (save) {
                while (!lines.empty() && lines.back().empty()) lines.pop_back();
                std::string fullText;
                for (const auto& l : lines) {
                    fullText += l;
                    fullText.push_back('\n');
                }
                std::string outData;
                if (!password.empty()) {
                    outData = MAGIC;
                    outData.push_back('\n');
                    std::string inner = MAGIC;
                    inner.push_back('\n');
                    inner += fullText;
                    outData += xorCrypt(inner, password);
                } else {
                    outData = fullText;
                }

                std::ofstream out(path, std::ios::binary);
                if (!out.is_open()) {
                    std::cout << "  [ERROR] Cannot write to path.\n";
                } else {
                    out.write(outData.data(), outData.size());
                    if (!out.good()) {
                        std::cout << "  [ERROR] Write failed.\n";
                    } else {
                        std::cout << "  [SUCCESS] Saved "
                                  << (password.empty() ? "(plain text)" : "(XOR-obfuscated)")
                                  << ".  " << lines.size() << " line(s).\n";
                    }
                    out.close();
                }
            } else {
                std::cout << "  [INFO] Aborted without saving.\n";
            }
            pause();
            continue;
        }

#ifdef _WIN32
        // ---- In-place multi-line editor (Win32) --------------------
        clearScreen();
        printSection("NOTE EDITOR  --  " + path);
        std::cout << "  [Up/Down] Move between lines   [Enter] Split line / new line\n";
        std::cout << "  [Backspace at col 0] Join with previous   [Del] Join next\n";
        std::cout << "  Commands on their own line:\n";
        std::cout << "    :wq   Save & Exit           :q    Quit\n";
        std::cout << "    :d    Delete current line   :pass Set / change / clear password\n";
        std::cout << "  ----------------------------------------------------------\n\n";
        std::cout.flush();

        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);

        CONSOLE_SCREEN_BUFFER_INFO csbi{};
        if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
            std::cout << "  [ERROR] No console available.\n";
            pause();
            continue;
        }
        const SHORT consoleWidth = csbi.dwSize.X;
        const SHORT bufferTopY   = csbi.dwCursorPosition.Y;
        const SHORT PREFIX_W     = 7;

        auto encodeUtf8 = [](wchar_t wc) -> std::string {
            std::string out;
            if (wc < 0x80) {
                out += (char)wc;
            } else if (wc < 0x800) {
                out += (char)(0xC0 | (wc >> 6));
                out += (char)(0x80 | (wc & 0x3F));
            } else {
                out += (char)(0xE0 | (wc >> 12));
                out += (char)(0x80 | ((wc >> 6) & 0x3F));
                out += (char)(0x80 | (wc & 0x3F));
            }
            return out;
        };

        auto drawLine = [&](size_t i, bool isCurrent) {
            if (i >= lines.size()) return;
            std::string numStr = std::to_string(i + 1);
            while (numStr.size() < 3) numStr = " " + numStr;

            std::string full = (isCurrent ? "> " : "  ") + numStr + "  " + lines[i];
            std::wstring wfull = utf8_to_wstring(full);
            if ((SHORT)wfull.size() < consoleWidth)
                wfull.append(consoleWidth - wfull.size(), L' ');
            else
                wfull.resize(consoleWidth);

            SetConsoleCursorPosition(hOut,
                { 0, (SHORT)(bufferTopY + (SHORT)i) });
            DWORD written = 0;
            WriteConsoleW(hOut, wfull.c_str(),
                          (DWORD)wfull.size(), &written, nullptr);
        };

        auto eraseSlot = [&](size_t i) {
            std::wstring blank(consoleWidth, L' ');
            SetConsoleCursorPosition(hOut,
                { 0, (SHORT)(bufferTopY + (SHORT)i) });
            DWORD written = 0;
            WriteConsoleW(hOut, blank.c_str(),
                          (DWORD)blank.size(), &written, nullptr);
        };

        auto placeCursor = [&](size_t lineIdx, size_t col) {
            SetConsoleCursorPosition(hOut,
                { (SHORT)(PREFIX_W + (SHORT)col),
                  (SHORT)(bufferTopY + (SHORT)lineIdx) });
        };

        drawLine(0, true);

        size_t cur       = 0;
        size_t cursorCol = 0;
        bool   editing   = true;
        bool   save      = false;

        DWORD oldMode = 0;
        GetConsoleMode(hIn, &oldMode);
        SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
        FlushConsoleInputBuffer(hIn);

        placeCursor(cur, cursorCol);

        while (editing) {
            INPUT_RECORD ir;
            DWORD read = 0;
            if (!ReadConsoleInputW(hIn, &ir, 1, &read)) continue;
            if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

            WORD  vk = ir.Event.KeyEvent.wVirtualKeyCode;
            WCHAR uc = ir.Event.KeyEvent.uChar.UnicodeChar;

            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) continue;

            // ---- Up ----
            if (vk == VK_UP) {
                if (cur > 0) {
                    drawLine(cur, false);
                    cur--;
                    if (cursorCol > lines[cur].size()) cursorCol = lines[cur].size();
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                }
                continue;
            }
            // ---- Down ----
            if (vk == VK_DOWN) {
                if (cur + 1 < lines.size()) {
                    drawLine(cur, false);
                    cur++;
                    if (cursorCol > lines[cur].size()) cursorCol = lines[cur].size();
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                }
                continue;
            }
            // ---- Escape: abort ----
            if (vk == VK_ESCAPE) {
                editing = false;
                SetConsoleCursorPosition(hOut,
                    { 0, (SHORT)(bufferTopY + (SHORT)lines.size() + 1) });
                break;
            }
            // ---- Enter / command dispatch ----
            if (vk == VK_RETURN) {
                if (lines[cur] == ":wq") { save = true; editing = false; break; }
                if (lines[cur] == ":q")  { editing = false; break; }

                // :pass — prompt for a new password (or clear it)
                if (lines[cur] == ":pass") {
                    // Park cursor below the buffer so any output is visible
                    SetConsoleCursorPosition(hOut,
                        { 0, (SHORT)(bufferTopY + (SHORT)lines.size() + 2) });

                    // Restore CRT console mode so getHiddenPassword's _getch() works
                    SetConsoleMode(hIn, oldMode);

                    std::cout << "\n  --- Password setup ---\n";
                    std::cout << "  Enter new password (empty = clear password):\n";
                    std::cout.flush();
                    std::string newPass = getHiddenPassword();

                    if (!newPass.empty()) {
                        password = newPass;
                        std::cout << "  [OK] Password set. Save with :wq to write encrypted.\n";
                    } else {
                        password.clear();
                        std::cout << "  [INFO] Password cleared. Save with :wq to write plain text.\n";
                    }
                    std::cout.flush();

                    // Re-enter raw mode for the editor
                    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
                    FlushConsoleInputBuffer(hIn);

                    // Clear the ":pass" text from the current line
                    lines[cur].clear();
                    cursorCol = 0;
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                    continue;
                }

                // :d — delete current line
                if (lines[cur] == ":d") {
                    if (lines.size() > 1) {
                        drawLine(cur, false);
                        lines.erase(lines.begin() + cur);
                        if (cur >= lines.size()) cur = lines.size() - 1;
                        cursorCol = 0;
                        for (size_t i = cur; i < lines.size(); ++i)
                            drawLine(i, false);
                        eraseSlot(lines.size());
                        drawLine(cur, true);
                        placeCursor(cur, cursorCol);
                    } else {
                        lines[0].clear();
                        cursorCol = 0;
                        drawLine(0, true);
                        placeCursor(0, 0);
                    }
                    continue;
                }

                // Regular Enter: split current line at cursor
                std::string tail = lines[cur].substr(cursorCol);
                lines[cur] = lines[cur].substr(0, cursorCol);
                lines.insert(lines.begin() + cur + 1, tail);

                drawLine(cur, false);
                for (size_t i = cur + 1; i < lines.size(); ++i)
                    drawLine(i, false);

                cur++;
                cursorCol = 0;
                drawLine(cur, true);
                placeCursor(cur, cursorCol);
                continue;
            }
            // ---- Left / Right ----
            if (vk == VK_LEFT) {
                if (cursorCol > 0) {
                    cursorCol--;
                    placeCursor(cur, cursorCol);
                } else if (cur > 0) {
                    cur--;
                    cursorCol = lines[cur].size();
                    drawLine(cur + 1, false);
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                }
                continue;
            }
            if (vk == VK_RIGHT) {
                if (cursorCol < lines[cur].size()) {
                    cursorCol++;
                    placeCursor(cur, cursorCol);
                } else if (cur + 1 < lines.size()) {
                    cur++;
                    cursorCol = 0;
                    drawLine(cur - 1, false);
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                }
                continue;
            }
            // ---- Home / End ----
            if (vk == VK_HOME) {
                cursorCol = 0;
                placeCursor(cur, 0);
                continue;
            }
            if (vk == VK_END) {
                cursorCol = lines[cur].size();
                placeCursor(cur, cursorCol);
                continue;
            }
            // ---- Backspace ----
            if (vk == VK_BACK) {
                if (cursorCol > 0) {
                    lines[cur].erase(cursorCol - 1, 1);
                    cursorCol--;
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                } else if (cur > 0) {
                    cursorCol = lines[cur - 1].size();
                    lines[cur - 1] += lines[cur];
                    lines.erase(lines.begin() + cur);
                    cur--;
                    for (size_t i = cur; i < lines.size(); ++i)
                        drawLine(i, false);
                    eraseSlot(lines.size());
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                }
                continue;
            }
            // ---- Delete ----
            if (vk == VK_DELETE) {
                if (cursorCol < lines[cur].size()) {
                    lines[cur].erase(cursorCol, 1);
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                } else if (cur + 1 < lines.size()) {
                    lines[cur] += lines[cur + 1];
                    lines.erase(lines.begin() + cur + 1);
                    for (size_t i = cur; i < lines.size(); ++i)
                        drawLine(i, false);
                    eraseSlot(lines.size());
                    drawLine(cur, true);
                    placeCursor(cur, cursorCol);
                }
                continue;
            }
            // ---- Printable character (BMP) ----
            if (uc >= 32 && !(uc >= 0xD800 && uc <= 0xDFFF)) {
                std::string enc = encodeUtf8((wchar_t)uc);
                lines[cur].insert(cursorCol, enc);
                cursorCol += enc.size();
                drawLine(cur, true);
                placeCursor(cur, cursorCol);
            }
        }

        SetConsoleMode(hIn, oldMode);

        // Park cursor below the buffer
        SetConsoleCursorPosition(hOut,
            { 0, (SHORT)(bufferTopY + (SHORT)lines.size() + 1) });
        std::cout << "\n";

#else
        // ---- Non-Windows fallback (line-by-line, no in-place) ----
        clearScreen();
        printSection("NOTE EDITOR  --  " + path);
        std::cout << "  Commands: :wq | :q | :d | :pass\n\n";
        std::cout << "  Current content (" << lines.size() << " line(s)):\n";
        for (size_t i = 0; i < lines.size(); ++i)
            std::cout << "  " << std::setw(3) << (i + 1) << "  " << lines[i] << "\n";
        std::cout << "\n";

        bool editing = true, save = false;
        while (editing) {
            std::string line = getAdvancedInput("  > ");
            if (line == ":wq") { save = true; editing = false; }
            else if (line == ":q")  { editing = false; }
            else if (line == ":pass") {
                std::cout << "  Enter new password (empty = clear):\n";
                std::string np = getHiddenPassword();
                if (!np.empty()) { password = np; std::cout << "  [OK] Password set.\n"; }
                else             { password.clear(); std::cout << "  [INFO] Password cleared.\n"; }
            }
            else if (line == ":d") {
                if (!lines.empty()) { lines.pop_back(); std::cout << "  [DELETED] last line\n"; }
            }
            else lines.push_back(line);
        }
#endif

        // ------------------------------------------------------------
        // SAVE
        // ------------------------------------------------------------
        if (save) {
            // Trim trailing blank lines the user left behind
            while (!lines.empty() && lines.back().empty()) lines.pop_back();

            std::string fullText;
            for (const auto& l : lines) {
                fullText += l;
                fullText.push_back('\n');
            }

            std::string outData;
            if (!password.empty()) {
                // MAGIC is written in PLAINTEXT first so the loader can
                // reliably recognise encrypted files. The XOR-encrypted
                // body begins with a second MAGIC that acts as a password
                // check.
                outData = MAGIC;
                outData.push_back('\n');

                std::string inner = MAGIC;
                inner.push_back('\n');
                inner += fullText;
                outData += xorCrypt(inner, password);
            } else {
                outData = fullText;   // raw plain text, no scrambling
            }

            std::ofstream out(path, std::ios::binary);
            if (!out.is_open()) {
                std::cout << "  [ERROR] Cannot write to path.\n";
            } else {
                out.write(outData.data(), outData.size());
                if (!out.good()) {
                    std::cout << "  [ERROR] Write failed.\n";
                } else {
                    std::cout << "  [SUCCESS] Saved "
                              << (password.empty() ? "(plain text)"
                                                   : "(XOR-obfuscated)")
                              << ".  " << lines.size() << " line(s).\n";
                }
                out.close();
            }
        } else {
            std::cout << "  [INFO] Aborted without saving.\n";
        }
        pause();
    }
}

void PasswordGeneratorPr() {
    while (true) {
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

        std::string again = getAdvancedInput("  Generate another? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

// ==========================================
// COLOR CONVERTER
// ==========================================
static void hslToRgb(double h, double s, double l, int& r, int& g, int& b) {
    auto hue2rgb = [](double p, double q, double t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0/6) return p + (q - p) * 6 * t;
        if (t < 1.0/2) return q;
        if (t < 2.0/3) return p + (q - p) * (2.0/3 - t) * 6;
        return p;
    };
    double rf, gf, bf;
    if (s == 0) { rf = gf = bf = l; }
    else {
        double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        double p = 2 * l - q;
        rf = hue2rgb(p, q, h / 360.0 + 1.0/3);
        gf = hue2rgb(p, q, h / 360.0);
        bf = hue2rgb(p, q, h / 360.0 - 1.0/3);
    }
    r = (int)(rf * 255 + 0.5);
    g = (int)(gf * 255 + 0.5);
    b = (int)(bf * 255 + 0.5);
}

static void rgbToHsl(int r, int g, int b, double& h, double& s, double& l) {
    double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    double mx = std::max({rf, gf, bf}), mn = std::min({rf, gf, bf});
    l = (mx + mn) / 2.0;
    if (mx == mn) { h = s = 0; return; }
    double d = mx - mn;
    s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    if      (mx == rf) h = (gf - bf) / d + (gf < bf ? 6 : 0);
    else if (mx == gf) h = (bf - rf) / d + 2;
    else               h = (rf - gf) / d + 4;
    h *= 60;
}

void ColorConverterPr() {
    while (true) {
        clearScreen();
        printSection("COLOR CONVERTER");

        std::cout << "  Input format:\n";
        std::cout << "  [1] HEX (#RRGGBB or #RGB)\n";
        std::cout << "  [2] RGB (e.g. 255,128,0)\n";
        std::cout << "  [3] HSL (e.g. 30,100,50)\n\n";

        std::string mode = getAdvancedInput("  Choice [1-3]: ");
        int r = 0, g = 0, b = 0;
        double h = 0, s = 0, l = 0;
        bool good = true;

        if (mode == "1") {
            std::string hex = getAdvancedInput("  HEX: ");
            if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            bool ok = true;
            for (char c : hex) if (hexVal(c) < 0) { ok = false; break; }
            if (hex.size() == 3 && ok) {
                r = hexVal(hex[0]) * 17; g = hexVal(hex[1]) * 17; b = hexVal(hex[2]) * 17;
            } else if (hex.size() == 6 && ok) {
                r = hexVal(hex[0])*16 + hexVal(hex[1]);
                g = hexVal(hex[2])*16 + hexVal(hex[3]);
                b = hexVal(hex[4])*16 + hexVal(hex[5]);
            } else {
                std::cout << "  [ERROR] Invalid HEX.\n"; good = false;
            }
            if (good) rgbToHsl(r, g, b, h, s, l);
        }
        else if (mode == "2") {
            std::string raw = getAdvancedInput("  RGB: ");
            for (char& c : raw) if (c == ',' || c == ';') c = ' ';
            std::stringstream ss(raw);
            int rr, gg, bb;
            if (!(ss >> rr >> gg >> bb) || rr < 0 || rr > 255 || gg < 0 || gg > 255 || bb < 0 || bb > 255) {
                std::cout << "  [ERROR] RGB must be three values 0-255.\n"; good = false;
            } else {
                r = rr; g = gg; b = bb;
                rgbToHsl(r, g, b, h, s, l);
            }
        }
        else if (mode == "3") {
            std::string raw = getAdvancedInput("  HSL (H 0-360, S 0-100, L 0-100): ");
            for (char& c : raw) if (c == ',' || c == ';') c = ' ';
            std::stringstream ss(raw);
            double hh, ssn, ll;
            if (!(ss >> hh >> ssn >> ll) || hh < 0 || hh > 360 || ssn < 0 || ssn > 100 || ll < 0 || ll > 100) {
                std::cout << "  [ERROR] HSL out of range.\n"; good = false;
            } else {
                h = hh; s = ssn / 100.0; l = ll / 100.0;
                hslToRgb(h, s, l, r, g, b);
                rgbToHsl(r, g, b, h, s, l);
            }
        }
        else {
            std::cout << "  [ERROR] Invalid choice.\n"; good = false;
        }

        if (good) {
            std::cout << "\n";
            printLine();
            std::cout << "  HEX  : #"
                      << std::uppercase << std::hex << std::setfill('0')
                      << std::setw(2) << r << std::setw(2) << g << std::setw(2) << b
                      << std::dec << std::nouppercase << std::setfill(' ') << "\n";
            std::cout << "  RGB  : " << r << ", " << g << ", " << b << "\n";
            {
                auto of = std::cout.flags();
                auto op = std::cout.precision();
                std::cout << std::fixed << std::setprecision(1);
                std::cout << "  HSL  : " << h << "°, " << (s * 100) << "%, " << (l * 100) << "%\n";
                std::cout.flags(of);
                std::cout.precision(op);    
            }
#ifdef _WIN32
            std::cout << "\nSwatch: \x1b[48;2;" << r << ";" << g << ";" << b
                      << "m        \x1b[0m\n";
#endif
            printLine();
        }
        pause();

        std::string again = getAdvancedInput("  Convert another? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

// ==========================================
// PRIME / FACTOR TOOLS
// ==========================================
// Refuse inputs whose trial division would take more than ~10^7 iterations.
static constexpr long long kPrimeToolLimit = 100000000000000LL;  // 10^15

static bool isPrimeLL(long long n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if (n % 2 == 0) return false;
    if (n > kPrimeToolLimit) return false;   // caller must warn before calling
    for (long long i = 3; i * i <= n; i += 2)
        if (n % i == 0) return false;
    return true;
}

static std::vector<long long> factorizeLL(long long n) {
    std::vector<long long> f;
    if (n < 2) return f;
    if (n > kPrimeToolLimit) return f;       // caller must warn before calling
    while (n % 2 == 0) { f.push_back(2); n /= 2; }
    for (long long i = 3; i * i <= n; i += 2)
        while (n % i == 0) { f.push_back(i); n /= i; }
    if (n > 1) f.push_back(n);
    return f;
}

void PrimeToolsPr() {
    bool run = true;
    while (run) {
        clearScreen();
        printSection("PRIME / FACTOR TOOLS");
        std::cout << "  [1] Check primality\n";
        std::cout << "  [2] Factorise N\n";
        std::cout << "  [3] Next prime after N\n";
        std::cout << "  [4] List primes up to N\n";
        std::cout << "  [5] Exit\n\n";

        std::string cRaw = getAdvancedInput("  Choice: ");
        if (cRaw.empty()) continue;
        int c = -1;
        
        try { c = std::stoi(cRaw); } catch (...) { std::cout << "  [ERROR] Invalid.\n"; pause(); continue; }
        if (c == 5) { run = false; continue; }
        if (c < 1 || c > 4) { std::cout << "  [ERROR] Invalid.\n"; pause(); continue; }

        
        std::string nRaw = getAdvancedInput("  N (>= 0, fits in 64-bit): ");
        long long n = -1;
        try {
            size_t pos = 0;
            n = std::stoll(nRaw, &pos);
            if (pos != nRaw.size() || n < 0) { std::cout << "  [ERROR] Invalid.\n"; pause(); continue; }
        } catch (...) { std::cout << "  [ERROR] Invalid.\n"; pause(); continue; }

        if (c == 1) {
            std::cout << "\n  " << n << (isPrimeLL(n) ? " IS" : " is NOT") << " prime.\n";
        }
        else if (c == 2) {
            if (n < 2) { std::cout << "\n  No prime factors (N < 2).\n"; }
            else {
                auto f = factorizeLL(n);
                std::cout << "\n  Prime factorisation of " << n << ":\n    ";
                for (size_t i = 0; i < f.size(); ++i) {
                    if (i) std::cout << " × ";
                    std::cout << f[i];
                }
                std::cout << "\n\n  Distinct primes: ";
                std::vector<long long> uniq;
                for (auto v : f) if (std::find(uniq.begin(), uniq.end(), v) == uniq.end()) uniq.push_back(v);
                for (size_t i = 0; i < uniq.size(); ++i) { if (i) std::cout << ", "; std::cout << uniq[i]; }
                std::cout << "\n  Total prime factors (with multiplicity): " << f.size() << "\n";
            }
        }
        else if (c == 3) {
            if (n < 2) { std::cout << "\n  Next prime after " << n << ": 2\n"; }
            else {
                long long p = n + 1;
                if (p % 2 == 0 && p != 2) p++;
                while (!isPrimeLL(p)) {
                    p += 2;
                    if (p < 0) { std::cout << "  [ERROR] Overflow.\n"; pause(); goto nextIter; }
                }
                std::cout << "\n  Next prime after " << n << ": " << p << "\n";
            }
        }
        else { // c == 4
            if (n < 2) { std::cout << "\n  No primes ≤ " << n << ".\n"; }
            else if (n > 1000000) {
                std::cout << "\n  [ERROR] Refusing to sieve above 1,000,000.\n";
            }
            else {
                std::vector<bool> sieve((size_t)n + 1, true);
                sieve[0] = sieve[1] = false;
                for (long long i = 2; i * i <= n; ++i)
                    if (sieve[i])
                        for (long long j = i * i; j <= n; j += i) sieve[j] = false;
                std::vector<long long> primes;
                for (long long i = 2; i <= n; ++i) if (sieve[i]) primes.push_back(i);
                std::cout << "\n  " << primes.size() << " prime(s) ≤ " << n << ":\n    ";
                for (size_t i = 0; i < primes.size(); ++i) {
                    if (i && i % 15 == 0) std::cout << "\n    ";
                    else if (i) std::cout << ", ";
                    std::cout << primes[i];
                }
                std::cout << "\n";
            }
        }
        nextIter:
        pause();
    }
}

// ==========================================
// ROMAN NUMERAL CONVERTER
// ==========================================
static std::string intToRoman(int n) {
    if (n <= 0 || n > 3999) return "";
    static const std::pair<int, const char*> table[] = {
        {1000,"M"},{900,"CM"},{500,"D"},{400,"CD"},{100,"C"},{90,"XC"},
        {50,"L"},{40,"XL"},{10,"X"},{9,"IX"},{5,"V"},{4,"IV"},{1,"I"}
    };
    std::string s;
    for (auto& [v, sym] : table) while (n >= v) { s += sym; n -= v; }
    return s;
}

static int romanToInt(const std::string& in) {
    if (in.empty()) return -1;
    std::string u;
    for (char c : in) u += (char)std::toupper((unsigned char)c);
    auto val = [](char c) -> int {
        switch (c) {
            case 'I': return 1;
            case 'V': return 5;
            case 'X': return 10;
            case 'L': return 50;
            case 'C': return 100;
            case 'D': return 500;
            case 'M': return 1000;
            default: return -1;
        }
    };
    int total = 0, prev = 0;
    for (int i = (int)u.size() - 1; i >= 0; --i) {
        int v = val(u[i]);
        if (v < 0) return -1;
        if (v < prev) total -= v;
        else { total += v; prev = v; }
    }
    if (intToRoman(total) != u) return -1;   // round-trip validation
    return total;
}

void RomanNumeralPr() {
    bool run = true;
    while (run) {
        clearScreen();
        printSection("ROMAN NUMERAL CONVERTER");
        std::cout << "  [1] Integer -> Roman (1-3999)\n";
        std::cout << "  [2] Roman -> Integer\n";
        std::cout << "  [3] Exit\n\n";

        std::string cRaw = getAdvancedInput("  Choice: ");
        if (cRaw.empty()) continue;
        int c = -1;
        try { c = std::stoi(cRaw); } catch (...) { std::cout << "  [ERROR] Invalid.\n"; pause(); continue; }
        if (c == 3) { run = false; continue; }

        if (c == 1) {
            std::string nRaw = getAdvancedInput("  Integer [1-3999]: ");
            int n = -1;
            try { n = std::stoi(nRaw); } catch (...) {}
            if (n < 1 || n > 3999) { std::cout << "  [ERROR] Out of range.\n"; pause(); continue; }
            std::cout << "\n  " << n << "  =  " << intToRoman(n) << "\n";
        }
        else if (c == 2) {
            std::string r = getAdvancedInput("  Roman numeral: ");
            int v = romanToInt(r);
            if (v < 0) { std::cout << "  [ERROR] Not a valid Roman numeral.\n"; pause(); continue; }
            std::cout << "\n  " << r << "  =  " << v << "\n";
        }
        else {
            std::cout << "  [ERROR] Invalid choice.\n";
        }
        pause();
    }
}

// ==========================================
// FILE HASHER
// ==========================================
static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static std::string crc32Hex(const std::string& data) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    crc ^= 0xFFFFFFFFu;
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08X", crc);
    return buf;
}

struct Sha256Ctx {
    uint32_t h[8];
    uint64_t bitLen;
    uint8_t  buf[64];
    size_t   bufLen;

    void init() {
        h[0]=0x6a09e667; h[1]=0xbb67ae85; h[2]=0x3c6ef372; h[3]=0xa54ff53a;
        h[4]=0x510e527f; h[5]=0x9b05688c; h[6]=0x1f83d9ab; h[7]=0x5be0cd19;
        bitLen = 0; bufLen = 0;
    }

    void block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16)
                 | ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    void update(const uint8_t* data, size_t len) {
        bitLen += (uint64_t)len * 8;
        while (len > 0) {
            size_t take = std::min(len, (size_t)64 - bufLen);
            std::memcpy(buf + bufLen, data, take);
            bufLen += take; data += take; len -= take;
            if (bufLen == 64) { block(buf); bufLen = 0; }
        }
    }

    std::string finalHex() {
        uint64_t bl = bitLen;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t z = 0;
        while (bufLen != 56) update(&z, 1);
        uint8_t lenBytes[8];
        for (int i = 7; i >= 0; --i) { lenBytes[i] = (uint8_t)(bl & 0xFF); bl >>= 8; }
        // bypass update() so bitLen bookkeeping doesn't matter anymore
        std::memcpy(buf + 56, lenBytes, 8);
        block(buf); bufLen = 0;

        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 8; ++i) ss << std::setw(8) << h[i];
        return ss.str();
    }
};

static std::string sha256Hex(const std::string& data) {
    Sha256Ctx c; c.init();
    c.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return c.finalHex();
}

// Streaming variant: hashes a file in 64 KiB chunks, never loads it whole.
static bool sha256FileStream(const std::filesystem::path& p, std::string& outHex) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return false;
    Sha256Ctx c; c.init();
    std::vector<char> buf(64 * 1024);
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize got = in.gcount();
        if (got > 0)
            c.update(reinterpret_cast<const uint8_t*>(buf.data()), (size_t)got);
    }
    outHex = c.finalHex();
    return true;
}

static bool crc32FileStream(const std::filesystem::path& p, std::string& outHex) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return false;

    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t cc = i;
            for (int j = 0; j < 8; ++j)
                cc = (cc & 1) ? (0xEDB88320u ^ (cc >> 1)) : (cc >> 1);
            table[i] = cc;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    std::vector<unsigned char> buf(64 * 1024);
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)buf.size());
        std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i)
            crc = table[(crc ^ buf[(size_t)i]) & 0xFF] ^ (crc >> 8);
    }
    crc ^= 0xFFFFFFFFu;
    char b[9];
    std::snprintf(b, sizeof(b), "%08X", crc);
    outHex = b;
    return true;
}

void FileHasherPr() {
    while (true) {
        clearScreen();
        printSection("FILE HASHER");

        std::string path = getAdvancedInput("  File path: ");
        if (path.empty()) { /* fall through to retry prompt */ }
        else {
            std::error_code ec;
            std::filesystem::path fp(path);
            if (!std::filesystem::is_regular_file(fp, ec)) {
                std::cout << "  [ERROR] Cannot open file.\n";
            } else {
                auto sz = std::filesystem::file_size(fp, ec);
                std::string crc, sha;
                if (!crc32FileStream(fp, crc) || !sha256FileStream(fp, sha)) {
                    std::cout << "  [ERROR] Cannot read file.\n";
                } else {
                    std::cout << "\n";
                    printLine();
                    std::cout << "  File size : " << (ec ? 0ULL : (unsigned long long)sz)
                              << " bytes\n";
                    std::cout << "\n  CRC32   : " << crc << "\n";
                    std::cout << "  SHA-256 : " << sha << "\n";
                    printLine();
                }
            }
        }
        pause();

        std::string again = getAdvancedInput("  Hash another? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

// ==========================================
// ENCODER / DECODER
// ==========================================
static std::string base64Encode(const std::string& in) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned v = ((unsigned char)in[i]     << 16) |
                     ((unsigned char)in[i + 1] <<  8) |
                      (unsigned char)in[i + 2];
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >>  6) & 0x3F];
        out += tbl[ v        & 0x3F];
        i += 3;
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        bool two = (i + 1 < in.size());
        if (two) v |= (unsigned char)in[i + 1] << 8;
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += two ? tbl[(v >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

static std::string base64Decode(const std::string& in) {
    static const std::string tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lookup[256];
    std::fill(std::begin(lookup), std::end(lookup), -1);
    for (int i = 0; i < 64; ++i) lookup[(unsigned char)tbl[i]] = i;

    std::string out;
    int buf = 0, bits = 0;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = lookup[c];
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((buf >> bits) & 0xFF);
        }
    }
    return out;
}

static std::string urlEncode(const std::string& in) {
    std::string out;
    char buf[4];
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static std::string urlDecode(const std::string& in) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int h = hex(in[i + 1]), l = hex(in[i + 2]);
            if (h >= 0 && l >= 0) {
                out += (char)(h * 16 + l);
                i += 2;
                continue;
            }
        }
        if (in[i] == '+') out += ' ';
        else              out += in[i];
    }
    return out;
}

static std::string hexEncode(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 2);
    char buf[4];
    for (unsigned char c : in) {
        std::snprintf(buf, sizeof(buf), "%02X", c);
        out += buf;
    }
    return out;
}

static bool hexDecode(const std::string& in, std::string& out) {
    std::string clean;
    for (char c : in) if (!std::isspace((unsigned char)c)) clean += c;
    if (clean.size() % 2 != 0) return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    out.reserve(clean.size() / 2);
    for (size_t i = 0; i + 1 < clean.size(); i += 2) {
        int h = hex(clean[i]), l = hex(clean[i + 1]);
        if (h < 0 || l < 0) return false;
        out += (char)(h * 16 + l);
    }
    return true;
}

static std::string caesarShift(const std::string& in, int shift) {
    std::string out = in;
    shift = ((shift % 26) + 26) % 26;
    for (char& c : out) {
        if      (c >= 'a' && c <= 'z') c = (char)('a' + (c - 'a' + shift) % 26);
        else if (c >= 'A' && c <= 'Z') c = (char)('A' + (c - 'A' + shift) % 26);
    }
    return out;
}

static void printBinarySafe(const std::string& s) {
    // Print characters, escaping non-printable bytes as \xNN.
    for (unsigned char c : s) {
        if (c >= 32 && c < 127) std::cout << (char)c;
        else if (c == '\t')      std::cout << "\\t";
        else if (c == '\n')      std::cout << "\\n";
        else if (c == '\r')      std::cout << "\\r";
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            std::cout << buf;
        }
    }
}

void EncoderDecoderPr() {
    bool run = true;
    while (run) {
        clearScreen();
        printSection("ENCODER / DECODER");
        std::cout << "  [1] Base64  encode\n";
        std::cout << "  [2] Base64  decode\n";
        std::cout << "  [3] URL     encode\n";
        std::cout << "  [4] URL     decode\n";
        std::cout << "  [5] Hex     encode\n";
        std::cout << "  [6] Hex     decode\n";
        std::cout << "  [7] ROT13 / Caesar shift\n";
        std::cout << "  [8] Exit\n\n";

        std::string cRaw = getAdvancedInput("  Choice: ");
        if (cRaw.empty()) continue;
        int c = -1;
        try { c = std::stoi(cRaw); } catch (...) {
            std::cout << "  [ERROR] Invalid.\n"; pause(); continue;
        }
        if (c == 8) { run = false; continue; }
        if (c < 1 || c > 7) {
            std::cout << "  [ERROR] Invalid choice.\n"; pause(); continue;
        }

        std::string in = getAdvancedInput("  Input: ");
        if (in.empty()) { std::cout << "  [ERROR] Empty input.\n"; pause(); continue; }

        std::cout << "\n";
        printLine();

        if (c == 1) {
            std::cout << "  Base64:  " << base64Encode(in) << "\n";
        }
        else if (c == 2) {
            std::string out = base64Decode(in);
            std::cout << "  Decoded: ";
            printBinarySafe(out);
            std::cout << "\n";
            std::cout << "  Bytes  : " << out.size() << "\n";
        }
        else if (c == 3) {
            std::cout << "  URL:     " << urlEncode(in) << "\n";
        }
        else if (c == 4) {
            std::cout << "  Decoded: " << urlDecode(in) << "\n";
        }
        else if (c == 5) {
            std::cout << "  Hex:     " << hexEncode(in) << "\n";
        }
        else if (c == 6) {
            std::string out;
            if (!hexDecode(in, out)) {
                std::cout << "  [ERROR] Invalid hex (odd length or non-hex chars).\n";
            } else {
                std::cout << "  Decoded: ";
                printBinarySafe(out);
                std::cout << "\n";
                std::cout << "  Bytes  : " << out.size() << "\n";
            }
        }
        else { // c == 7
            std::string shiftRaw = getAdvancedInput("  Shift amount (13 = ROT13): ");
            int shift = 13;
            try { shift = std::stoi(shiftRaw); } catch (...) {}
            std::cout << "  Shifted: " << caesarShift(in, shift) << "\n";
        }

        printLine();
        pause();
    }
}

// ==========================================
// RANDOM PICKER
// ==========================================
void RandomPickerPr() {
    bool run = true;
    std::mt19937_64 rng(std::random_device{}());

    while (run) {
        clearScreen();
        printSection("RANDOM PICKER");
        std::cout << "  [1] Coin flip\n";
        std::cout << "  [2] Roll dice (custom sides, custom count)\n";
        std::cout << "  [3] Pick from list (comma-separated)\n";
        std::cout << "  [4] Exit\n\n";

        std::string cRaw = getAdvancedInput("  Choice: ");
        if (cRaw.empty()) continue;
        int c = -1;
        try { c = std::stoi(cRaw); } catch (...) {
            std::cout << "  [ERROR] Invalid.\n"; pause(); continue;
        }
        if (c == 4) { run = false; continue; }
        if (c < 1 || c > 3) {
            std::cout << "  [ERROR] Invalid choice.\n"; pause(); continue;
        }

        if (c == 1) {
            std::uniform_int_distribution<int> d(0, 1);
            std::cout << "\n  Result: " << (d(rng) ? "HEADS" : "TAILS") << "\n";
        }
        else if (c == 2) {
            std::string sidesRaw = getAdvancedInput("  Sides per die [default 6]: ");
            std::string countRaw = getAdvancedInput("  Number of dice [default 1]: ");
            int sides = 6, count = 1;
            try { if (!sidesRaw.empty()) sides = std::stoi(sidesRaw); } catch (...) {}
            try { if (!countRaw.empty()) count = std::stoi(countRaw); } catch (...) {}
            if (sides < 2 || sides > 1000000 || count < 1 || count > 100) {
                std::cout << "  [ERROR] Sides 2..1,000,000, count 1..100.\n";
                pause(); continue;
            }
            std::uniform_int_distribution<int> d(1, sides);
            long long total = 0;
            std::cout << "\n  Rolls: ";
            for (int i = 0; i < count; ++i) {
                int r = d(rng);
                total += r;
                if (i) std::cout << " + ";
                std::cout << r;
            }
            if (count > 1) std::cout << "  =  " << total;
            std::cout << "\n";
        }
        else { // c == 3
            std::string list = getAdvancedInput("  List (comma-separated): ");
            std::vector<std::string> items;
            size_t start = 0;
            while (start <= list.size()) {
                size_t comma = list.find(',', start);
                std::string token;
                if (comma == std::string::npos) {
                    token = list.substr(start);
                    start = list.size() + 1;
                } else {
                    token = list.substr(start, comma - start);
                    start = comma + 1;
                }
                // trim
                size_t a = 0, b = token.size();
                while (a < b && std::isspace((unsigned char)token[a])) ++a;
                while (b > a && std::isspace((unsigned char)token[b - 1])) --b;
                if (b > a) items.push_back(token.substr(a, b - a));
                if (comma == std::string::npos) break;
            }
            if (items.empty()) {
                std::cout << "  [ERROR] No items to pick from.\n";
                pause(); continue;
            }
            std::uniform_int_distribution<size_t> d(0, items.size() - 1);
            size_t chosen = d(rng);
            std::cout << "\n  Items (" << items.size() << "): ";
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << items[i];
            }
            std::cout << "\n\n  Picked: " << items[chosen] << "\n";
        }

        pause();
    }
}

// ==========================================
// SECURE FILE MOVER
// ==========================================
namespace fs = std::filesystem;

static std::string sha256File(const fs::path& p) {
    std::string hex;
    return sha256FileStream(p, hex) ? hex : std::string();
}

// Simple wildcard matcher: '*' = any run, '?' = any one char.
// Case-insensitive on Windows, case-sensitive elsewhere.
static bool wildcardMatch(const std::string& name, const std::string& pattern) {
    auto lower = [](char c) -> char {
#ifdef _WIN32
        return (char)std::tolower((unsigned char)c);
#else
        return c;
#endif
    };

    size_t ni = 0, pi = 0;
    size_t star = std::string::npos, matchStart = 0;

    while (ni < name.size()) {
        if (pi < pattern.size() &&
            (pattern[pi] == '?' || lower(pattern[pi]) == lower(name[ni]))) {
            pi++; ni++;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            matchStart = ni;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ni = ++matchStart;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') pi++;
    return pi == pattern.size();
}

// Error-tolerant recursive walk.  Never throws; skips anything the OS
// refuses to stat — reparse points, $RECYCLE.BIN, protected dirs, etc.
struct WalkResult {
    std::vector<fs::path> files;
    std::vector<fs::path> dirs;
};

static void walkRecursive(const fs::path& dir,
                          const std::string& filter,
                          WalkResult& out) {
    std::error_code ec;
    fs::directory_iterator it(dir,
        fs::directory_options::skip_permission_denied, ec);
    if (ec) return;                       // can't open this dir — skip

    const fs::directory_iterator end;
    while (it != end) {
        // Snapshot the entry, THEN advance.  If advancing fails,
        // we still have a valid entry to inspect.
        fs::directory_entry entry = *it;
        it.increment(ec);
        if (ec) break;                    // stop this level, don't throw

        std::error_code typeEc;
        bool isDir = entry.is_directory(typeEc);

        if (isDir) {
            // Skip reparse points — symlinks, junctions, mount points —
            // because they cause loops and access errors.
            std::error_code slEc;
            if (entry.is_symlink(slEc)) continue;

            out.dirs.push_back(entry.path());
            walkRecursive(entry.path(), filter, out);
        } else {
            std::error_code fileEc;
            if (!entry.is_regular_file(fileEc) || fileEc) continue;
            if (wildcardMatch(entry.path().filename().string(), filter))
                out.files.push_back(entry.path());
        }
    }
}

static std::string sanitizePathInput(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
    if (s.size() >= 2 &&
        ((s.front() == '"'  && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\'')))
        s = s.substr(1, s.size() - 2);
    if (!s.empty() && s[0] == '~') {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) s = std::string(home) + s.substr(1);
    }
    return s;
}

void SecureFileMoverPr() {
    while (true) {
        clearScreen();
        printSection("SECURE FILE MOVER");
        std::cout << "  Recursive move or copy with SHA-256 verification.\n";
        std::cout << "  A file is only deleted from source after the copy verifies.\n\n";

        std::string srcStr = getAdvancedInput("  Source directory: ");
        if (srcStr.empty()) { pause(); return; }
        srcStr = sanitizePathInput(srcStr);


        std::error_code ec;
        fs::path src = fs::path(srcStr);
        if (!fs::exists(src, ec) || !fs::is_directory(src, ec)) {
            std::cout << "  [ERROR] Source directory not found.\n";
            pause();
            std::string again = getAdvancedInput("  Run again? [y/n]: ");
            if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) return;
            continue;
        }

        
        std::string dstStr = getAdvancedInput("  Destination directory: ");
        if (dstStr.empty()) { pause(); return; }
        dstStr = sanitizePathInput(dstStr);
        fs::path dst = fs::path(dstStr);

        std::string filterStr = getAdvancedInput("  Filename filter [default *]: ");
        if (filterStr.empty()) filterStr = "*";

        std::string incAns = getAdvancedInput("  Include source folder name in destination? [y/n]: ");
        bool includeSrc = !incAns.empty() && (incAns[0] == 'y' || incAns[0] == 'Y');

        std::string modeAns = getAdvancedInput("  Mode [m]ove or [c]opy: ");
        bool onlyCopy = !modeAns.empty() && (modeAns[0] == 'c' || modeAns[0] == 'C');

        // Determine the prefix to strip when computing relative paths
        fs::path stripBase = includeSrc ? src.parent_path() : src;
        std::string stripStr = stripBase.string();

        std::cout << "\n  Scanning...\n";

        // Error-tolerant walk — replaces the throwing recursive_directory_iterator
        WalkResult walk;
        walkRecursive(src, filterStr, walk);
        std::vector<fs::path>& files = walk.files;

        if (files.empty()) {
            std::cout << "  [INFO] No files matched.\n";
            pause();
            std::string again = getAdvancedInput("  Run again? [y/n]: ");
            if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) return;
            continue;
        }

        std::cout << "  Matched " << files.size() << " file(s).\n\n";

        int ok = 0, failed = 0;

        for (const auto& srcFile : files) {
            std::string srcFull = srcFile.string();
            if (srcFull.size() <= stripStr.size()) continue;

            std::string rel = srcFull.substr(stripStr.size());
            while (!rel.empty() && (rel[0] == '\\' || rel[0] == '/'))
                rel.erase(0, 1);
            if (rel.empty()) continue;

            fs::path dstFile = dst / rel;
            fs::path dstDir  = dstFile.parent_path();

            // Create destination directory (best-effort — never at drive root)
            std::string dirStr = dstDir.string();
            if (!(dirStr.size() == 3 && dirStr[1] == ':' &&
                  (dirStr[2] == '\\' || dirStr[2] == '/'))) {
                ec.clear();
                fs::create_directories(dstDir, ec);
            }

            // Copy (overwrite if present)
            ec.clear();
            fs::copy_file(srcFile, dstFile,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cout << "  [FAIL] Copy failed: " << rel
                          << "  (" << ec.message() << ")\n";
                failed++;
                continue;
            }

            // Verify: SHA-256 comparison, retry a few times in case
            // the OS hasn't flushed the write yet.
            std::string srcHash = sha256File(srcFile);
            bool verified = false;
            for (int attempt = 0; attempt < 3; ++attempt) {
                std::string dstHash = sha256File(dstFile);
                if (!srcHash.empty() && srcHash == dstHash) {
                    verified = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            if (!verified) {
                std::cout << "  [FAIL] Verification mismatch: " << rel << "\n";
                failed++;
                continue;
            }

            // Delete source only if moving
            if (!onlyCopy) {
                ec.clear();
                fs::remove(srcFile, ec);
                if (ec) {
                    std::cout << "  [WARN] Verified but could not delete source: "
                              << rel << "  (" << ec.message() << ")\n";
                } else {
                    std::cout << "  [OK] VERIFIED + MOVED: " << rel << "\n";
                }
            } else {
                std::cout << "  [OK] VERIFIED + COPIED: " << rel << "\n";
            }
            ok++;
        }

        // Clean up empty source directories (deepest-first) when moving.
        // Reuses the dir list from the walk — no second traversal.
        if (!onlyCopy) {
            std::vector<fs::path> dirs = walk.dirs;
            std::sort(dirs.begin(), dirs.end(),
                [](const fs::path& a, const fs::path& b) {
                    return std::distance(a.begin(), a.end()) >
                           std::distance(b.begin(), b.end());
                });

            int removed = 0;
            for (const auto& d : dirs) {
                ec.clear();
                if (fs::is_empty(d, ec) && !ec) {
                    fs::remove(d, ec);
                    if (!ec) removed++;
                }
            }
            if (removed > 0)
                std::cout << "  Cleaned up " << removed
                          << " empty source folder(s).\n";
        }

        std::cout << "\n";
        printLine();
        std::cout << "  Processed: " << (ok + failed) << "\n";
        std::cout << "  Success  : " << ok        << "\n";
        std::cout << "  Failed   : " << failed    << "\n";
        printLine();
        pause();

        std::string again = getAdvancedInput("  Run again? [y/n]: ");
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) break;
    }
}

// ==========================================
// RAM QUERY  (Windows-only)
// ==========================================
#ifdef _WIN32
#ifndef PROCESS_SUSPEND_RESUME
#define PROCESS_SUSPEND_RESUME 0x0800
#endif
#include <tlhelp32.h>

struct ProcInfo {
    DWORD       pid;
    DWORD       parentPid;
    std::string name;
    SIZE_T      ramBytes;
    ULONGLONG   cpuTicks;   // 100-ns units
};

static std::string stripExeLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".exe") == 0)
        s.resize(s.size() - 4);
    return s;
}

static std::string humanSize(SIZE_T bytes) {
    char buf[32];
    double mb = bytes / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        std::snprintf(buf, sizeof(buf), "%.2f GB", mb / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.2f MB", mb);
    return buf;
}

static std::string humanCPU(ULONGLONG hundredNs) {
    char buf[32];
    double sec = hundredNs / 1e7;
    if (sec >= 3600.0)      std::snprintf(buf, sizeof(buf), "%.2f h", sec / 3600.0);
    else if (sec >= 60.0)   std::snprintf(buf, sizeof(buf), "%.2f m", sec / 60.0);
    else                    std::snprintf(buf, sizeof(buf), "%.2f s", sec);
    return buf;
}

static std::vector<ProcInfo> enumerateProcs() {
    std::vector<ProcInfo> list;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return list;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) {
        CloseHandle(snap);
        return list;
    }

    do {
        ProcInfo pi{};
        pi.pid       = pe.th32ProcessID;
        pi.parentPid = pe.th32ParentProcessID;
        pi.name      = wstring_to_utf8(pe.szExeFile);
        pi.ramBytes  = 0;
        pi.cpuTicks  = 0;

        HANDLE h = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE, pi.pid);
        if (h) {
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                pi.ramBytes = pmc.WorkingSetSize;

            FILETIME ct, et, kt, ut;
            if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
                ULARGE_INTEGER k, u;
                k.LowPart  = kt.dwLowDateTime; k.HighPart = kt.dwHighDateTime;
                u.LowPart  = ut.dwLowDateTime; u.HighPart = ut.dwHighDateTime;
                pi.cpuTicks = k.QuadPart + u.QuadPart;
            }
            CloseHandle(h);
        }
        list.push_back(pi);
    } while (Process32NextW(snap, &pe));

    CloseHandle(snap);
    return list;
}

static void ramSystemMemoryMB(double& totalMB, double& usedMB, double& freeMB) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    totalMB = ms.ullTotalPhys / (1024.0 * 1024.0);
    freeMB  = ms.ullAvailPhys / (1024.0 * 1024.0);
    usedMB  = totalMB - freeMB;
}

static std::string ramUptime() {
    ULONGLONG sec = GetTickCount64() / 1000;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llud %lluh %llum",
                  sec / 86400, (sec % 86400) / 3600, (sec % 3600) / 60);
    return buf;
}

static double ramCpuLoad(int sampleMs = 250) {
    FILETIME i1, k1, u1, i2, k2, u2;
    if (!GetSystemTimes(&i1, &k1, &u1)) return 0.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(sampleMs));
    if (!GetSystemTimes(&i2, &k2, &u2)) return 0.0;

    auto u64 = [](const FILETIME& ft) -> ULONGLONG {
        ULARGE_INTEGER v;
        v.LowPart  = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return v.QuadPart;
    };

    ULONGLONG idle  = u64(i2) - u64(i1);
    ULONGLONG total = (u64(k2) - u64(k1)) + (u64(u2) - u64(u1));
    if (total == 0) return 0.0;

    double pct = 100.0 * (1.0 - (double)idle / (double)total);
    if (pct < 0.0)   pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}

static void ramPrintBar(double pct, int width) {
    int filled = (int)(pct / 100.0 * width + 0.5);
    if (filled < 0)     filled = 0;
    if (filled > width) filled = width;
    std::cout << "[";
    for (int i = 0; i < filled; ++i) std::cout << "#";
    for (int i = filled; i < width; ++i) std::cout << "-";
    std::cout << "]";
}

static void ramPrintTopN(const std::vector<ProcInfo>& list, int n) {
    std::vector<ProcInfo> sorted = list;
    std::sort(sorted.begin(), sorted.end(),
        [](const ProcInfo& a, const ProcInfo& b) {
            return a.ramBytes > b.ramBytes;
        });
    if ((int)sorted.size() > n) sorted.resize(n);

    double totalMB, usedMB, freeMB;
    ramSystemMemoryMB(totalMB, usedMB, freeMB);

    std::cout << "  " << std::left
              << std::setw(28) << "Process"
              << std::setw(8)  << "PID"
              << std::setw(12) << "RAM"
              << std::setw(10) << "RAM %"
              << std::setw(14) << "CPU time" << "\n";
    std::cout << "  " << std::string(72, '-') << "\n";

    for (const auto& p : sorted) {
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%.2f%%",
                      100.0 * (p.ramBytes / (1024.0 * 1024.0)) / totalMB);

        std::string name = p.name;
        if (name.size() > 26) name = name.substr(0, 24) + "..";

        std::cout << "  " << std::left
                  << std::setw(28) << name
                  << std::setw(8)  << p.pid
                  << std::setw(12) << humanSize(p.ramBytes)
                  << std::setw(10) << pct
                  << std::setw(14) << humanCPU(p.cpuTicks) << "\n";
    }
    std::cout << std::right;
}

static void ramPrintTreeRecursive(const std::vector<ProcInfo>& all,
                                   DWORD parent, int depth) {
    if (depth > 8) return;
    for (const auto& c : all) {
        if (c.parentPid != parent) continue;
        for (int i = 0; i < depth; ++i) std::cout << "     ";
        std::cout << "  |-- " << c.name
                  << " (PID " << c.pid << ")  "
                  << humanSize(c.ramBytes) << "\n";
        ramPrintTreeRecursive(all, c.pid, depth + 1);
    }
}

static bool ramSuspendResume(const std::string& name, bool suspend) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto fn = (LONG (NTAPI*)(HANDLE))GetProcAddress(ntdll,
        suspend ? "NtSuspendProcess" : "NtResumeProcess");
    if (!fn) return false;

    std::vector<ProcInfo> procs = enumerateProcs();
    std::string target = stripExeLower(name);
    bool any = false;

    for (const auto& p : procs) {
        if (stripExeLower(p.name) != target) continue;
        HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, p.pid);
        if (!h) h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, p.pid);
        if (!h) continue;
        fn(h);
        CloseHandle(h);
        any = true;
    }
    return any;
}

// Key input for RAM Query: the real console when running in one, or the GUI
// session's key queue when running embedded in the hub.
static bool ramKeyWaiting() {
    if (g_consoleHost) return g_consoleHost->keyWaiting();
    return _kbhit() != 0;
}

static int ramReadKey() {
    if (g_consoleHost) return g_consoleHost->readKey();
    return _getch();
}

void RAMQueryPr() {
    bool run = true;
    while (run) {
        clearScreen();
        printSection("RAM QUERY");
        std::cout << "   1. Top N processes by RAM\n";
        std::cout << "   2. System specifications\n";
        std::cout << "   3. Full health report\n";
        std::cout << "   4. Bloatware scan\n";
        std::cout << "   5. Danger scan (>30% RAM per process)\n";
        std::cout << "   6. Single top RAM consumer\n";
        std::cout << "   7. Process tree\n";
        std::cout << "   8. Pause a process\n";
        std::cout << "   9. Resume a process\n";
        std::cout << "  10. Kill a process\n";
        std::cout << "  11. Compact dock (CPU/RAM/Disk)\n";
        std::cout << "  12. Visual chart (top 5)\n";
        std::cout << "  13. Mini summary (top 5 quick table)\n";
        std::cout << "  14. Watch mode\n";
        std::cout << "   0. Exit to Hub\n\n";

        std::string cRaw = getAdvancedInput("  Choice: ");
        if (cRaw.empty()) continue;
        int c = -1;
        try { c = std::stoi(cRaw); } catch (...) {
            std::cout << "  [ERROR] Invalid.\n"; pause(); continue;
        }
        if (c == 0) { run = false; continue; }

        clearScreen();
        printSection("RAM QUERY");

        std::vector<ProcInfo> procs = enumerateProcs();

        if (c == 1) {
            std::string nRaw = getAdvancedInput("  How many? [default 10]: ");
            int n = 10;
            try { if (!nRaw.empty()) n = std::stoi(nRaw); } catch (...) {}
            if (n < 1) n = 1;
            if (n > 200) n = 200;

            double totalMB, usedMB, freeMB;
            ramSystemMemoryMB(totalMB, usedMB, freeMB);
            std::cout << "\n";
            printLine();
            std::cout << "  System RAM: " << (long long)totalMB << " MB total, "
                      << (long long)usedMB << " MB used, "
                      << (long long)freeMB << " MB free\n";
            printLine();
            std::cout << "\n";
            ramPrintTopN(procs, n);

            std::vector<ProcInfo> sorted = procs;
            std::sort(sorted.begin(), sorted.end(),
                [](const ProcInfo& a, const ProcInfo& b) {
                    return a.ramBytes > b.ramBytes;
                });
            if ((int)sorted.size() > n) sorted.resize(n);
            double topSum = 0;
            for (auto& p : sorted) topSum += p.ramBytes / (1024.0 * 1024.0);

            double pct = 100.0 * topSum / totalMB;
            char tail[64];
            std::snprintf(tail, sizeof(tail), "  %.1f%%", pct);
            std::cout << "\n  Top " << n << " combined: "
                      << (long long)topSum << " MB  ";
            ramPrintBar(pct, 20);
            std::cout << tail << "\n";
        }
        else if (c == 2) {
            double totalMB, usedMB, freeMB;
            ramSystemMemoryMB(totalMB, usedMB, freeMB);

            SYSTEM_INFO si{};
            GetNativeSystemInfo(&si);
            const char* arch = "Unknown";
            switch (si.wProcessorArchitecture) {
                case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64 (AMD64)"; break;
                case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86 (IA-32)"; break;
                case PROCESSOR_ARCHITECTURE_ARM64: arch = "ARM64";       break;
                case PROCESSOR_ARCHITECTURE_ARM:   arch = "ARM";         break;
                default: break;
            }

            ULONGLONG diskFreeB = 0, diskTotalB = 0;
            GetDiskFreeSpaceExW(L"C:\\",
                (PULARGE_INTEGER)&diskFreeB,
                (PULARGE_INTEGER)&diskTotalB, nullptr);

            printLine();
            std::cout << "  SYSTEM SPECIFICATIONS\n";
            printLine();
            std::cout << "  Architecture : " << arch << "\n";
            std::cout << "  Logical CPUs : " << si.dwNumberOfProcessors << "\n";
            std::cout << "  Page size    : " << si.dwPageSize << " bytes\n";
            std::cout << "  Total RAM    : " << (long long)totalMB << " MB\n";
            std::cout << "  Used RAM     : " << (long long)usedMB  << " MB\n";
            std::cout << "  Free RAM     : " << (long long)freeMB  << " MB\n";
            std::cout << "  C: Free      : "
                      << (diskFreeB  / (1024ULL * 1024 * 1024)) << " GB / "
                      << (diskTotalB / (1024ULL * 1024 * 1024)) << " GB\n";
            std::cout << "  Uptime       : " << ramUptime() << "\n";
            printLine();
        }
        else if (c == 3) {
            double totalMB, usedMB, freeMB;
            ramSystemMemoryMB(totalMB, usedMB, freeMB);
            double ramPct = 100.0 * usedMB / totalMB;
            double cpuPct = ramCpuLoad(250);

            ULONGLONG diskFreeB = 0, diskTotalB = 0;
            GetDiskFreeSpaceExW(L"C:\\",
                (PULARGE_INTEGER)&diskFreeB,
                (PULARGE_INTEGER)&diskTotalB, nullptr);
            double diskPct = (diskTotalB > 0)
                ? 100.0 * diskFreeB / (double)diskTotalB : 0.0;

            printLine();
            std::cout << "  SYSTEM HEALTH REPORT\n";
            printLine();
            std::cout << "  CPU   : ";
            ramPrintBar(cpuPct, 20);
            char line1[64];
            std::snprintf(line1, sizeof(line1), "  %d%%\n", (int)cpuPct);
            std::cout << line1;

            std::cout << "  RAM   : ";
            ramPrintBar(ramPct, 20);
            char line2[128];
            std::snprintf(line2, sizeof(line2), "  %.0f%% (%.0f / %.0f MB)\n",
                          ramPct, usedMB, totalMB);
            std::cout << line2;

            std::cout << "  Disk  : "
                      << (diskFreeB  / (1024ULL * 1024 * 1024)) << " GB free / "
                      << (diskTotalB / (1024ULL * 1024 * 1024)) << " GB  ("
                      << (int)diskPct << "% free)\n";
            std::cout << "  Uptime: " << ramUptime() << "\n";
            printLine();

            std::cout << "\n  Top 3 RAM consumers:\n";
            ramPrintTopN(procs, 3);
        }
        else if (c == 4) {
            static const char* bloat[] = {
                "chrome", "firefox", "msedge", "opera", "brave",
                "discord", "slack", "teams", "zoom", "spotify",
                "obs64", "obs32", "code", "devenv", "photoshop",
                "afterfx", "premiere", "illustrator", "blender"
            };
            std::vector<ProcInfo> found;
            for (const auto& p : procs) {
                std::string base = stripExeLower(p.name);
                for (const char* b : bloat) {
                    if (base == b) { found.push_back(p); break; }
                }
            }
            if (found.empty()) {
                std::cout << "  [OK] No known heavy apps running.\n";
            } else {
                std::cout << "  Found " << found.size()
                          << " known heavy app(s):\n\n";
                ramPrintTopN(found, (int)found.size());
                std::cout << "\n  [TIP] Use option 10 (Kill) to close one.\n";
            }
        }
        else if (c == 5) {
            double totalMB, usedMB, freeMB;
            ramSystemMemoryMB(totalMB, usedMB, freeMB);
            double threshold = totalMB * 0.30;

            std::vector<ProcInfo> danger;
            for (const auto& p : procs)
                if (p.ramBytes / (1024.0 * 1024.0) > threshold)
                    danger.push_back(p);

            if (danger.empty()) {
                std::cout << "  [OK] No process exceeds 30% of system RAM.\n";
            } else {
                std::cout << "  [DANGER] " << danger.size()
                          << " process(es) exceed 30% ("
                          << (long long)threshold << " MB):\n\n";
                ramPrintTopN(danger, (int)danger.size());
            }
        }
        else if (c == 6) {
            if (procs.empty()) {
                std::cout << "  [ERROR] No processes found.\n";
            } else {
                auto it = std::max_element(procs.begin(), procs.end(),
                    [](const ProcInfo& a, const ProcInfo& b) {
                        return a.ramBytes < b.ramBytes;
                    });
                double totalMB, usedMB, freeMB;
                ramSystemMemoryMB(totalMB, usedMB, freeMB);
                double pct = 100.0 * (it->ramBytes / (1024.0 * 1024.0)) / totalMB;
                printLine();
                std::cout << "  TOP RAM CONSUMER\n";
                printLine();
                std::cout << "  Process  : " << it->name << "\n";
                std::cout << "  PID      : " << it->pid << "\n";
                std::cout << "  RAM      : " << humanSize(it->ramBytes)
                          << "  (" << (int)pct << "% of system)\n";
                std::cout << "  CPU time : " << humanCPU(it->cpuTicks) << "\n";
                printLine();
            }
        }
        else if (c == 7) {
            std::string target = getAdvancedInput("  Process name (e.g. chrome): ");
            if (target.empty()) { pause(); continue; }

            std::string key = stripExeLower(target);
            bool found = false;
            for (const auto& root : procs) {
                if (stripExeLower(root.name) != key) continue;
                found = true;
                std::cout << "\n  " << root.name << " (PID " << root.pid
                          << ")  " << humanSize(root.ramBytes) << "\n";
                ramPrintTreeRecursive(procs, root.pid, 1);
            }
            if (!found)
                std::cout << "  [INFO] No process named '" << target << "'.\n";
        }
        else if (c == 8 || c == 9) {
            const char* verb = (c == 8) ? "pause" : "resume";
            std::string name = getAdvancedInput(
                std::string("  Process name to ") + verb + ": ");
            if (name.empty()) { pause(); continue; }
            bool ok = ramSuspendResume(name, c == 8);
            if (ok)
                std::cout << "  [OK] " << (c == 8 ? "Paused" : "Resumed")
                          << " all instances of '" << name << "'.\n";
            else
                std::cout << "  [ERROR] Could not " << verb << " '" << name
                          << "'. Not found or access denied.\n";
        }
        else if (c == 10) {
            std::string name = getAdvancedInput("  Process name to kill: ");
            if (name.empty()) { pause(); continue; }

            std::string key = stripExeLower(name);
            int killed = 0;
            for (const auto& p : procs) {
                if (stripExeLower(p.name) != key) continue;
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p.pid);
                if (!h) continue;
                if (TerminateProcess(h, 0)) killed++;
                CloseHandle(h);
            }
            if (killed > 0)
                std::cout << "  [OK] Terminated " << killed
                          << " instance(s) of '" << name << "'.\n";
            else
                std::cout << "  [ERROR] Could not kill '" << name
                          << "'. Not found or access denied.\n";
        }
        else if (c == 11) {
            double totalMB, usedMB, freeMB;
            ramSystemMemoryMB(totalMB, usedMB, freeMB);
            double ramPct = 100.0 * usedMB / totalMB;
            double cpuPct = ramCpuLoad(250);
            ULONGLONG diskFreeB = 0;
            GetDiskFreeSpaceExW(L"C:\\",
                (PULARGE_INTEGER)&diskFreeB, nullptr, nullptr);
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "  [CPU: %d%%] [RAM: %d%%] [C: %.1f GB free]\n",
                (int)cpuPct, (int)ramPct,
                diskFreeB / (1024.0 * 1024 * 1024));
            std::cout << buf;
        }
        else if (c == 12) {
            std::vector<ProcInfo> sorted = procs;
            std::sort(sorted.begin(), sorted.end(),
                [](const ProcInfo& a, const ProcInfo& b) {
                    return a.ramBytes > b.ramBytes;
                });
            if (sorted.size() > 5) sorted.resize(5);

            double maxMB = sorted.empty() ? 1.0 :
                sorted[0].ramBytes / (1024.0 * 1024.0);

            std::cout << "  Top 5 processes by RAM:\n\n";
            for (const auto& p : sorted) {
                double mb = p.ramBytes / (1024.0 * 1024.0);
                int barLen = (int)(mb / maxMB * 40.0 + 0.5);
                if (barLen < 1) barLen = 1;

                std::string name = p.name;
                if (name.size() > 20) name = name.substr(0, 18) + "..";

                std::cout << "  " << std::left << std::setw(20) << name << " [";
                for (int i = 0; i < barLen; ++i) std::cout << "#";
                for (int i = barLen; i < 40; ++i) std::cout << " ";
                char tail[32];
                std::snprintf(tail, sizeof(tail), "] %.1f MB", mb);
                std::cout << tail << "\n";
            }
            std::cout << std::right;
        }
        else if (c == 13) {
            ramPrintTopN(procs, 5);
        }
        else if (c == 14) {
            if (g_consoleHost) {
                std::cout << "  [INFO] Watch mode needs a real console.\n";
                std::cout << "  Launch with --cli and pick option 14 there.\n";
            } else {
                std::string secRaw = getAdvancedInput(
                    "  Refresh interval (seconds) [default 3]: ");
                int sec = 3;
                try { if (!secRaw.empty()) sec = std::stoi(secRaw); } catch (...) {}
                if (sec < 1)  sec = 1;
                if (sec > 60) sec = 60;

                std::cout << "\n  Watching every " << sec
                          << "s. Press any key to stop.\n";
                std::this_thread::sleep_for(std::chrono::seconds(1));

                while (!ramKeyWaiting()) {
                    clearScreen();
                    printSection("RAM QUERY  --  WATCH MODE");

                    double totalMB, usedMB, freeMB;
                    ramSystemMemoryMB(totalMB, usedMB, freeMB);
                    std::vector<ProcInfo> p2 = enumerateProcs();
                    ramPrintTopN(p2, 10);

                    double ramPct = 100.0 * usedMB / totalMB;
                    std::cout << "\n  RAM: ";
                    ramPrintBar(ramPct, 30);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "  %.1f%%\n", ramPct);
                    std::cout << buf;
                    std::cout << "  Uptime: " << ramUptime()
                              << "   |   Press any key to stop.\n";

                    for (int i = 0; i < sec * 10; ++i) {
                        if (ramKeyWaiting()) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
                while (ramKeyWaiting()) (void)ramReadKey();
            }
        }
        else {
            std::cout << "  [ERROR] Invalid choice.\n";
        }

        pause();
    }
}

#else
void RAMQueryPr() {
    std::cout << "  [INFO] RAM Query is a Windows-only utility.\n";
    pause();
}
#endif

// ==========================================
// MAIN HUB
// ==========================================
#ifdef _WIN32
// The child started by "Open in CLI" owns a new console, but a GUI-subsystem
// build (-mwindows) starts with no standard handles. Bind them to that console.
static void BindConsoleStreams() {
    HANDLE hIn  = CreateFileW(L"CONIN$",  GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hIn  != INVALID_HANDLE_VALUE) SetStdHandle(STD_INPUT_HANDLE, hIn);
    if (hOut != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, hOut);
        SetStdHandle(STD_ERROR_HANDLE, hOut);
    }
    std::freopen("CONIN$",  "r", stdin);
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
    std::cin.clear();
    std::cout.clear();
    std::cerr.clear();
    std::clog.clear();
}

#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    // Default: graphical hub.  Pass --cli (or -c) for the classic text hub.
    bool classicHub = false;
    int  toolIdx    = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--cli" || a == "-c" || a == "/cli") classicHub = true;
        if (a == "--tool" && i + 1 < argc) toolIdx = std::atoi(argv[++i]);
    }
    if (toolIdx >= 0) {
        // A GUI-subsystem program gets no console window by itself; asking for one
        // with AllocConsole gives it a visible console.
        if (!GetConsoleWindow()) AllocConsole();
        BindConsoleStreams();
        ConfigureConsole();
        return RunToolCli(toolIdx);
    }
    if (classicHub) EnsureConsoleStreams(true);
#else
    (void)argc; (void)argv;
#endif
#ifdef _WIN32
    if (!classicHub && RunGuiHub()) return 0;      // GUI ran and was closed
    EnsureConsoleStreams(true);                    // GUI unavailable (or --cli): classic console hub
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
        {11, "Encoder / Decoder",        {"encode", "decode", "base64", "url", "rot13", "hex"}},
        {12, "System Info",              {"info",   "system", "build","version"}},
        {13, "Clean Up Compute",         {"clean",  "ram",    "memory","optimize"}},
        {14, "Quick Note Editor",        {"note",   "vim",    "save","write"}},
        {15, "Random Picker",            {"pick",   "coin",   "dice", "lottery", "choose"}},
        {16, "Color Converter",          {"color", "colour", "hex", "rgb", "hsl"}},
        {17, "Prime / Factor Tools",     {"prime", "factor", "factorize", "sieve"}},
        {18, "Roman Numeral Converter",  {"roman", "numeral", "latin"}},
        {19, "File Hasher",              {"hash", "sha", "crc", "checksum", "integrity"}},
        {20, "Secure File Mover",        {"move", "copy", "transfer", "verify", "sync", "robocopy", "move-sh"}},
        {21, "RAM Query",                {"ram", "memory", "process", "cpu", "task", "monitor"}},
        {22, "Exit System",              {"exit", "quit", "bye", "close"}},
    };

    bool hubRunning = true;
    while (hubRunning) {
        clearScreen();
        printHubBanner();
        printLine();
        std::cout << "  Logged in as: " << userName << "\n";
        printLine();
        std::cout << "\n  [1]  Calculator       [6]  Random Gen     [11] Encoder        [16] Color-Picker\n";
        std::cout << "  [2]  Guessing Game    [7]  BMI Calc       [12] System Info    [17] Prime/Factor tools\n";
        std::cout << "  [3]  Unit Converter   [8]  Age Calc       [13] Clean RAM      [18] Roman Numeral Converter\n";
        std::cout << "  [4]  Password Gen     [9]  Text Analyzer  [14] Quick Note     [19] File-Hasher \n";
        std::cout << "  [5]  Base Converter   [10] Text Formatter  [15] Random Pick    [20] File-Mover\n";
        std::cout << "  [21] Ram Query        [22] Exit \n";
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
            case 11: EncoderDecoderPr(); break;
            case 12:
                clearScreen();
                printSection("SYSTEM INFO");
                std::cout << "  Developer   : Shivansh\n";
                std::cout << "  Version     : " << programVersion << "\n";
                std::cout << "  Build Date  : " << __DATE__ << "\n";
                std::cout << "  Architecture: Standard C++ Separation\n";
                std::cout << "  Code - 13929-0611-" << programVersion << "\n";
                pause();
                break;
            case 13: CleanUpCompute(); break;
            case 14: QuickNotePr(); break;
            case 15: RandomPickerPr(); break;
            case 16: ColorConverterPr(); break;
            case 17: PrimeToolsPr(); break;
            case 18: RomanNumeralPr(); break;
            case 19: FileHasherPr(); break;
            case 20: SecureFileMoverPr(); break;
            case 21: RAMQueryPr(); break;
            case 22: hubRunning = false; break;
            default:
                std::cout << "\n  [ERROR] Invalid choice.\n";
                pause();
        }
    }   // <-- closes while (hubRunning) — THIS was missing

    clearScreen();
    std::cout << "\n  Shutting down... Goodbye " << userName << "!\n\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));  // wait for Enter so the window doesn't vanish instantly
    return 0;
}

