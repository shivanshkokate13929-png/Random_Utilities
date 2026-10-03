#include "RandomUtilities.h"
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

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
static std::string getClipboardText() {
    std::string result;
    if (!OpenClipboard(nullptr))
        return result;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData) {
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText) {
            result = pszText;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

static void setClipboardText(const std::string& text) {
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hMem) {
            char* pData = (char*)GlobalLock(hMem);
            if (pData) {
                memcpy(pData, text.c_str(), text.size() + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
        }
        CloseClipboard();
    }
}
#endif

// ==========================================
// HELPER UTILITIES
// ==========================================
void clearScreen() {
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
    int ch;
    do { ch = _getch(); } while (ch != 13 && ch != 10);
#else
    while (getchar() != '\n') {}
#endif
}

std::string getHiddenPassword() {
    std::string pass = "";
    bool show = false;
    char ch;

    std::cout << "  (TAB to toggle [Show Password], ENTER to confirm)\n  > ";
    std::cout.flush();

    while (true) {
        ch = _getch();

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
            pass += ch;
            if (show) std::cout << ch;
            else std::cout << '*';
            std::cout.flush();
        }
    }
    return pass;
}

#ifdef _WIN32
void SummonDedicatedWindow() {
    FreeConsole();
    if (!AllocConsole()) {
        return;
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);

    COORD       bufferSize = { 120, 9000 };
    SMALL_RECT  windowSize = { 0, 0, 119, 40 };
    SetConsoleScreenBufferSize(hOut, bufferSize);
    SetConsoleWindowInfo(hOut, TRUE, &windowSize);

    bool ok = true;
    if (!freopen("CONIN$",  "r", stdin))  ok = false;
    if (!freopen("CONOUT$", "w", stdout)) ok = false;
    if (!freopen("CONOUT$", "w", stderr)) ok = false;
    if (!ok) {
        fprintf(stderr, "FATAL: Could not redirect stdio to console.\n");
        ExitProcess(1);
    }

    std::ios_base::sync_with_stdio(true);
    SetConsoleTitle(TEXT("Random Utilities - Shivansh's C++ Hub"));

    DWORD dwMode = 0;
    if (GetConsoleMode(hIn, &dwMode)) {
        dwMode &= ~ENABLE_QUICK_EDIT_MODE;
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

// String Conversions for Unicode handling
static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
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
    c.X = totalChars % state.consoleWidth;
    c.Y = state.startY + (totalChars / state.consoleWidth);
    return c;
}

static void clearPreviousDrawing(HANDLE hOut, const InputState& state) {
    DWORD written;
    COORD start = { state.startX, state.startY };
    int totalCharsToClear = state.consoleWidth - state.startX + ((state.previousRows - 1) * state.consoleWidth);
    if (totalCharsToClear > 0) {
        FillConsoleOutputCharacterW(hOut, L' ', totalCharsToClear, start, &written);
    }
}

static void redrawInput(HANDLE hOut, InputState& state, const std::string& prompt) {
    clearPreviousDrawing(hOut, state);
    
    COORD start = { state.startX, state.startY };
    SetConsoleCursorPosition(hOut, start);
    
    // Draw the prompt (ansi/utf8)
    std::cout << prompt;
    std::cout.flush();
    
    // Draw the user input
    DWORD written;
    if (!state.text.empty()) {
        WriteConsoleW(hOut, state.text.c_str(), (DWORD)state.text.length(), &written, NULL);
    }
    
    // Calculate and save space required for next potential clearance
    int promptLen = (int)prompt.length();
    int totalChars = state.startX + promptLen + (int)state.text.length();
    state.previousRows = (totalChars / state.consoleWidth) + 1;
    
    // Reposition cursor visually
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
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    DWORD oldModeIn;
    GetConsoleMode(hIn, &oldModeIn);
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS);

    InputState state;
    state.text = L"";
    state.cursor = 0;
    state.previousRows = 1;
    
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    state.startX = csbi.dwCursorPosition.X;
    state.startY = csbi.dwCursorPosition.Y;
    state.consoleWidth = csbi.dwSize.X;
    
    redrawInput(hOut, state, prompt);
    
    bool done = false;
    while (!done) {
        INPUT_RECORD ir;
        DWORD read;
        ReadConsoleInputW(hIn, &ir, 1, &read);
        
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            updateConsoleSize(hOut, state);
            redrawInput(hOut, state, prompt);
            continue;
        }
        
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) {
            continue;
        }
        
        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        WCHAR uc = ir.Event.KeyEvent.uChar.UnicodeChar;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        
        // Ignore modifier-only keys
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || 
            vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) {
            continue; 
        }

        // Ctrl+C (Copy)
        if (vk == 'C' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            setClipboardText(wstring_to_utf8(state.text));
            continue;
        }
        
        // Ctrl+V (Paste)
        if (vk == 'V' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
            std::string clip = getClipboardText();
            if (!clip.empty()) {
                std::wstring wclip = utf8_to_wstring(clip);
                state.text.insert(state.cursor, wclip);
                state.cursor += wclip.length();
                redrawInput(hOut, state, prompt);
            }
            continue;
        }

        // F5 (Copy shortcut)
        if (vk == VK_F5) {
            setClipboardText(wstring_to_utf8(state.text));
            continue;
        }

        // Escape
        if (vk == VK_ESCAPE) {
            std::cout << "\n";
            SetConsoleMode(hIn, oldModeIn);
            return "";
        }
        
        // Enter
        if (vk == VK_RETURN) {
            COORD eol = calculateCursorPosition(state, prompt.length(), state.text.length());
            SetConsoleCursorPosition(hOut, eol);
            std::cout << "\n";
            std::cout.flush();
            done = true;
            continue;
        }

        // Home
        if (vk == VK_HOME) {
            state.cursor = 0;
            redrawInput(hOut, state, prompt);
            continue;
        }
        
        // End
        if (vk == VK_END) {
            state.cursor = state.text.length();
            redrawInput(hOut, state, prompt);
            continue;
        }
        
        // Left Arrow
        if (vk == VK_LEFT) {
            moveCursor(state, -1);
            redrawInput(hOut, state, prompt);
            continue;
        }
        
        // Right Arrow
        if (vk == VK_RIGHT) {
            moveCursor(state, 1);
            redrawInput(hOut, state, prompt);
            continue;
        }

        // Backspace
        if (vk == VK_BACK) {
            if (state.cursor > 0) {
                state.text.erase(state.cursor - 1, 1);
                state.cursor--;
                redrawInput(hOut, state, prompt);
            }
            continue;
        }

        // Delete
        if (vk == VK_DELETE) {
            if (state.cursor < state.text.length()) {
                state.text.erase(state.cursor, 1);
                redrawInput(hOut, state, prompt);
            }
            continue;
        }

        // Printable Characters
        if (uc >= 32) {
            state.text.insert(state.cursor, 1, uc);
            state.cursor++;
            redrawInput(hOut, state, prompt);
        }
    }
    
    SetConsoleMode(hIn, oldModeIn);
    return wstring_to_utf8(state.text);
}

#else
// Non-Windows fallback (unchanged)
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
            if (pos == raw.size()) return true;
        } catch (...) {}
        std::cout << "  [ERROR] Not a valid number. Try again.\n";
    }
}

// ----------------------------------------------
// UTILITY FUNCTIONS (Calculator, Guessing, etc.)
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
            else { // c == 3
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
            history.push_back(
                std::to_string(n1) + " op -> " + std::to_string(res));
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
        for ( unsigned char c : raw) if (!std::isdigit(c)) { ok = false; break; }
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
                int idx = std::stoi(raw) - 1;
                if (idx >= 0 && idx < (int)units.size()) return idx;
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
        int catIdx = std::stoi(catRaw) - 1;
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
                if (pos == vRaw.size()) break;
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
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "  " << value  << " " << cat.units[fromIdx].abbr
                  << "  =  "
                  << result << " " << cat.units[toIdx].abbr << "\n";
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
            long long nextYearDays = dateToDays(nextYear, tempMonth, tempDay);
            if (nextYearDays <= currentDays) {
                ageYears++;
                tempYear = nextYear;
            } else break;
        }
        long long remaining = currentDays - dateToDays(tempYear, tempMonth, tempDay);
        int ageMonths = 0;
        while (true) {
            int nextMonth = tempMonth + 1;
            if (nextMonth > 12) { nextMonth = 1; tempYear++; }
            long long nextMonthDays = dateToDays(tempYear, nextMonth, tempDay);
            if (nextMonthDays <= currentDays) {
                ageMonths++;
                tempMonth = nextMonth;
            } else break;
        }
        int ageDays = (int)(currentDays - dateToDays(tempYear, tempMonth, tempDay));

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

    std::cout << "\n  BMI      : " << std::fixed << std::setprecision(2) << bmi << "\n";
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
    if (words > 0)
        std::cout << "  Avg word length    : " << std::fixed << std::setprecision(1)
                  << (double)letters / words << "\n";
    pause();
}

void CleanUpCompute() {
    clearScreen();
    printSection("CLEANUP PROTOCOL");
    std::cout << "  Optimizing memory workspace...\n";

#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    SIZE_T before = pmc.WorkingSetSize;

    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
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
// SECURE QUICK NOTE
// ==========================================
void QuickNotePr() {
    bool run = true;
    const std::string MAGIC = "SHIVANSH_V1"; 

    while (run) {
        clearScreen();
        printSection("SECURE QUICK NOTE");
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
                if (line == ":wq") {
                    save = true; editing = false;
                }
                else if (line == ":q") {
                    editing = false;
                }
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
                else if (line == ":l") {
                    printBuffer();
                }
                else {
                    lines.push_back(line);
                }
            }

            if (save) {
                std::string fullText = MAGIC + "\n";
                for (const auto& l : lines) fullText += l + "\n";
                std::string encrypted = xorCrypt(fullText, password);
                std::ofstream out(path, std::ios::binary);
                if (out.is_open()) {
                    out.write(encrypted.data(), encrypted.size());
                    out.close();
                    std::cout << "  [SUCCESS] Saved securely.\n";
                } else {
                    std::cout << "  [ERROR] Cannot write to path.\n";
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
// MAIN HUB & LEVENSHTEIN SEARCH ENGINE
// ==========================================
int main() {
#ifdef _WIN32
    SummonDedicatedWindow();
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
        {10, "Reserved",                 {"reserved"}},
        {11, "System Info",              {"info",   "system", "build","version"}},
        {12, "Clean Up Compute",         {"clean",  "ram",    "memory","optimize"}},
        {13, "Quick Note Editor",        {"note",   "vim",    "save","write"}},
        {14, "Exit System",              {"exit",   "quit",   "bye","close"}},
    };

    bool hubRunning = true;
    while (hubRunning) {
        clearScreen();
        printHubBanner();
        printLine();
        std::cout << "  Logged in as: " << userName << "\n";
        printLine();
        std::cout << "\n  [1]  Calculator       [6]  Random Gen     [11] System Info\n";
        std::cout << "  [2]  Guessing Game    [7]  BMI Calc       [12] Clean RAM\n";
        std::cout << "  [3]  Unit Converter   [8]  Age Calc       [13] Quick Note\n";
        std::cout << "  [4]  Password Gen     [9]  Text Analyzer  [14] Exit System\n";
        std::cout << "  [5]  Base Converter   [10] (Reserved)\n";

        std::cout <<"\n";
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
            std::transform(query.begin(), query.end(), query.begin(), ::tolower);
            int minDist = 999;
            std::string bestMatch = "";

            for (const auto& feat : featureIndex) {
                std::string lowerName = feat.name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

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
            case 10:
                clearScreen();
                printSection("RESERVED SLOT");
                std::cout << "  [INFO] This slot is reserved for a future utility.\n";
                std::cout << "         Stay tuned for the next update!\n";
                pause();
                break;
            case 11:
                clearScreen();
                printSection("SYSTEM INFO");
                std::cout << "  Developer   : Shivansh\n";
                std::cout << "  Version     : 1.1.9b\n";          
                std::cout << "  Build Date  : " << __DATE__ << "\n";  
                std::cout << "  Architecture: Standard C++ Separation\n";
                pause();
                break;
            case 12: CleanUpCompute(); break;
            case 13: QuickNotePr(); break;
            case 14: hubRunning = false; break;
            default:
                std::cout << "\n  [ERROR] Invalid choice.\n";
                pause();
        }
    }

    clearScreen();
    std::cout << "\n  Shutting down... Goodbye " << userName << "!\n\n";
    return 0;
}
