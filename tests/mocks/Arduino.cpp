#include "Arduino.h"
#include <iostream>

static uint8_t lastPin = 0;
static uint8_t lastValue = 0;
static bool pinModeCalled = false;

void digitalWrite(uint8_t pin, uint8_t value)
{
    lastPin = pin;
    lastValue = value;
}

void pinMode(uint8_t pin, uint8_t mode)
{
    pinModeCalled = true;
}

uint8_t getLastDigitalWritePin()
{
    return lastPin;
}

uint8_t getLastDigitalWriteValue()
{
    return lastValue;
}

void resetDigitalWriteHistory()
{
    lastPin = 0;
    lastValue = 0;
    pinModeCalled = false;
}

