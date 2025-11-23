#pragma once

// Mock Controllino.h for PC testing

// RTC functions
inline void Controllino_RTC_init(int unused) { (void)unused; }
inline int Controllino_GetYear() { return 2024; }
inline int Controllino_GetMonth() { return 1; }
inline int Controllino_GetDay() { return 1; }
inline int Controllino_GetHour() { return 0; }
inline int Controllino_GetMinute() { return 0; }
inline int Controllino_GetSecond() { return 0; }

// Pin definitions - just define dummy values
#define CONTROLLINO_R10 10
#define CONTROLLINO_R12 12
#define CONTROLLINO_R13 13

