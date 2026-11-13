
#include "ServoService.h"
#include "ES_Framework.h"
#include "ES_Configure.h"
#include "ES_Timers.h"
#include "dbprintf.h"

// Store the priority assigned to this service
static uint8_t MyPriority;

/****************************************************************************
 Function: InitServoService
 Purpose: Initialize the Servo Service
****************************************************************************/
bool InitServoService(uint8_t Priority)
{
    MyPriority = Priority;

    ES_Event_t InitialEvent;
    InitialEvent.EventType = ES_INIT;
    InitialEvent.EventParam = 0;

    return PostServoService(InitialEvent);
}

/****************************************************************************
 Function: PostServoService
 Purpose: Add an event to the Servo Service queue
****************************************************************************/
bool PostServoService(ES_Event_t ThisEvent)
{
    return ES_PostToService(MyPriority, ThisEvent);
}

/****************************************************************************
 Function: RunServoService
 Purpose: Process events sent to the Servo Service
****************************************************************************/
ES_Event_t RunServoService(ES_Event_t ThisEvent)
{
    ES_Event_t ReturnEvent;
    ReturnEvent.EventType = ES_NO_EVENT;
    ReturnEvent.EventParam = 0;

    switch (ThisEvent.EventType)
    {
        case ES_INIT:
            DB_printf("Servo Service Initialized\r\n");
            break;

        case ES_SERVO_CARROT_OPEN:
            DB_printf("Carrot Open\r\n");
            break;

        case ES_SERVO_CARROT_CLOSE:
            DB_printf("Carrot Close\r\n");
            break;

        case ES_SERVO_SOUP_SPEED:
            DB_printf("Soup Speed: %d\r\n",
                      ThisEvent.EventParam);
            break;

        case ES_SERVO_SOUP_STOP:
            DB_printf("Soup Stop\r\n");
            break;

        case ES_SERVO_DISPENSE_START:
            DB_printf("Dispense Start\r\n");
            break;

        case ES_SERVO_DISPENSE_STOP:
            DB_printf("Dispense Stop\r\n");
            break;

        default:
            break;
    }

    return ReturnEvent;
}
