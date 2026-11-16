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
#define PROGRESS_ROW_TOP        6U   // bottom 2 rows of the matrix
#define PROGRESS_ROW_BOTTOM     7U
#define PROGRESS_COLS_PER_TASK  4U
#define PROGRESS_MAX_TASKS      (DISPLAY_WIDTH_COLUMNS / PROGRESS_COLS_PER_TASK)

typedef enum {
    LED_INITIALIZE,
    LED_READY,
    LED_UPDATING
} LEDState_t;

static uint8_t MyPriority;
static LEDState_t CurrentState = LED_INITIALIZE;

static unsigned char CharQueue[CHAR_QUEUE_SIZE];
static uint8_t CharQueueHead = 0;
static uint8_t CharQueueTail = 0;
static uint8_t CharQueueCount = 0;
static uint8_t PendingNumber;
static uint8_t PendingProgress;
static uint8_t PendingCountdown;
static bool NumberPending = false;
static bool CountdownPending = false;
static bool CongratsPending = false;
static bool GoPending = false;
static bool LossPending = false;
static bool ClearPending = false;

static bool PostNextLEDService(ES_EventType_t EventType);
static bool EnqueueChar(unsigned char NextChar);
static bool DrawCenteredText(const char *Text);
static bool DrawCenteredCountdown(uint8_t Remaining);
static void DrawProgressBar(uint8_t TasksComplete);
static bool DequeueChar(unsigned char *NextChar);
static void StartNextChar(void);
static void StartNextDisplayItem(void);

static uint16_t InitCallCount = 0;

// ES_LED_INIT_STEP,
// ES_LED_ADD_CHAR,
// ES_LED_UPDATE_STEP,
// ES_LED_SET_NUMBER,

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
    return PostLEDDisplayTimer(Number, 0U);
}

bool PostLEDDisplayTimer(uint8_t Number, uint8_t TasksComplete)
{
    ES_Event_t ThisEvent;

    if (Number > 99U) {
        return false;
    }
    if (TasksComplete > PROGRESS_MAX_TASKS) {
        TasksComplete = PROGRESS_MAX_TASKS;
    }

    // Low byte: number to show, high byte: completed task count
    ThisEvent.EventType = ES_LED_SET_NUMBER;
    ThisEvent.EventParam = (uint16_t)(((uint16_t)TasksComplete << 8) | Number);
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

bool PostLEDDisplayLoss(void)
{
    ES_Event_t ThisEvent;

    ThisEvent.EventType = ES_LED_SHOW_LOSS;
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

            // Always enqueue the character, even while scrolling
            if (CharQueueCount < CHAR_QUEUE_SIZE){
                CharQueue[CharQueueTail] = NewChar;
                CharQueueTail = (CharQueueTail + 1) % CHAR_QUEUE_SIZE;
                CharQueueCount++;
            } else {
                DB_printf("LED character queue full\r\n");
            }

            // Only start processing if the display is idle
            if (CurrentState == LED_READY){
                StartNextDisplayItem();
            }

            break;
        }

        case ES_LED_SET_NUMBER:{
            uint8_t Number = (uint8_t)(ThisEvent.EventParam & 0xFFU);
            if (Number <= 99U) {
                PendingNumber = Number;
                PendingProgress = (uint8_t)(ThisEvent.EventParam >> 8);
                NumberPending = true;
                CountdownPending = false;
                GoPending = false;
                CongratsPending = false;
                ClearPending = false;
                LossPending = false;

                if (CurrentState == LED_READY) {
                    StartNextDisplayItem();
                }
            } else {
                DB_printf("LED number must be between 0 and 99\r\n");
            }
            break;
        }

        case ES_LED_SHOW_CONGRATS:
            CharQueueHead = 0;
            CharQueueTail = 0;
            CharQueueCount = 0;
            NumberPending = false;
            CountdownPending = false;
            ClearPending = false;
            GoPending = false;
            CongratsPending = true;
            LossPending = false;

            if (CurrentState == LED_READY) {
                StartNextDisplayItem();
            }
            break;

        case ES_LED_CLEAR_DISPLAY:
            CharQueueHead = 0;
            CharQueueTail = 0;
            CharQueueCount = 0;
            NumberPending = false;
            CountdownPending = false;
            GoPending = false;
            CongratsPending = false;
            ClearPending = true;
            LossPending = false;

            if (CurrentState == LED_READY) {
                StartNextDisplayItem();
            }
            break;

        case ES_LED_SHOW_GO:
            CharQueueHead = 0;
            CharQueueTail = 0;
            CharQueueCount = 0;
            NumberPending = false;
            CountdownPending = false;
            CongratsPending = false;
            ClearPending = false;
            GoPending = true;
            LossPending = false;

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
                CountdownPending = true;
                NumberPending = false;
                CongratsPending = false;
                GoPending = false;
                ClearPending = false;
                LossPending = false;

                if (CurrentState == LED_READY) {
                    StartNextDisplayItem();
                }
            } else {
                DB_printf("LED countdown step must be between 1 and 3\r\n");
            }
            break;

        case ES_LED_SHOW_LOSS:
            CharQueueHead = 0;
            CharQueueTail = 0;
            CharQueueCount = 0;
            NumberPending = false;
            CountdownPending = false;
            CongratsPending = false;
            ClearPending = false;
            GoPending = false;
            LossPending = true;

            if (CurrentState == LED_READY) {
                StartNextDisplayItem();
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


// Fills the bottom 2 rows from the left, 4 columns per completed task
static void DrawProgressBar(uint8_t TasksComplete)
{
    uint32_t RowData = 0U;

    if (TasksComplete > PROGRESS_MAX_TASKS)
    {
        TasksComplete = PROGRESS_MAX_TASKS;
    }

    if (TasksComplete > 0U)
    {
        // Bit 31 is the left-most column
        RowData = 0xFFFFFFFFUL <<
            (DISPLAY_WIDTH_COLUMNS - (TasksComplete * PROGRESS_COLS_PER_TASK));
    }

    (void)DM_PutDataIntoBufferRow(RowData, PROGRESS_ROW_TOP);
    (void)DM_PutDataIntoBufferRow(RowData, PROGRESS_ROW_BOTTOM);
}

static bool DequeueChar(unsigned char *NextChar) {
    if ((CharQueueCount == 0) || (NextChar == NULL)) {
        return false;
    }

    *NextChar = CharQueue[CharQueueHead];

    CharQueueHead++;
    if (CharQueueHead >= CHAR_QUEUE_SIZE) {
        CharQueueHead = 0;
    }
    CharQueueCount--;
    return true;
}

static void StartNextChar(void) {
    unsigned char NextChar;
    if (CurrentState != LED_READY) {
        return;
    }
    if (DequeueChar(&NextChar)) {
        DM_ScrollDisplayBuffer(SCROLL_COLS);
        DM_AddChar2DisplayBuffer(NextChar);

        CurrentState = LED_UPDATING;

        (void)PostNextLEDService((ES_EventType_t)ES_LED_UPDATE_STEP);
    }
}

static void StartNextDisplayItem(void)
{
    if (CurrentState != LED_READY) {
        return;
    }

    if (ClearPending) {
        ClearPending = false;
        DM_ClearDisplayBuffer();
        CurrentState = LED_UPDATING;
        if (!PostNextLEDService(ES_LED_UPDATE_STEP)) {
            CurrentState = LED_READY;
            ClearPending = true;
            DB_printf("ERROR: Could not post LED display clear\r\n");
        }
        return;
    }

    if (CountdownPending) {
        CountdownPending = false;
        DM_ClearDisplayBuffer();
        if (!DrawCenteredCountdown(PendingCountdown))
        {
            CountdownPending = true;
            DB_printf("ERROR: Could not draw LED countdown step\r\n");
            return;
        }

        CurrentState = LED_UPDATING;
        if (!PostNextLEDService(ES_LED_UPDATE_STEP)) {
            CurrentState = LED_READY;
            CountdownPending = true;
            DB_printf("ERROR: Could not post LED countdown update\r\n");
        }
        return;
    }

    if (CongratsPending) {
        CongratsPending = false;
        DM_ClearDisplayBuffer();
        if (!DrawCenteredText("D O N E")) {
            DB_printf("ERROR: Could not draw congrats text\r\n");
            return;
        }

        CurrentState = LED_UPDATING;
        if (!PostNextLEDService(ES_LED_UPDATE_STEP)) {
            CurrentState = LED_READY;
            CongratsPending = true;
            DB_printf("ERROR: Could not post congrats display update\r\n");
        }
        return;
    }

    if (GoPending) {
        GoPending = false;
        DM_ClearDisplayBuffer();
        if (!DrawCenteredText("G O")) {
            DB_printf("ERROR: Could not draw GO text\r\n");
            return;
        }

        CurrentState = LED_UPDATING;
        if (!PostNextLEDService(ES_LED_UPDATE_STEP)) {
            CurrentState = LED_READY;
            GoPending = true;
            DB_printf("ERROR: Could not post GO display update\r\n");
        }
        return;
    }
    if(LossPending) {
        LossPending = false;
        DM_ClearDisplayBuffer();
        if (!DrawCenteredText("L O S T")) {
            DB_printf("ERROR: Could not draw LOSS text\r\n");
            return;
        }

        CurrentState = LED_UPDATING;
        if (!PostNextLEDService(ES_LED_UPDATE_STEP)) {
            CurrentState = LED_READY;
            LossPending = true;
            DB_printf("ERROR: Could not post LOSS display update\r\n");
        }
        return;
    }

    if (NumberPending) {
        NumberPending = false;
        DM_ClearDisplayBuffer();

        if (PendingNumber >= 10U) {
            DM_AddChar2DisplayBuffer((unsigned char)('0' + (PendingNumber / 10U)));
            DM_ScrollDisplayBuffer(8U);
        }
        DM_AddChar2DisplayBuffer((unsigned char)('0' + (PendingNumber % 10U)));
        DrawProgressBar(PendingProgress);

        CurrentState = LED_UPDATING;
        if (!PostNextLEDService(ES_LED_UPDATE_STEP)) {
            CurrentState = LED_READY;
            NumberPending = true;
            DB_printf("ERROR: Could not post LED display update\r\n");
        }
        return;
    }

    StartNextChar();
}
