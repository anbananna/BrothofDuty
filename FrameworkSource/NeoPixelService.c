#include "NeoPixelService.h"
#include <xc.h>

#define MAX_LEDS    10
#define WS_PIN      LATBbits.LATB11
#define WS_TRIS     TRISBbits.TRISB11

static uint8_t ledColors[MAX_LEDS][3]; // (G,R,B)

/****************************************************************************
// Initialize the NeoPixel service
****************************************************************************/
void InitNeoPixelService(void)
{
    WS_TRIS = 0; // Set WS_PIN as output
    WS_PIN = 0;  // Initialize WS_PIN to low
    for (int i = 0; i < MAX_LEDS; i++)
    {
        ledColors[i][0] = 0; // Green
        ledColors[i][1] = 0; // Red
        ledColors[i][2] = 0; // Blue
    }
}

/****************************************************************************
// Send a single bit to the NeoPixel strip
// __asm__ - tells xc32 compiler to insert assembly code
// volatile - tells compiler not to optimize this code away
// "nop" - assembly instruction for "no operation", used for timing
****************************************************************************/
static inline void SendBit(uint8_t bit)
{
    if (bit)
    {
        // Send '1' bit
        WS_PIN = 1;
        __asm__ volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"); // needs calibrating - high
        WS_PIN = 0;
        __asm__ volatile("nop\n nop\n nop\n nop\n"); // needs calibrating - low
    }
    else
    {
        // Send '0' bit
        WS_PIN = 1;
        __asm__ volatile("nop\n nop\n"); // needs calibrating - high
        WS_PIN = 0;
        __asm__ volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"); // needs calibrating - low
    }
}

/****************************************************************************
// Send a single byte to the NeoPixel strip
****************************************************************************/
static void SendByte(uint8_t byte)
{
    for (int i = 7; i >= 0; i--)
    {
        SendBit((byte >> i) & 0x01);
    }
}

/****************************************************************************
// Update LEDs with current colors
****************************************************************************/
void UpdateNeoPixels(void)
{
    for (int i = 0; i < MAX_LEDS; i++)
    {
        SendByte(ledColors[i][0]); // Green
        SendByte(ledColors[i][1]); // Red
        SendByte(ledColors[i][2]); // Blue
    }
    // Latch the data by holding low for >50us
    WS_PIN = 0;
    __asm__ volatile("nop"); // needs calibrating
}

/****************************************************************************
// Set color of a specific LED
****************************************************************************/
void SetNeoPixelColor(uint8_t ledIndex, uint8_t red, uint8_t green, uint8_t blue)
{
    if (ledIndex < MAX_LEDS)
    {
        ledColors[ledIndex][0] = green;
        ledColors[ledIndex][1] = red;
        ledColors[ledIndex][2] = blue;
    }
}

/****************************************************************************
// Clear all LEDs
****************************************************************************/
void ClearNeoPixels(void)
{
    for (int i = 0; i < MAX_LEDS; i++)
    {
        ledColors[i][0] = 0; // Green
        ledColors[i][1] = 0; // Red
        ledColors[i][2] = 0; // Blue
    }
}
