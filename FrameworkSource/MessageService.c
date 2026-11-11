#include "MessageService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "LEDService.h"
#include "dbprintf.h"

#include <stdint.h>
#include <stdbool.h>

#define MESSAGE_TIMER       SERVICE2_TIMER
#define MESSAGE_INTERVAL    1000

static const unsigned char Message[] =
    "THIS IS THE 50 PLUS CHARACTER LONG MESSAGE. LOREM IPSUM.";

static uint8_t MyPriority;
static uint16_t MessageIndex = 0;

static void SendNextCharacter(void);

bool InitMessageService(uint8_t Priority){
    ES_Event_t ThisEvent;

    MyPriority = Priority;

    MessageIndex = 0;

    ThisEvent.EventType = ES_MESSAGE_START;
    ThisEvent.EventParam = 0;

    return ES_PostToService(MyPriority, ThisEvent);
}


bool PostMessageService(ES_Event_t ThisEvent){
    return ES_PostToService(MyPriority, ThisEvent);
}


ES_Event_t RunMessageService(ES_Event_t ThisEvent){
    ES_Event_t ReturnEvent;

    ReturnEvent.EventType = ES_NO_EVENT;
    ReturnEvent.EventParam = 0;

    switch (ThisEvent.EventType){

        case ES_MESSAGE_START:{
            DB_printf("MESSAGE SERVICE STARTED\r\n");
            ES_Timer_InitTimer(MESSAGE_TIMER, MESSAGE_INTERVAL);
            break;
        }

        case ES_TIMEOUT:{
            SendNextCharacter();
            break;
        }

        default:{
            break;
        }
    }

    return ReturnEvent;
}

static void SendNextCharacter(void)
{
    ES_Event_t LEDEvent;
    if (MessageIndex >= (sizeof(Message) - 1)){
        DB_printf("MESSAGE TRANSMISSION COMPLETE\r\n");
        return;
    }

    LEDEvent.EventType = ES_LED_ADD_CHAR;
    LEDEvent.EventParam = Message[MessageIndex];

    if (PostLEDService(LEDEvent)){
        DB_printf("MESSAGE CHARACTER SENT: %c\r\n",
                  Message[MessageIndex]);

        MessageIndex++;

        if (MessageIndex < (sizeof(Message) - 1)){
            ES_Timer_InitTimer(MESSAGE_TIMER, MESSAGE_INTERVAL);
        }
    } else {
        DB_printf("ERROR: LED SERVICE QUEUE FULL\r\n");

        ES_Timer_InitTimer(MESSAGE_TIMER, MESSAGE_INTERVAL);
    }
}
