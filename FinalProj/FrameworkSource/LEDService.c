#include "LEDService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "dbprintf.h"
#include "DM_Display.h"

#include <stdint.h>
#include <stdbool.h>

#define SCROLL_COLS     4
#define CHAR_QUEUE_SIZE 8

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

static bool PostNextLEDService(ES_EventType_t EventType);
static bool EnqueueChar(unsigned char NextChar);
static bool DequeueChar(unsigned char *NextChar);
static void StartNextChar(void);

static uint16_t InitCallCount = 0;

// ES_LED_INIT_STEP,
// ES_LED_ADD_CHAR,
// ES_LED_UPDATE_STEP,

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
                    // (void)EnqueueChar('A');
                    StartNextChar();
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
            if ((CurrentState == LED_READY) && (CharQueueCount > 0)){
                StartNextChar();
            }

            break;
        }

        case ES_LED_UPDATE_STEP:{
            DB_printf("\r\nLED SERVICE UPDATE STEP\r\n");

            if (CurrentState == LED_UPDATING){
                if (DM_TakeDisplayUpdateStep())
                {
                    CurrentState = LED_READY;
                    StartNextChar();
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
