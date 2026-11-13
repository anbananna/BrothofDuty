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
// Set to 1 to drive the servos from Tera Term keys 1-6 for bench testing
#define SERVO_KEY_TEST          1

// Set to 1 to run every servo for a few seconds at power-up (debug)
#define SERVO_STARTUP_TEST      1
#define SERVO_TEST_DURATION     3000    // ms
#define SERVO_TEST_PRINT_DELAY  500     // ms before printing registers

// PWM channel numbers (OCx)
#define DISPENSE_CHANNEL        2
#define SOUP_CHANNEL            3
#define CARROT_CHANNEL          4
#define NUM_PWM_CHANNELS        4   // library configures channels 1..N

#define SERVO_FREQ_HZ           50

// Pulse widths in 0.4 us ticks
#define TICKS_PER_MS            2500
#define PULSE_CENTER            3750    // 1.5 ms

// Continuous servos: 1.5 ms is stop, +/- 0.5 ms is full speed either way
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
#if SERVO_STARTUP_TEST
            // Debug: carrot to 180, both continuous servos running
            DB_printf("SERVO TEST: carrot 180, soup + dispense running\r\n");
            SetCarrotAngle(CARROT_OPEN_DEG);
            SetContinuousSpeed(SOUP_CHANNEL, 50);
            SetContinuousSpeed(DISPENSE_CHANNEL, DISPENSE_SPEED);
            // Print registers shortly after, once startup messages have drained
            ServoTestRegsPrinted = false;
            ES_Timer_InitTimer(SERVO_TEST_TIMER, SERVO_TEST_PRINT_DELAY);
#endif
            break;

#if SERVO_STARTUP_TEST
        case ES_TIMEOUT:
            if ((ThisEvent.EventParam == SERVO_TEST_TIMER) && !ServoTestRegsPrinted)
            {
                ServoTestRegsPrinted = true;
                DB_printf("OC2RS=%u OC3RS=%u OC4RS=%u\r\n",
                          (uint16_t)OC2RS, (uint16_t)OC3RS, (uint16_t)OC4RS);
                DB_printf("PR2=%u T2ON=%u OC2ON=%u OC3ON=%u OC4ON=%u\r\n",
                          (uint16_t)PR2, (uint16_t)T2CONbits.ON,
                          (uint16_t)OC2CONbits.ON, (uint16_t)OC3CONbits.ON,
                          (uint16_t)OC4CONbits.ON);
                ES_Timer_InitTimer(SERVO_TEST_TIMER,
                                   SERVO_TEST_DURATION - SERVO_TEST_PRINT_DELAY);
            }
            else if (ThisEvent.EventParam == SERVO_TEST_TIMER)
            {
                DB_printf("SERVO TEST: done, carrot 0, all stopped\r\n");
                SetCarrotAngle(CARROT_CLOSED_DEG);
                StopContinuous(SOUP_CHANNEL);
                StopContinuous(DISPENSE_CHANNEL);
            }
            break;
#endif

        case ES_SERVO_CARROT_OPEN:
            DB_printf("Carrot Open\r\n");
            SetCarrotAngle(CARROT_OPEN_DEG);
            break;

        case ES_SERVO_CARROT_CLOSE:
            DB_printf("Carrot Close\r\n");
            SetCarrotAngle(CARROT_CLOSED_DEG);
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

#if SERVO_KEY_TEST
        case ES_NEW_KEY:
        {
            ES_Event_t TestEvent;
            TestEvent.EventParam = 0;
            TestEvent.EventType = ES_NO_EVENT;

            switch ((char)ThisEvent.EventParam)
            {
                case '1': TestEvent.EventType = ES_SERVO_CARROT_OPEN;    break;
                case '2': TestEvent.EventType = ES_SERVO_CARROT_CLOSE;   break;
                case '3': TestEvent.EventType = ES_SERVO_SOUP_SPEED;
                          TestEvent.EventParam = 50;                     break;
                case '4': TestEvent.EventType = ES_SERVO_SOUP_STOP;      break;
                case '5': TestEvent.EventType = ES_SERVO_DISPENSE_START; break;
                case '6': TestEvent.EventType = ES_SERVO_DISPENSE_STOP;  break;
                default: break;
            }

            if (TestEvent.EventType != ES_NO_EVENT)
            {
                PostServoService(TestEvent);
            }
            break;
        }
#endif

        default:
            break;
    }

    return ReturnEvent;
}

/*************************** Private Functions ****************************/

static void InitServoHardware(void)
{
    // Pins as digital outputs (RA4, RB5, RB9 have no analog function,
    // but TRIS still needs clearing)
    TRISAbits.TRISA4 = 0;
    TRISBbits.TRISB5 = 0;
    TRISBbits.TRISB9 = 0;

    // Sets up Timer2/Timer3 at PBClk/8 and OC1..OC4 in PWM mode, 0% duty
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

// SpeedPercent: -100 (full reverse) .. 0 (stop) .. 100 (full forward)
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

// Stop by sending no pulses at all. A continuous servo with no signal stops,
// which avoids the creep you get from 1.5 ms if its center isn't trimmed.
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
