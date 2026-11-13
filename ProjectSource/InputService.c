/****************************************************************************
 Module
     InputService.c

 Description
     Handles all user input hardware for Karl's Kitchen.

     Inputs:
       RB2  - Carrot Button
       RB3  - Start Button
       RB12 - Potentiometer (AN12)
       RB13 - Reflective IR Sensor (AN11)

     Sampling:
       Digital buttons: 10 ms polling with software debounce
       Analog sensors: ADC auto-scan

****************************************************************************/

#include "InputService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Events.h"

#include "PIC32_AD_Lib.h"
#include "GameService.h"

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>


/****************************************************************************
   Module Defines
****************************************************************************/

// Input sampling period
#define INPUT_POLL_PERIOD       10

// Button debounce
#define DEBOUNCE_SAMPLES        3

// Analog change thresholds
#define POT_CHANGE_THRESHOLD    8
#define IR_CHANGE_THRESHOLD     8

// ADC channels
#define IR_ADC_CHANNEL          11
#define POT_ADC_CHANNEL         12

// ADC scan mask
#define INPUT_ADC_MASK          ((1u << IR_ADC_CHANNEL) | \
                                 (1u << POT_ADC_CHANNEL))


/****************************************************************************
   Module Variables
 ****************************************************************************/

static uint8_t MyPriority;

// Latest analog readings
static uint16_t PotValue = 0;
static uint16_t IRValue = 0;

static uint16_t LastReportedPotValue = 0;
static uint16_t LastReportedIRValue = 0;

static bool ADCInitialized = false;

// Button state
typedef struct
{
    bool StablePressed;
    bool CandidatePressed;
    uint8_t DebounceCount;

} ButtonState_t;

static ButtonState_t StartButton;
static ButtonState_t CarrotButton;


/****************************************************************************
   Private Function Prototypes
 ****************************************************************************/

static bool InitInputHardware(void);

static void InitializeButtonStates(void);

static void PollInputs(void);
static void PollButtons(void);
static void PollAnalogInputs(void);

static bool ReadStartButton(void);
static bool ReadCarrotButton(void);

static void DebounceButton(
    ButtonState_t *Button,
    bool RawPressed,
    ES_EventType_t PressEvent,
    ES_EventType_t ReleaseEvent
);

static void PostInputEvent(
    ES_EventType_t EventType,
    uint16_t EventParam
);

static bool DifferenceExceeds(
    uint16_t NewValue,
    uint16_t OldValue,
    uint16_t Threshold
);

/****************************************************************************
 Function
     InitInputService

 Description
     Initializes the input service
****************************************************************************/

bool InitInputService(uint8_t Priority)
{
    ES_Event_t ThisEvent;

    MyPriority = Priority;

    ThisEvent.EventType = ES_INIT;
    ThisEvent.EventParam = 0;

    return ES_PostToService(MyPriority, ThisEvent);
}

/****************************************************************************
 Function
     PostInputService

 Description
     Posts an event to InputService
****************************************************************************/

bool PostInputService(ES_Event_t ThisEvent)
{
    return ES_PostToService(MyPriority, ThisEvent);
}


/****************************************************************************
 Function
     RunInputService

 Description
     Processes input events and periodically samples hardware
****************************************************************************/

ES_Event_t RunInputService(ES_Event_t ThisEvent)
{
    ES_Event_t ReturnEvent;

    ReturnEvent.EventType = ES_NO_EVENT;
    ReturnEvent.EventParam = 0;

    switch (ThisEvent.EventType)
    {
        case ES_INIT:

            // Initialize all hardware
            if (InitInputHardware())
            {
                ADCInitialized = true;
                InitializeButtonStates();
                // Start periodic input polling
                ES_Timer_InitTimer(
                    INPUT_POLL_TIMER,
                    INPUT_POLL_PERIOD
                );
            }
            break;

        case ES_TIMEOUT:
            // Only respond to our own polling timer
            if (ThisEvent.EventParam == INPUT_POLL_TIMER)
            {
                if (ADCInitialized)
                {
                    PollInputs();
                }

                // Restart polling timer
                ES_Timer_InitTimer(
                    INPUT_POLL_TIMER,
                    INPUT_POLL_PERIOD
                );
            }
            break;

        default:
            break;
    }

    return ReturnEvent;
}


/****************************************************************************
 Function
     InitInputHardware

 Description
     Initializes GPIO and ADC hardware
****************************************************************************/

static bool InitInputHardware(void)
{
    // Enable analog functionality on AN11 and AN12
    ANSELBbits.ANSB13 = 1;      // AN11 = IR sensor <--> RB13
    ANSELBbits.ANSB12 = 1;      // AN12 = Potentiometer <--> RB12

    // Configure button pins as digital inputs
    TRISBbits.TRISB2 = 1;       // Carrot button
    TRISBbits.TRISB3 = 1;       // Start button

    // Configure analog sensor pins as inputs
    TRISBbits.TRISB12 = 1;      // Potentiometer
    TRISBbits.TRISB13 = 1;      // IR sensor

    // Configure ADC to scan AN11 and AN12
    if (!ADC_ConfigAutoScan(INPUT_ADC_MASK))
    {
        return false;
    }
    return true;
}

/****************************************************************************
 Function
     InitializeButtonStates

 Description
     Reads initial button states so that a button held during startup
     does not generate a false press event
****************************************************************************/

static void InitializeButtonStates(void)
{
    StartButton.StablePressed = ReadStartButton();
    StartButton.CandidatePressed = StartButton.StablePressed;
    StartButton.DebounceCount = 0;

    CarrotButton.StablePressed = ReadCarrotButton();
    CarrotButton.CandidatePressed = CarrotButton.StablePressed;
    CarrotButton.DebounceCount = 0;
}


/****************************************************************************
 Function
     PollInputs

 Description
     Samples all input hardware
****************************************************************************/

static void PollInputs(void)
{
    PollButtons();
    PollAnalogInputs();
}

/****************************************************************************
 Function
     PollButtons

 Description
     Reads and debounces both arcade buttons
****************************************************************************/

static void PollButtons(void)
{
    DebounceButton(
        &StartButton,
        ReadStartButton(),
        ES_INPUT_START_PRESSED,
        ES_INPUT_START_RELEASED
    );

    DebounceButton(
        &CarrotButton,
        ReadCarrotButton(),
        ES_INPUT_CARROT_PRESSED,
        ES_INPUT_CARROT_RELEASED
    );
}

/****************************************************************************
 Function
     ReadStartButton

 Returns
     true if pressed

 Description
     Start button is active LOW
****************************************************************************/

static bool ReadStartButton(void)
{
    return (PORTBbits.RB3 == 0);
}

/****************************************************************************
 Function
     ReadCarrotButton

 Returns
     true if pressed

 Description
     Carrot button is active LOW
****************************************************************************/

static bool ReadCarrotButton(void)
{
    return (PORTBbits.RB2 == 0);
}

/****************************************************************************
 Function
     DebounceButton

 Description
     Requires three consecutive 10 ms samples before accepting a
     button state transition
****************************************************************************/

static void DebounceButton(ButtonState_t *Button, bool RawPressed, ES_EventType_t PressEvent, ES_EventType_t ReleaseEvent)
{
    // No change from the stable state
    if (RawPressed == Button->StablePressed)
    {
        Button->CandidatePressed = RawPressed;
        Button->DebounceCount = 0;
        return;
    }

    // New candidate state detected
    if (RawPressed != Button->CandidatePressed)
    {
        Button->CandidatePressed = RawPressed;
        Button->DebounceCount = 1;
    } else
    {
        if (Button->DebounceCount < DEBOUNCE_SAMPLES)
        {
            Button->DebounceCount++;
        }
    }

    // Accept transition after enough consecutive samples
    if (Button->DebounceCount >= DEBOUNCE_SAMPLES)
    {
        Button->StablePressed = Button->CandidatePressed;
        Button->DebounceCount = 0;

        if (Button->StablePressed)
        {
            PostInputEvent(PressEvent, 1);
        } else
        {
            PostInputEvent(ReleaseEvent, 0);
        }
    }
}

/****************************************************************************
 Function
     PollAnalogInputs

 Description
     Reads the latest ADC conversion values

     ADC_MultiRead returns channels in ascending channel order:

       Results[0] = AN11 = IR sensor
       Results[1] = AN12 = Potentiometer
****************************************************************************/

static void PollAnalogInputs(void)
{
    uint32_t ADCResults[2];
    ADC_MultiRead(ADCResults);

    // AN11: IR sensor
    IRValue = (uint16_t)ADCResults[0];
    // AN12: Potentiometer
    PotValue = (uint16_t)ADCResults[1];
    // First poll establishes baseline
    static bool FirstAnalogRead = true;

    if (FirstAnalogRead)
    {
        LastReportedPotValue = PotValue;
        LastReportedIRValue = IRValue;

        FirstAnalogRead = false;
        return;
    }

    // Detect potentiometer changes
    if (DifferenceExceeds(PotValue, LastReportedPotValue, POT_CHANGE_THRESHOLD))
    {
        LastReportedPotValue = PotValue;
        PostInputEvent(ES_INPUT_POT_CHANGED, PotValue);
    }

    // Detect IR sensor changes
    if (DifferenceExceeds(IRValue, LastReportedIRValue, IR_CHANGE_THRESHOLD))
    {
        LastReportedIRValue = IRValue;
        PostInputEvent(ES_INPUT_IR_CHANGED, IRValue);
    }
}

/****************************************************************************
 Function
     DifferenceExceeds

 Description
     Returns true when the difference between two values exceeds
     the specified threshold.
****************************************************************************/

static bool DifferenceExceeds(uint16_t NewValue, uint16_t OldValue, uint16_t Threshold)
{
    if (NewValue >= OldValue)
    {
        return ((NewValue - OldValue) >= Threshold);
    } else
    {
        return ((OldValue - NewValue) >= Threshold);
    }
}


/****************************************************************************
 Function
     PostInputEvent

 Description
     Sends input events to GameService.
****************************************************************************/

static void PostInputEvent(ES_EventType_t EventType, uint16_t EventParam)
{
    ES_Event_t NewEvent;

    NewEvent.EventType = EventType;
    NewEvent.EventParam = EventParam;

    (void)PostGameService(NewEvent);
}

/****************************************************************************
 Public Query Functions
****************************************************************************/

uint16_t QueryPotValue(void)
{
    return PotValue;
}

uint16_t QueryIRValue(void)
{
    return IRValue;
}

bool QueryStartButton(void)
{
    return StartButton.StablePressed;
}

bool QueryCarrotButton(void)
{
    return CarrotButton.StablePressed;
}
