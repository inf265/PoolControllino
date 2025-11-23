#pragma once

// Mock Arduino.h for PC testing
#include <cstdint>
#include <cstring>
#include <string>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>

// Mock FlashStringHelper for PROGMEM strings
class __FlashStringHelper {};
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(string_literal))

// Mock Print class
class Print {
public:
    virtual size_t write(uint8_t) { return 0; }
    virtual size_t write(const uint8_t *buffer, size_t size) { return size; }
};

// Mock Stream class (inherits from Print)
class Stream : public Print {
public:
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual size_t readBytes(char *buffer, size_t length) {
        (void)buffer;
        (void)length;
        return 0;
    }
};

// Mock Printable class
class Printable {
public:
    virtual size_t printTo(Print& p) const = 0;
};

// min/max - provide as inline functions to avoid macro conflicts with std::min/max
// Arduino code often uses these as macros, but functions are safer
// Note: Some Arduino code expects macros, so we define them AFTER standard library includes
// Use a wrapper approach - only define if not already defined by std
#ifndef _GLIBCXX_ALGORITHM  // Only define if std::min not yet included
// min/max functions for Arduino compatibility
inline int min(int a, int b) { return (a < b) ? a : b; }
inline unsigned int min(unsigned int a, unsigned int b) { return (a < b) ? a : b; }
inline long min(long a, long b) { return (a < b) ? a : b; }
inline float min(float a, float b) { return (a < b) ? a : b; }
inline double min(double a, double b) { return (a < b) ? a : b; }
inline size_t min(size_t a, size_t b) { return (a < b) ? a : b; }

inline int max(int a, int b) { return (a > b) ? a : b; }
inline unsigned int max(unsigned int a, unsigned int b) { return (a > b) ? a : b; }
inline long max(long a, long b) { return (a > b) ? a : b; }
inline float max(float a, float b) { return (a > b) ? a : b; }
inline double max(double a, double b) { return (a > b) ? a : b; }
inline size_t max(size_t a, size_t b) { return (a > b) ? a : b; }
#endif

// String class mock - simplified version
class String
{
public:
    String() : str_("") {}
    String(const char *cstr) : str_(cstr ? cstr : "") {}
    String(const std::string &str) : str_(str) {}
    String(char c) : str_(1, c) {}
    
    String &operator=(const char *cstr) { str_ = cstr ? cstr : ""; return *this; }
    String &operator=(const std::string &str) { str_ = str; return *this; }
    String &operator+=(const char *cstr) { str_ += cstr ? cstr : ""; return *this; }
    String &operator+=(const String &other) { str_ += other.str_; return *this; }
    String &operator+=(char c) { str_ += c; return *this; }
    
    const char *c_str() const { return str_.c_str(); }
    size_t length() const { return str_.length(); }
    bool operator==(const String &other) const { return str_ == other.str_; }
    
    char operator[](size_t index) const { return str_[index]; }
    char &operator[](size_t index) { return str_[index]; }
    
    int toInt() const { 
        try {
            return std::stoi(str_); 
        } catch (...) {
            return 0;
        }
    }
    
    unsigned char concat(const char *cstr) {
        if (cstr) {
            str_ += cstr;
            return 1;
        }
        return 0;
    }
    
    unsigned char concat(const String &other) {
        str_ += other.str_;
        return 1;
    }
    
    operator const char*() const { return str_.c_str(); }
    operator bool() const { return str_.length() > 0; }
    
private:
    std::string str_;
};

inline bool operator==(const String &lhs, const char *rhs) { return lhs.c_str() == String(rhs); }
inline bool operator==(const char *lhs, const String &rhs) { return String(lhs) == rhs; }

// Arduino pin states
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1

// Mock digitalWrite - tracks calls for verification
void digitalWrite(uint8_t pin, uint8_t value);
void pinMode(uint8_t pin, uint8_t mode);

// Get last digitalWrite call for verification
uint8_t getLastDigitalWritePin();
uint8_t getLastDigitalWriteValue();
void resetDigitalWriteHistory();

// Mock PROGMEM (just ignore it on PC)
#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define memcpy_P(dest, src, size) memcpy(dest, src, size)

// Define min/max as macros - but ONLY if not already defined
// This avoids conflicts with std::min/std::max
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

