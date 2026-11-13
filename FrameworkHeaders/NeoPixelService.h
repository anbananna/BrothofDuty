#ifndef NEOPIXELSERVICE_H
#define NEOPIXELSERVICE_H

#include <stdint.h>
#include <stdbool.h>

void InitNeoPixelService(void);
void UpdateNeoPixels(void);
void SetNeoPixelColor(uint8_t ledIndex, uint8_t red, uint8_t green, uint8_t blue);
void ClearNeoPixels(void);

#endif // NEOPIXELSERVICE_H
