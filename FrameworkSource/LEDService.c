#include "LEDService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "dbprintf.h"
#include "DM_Display.h"
#include "FontStuff.h"

#include <stdint.h>
#include <stdbool.h>

#define SCROLL_COLS     4
#define CHAR_QUEUE_SIZE 8
#define DISPLAY_WIDTH_COLUMNS 32U
#define FONT_CHAR_WIDTH_COLUMNS 4U
#define COUNTDOWN_ELLIPSIS_ROW 4U
#define COUNTDOWN_ELLIPSIS_BITS 0x49U

// Three-column digits stored in bits 3..1, sized for a 4x4 quadrant.
static const uint8_t TimerDigitRows[10][4] = {
    {0xE, 0xA, 0xA, 0xE},
    {0x4, 0xC, 0x4, 0xE},
    {0xE, 0x2, 0x4, 0xE},
    {0xE, 0x2, 0x6, 0xE},
    {0xA, 0xA, 0xE, 0x2},
    {0xE, 0x8, 0xE, 0xE},
    {0x6, 0x8, 0xE, 0xA},
    {0xE, 0x2, 0x4, 0x4},
    {0xE, 0xA, 0xE, 0xA},
    {0xE, 0xA, 0xE, 0x2}
};

typedef enum {
    LED_INITIALIZE,
    LED_READY,
    LED_UPDATING
} LEDState_t;

typedef enum {
    LED_PENDING_NONE,
    LED_PENDING_CLEAR,
    LED_PENDING_COUNTDOWN,
    LED_PENDING_CONGRATS,
    LED_PENDING_GO,
    LED_PENDING_NUMBER
} LEDPendingDisplay_t;

static uint8_t MyPriority;
static LEDState_t CurrentState = LED_INITIALIZE;
static LEDPendingDisplay_t PendingDisplay = LED_PENDING_NONE;

static unsigned char CharQueue[CHAR_QUEUE_SIZE];
static uint8_t CharQueueHead = 0;
static uint8_t CharQueueTail = 0;
static uint8_t CharQueueCount = 0;
static uint8_t PendingNumber;
static uint8_t PendingCountdown;

static bool PostNextLEDService(ES_EventType_t EventType);
static bool EnqueueChar(unsigned char NextChar);
static bool DrawCenteredText(const char *Text);
static bool DrawCenteredCountdown(uint8_t Remaining);
static bool DrawTimerQuadrants(uint8_t Number);
static bool BeginDisplayUpdate(void);
static void StartNextChar(void);
static void StartNextDisplayItem(void);

bool InitLEDService(uint8_t Priority)
{
    DB_printf("LED INIT FUNCTION ENTERED\r\n");

    ES_Event_t ThisEvent;
    MyPriority = Priority;
    
    ThisEvent.EventType = ES_LED_INIT_STEP;
    ThisEvent.EventParam = 0;

    return ES_PostToService(MyPriority, ThisEvent);
}

bool PostLEDService(ES_Event_t ThisEvent)
{
    return ES_PostToService(MyPriority, ThisEvent);
}

bool PostLEDDisplayNumber(uint8_t Number)
{
    ES_Event_t ThisEvent;

    if (Number > 99U) {
        return false;
    }

    ThisEvent.EventType = ES_LED_SET_NUMBER;
    ThisEvent.EventParam = Number;
    return PostLEDService(ThisEvent);
}

bool PostLEDCountdownStep(uint8_t Remaining)
{
    ES_Event_t ThisEvent;

    if ((Remaining == 0U) || (Remaining > 3U)) {
        return false;
    }

    ThisEvent.EventType = ES_LED_COUNTDOWN_STEP;
    ThisEvent.EventParam = Remaining;
    return PostLEDService(ThisEvent);
}

bool PostLEDDisplayGo(void)
{
    ES_Event_t ThisEvent;

    ThisEvent.EventType = ES_LED_SHOW_GO;
    ThisEvent.EventParam = 0;
    return PostLEDService(ThisEvent);
}

bool PostLEDClearDisplay(void)
{
    ES_Event_t ThisEvent;

    ThisEvent.EventType = ES_LED_CLEAR_DISPLAY;
    ThisEvent.EventParam = 0;
    return PostLEDService(ThisEvent);
}

bool PostLEDShowCongrats(bool Visible)
{
    ES_Event_t ThisEvent;

    if (!Visible) {
        return PostLEDClearDisplay();
    }

    ThisEvent.EventType = ES_LED_SHOW_CONGRATS;
    ThisEvent.EventParam = 0;
    return PostLEDService(ThisEvent);
}


ES_Event_t RunLEDService(ES_Event_t ThisEvent)
{
    ES_Event_t ReturnEvent;
    ReturnEvent.EventType = ES_NO_EVENT;
    ReturnEvent.EventParam = 0;

    switch (ThisEvent.EventType)
    {
        case ES_LED_INIT_STEP:{
            // Handle LED initialization step event
            DB_printf("\r\nLED SERVICE INITIALIZING\r\n");
            if (CurrentState == LED_INITIALIZE) {
                bool InitComplete = DM_TakeInitDisplayStep();
                DB_printf("DISPLAY INIT COMPLETE: %d\r\n", InitComplete);
                if (InitComplete) {
                    DB_printf("DISPLAY INITIALIZATION FINISHED\r\n");
                    CurrentState = LED_READY;
                    StartNextDisplayItem();
                }
                else {
                    (void)PostNextLEDService(ES_LED_INIT_STEP);
                }
            }
            break;
        }

        case ES_LED_ADD_CHAR:{
            unsigned char NewChar = (unsigned char)ThisEvent.EventParam;

            if (!EnqueueChar(NewChar)) {
                DB_printf("LED character queue full\r\n");
            }

            // Only start processing if the display is idle
            if (CurrentState == LED_READY){
                StartNextDisplayItem();
            }

            break;
        }

        case ES_LED_SET_NUMBER:{
            if (ThisEvent.EventParam <= 99U) {
                PendingNumber = (uint8_t)ThisEvent.EventParam;
                PendingDisplay = LED_PENDING_NUMBER;

                if (CurrentState == LED_READY) {
                    StartNextDisplayItem();
                }
            }
            break;
        }

        case ES_LED_SHOW_CONGRATS:
            CharQueueHead = 0;
            CharQueueTail = 0;
            CharQueueCount = 0;
            PendingDisplay = LED_PENDING_CONGRATS;

            if (CurrentState == LED_READY) {
                StartNextDisplayItem();
            }
            break;

        case ES_LED_CLEAR_DISPLAY:
            CharQueueHead = 0;
            CharQueueTail = 0;
            CharQueueCount = 0;
            PendingDisplay = LED_PENDING_CLEAR;

            if (CurrentState == LED_READY) {
                StartNextDisplayItem();
            }
            break;

        case ES_LED_SHOW_GO:
            CharQueueHead = 0;
            CharQueueTail = 0;
            CharQueueCount = 0;
            PendingDisplay = LED_PENDING_GO;

            if (CurrentState == LED_READY) {
                StartNextDisplayItem();
            }
            break;

        case ES_LED_COUNTDOWN_STEP:
            if ((ThisEvent.EventParam > 0U) &&
                (ThisEvent.EventParam <= 3U))
            {
                CharQueueHead = 0;
                CharQueueTail = 0;
                CharQueueCount = 0;
                PendingCountdown = (uint8_t)ThisEvent.EventParam;
                PendingDisplay = LED_PENDING_COUNTDOWN;

                if (CurrentState == LED_READY) {
                    StartNextDisplayItem();
                }
            } else {
                DB_printf("LED countdown step must be between 1 and 3\r\n");
            }
            break;

        case ES_LED_UPDATE_STEP:{
            // DB_printf("\r\nLED SERVICE UPDATE STEP\r\n");

            if (CurrentState == LED_UPDATING){
                if (DM_TakeDisplayUpdateStep())
                {
                    CurrentState = LED_READY;
                    StartNextDisplayItem();
                } else {
                    // Wait before processing the next display update
                    ES_Timer_InitTimer(SERVICE1_TIMER, 1);
                }
            }
            break;
        }

        case ES_TIMEOUT:
        {
            if (CurrentState == LED_UPDATING)
            {
                (void)PostNextLEDService(ES_LED_UPDATE_STEP);
            }
            break;
        }

        default:{
            // Handle other events
        }
        break;
    }


    return ReturnEvent;
}


static bool PostNextLEDService(ES_EventType_t EventType) {
    ES_Event_t NextEvent;
    NextEvent.EventType = EventType;
    NextEvent.EventParam = 0;
    return ES_PostToService(MyPriority, NextEvent);
}


static bool EnqueueChar(unsigned char NextChar) {
    if (CharQueueCount >= CHAR_QUEUE_SIZE) {
        return false;
    }

    CharQueue[CharQueueTail] = NextChar;
    CharQueueTail++;
    if (CharQueueTail >= CHAR_QUEUE_SIZE) {
        CharQueueTail = 0;
    }
    CharQueueCount++;
    return true;
}

static bool DrawCenteredText(const char *Text)
{
    uint8_t TextLength = 0;
    uint8_t LeftMargin;
    uint8_t Row;

    while ((Text[TextLength] != '\0') &&
           (TextLength < (DISPLAY_WIDTH_COLUMNS / FONT_CHAR_WIDTH_COLUMNS)))
    {
        TextLength++;
    }

    if ((Text[TextLength] != '\0') || (TextLength == 0U))
    {
        return false;
    }

    LeftMargin = (uint8_t)(
        (DISPLAY_WIDTH_COLUMNS - (TextLength * FONT_CHAR_WIDTH_COLUMNS)) / 2U);

    for (Row = 0; Row < 5U; Row++)
    {
        uint8_t TextIndex;
        uint32_t RowData = 0;

        for (TextIndex = 0; TextIndex < TextLength; TextIndex++)
        {
            RowData = (RowData << FONT_CHAR_WIDTH_COLUMNS) |
                      getFontLine((unsigned char)Text[TextIndex], Row);
        }

        RowData <<= LeftMargin;
        if (!DM_PutDataIntoBufferRow(RowData, (uint8_t)(Row + 2U)))
        {
            return false;
        }
    }

    return true;
}

static bool DrawCenteredCountdown(uint8_t Remaining)
{
    uint8_t FontRow;
    uint8_t ModuleShift;

    if ((Remaining == 0U) || (Remaining > 3U))
    {
        return false;
    }

    ModuleShift = (uint8_t)((Remaining * 8U) + 2U);
    for (FontRow = 0; FontRow < 5U; FontRow++)
    {
        uint8_t DisplayRow = (uint8_t)(FontRow + 2U);
        uint32_t RowData =
            (uint32_t)getFontLine((unsigned char)('0' + Remaining), FontRow)
            << ModuleShift;

        if ((Remaining == 1U) && (DisplayRow == COUNTDOWN_ELLIPSIS_ROW))
        {
            RowData |= COUNTDOWN_ELLIPSIS_BITS;
        }

        if (!DM_PutDataIntoBufferRow(RowData, DisplayRow))
        {
            return false;
        }
    }

    return true;
}


static void StartNextChar(void) {
    unsigned char NextChar;
    if ((CurrentState != LED_READY) || (CharQueueCount == 0U)) {
        return;
    }

    NextChar = CharQueue[CharQueueHead];
    if (!BeginDisplayUpdate()) {
        DB_printf("ERROR: Could not post LED character update\r\n");
        return;
    }

    CharQueueHead = (uint8_t)((CharQueueHead + 1U) % CHAR_QUEUE_SIZE);
    CharQueueCount--;
    DM_ScrollDisplayBuffer(SCROLL_COLS);
    DM_AddChar2DisplayBuffer(NextChar);
}

static bool BeginDisplayUpdate(void)
{
    CurrentState = LED_UPDATING;
    if (!PostNextLEDService(ES_LED_UPDATE_STEP)) {
        CurrentState = LED_READY;
        return false;
    }
    return true;
}

static void StartNextDisplayItem(void)
{
    if (CurrentState != LED_READY) {
        return;
    }

    if (PendingDisplay == LED_PENDING_CLEAR) {
        PendingDisplay = LED_PENDING_NONE;
        DM_ClearDisplayBuffer();
        if (!BeginDisplayUpdate()) {
            PendingDisplay = LED_PENDING_CLEAR;
            DB_printf("ERROR: Could not post LED display clear\r\n");
        }
        return;
    }

    if (PendingDisplay == LED_PENDING_COUNTDOWN) {
        PendingDisplay = LED_PENDING_NONE;
        DM_ClearDisplayBuffer();
        if (!DrawCenteredCountdown(PendingCountdown))
        {
            PendingDisplay = LED_PENDING_COUNTDOWN;
            DB_printf("ERROR: Could not draw LED countdown step\r\n");
            return;
        }

        if (!BeginDisplayUpdate()) {
            PendingDisplay = LED_PENDING_COUNTDOWN;
            DB_printf("ERROR: Could not post LED countdown update\r\n");
        }
        return;
    }

    if (PendingDisplay == LED_PENDING_CONGRATS) {
        PendingDisplay = LED_PENDING_NONE;
        DM_ClearDisplayBuffer();
        if (!DrawCenteredText("D O N E")) {
            DB_printf("ERROR: Could not draw congrats text\r\n");
            return;
        }

        if (!BeginDisplayUpdate()) {
            PendingDisplay = LED_PENDING_CONGRATS;
            DB_printf("ERROR: Could not post congrats display update\r\n");
        }
        return;
    }

    if (PendingDisplay == LED_PENDING_GO) {
        PendingDisplay = LED_PENDING_NONE;
        DM_ClearDisplayBuffer();
        if (!DrawCenteredText("G O")) {
            DB_printf("ERROR: Could not draw GO text\r\n");
            return;
        }

        if (!BeginDisplayUpdate()) {
            PendingDisplay = LED_PENDING_GO;
            DB_printf("ERROR: Could not post GO display update\r\n");
        }
        return;
    }

    if (PendingDisplay == LED_PENDING_NUMBER) {
        PendingDisplay = LED_PENDING_NONE;
        DM_ClearDisplayBuffer();

        if (!DrawTimerQuadrants(PendingNumber)) {
            PendingDisplay = LED_PENDING_NUMBER;
            DB_printf("ERROR: Could not draw game timer\r\n");
            return;
        }

        if (!BeginDisplayUpdate()) {
            PendingDisplay = LED_PENDING_NUMBER;
            DB_printf("ERROR: Could not post LED display update\r\n");
        }
        return;
    }

    StartNextChar();
}

static bool DrawTimerQuadrants(uint8_t Number)
{
    uint8_t DigitRow;
    uint8_t Tens = (uint8_t)(Number / 10U);
    uint8_t Ones = (uint8_t)(Number % 10U);

    if (Number > 99U) {
        return false;
    }

    for (DigitRow = 0; DigitRow < 4U; DigitRow++)
    {
        uint32_t RowData =
            (uint32_t)TimerDigitRows[Tens][DigitRow] << 28U;
        if (!DM_PutDataIntoBufferRow(RowData, DigitRow))
        {
            return false;
        }
    }

    for (DigitRow = 0; DigitRow < 4U; DigitRow++)
    {
        uint32_t RowData =
            (uint32_t)TimerDigitRows[Ones][DigitRow] << 24U;
        if (!DM_PutDataIntoBufferRow(RowData, (uint8_t)(DigitRow + 4U)))
        {
            return false;
        }
    }

    return true;
}
