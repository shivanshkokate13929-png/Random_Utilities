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

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

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

#endif // RANDOM_UTILITIES_H