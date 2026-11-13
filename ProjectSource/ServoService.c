/****************************************************************************
 Module
     ServoService.c

 Description
     Drives the three servos on PWM (Output Compare) channels, all running
     off Timer2 at the standard 50 Hz servo rate.

       Servo      Type             Pin    PWM channel
       --------   --------------   ----   -----------
       Dispense   continuous       RB5    OC2
       Soup       continuous       RB9    OC3
       Carrot     180 deg          RA4    OC4

     Pulse widths are in PWM library ticks: 0.4 us each, 2500 ticks = 1 ms.

 Events handled
     ES_SERVO_CARROT_OPEN / ES_SERVO_CARROT_CLOSE
     ES_SERVO_SOUP_SPEED      EventParam = speed, -100..100 (cast int16_t)
     ES_SERVO_SOUP_STOP
     ES_SERVO_DISPENSE_START
     ES_SERVO_DISPENSE_STOP
****************************************************************************/

#include "ServoService.h"
#include "ES_Framework.h"
#include "ES_Configure.h"
#include "PWM_PIC32.h"
#include "dbprintf.h"

#include <xc.h>

/*----------------------------- Module Defines ----------------------------*/

// PWM channel numbers (OCx)
#define DISPENSE_CHANNEL        2
#define SOUP_CHANNEL            3
#define CARROT_CHANNEL          4
#define NUM_PWM_CHANNELS        4 

#define SERVO_FREQ_HZ           50

// Pulse widths in 0.4 us ticks
#define TICKS_PER_MS            2500
#define PULSE_CENTER            3750    // 1.5 ms

// Continuous servos, 1.5 ms is stop, +/- 0.5 ms is full speed either way
#define CONT_SPEED_SPAN         1250    // 0.5 ms
#define DISPENSE_SPEED          60      // percent, -100..100; flip sign to reverse

// Carrot (180 deg) servo positions. Many hobby servos use 0.5-2.5 ms for the
// full 180 deg; calibrate these on the real hardware.
#define CARROT_MIN_PULSE        1250    // 0.5 ms  ->   0 deg
#define CARROT_MAX_PULSE        6250    // 2.5 ms  -> 180 deg
#define CARROT_CLOSED_DEG       0
#define CARROT_OPEN_DEG         180

/*---------------------------- Module Functions ---------------------------*/
static void InitServoHardware(void);
static void SetContinuousSpeed(uint8_t Channel, int16_t SpeedPercent);
static void StopContinuous(uint8_t Channel);
static void SetCarrotAngle(uint8_t Degrees);
static void PrintPwmDebug(void);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static bool ServoTestRegsPrinted = false;

/****************************************************************************
 Function: InitServoService
****************************************************************************/
bool InitServoService(uint8_t Priority)
{
    ES_Event_t InitialEvent;

    MyPriority = Priority;

    InitServoHardware();

    InitialEvent.EventType = ES_INIT;
    InitialEvent.EventParam = 0;

    return ES_PostToService(MyPriority, InitialEvent);
}

/****************************************************************************
 Function: PostServoService
****************************************************************************/
bool PostServoService(ES_Event_t ThisEvent)
{
    return ES_PostToService(MyPriority, ThisEvent);
}

/****************************************************************************
 Function: RunServoService
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
            SetCarrotAngle(CARROT_OPEN_DEG);
            PrintPwmDebug();
            break;

        case ES_SERVO_CARROT_CLOSE:
            DB_printf("Carrot Close\r\n");
            SetCarrotAngle(CARROT_CLOSED_DEG);
            PrintPwmDebug();
            break;

        case ES_SERVO_SOUP_SPEED:
        {
            int16_t Speed = (int16_t)ThisEvent.EventParam;
            DB_printf("Soup Speed: %d\r\n", Speed);
            SetContinuousSpeed(SOUP_CHANNEL, Speed);
            break;
        }

        case ES_SERVO_SOUP_STOP:
            DB_printf("Soup Stop\r\n");
            StopContinuous(SOUP_CHANNEL);
            break;

        case ES_SERVO_DISPENSE_START:
            DB_printf("Dispense Start\r\n");
            SetContinuousSpeed(DISPENSE_CHANNEL, DISPENSE_SPEED);
            break;

        case ES_SERVO_DISPENSE_STOP:
            DB_printf("Dispense Stop\r\n");
            StopContinuous(DISPENSE_CHANNEL);
            break;

        default:
            break;
    }

    return ReturnEvent;
}

/*************************** Private Functions ****************************/

static void InitServoHardware(void)
{
    // digital outputs (RA4, RB5, RB9)
    TRISAbits.TRISA4 = 0;
    TRISBbits.TRISB5 = 0;
    TRISBbits.TRISB9 = 0;

    // Sets up Timer2/Timer3 at PBClk/8 and OC1-OC4 in PWM mode
    if (!PWMSetup_BasicConfig(NUM_PWM_CHANNELS))
    {
        DB_printf("Servo: PWM basic config FAILED\r\n");
    }

    PWMSetup_SetFreqOnTimer(SERVO_FREQ_HZ, _Timer2_);

    PWMSetup_AssignChannelToTimer(DISPENSE_CHANNEL, _Timer2_);
    PWMSetup_AssignChannelToTimer(SOUP_CHANNEL, _Timer2_);
    PWMSetup_AssignChannelToTimer(CARROT_CHANNEL, _Timer2_);

    if (!PWMSetup_MapChannelToOutputPin(DISPENSE_CHANNEL, PWM_RPB5) ||
        !PWMSetup_MapChannelToOutputPin(SOUP_CHANNEL, PWM_RPB9) ||
        !PWMSetup_MapChannelToOutputPin(CARROT_CHANNEL, PWM_RPA4))
    {
        DB_printf("Servo: PWM pin mapping FAILED\r\n");
    }

    // Safe startup state
    StopContinuous(DISPENSE_CHANNEL);
    StopContinuous(SOUP_CHANNEL);
    SetCarrotAngle(CARROT_CLOSED_DEG);
}

static void SetContinuousSpeed(uint8_t Channel, int16_t SpeedPercent)
{
    if (SpeedPercent > 100)
    {
        SpeedPercent = 100;
    }
    else if (SpeedPercent < -100)
    {
        SpeedPercent = -100;
    }

    if (SpeedPercent == 0)
    {
        StopContinuous(Channel);
        return;
    }

    int32_t Pulse = PULSE_CENTER +
                    ((int32_t)SpeedPercent * CONT_SPEED_SPAN) / 100;

    PWMOperate_SetPulseWidthOnChannel((uint16_t)Pulse, Channel);
}

// DEBUG: live PWM state for the carrot channel (OC4 on RA4).
// Two TMR2 reads should differ if Timer2 is running.
static void PrintPwmDebug(void)
{
    uint16_t Tmr1 = (uint16_t)TMR2;
    // Wait ~5 us (100 core-timer ticks); Timer2 should advance ~12 counts
    uint32_t Start = _CP0_GET_COUNT();
    while ((uint32_t)(_CP0_GET_COUNT() - Start) < 100)
    {
    }
    uint16_t Tmr2 = (uint16_t)TMR2;
    DB_printf("PWM: OC4RS=%u OC4CON=0x%x RPA4R=%u TRISA4=%u\r\n",
              (uint16_t)OC4RS, (uint16_t)OC4CON,
              (uint16_t)RPA4R, (uint16_t)TRISAbits.TRISA4);
    DB_printf("PWM: PR2=%u T2CON=0x%x TMR2=%u,%u\r\n",
              (uint16_t)PR2, (uint16_t)T2CON, Tmr1, Tmr2);
}

static void StopContinuous(uint8_t Channel)
{
    PWMOperate_SetDutyOnChannel(0, Channel);
}

static void SetCarrotAngle(uint8_t Degrees)
{
    if (Degrees > 180)
    {
        Degrees = 180;
    }

    uint32_t Pulse = CARROT_MIN_PULSE +
                     ((uint32_t)Degrees * (CARROT_MAX_PULSE - CARROT_MIN_PULSE)) / 180;

    PWMOperate_SetPulseWidthOnChannel((uint16_t)Pulse, CARROT_CHANNEL);
}
