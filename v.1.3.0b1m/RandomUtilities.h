#ifndef RANDOM_UTILITIES_H
#define RANDOM_UTILITIES_H

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

// ==========================================
// BRANDING  --  the icon and logo used by every window
//
// Put the logo file next to RandomUtilities.exe (the program also looks in the
// folder you start it from). PNG works best; a square image makes the nicest
// icon. If the file is missing, the default Windows icon is used and no logo
// is drawn.
// ==========================================
constexpr const wchar_t* kLogoFileName   = L"logo.png";
constexpr float          kLogoHeightHub  = 52.f;   // logo height in the hub header (logical px)
constexpr float          kLogoHeightTool = 30.f;   // logo height in tool and sub-window headers

struct Feature {
    int id;
    std::string name;
    std::vector<std::string> keywords;
};

void clearScreen();
void clearInputBuffer();
void pause();
void printLine();
void printSection(const std::string& title);
void printHubBanner();
std::string toBinary(long long n);
std::string getAdvancedInput(const std::string& prompt);
std::string getAdvancedInputWithNav(const std::string& prompt,
                                     const std::string& preload,
                                     int& navOut);   // navOut: 0=enter, -1=up, 1=down, -2=esc
std::string getHiddenPassword();
int levenshteinDistance(const std::string& s1, const std::string& s2);
std::string xorCrypt(const std::string& data, const std::string& key);

#ifdef _WIN32
void ConfigureConsole();
#endif

void CalculatorPr();
void GuessingGamePr();
void UnitConverterPr();
void PasswordGeneratorPr();
void BaseConverterPr();
void RandomNumberGeneratorPr();
void BMICalculatorPr();
void AgeCalculatorPr();
void TextAnalyzerPr();
void TextFormatterPr();
void CleanUpCompute();
void QuickNotePr();

// --- v1.2.1b1 additions ---
void ColorConverterPr();
void PrimeToolsPr();
void RomanNumeralPr();
void FileHasherPr();

// --- v1.2.1b5 additions ---
void EncoderDecoderPr();
void RandomPickerPr();

// --- v1.2.5b1m additions ---
void SecureFileMoverPr();
void RAMQueryPr();

// --- v1.3.0 additions (graphical hub) ---
#ifdef _WIN32
int  RunToolCli(int toolId);    // "--tool N": run one tool in this process's console
bool RunGuiHub();                                  // GDI+ launcher; returns false if it could not start
bool EnsureConsoleStreams(bool attachParent = true); // attach/alloc a console and rebind std streams
#endif
// ==========================================
// VIRTUAL CONSOLE HOST INTERFACE
//
// When non-null, the util functions below route their I/O here instead of
// touching the real Win32 console.  Set by the GUI hub before it runs a
// tool on a worker thread; cleared afterwards.  CLI mode leaves it null and
// everything behaves exactly as before.
// ==========================================
struct IConsoleHost {
    virtual ~IConsoleHost() = default;
    virtual void        clear()                              = 0;
    virtual std::string readLine(const std::string& prompt)  = 0;
    virtual std::string readPassword(const std::string& p)   = 0;
    virtual void        waitForEnter(const std::string& msg) = 0;
    // Non-blocking key check and blocking key read, used by RAM Query's live
    // view. The defaults keep other hosts working; the GUI hub overrides them.
    virtual bool        keyWaiting() { return false; }
    virtual int         readKey()    { return -1; }
};

extern thread_local IConsoleHost* g_consoleHost;   // per-thread: each tool session has its own

#endif // RANDOM_UTILITIES_H