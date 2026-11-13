/****************************************************************************
 Module
     NeoPixelService.c

 Description
     WS2812 ("NeoPixel") driver on RB11, using SPI2 to generate the waveform.

     Bit-banging doesn't work here: the project builds at -O0 and the flash
     runs with the reset-default wait states, so the CPU is far too slow to
     hit the 0.4 us / 0.8 us pulse widths. SPI hardware gets the timing exact
     regardless of CPU speed.

     Encoding: SPI2 runs at 2.5 MHz (400 ns per SPI bit). Each WS2812 data
     bit is sent as four SPI bits (one nibble):
         1 -> 1100  (800 ns high, 800 ns low)
         0 -> 1000  (400 ns high, 1200 ns low)
     SPI2 runs in 32-bit mode, so one color byte is exactly one SPI word
     (12.8 us on the wire). The 4-word FIFO holds ~50 us, so even the slow
     -O0 code keeps it full and the stream has no gaps.

     Pins: SDO2 -> RB11 (via PPS). SCK2 is fixed on RB15 (unused, NC).
     SPI1 is left alone (it drives the display).
****************************************************************************/
#include "NeoPixelService.h"
#include <xc.h>
#include <stdbool.h>
#include "dbprintf.h"

#define MAX_LEDS            30

#define WS_TRIS             TRISBbits.TRISB11
#define WS_LAT              LATBbits.LATB11
#define PPS_SDO2            0b0100      // RPB11R value for SDO2

// SPI2 bit time: PBCLK 20 MHz / (2 * (BRG + 1)) = 2.5 MHz -> 400 ns
#define SPI2_BRG_2_5MHZ     3

// Latch: hold the line low > 280 us. Core timer runs at SYSCLK/2 = 20 MHz.
#define LATCH_TICKS         6000        // 300 us

#define SPI_WORDS_PER_LED   3           // one 32-bit word per color byte

// Give up on a frame after 5 ms (core timer ticks, 50 ns each)
#define FRAME_TIMEOUT_TICKS 100000
#define Elapsed(Start)      ((uint32_t)(_CP0_GET_COUNT() - (Start)))

static uint8_t ledColors[MAX_LEDS][3];                    // (G,R,B) wire order
static uint32_t spiFrame[MAX_LEDS * SPI_WORDS_PER_LED];   // encoded waveform

static void InitSPI2(void);
static uint32_t EncodeByte(uint8_t ColorByte);
static void WaitTicks(uint32_t Ticks);

/****************************************************************************
// Initialize SPI2 on RB11 and blank the strip
****************************************************************************/
void InitNeoPixelService(void)
{
    WS_TRIS = 0;        // RB11 output (no analog function on this pin)
    WS_LAT = 0;

    InitSPI2();

    ClearNeoPixels();
    UpdateNeoPixels();
}

/****************************************************************************
// Send the stored colors to the strip
****************************************************************************/
void UpdateNeoPixels(void)
{
    uint16_t i;
    uint16_t n = 0;

    // Encode everything first so the send loop is as tight as possible
    for (i = 0; i < MAX_LEDS; i++)
    {
        spiFrame[n++] = EncodeByte(ledColors[i][0]);    // Green
        spiFrame[n++] = EncodeByte(ledColors[i][1]);    // Red
        spiFrame[n++] = EncodeByte(ledColors[i][2]);    // Blue
    }

    SPI2STATCLR = _SPI2STAT_SPIROV_MASK;    // clear any old receive overflow

    // Block interrupts for the frame (~1.2 ms) so an ISR can't stall the
    // FIFO refill and open a gap mid-frame. The framework tick ISR catches
    // up on any tick it misses, so no time is lost.
    uint32_t IntState = __builtin_get_isr_state();
    __builtin_disable_interrupts();

    // A full frame takes ~1.2 ms; give up after FRAME_TIMEOUT_TICKS so a
    // misbehaving SPI module can never freeze the whole program
    uint32_t FrameStart = _CP0_GET_COUNT();
    bool TimedOut = false;

    for (i = 0; (i < n) && !TimedOut; i++)
    {
        while (SPI2STATbits.SPITBF)         // wait for room in the TX FIFO
        {
            if (Elapsed(FrameStart) > FRAME_TIMEOUT_TICKS)
            {
                TimedOut = true;
                break;
            }
        }
        if (!TimedOut)
        {
            SPI2BUF = spiFrame[i];
        }

        // Discard received words so the RX FIFO never overflows
        while (!SPI2STATbits.SPIRBE)
        {
            (void)SPI2BUF;
        }
    }

    // Wait for the last bits to leave the shift register
    while (!TimedOut && !SPI2STATbits.SRMT)
    {
        if (Elapsed(FrameStart) > FRAME_TIMEOUT_TICKS)
        {
            TimedOut = true;
        }
    }
    while (!SPI2STATbits.SPIRBE)
    {
        (void)SPI2BUF;
    }

    __builtin_set_isr_state(IntState);

    if (TimedOut)
    {
        DB_printf("NEOPIXEL: SPI2 timeout (sent %u of %u words, STAT=0x%x)\r\n",
                  i, n, (uint16_t)SPI2STAT);
    }

    // Latch: line idles low; hold it so the strip shows the new data
    WaitTicks(LATCH_TICKS);
}

/****************************************************************************
// Set color of one LED (takes effect on the next UpdateNeoPixels)
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
// Set every LED to the same color (takes effect on the next UpdateNeoPixels)
****************************************************************************/
void SetAllNeoPixels(uint8_t red, uint8_t green, uint8_t blue)
{
    for (uint8_t i = 0; i < MAX_LEDS; i++)
    {
        SetNeoPixelColor(i, red, green, blue);
    }
}

/****************************************************************************
// Turn all LEDs off (takes effect on the next UpdateNeoPixels)
****************************************************************************/
void ClearNeoPixels(void)
{
    SetAllNeoPixels(0, 0, 0);
}

/*************************** Private Functions ****************************/

static void InitSPI2(void)
{
    SPI2CON = 0;                    // module off, reset config
    (void)SPI2BUF;                  // flush RX
    SPI2BRG = SPI2_BRG_2_5MHZ;
    SPI2STATCLR = _SPI2STAT_SPIROV_MASK;

    SPI2CONbits.MSTEN = 1;          // leader (master) mode, drives SCK2
    SPI2CONbits.MODE32 = 1;         // 32-bit transfers: one word per color byte
    SPI2CONbits.MODE16 = 0;
    SPI2CONbits.ENHBUF = 1;         // FIFO so words go out back-to-back
    SPI2CONbits.DISSDI = 1;         // no data input pin needed
    SPI2CONbits.SSEN = 0;           // no slave select
    SPI2CONbits.CKP = 0;
    SPI2CONbits.CKE = 1;

    RPB11R = PPS_SDO2;              // route SDO2 to RB11

    SPI2CONbits.ON = 1;
}

// One color byte -> one 32-bit SPI word: each WS bit becomes a nibble,
// MSB first (1 -> 0xC = 1100, 0 -> 0x8 = 1000)
static uint32_t EncodeByte(uint8_t ColorByte)
{
    uint32_t Word = 0;

    for (int8_t b = 7; b >= 0; b--)
    {
        Word <<= 4;
        Word |= ((ColorByte >> b) & 0x01) ? 0xC : 0x8;
    }
    return Word;
}

static void WaitTicks(uint32_t Ticks)
{
    uint32_t Start = _CP0_GET_COUNT();
    while ((uint32_t)(_CP0_GET_COUNT() - Start) < Ticks)
    {
    }
}
