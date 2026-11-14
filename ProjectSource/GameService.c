/****************************************************************************
 Module
     GameService.c

 Description
     Main game state machine for Karl's Kitchen.
****************************************************************************/

// Module header
#include "GameService.h"

// Framework
#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_DeferRecall.h"

// Hardware / other services
#include "LEDService.h"
#include "ES_Port.h"
#include "dbprintf.h"
#include "InputService.h"
#include "ServoService.h"
#include "NeoPixelService.h"

// C libraries
#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>


/*******************************
   Module Defines
 *******************************/
#define MAX_GAME_PROGRESS       15
#define GAME_DURATION           60000
#define INACTIVITY_DURATION     20000
#define INTERACTION_DURATION    2000
#define COUNTDOWN_DURATION      1000    // ms per countdown step
#define COUNTDOWN_SECONDS       3

// Carrot task
#define CARROT_PRESSES_MIN      2U
#define CARROT_PRESSES_MAX      5U
#define CARROT_BLINK_DURATION   500U
#define CARROT_LED              LATAbits.LATA3
#define CARROT_LED_TRIS         TRISAbits.TRISA3
#define LED_ON                  1
#define LED_OFF                 0

// Start button LED (the switch itself is on RB3)
#define START_LED               LATBbits.LATB10
#define START_LED_TRIS          TRISBbits.TRISB10

// Stir pot task target ranges across the 10-bit ADC's usable range
#define SOUP_STIR_SPEED         50      // percent, -100..100

// Heat coil task: hand must hold the IR (ITR20001, RB13) reading in a
// window. Closer hand -> more reflected light -> higher voltage.
// The window is relative to an ambient baseline taken when the task starts
// (no hand over the sensor), so room lighting cancels out.
// Window = baseline + selected rise target +/- HEAT_TOLERANCE_COUNTS.
// R8 = 100k. 1 count = ~3.2 mV.
#define HEAT_RISE_TARGET        150     // counts above ambient at the right height
#define HEAT_TOLERANCE_COUNTS   20
#define HEAT_TARGET_VARIANT_COUNT 3U
#define HEAT_TARGET_STEP_COUNTS   40U
// If the task starts with a hand already over the sensor, the baseline would
// be too high; cap it so the window stays reachable
#define HEAT_BASELINE_MAX       150
#define SENSOR_PRINT_PERIOD     500     // ms between IR readouts during the task
#define HEAT_SERIAL_READOUT     1       // set 0 to stop printing IR values

static const uint16_t HeatTargetRiseOptions[HEAT_TARGET_VARIANT_COUNT] =
{
    HEAT_RISE_TARGET - HEAT_TARGET_STEP_COUNTS,
    HEAT_RISE_TARGET,
    HEAT_RISE_TARGET + HEAT_TARGET_STEP_COUNTS
};

// Coil "glow" color for the NeoPixels (R, G, B)
#define COIL_RED                200
#define COIL_GREEN              0
#define COIL_BLUE               0

// Pause between finishing one task and starting the next
#define TASK_GAP_DURATION       2500


/*******************************
   State Definitions
 *******************************/
typedef enum
{
    GAME_INITIALIZING,
    GAME_WELCOME,
    GAME_COUNTDOWN,
    GAME_SELECT_TASK,
    GAME_CUT_CARROT,
    GAME_STIR_POT,
    GAME_HEAT_COIL,
    GAME_WINNING,
    GAME_LOSING,
    GAME_INACTIVE,
    GAME_TEST
} GameState_t;


/*******************************
   Task Definitions
 *******************************/
typedef enum
{
    TASK_CUT_CARROT = 0,
    TASK_STIR_POT,
    TASK_HEAT_COIL,
    TASK_COUNT
} GameTask_t;

typedef enum
{
    STIR_TARGET_LOW = 0,
    STIR_TARGET_MID,
    STIR_TARGET_HIGH,
    STIR_TARGET_COUNT
} StirTarget_t;

typedef struct
{
    uint16_t Low;
    uint16_t High;
} StirTargetRange_t;

static const StirTargetRange_t StirTargetRanges[STIR_TARGET_COUNT] =
{
    { 225U, 287U },
    { 481U, 543U },
    { 737U, 799U }
};


/*******************************
   Module Variables
 *******************************/
static uint8_t MyPriority;

static GameState_t CurrentState = GAME_INITIALIZING;
static GameTask_t CurrentTask;

static uint8_t GameProgress = 0;
static uint8_t GameTime = GAME_DURATION / 1000;
static uint8_t CountdownRemaining = 0;
static uint8_t LEDCountdownRemaining = 0;

static GameTask_t LastTask = TASK_COUNT;

static uint8_t TargetButtonPresses = 0;
static uint8_t DetectedButtonPresses = 0;
static uint8_t CarrotBlinksRemaining = 0;
static bool CarrotBlinkOn = false;
static bool CarrotInputEnabled = false;

static uint16_t DetectedPotValue = 0;
static StirTarget_t CurrentStirTarget;
static StirTarget_t LastStirTarget = STIR_TARGET_COUNT;
static bool SoupStirring = false;
static bool CoilHeating = false;
static uint16_t HeatBaseline = 0;       // ambient IR reading at task start

static uint16_t TargetHandValue = 0;
static uint16_t DetectedHandValue = 0;

static uint16_t TestCarrotPressCount = 0;
static uint16_t TestPotValue = 0;
static uint16_t TestIRValue = 0;

/*******************************
   Private Function Prototypes
 *******************************/
static void InitHWPins(void);
static void StartNewGame(void);
static void AssignNewTask(void);
static void UpdateGameProgress(void);
static void CompleteTask(void);
static void EnterWelcomeMode(void);
static void EnterCarrotTask(void);
static void StartCarrotBlinkSequence(void);
static void EnterStirTask(void);
static void UpdateSoupStirring(uint16_t PotReading);
static void EnterHeatTask(void);
static uint16_t HeatWindowLow(void);
static uint16_t HeatWindowHigh(void);
static void UpdateCoilHeating(uint16_t IRReading);
static void SetCoilGlow(bool On);
static void PostServoCommand(ES_EventType_t Command);

/*******************************
   Initialization
 *******************************/
bool InitGameService(uint8_t Priority)
{
    ES_Event_t ThisEvent;

    MyPriority = Priority;

    // Initialize game variables
    GameProgress = 0;
    GameTime = 60;

    // Initialize hardware pins
    InitHWPins();

    // Start in welcome state
    CurrentState = GAME_INITIALIZING;

    ThisEvent.EventType = ES_INIT;
    ThisEvent.EventParam = 0;

    return ES_PostToService(MyPriority, ThisEvent);
}

/*******************************
   Post Function
 *******************************/
bool PostGameService(ES_Event_t ThisEvent)
{
    return ES_PostToService(MyPriority, ThisEvent);
}

/*******************************
   Run Function
 *******************************/
ES_Event_t RunGameService(ES_Event_t ThisEvent)
{
    ES_Event_t ReturnEvent;

    DB_printf("GameService: State=%d, Event=%d, Param=%u\r\n",
          CurrentState,
          ThisEvent.EventType,
          ThisEvent.EventParam);

    ReturnEvent.EventType = ES_NO_EVENT;
    ReturnEvent.EventParam = 0;

    switch (CurrentState)
    {
        case GAME_INITIALIZING:
            if (ThisEvent.EventType == ES_INIT)
            {
                EnterWelcomeMode();
                CurrentState = GAME_WELCOME;
                DB_printf("GAME INITIALIZED\r\n");
            }
            break;

        case GAME_WELCOME:
            // DB_printf("In GAME WELCOME\r\n");
            switch (ThisEvent.EventType)
            {
                case ES_GAME_START:
                case ES_INPUT_START_PRESSED:
                    DB_printf("GAME STARTED\r\n");
                    START_LED = LED_OFF;
                    StartNewGame();

                    // Begin countdown
                    CountdownRemaining = COUNTDOWN_SECONDS;
                    DB_printf("COUNTDOWN: %u\r\n", CountdownRemaining);
                    ES_Timer_InitTimer(SEQUENCE_TIMER, COUNTDOWN_DURATION);
                    CurrentState = GAME_COUNTDOWN;
                    break;

                case ES_NEW_KEY:
                {
                    // Preserve the original key before any deferral modifications
                    unsigned char Key = (unsigned char)ThisEvent.EventParam;
                    DB_printf("ES_NEW_KEY received with -> %c <- in Service 0\r\n",
                            Key);

                    if (Key == 't')
                    {
                        CurrentState = GAME_TEST;

                        TestCarrotPressCount = 0;

                        TestPotValue = QueryPotValue();
                        TestIRValue = QueryIRValue();

                        DB_printf("\r\n");
                        DB_printf("============================\r\n");
                        DB_printf("       INPUT TEST MODE      \r\n");
                        DB_printf("============================\r\n");

                        DB_printf("Initial POT: %u\r\n", TestPotValue);
                        DB_printf("Initial IR: %u\r\n", TestIRValue);

                        DB_printf("NeoPixels: r/g/b/w = dim color, o = off\r\n");
                        DB_printf("Press X to exit test mode\r\n");
                    }
                    break;
                }

                default:
                    break;
            }
            break;

        case GAME_COUNTDOWN:
            if ((ThisEvent.EventType == ES_TIMEOUT) &&
                (ThisEvent.EventParam == SEQUENCE_TIMER))
            {
                CountdownRemaining--;

                if (CountdownRemaining > 0)
                {
                    DB_printf("COUNTDOWN: %u\r\n", CountdownRemaining);
                    ES_Timer_InitTimer(SEQUENCE_TIMER, COUNTDOWN_DURATION);
                }
                else
                {
                    DB_printf("GO!\r\n");
                    CurrentState = GAME_SELECT_TASK;

                    // Request first task
                    ES_Event_t NextEvent;

                    NextEvent.EventType = ES_ASSIGN_TASK;
                    NextEvent.EventParam = 0;

                    PostGameService(NextEvent);
                }
            }
            break;

        case GAME_SELECT_TASK:
            DB_printf("GAME SELECT TASK\r\n");
            // Pick a task on request, or when the pause after a task ends
            if ((ThisEvent.EventType == ES_ASSIGN_TASK) ||
                ((ThisEvent.EventType == ES_TIMEOUT) &&
                 (ThisEvent.EventParam == SEQUENCE_TIMER)))
            {
                AssignNewTask();
                switch (CurrentTask)
                {
                    case TASK_CUT_CARROT:
                        EnterCarrotTask();
                        CurrentState = GAME_CUT_CARROT;
                        break;
                    case TASK_STIR_POT:
                        EnterStirTask();
                        CurrentState = GAME_STIR_POT;
                        break;
                    case TASK_HEAT_COIL:
                        EnterHeatTask();
                        CurrentState = GAME_HEAT_COIL;
                        break;
                }
            }
            break;

        case GAME_CUT_CARROT:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_CARROT_PRESSED:
                    if (CarrotInputEnabled)
                    {
                        // Finish after the required presses and an interaction interval without a press.
                        ES_Timer_InitTimer(INTERACTION_TIMER, INTERACTION_DURATION);

                        if (DetectedButtonPresses <= TargetButtonPresses)
                        {
                            DetectedButtonPresses++;
                        }
                        DB_printf("CARROT CHOP %u / %u\r\n",
                                  DetectedButtonPresses,
                                  TargetButtonPresses);
                    }
                    break;

                case ES_TIMEOUT:
                    if ((ThisEvent.EventParam == SEQUENCE_TIMER) &&
                        !CarrotInputEnabled)
                    {
                        if (CarrotBlinkOn)
                        {
                            CARROT_LED = LED_OFF;
                            CarrotBlinkOn = false;
                            CarrotBlinksRemaining--;

                            if (CarrotBlinksRemaining == 0U)
                            {
                                CarrotInputEnabled = true;
                                DB_printf("BLINKING COMPLETE. Press the carrot button %u times.\r\n",
                                          TargetButtonPresses);
                            }
                            else
                            {
                                ES_Timer_InitTimer(SEQUENCE_TIMER, CARROT_BLINK_DURATION);
                            }
                        }
                        else
                        {
                            CARROT_LED = LED_ON;
                            CarrotBlinkOn = true;
                            ES_Timer_InitTimer(SEQUENCE_TIMER, CARROT_BLINK_DURATION);
                        }
                    }
                    else if ((ThisEvent.EventParam == INTERACTION_TIMER) &&
                             CarrotInputEnabled)
                    {
                        if (DetectedButtonPresses == TargetButtonPresses)
                        {
                            ES_Event_t DoneEvent;
                            DoneEvent.EventType = ES_CARROT_TASK_COMPLETE;
                            DoneEvent.EventParam = 0;
                            if (!PostGameService(DoneEvent))
                            {
                                DB_printf("ERROR: Could not post carrot task completion\r\n");
                            }
                        }
                        else if (DetectedButtonPresses > TargetButtonPresses)
                        {
                            DB_printf("TOO MANY PRESSES. Try the carrot task again.\r\n");
                            DetectedButtonPresses = 0;
                            StartCarrotBlinkSequence();
                        }
                    }
                    break;

                case ES_CARROT_TASK_COMPLETE:
                    if (CarrotInputEnabled &&
                        (DetectedButtonPresses == TargetButtonPresses))
                    {
                        DB_printf("CARROT CUT!\r\n");
                        CARROT_LED = LED_OFF;
                        PostServoCommand(ES_SERVO_CARROT_OPEN);
                        ES_Timer_StopTimer(INTERACTION_TIMER);
                        CompleteTask();
                    }
                    break;

                default:
                    break;
            }
            break;

        case GAME_STIR_POT:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_POT_CHANGED:
                    UpdateSoupStirring(ThisEvent.EventParam);
                    break;

                case ES_TIMEOUT:
                    // Pot stayed in the window for the full hold time
                    if ((ThisEvent.EventParam == INTERACTION_TIMER) && SoupStirring)
                    {
                        ES_Event_t DoneEvent;
                        DoneEvent.EventType = ES_STIR_TASK_COMPLETE;
                        DoneEvent.EventParam = 0;
                        PostGameService(DoneEvent);
                    }
                    break;

                case ES_STIR_TASK_COMPLETE:
                    DB_printf("SOUP STIRRED!\r\n");
                    ES_Timer_StopTimer(INTERACTION_TIMER);
                    // Make sure the soup servo is off when leaving the task
                    if (SoupStirring)
                    {
                        SoupStirring = false;
                        PostServoCommand(ES_SERVO_SOUP_STOP);
                    }
                    CompleteTask();
                    break;

                default:
                    break;
            }
            break;

        case GAME_HEAT_COIL:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_IR_CHANGED:
                    UpdateCoilHeating(ThisEvent.EventParam);
                    break;

                case ES_TIMEOUT:
                    if (ThisEvent.EventParam == SENSOR_PRINT_TIMER)
                    {
                        // Calibration readout
                        uint16_t IR = QueryIRValue();
                        // Rise above ambient is what the window checks
                        int16_t Rise = (int16_t)IR - (int16_t)HeatBaseline;
                        DB_printf("IR: %u, rise above baseline: %d\r\n", IR, Rise);
                        ES_Timer_InitTimer(SENSOR_PRINT_TIMER, SENSOR_PRINT_PERIOD);
                    }
                    else if ((ThisEvent.EventParam == INTERACTION_TIMER) && CoilHeating)
                    {
                        // Hand stayed in the window for the full hold time
                        ES_Event_t DoneEvent;
                        DoneEvent.EventType = ES_HEAT_TASK_COMPLETE;
                        DoneEvent.EventParam = 0;
                        PostGameService(DoneEvent);
                    }
                    break;

                case ES_HEAT_TASK_COMPLETE:
                    DB_printf("COIL HEATED!\r\n");
                    ES_Timer_StopTimer(INTERACTION_TIMER);
                    ES_Timer_StopTimer(SENSOR_PRINT_TIMER);
                    if (CoilHeating)
                    {
                        CoilHeating = false;
                        SetCoilGlow(false);
                    }
                    CompleteTask();
                    break;

                default:
                    break;
            }
            break;

        case GAME_WINNING:
            // TREAT dispensing logic will go here
            DB_printf("YOU'RE A WINNER!\r\n");
            break;

        case GAME_LOSING:
            // Participation award logic will go here
            DB_printf("NEXT TIME\r\n");
            break;

        case GAME_INACTIVE:
            // Inactivity timeout logic will go here
            DB_printf("NO INPUT\r\n");
            break;

        case GAME_TEST:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_START_PRESSED:
                    DB_printf("\r\n[BUTTON TEST]");
                    DB_printf(" START BUTTON PRESSED - RB3\r\n");
                    break;

                case ES_INPUT_START_RELEASED:
                    DB_printf("[BUTTON TEST]");
                    DB_printf(" START BUTTON RELEASED - RB3\r\n");
                    break;

                case ES_INPUT_CARROT_PRESSED:
                    TestCarrotPressCount++;
                    DB_printf("\r\n[BUTTON TEST]");
                    DB_printf(" CARROT BUTTON PRESSED - RB2\r\n");
                    DB_printf("Total presses: %u\r\n", TestCarrotPressCount);
                    break;

                case ES_INPUT_CARROT_RELEASED:
                    DB_printf("[BUTTON TEST]");
                    DB_printf(" CARROT BUTTON RELEASED - RB2\r\n");
                    break;

                case ES_INPUT_POT_CHANGED:
                    TestPotValue = ThisEvent.EventParam;
                    DB_printf("\r\n[POTENTIOMETER TEST]\r\n");
                    DB_printf("Raw ADC: %u / 1023\r\n", TestPotValue);
                    DB_printf("Position: %u%%\r\n", (uint16_t)((TestPotValue * 100U) / 1023U));
                    break;

                case ES_INPUT_IR_CHANGED:
                    TestIRValue = ThisEvent.EventParam;
                    DB_printf("\r\n[IR SENSOR TEST]\r\n");
                    DB_printf("Raw ADC: %u / 1023\r\n", TestIRValue);
                    DB_printf("Relative reading: %u%%\r\n", (uint16_t)((TestIRValue * 100U) / 1023U));
                    break;

                case ES_NEW_KEY:
                    switch ((char)ThisEvent.EventParam)
                    {
                        case 'x':
                            DB_printf("\r\nEXITING INPUT TEST MODE\r\n");
                            ClearNeoPixels();
                            UpdateNeoPixels();
                            EnterWelcomeMode();
                            CurrentState = GAME_WELCOME;
                            break;

                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }

            break;

        default:
            break;
    }

    if ((ThisEvent.EventType == ES_NEW_KEY) &&
        ((unsigned char)ThisEvent.EventParam == 'l'))
    {
        LEDCountdownRemaining = GameTime;
        DB_printf("LED COUNTDOWN: %u\r\n", LEDCountdownRemaining);

        if (!PostLEDDisplayNumber(LEDCountdownRemaining))
        {
            DB_printf("ERROR: Could not post LED countdown value\r\n");
        }

        ES_Timer_InitTimer(GAME_SERVICE_TIMER, COUNTDOWN_DURATION);
    }
    else if ((ThisEvent.EventType == ES_TIMEOUT) &&
             (ThisEvent.EventParam == GAME_SERVICE_TIMER) &&
             (LEDCountdownRemaining > 0U))
    {
        LEDCountdownRemaining--;
        DB_printf("LED COUNTDOWN: %u\r\n", LEDCountdownRemaining);

        if (!PostLEDDisplayNumber(LEDCountdownRemaining))
        {
            DB_printf("ERROR: Could not post LED countdown value\r\n");
        }

        if (LEDCountdownRemaining > 0U)
        {
            ES_Timer_InitTimer(GAME_SERVICE_TIMER, COUNTDOWN_DURATION);
        }
    }

    return ReturnEvent;
}


/*******************************
   Private Functions
 *******************************/

static void InitHWPins(void)
{
    // Carrot button LED on RA3 (digital output, no analog function)
    CARROT_LED_TRIS = 0;
    CARROT_LED = LED_OFF;

    // Start button LED on RB10 (digital output, no analog function)
    START_LED_TRIS = 0;
    START_LED = LED_OFF;

    // NeoPixel strip on RB11, blanked at startup
    InitNeoPixelService();
}


// Called on every entry to the carrot task
static void EnterCarrotTask(void)
{
    TargetButtonPresses = (uint8_t)(CARROT_PRESSES_MIN +
        (rand() % (CARROT_PRESSES_MAX - CARROT_PRESSES_MIN + 1U)));
    DetectedButtonPresses = 0;
    StartCarrotBlinkSequence();
    PostServoCommand(ES_SERVO_CARROT_CLOSE);   // always reset carrot to 0 deg
    DB_printf("CUT CARROT! Wait for %u blinks.\r\n", TargetButtonPresses);
}


static void StartCarrotBlinkSequence(void)
{
    CarrotBlinksRemaining = TargetButtonPresses;
    CarrotBlinkOn = true;
    CarrotInputEnabled = false;
    CARROT_LED = LED_ON;
    ES_Timer_InitTimer(SEQUENCE_TIMER, CARROT_BLINK_DURATION);
}


// Called on every entry to the stir pot task
static void EnterStirTask(void)
{
    // Choose one of three ranges, excluding the previous soup-task range.
    do
    {
        CurrentStirTarget = (StirTarget_t)(rand() % STIR_TARGET_COUNT);
    } while (CurrentStirTarget == LastStirTarget);

    LastStirTarget = CurrentStirTarget;
    DB_printf("STIR POT! Set knob to ADC range %u-%u\r\n",
              (unsigned int)StirTargetRanges[CurrentStirTarget].Low,
              (unsigned int)StirTargetRanges[CurrentStirTarget].High);

    // Start from a known state: soup servo off
    SoupStirring = false;
    PostServoCommand(ES_SERVO_SOUP_STOP);

    // Pot events only arrive when the pot moves, so check where it is now.
    // It may already be in the window when the task starts.
    UpdateSoupStirring(QueryPotValue());
}


// Soup servo runs while the pot is inside the window, stops outside it
static void UpdateSoupStirring(uint16_t PotReading)
{
    StirTargetRange_t Target = StirTargetRanges[CurrentStirTarget];
    bool InWindow = (PotReading >= Target.Low) &&
                    (PotReading <= Target.High);

    if (InWindow && !SoupStirring)
    {
        SoupStirring = true;
        DB_printf("POT IN RANGE (%u) - STIRRING\r\n", PotReading);

        ES_Event_t ServoEvent;
        ServoEvent.EventType = ES_SERVO_SOUP_SPEED;
        ServoEvent.EventParam = (uint16_t)SOUP_STIR_SPEED;
        PostServoService(ServoEvent);

        // Task completes if the pot stays in range for the interaction interval.
        ES_Timer_InitTimer(INTERACTION_TIMER, INTERACTION_DURATION);
    }
    else if (!InWindow && SoupStirring)
    {
        SoupStirring = false;
        DB_printf("POT OUT OF RANGE (%u) - STOPPED\r\n", PotReading);
        PostServoCommand(ES_SERVO_SOUP_STOP);

        // Left the window before the hold time: start over next time
        ES_Timer_StopTimer(INTERACTION_TIMER);
    }
}


// Heat window bounds, relative to the ambient baseline
static uint16_t HeatWindowLow(void)
{
    return TargetHandValue - HEAT_TOLERANCE_COUNTS;
}

static uint16_t HeatWindowHigh(void)
{
    return TargetHandValue + HEAT_TOLERANCE_COUNTS;
}


// Called on every entry to the heat coil task
static void EnterHeatTask(void)
{
    uint16_t TargetRise;

    // Ambient baseline, assumes no hand over the sensor right now
    HeatBaseline = QueryIRValue();
    if (HeatBaseline > HEAT_BASELINE_MAX)
    {
        HeatBaseline = HEAT_BASELINE_MAX;
    }

    TargetRise = HeatTargetRiseOptions[rand() % HEAT_TARGET_VARIANT_COUNT];
    TargetHandValue = HeatBaseline + TargetRise;

    DB_printf("HEAT COIL! Baseline %u, target %u, hold hand over sensor: %u-%u\r\n",
              (unsigned int)HeatBaseline,
              (unsigned int)TargetHandValue,
              (unsigned int)HeatWindowLow(),
              (unsigned int)HeatWindowHigh());

    CoilHeating = false;
    SetCoilGlow(false);

#if HEAT_SERIAL_READOUT
    ES_Timer_InitTimer(SENSOR_PRINT_TIMER, SENSOR_PRINT_PERIOD);
#endif

    // IR events only arrive on change, so check the current reading now
    UpdateCoilHeating(QueryIRValue());
}


// NeoPixels glow while the hand is inside the window, off outside it
static void UpdateCoilHeating(uint16_t IRReading)
{
    DetectedHandValue = IRReading;
    bool InWindow = (DetectedHandValue >= HeatWindowLow()) &&
                    (DetectedHandValue <= HeatWindowHigh());

    if (InWindow && !CoilHeating)
    {
        CoilHeating = true;
        DB_printf("HAND IN RANGE (%u) - HEATING\r\n", IRReading);
        SetCoilGlow(true);

        // Task completes if the hand stays in range for the interaction interval.
        ES_Timer_InitTimer(INTERACTION_TIMER, INTERACTION_DURATION);
    }
    else if (!InWindow && CoilHeating)
    {
        CoilHeating = false;
        DB_printf("HAND OUT OF RANGE (%u) - COOLING\r\n", IRReading);
        SetCoilGlow(false);

        // Left the window before the hold time: start over next time
        ES_Timer_StopTimer(INTERACTION_TIMER);
    }
}


static void SetCoilGlow(bool On)
{
    if (On)
    {
        SetAllNeoPixels(COIL_RED, COIL_GREEN, COIL_BLUE);
    }
    else
    {
        ClearNeoPixels();
    }
    UpdateNeoPixels();
}


static void PostServoCommand(ES_EventType_t Command)
{
    ES_Event_t ServoEvent;
    ServoEvent.EventType = Command;
    ServoEvent.EventParam = 0;
    PostServoService(ServoEvent);
}


static void StartNewGame(void)
{
    GameProgress = 0;
    GameTime = 60;
    LastTask = TASK_COUNT;
    LastStirTarget = STIR_TARGET_COUNT;

    DetectedButtonPresses = 0;

    DB_printf("NEW GAME\r\n");

    // Timer initialization will go here.
}


static void AssignNewTask(void)
{

    do
    {
        CurrentTask = (GameTask_t)(rand() % TASK_COUNT);
    } while (CurrentTask == LastTask);

    LastTask = CurrentTask;
    DB_printf("NEW TASK: %d\r\n", CurrentTask);
}


// Score the finished task, then move on to the next one after a short pause
static void CompleteTask(void)
{
    UpdateGameProgress();

    // UpdateGameProgress switches to GAME_WINNING on the final point
    if (CurrentState != GAME_WINNING)
    {
        CurrentState = GAME_SELECT_TASK;
        ES_Timer_InitTimer(SEQUENCE_TIMER, TASK_GAP_DURATION);
    }
}


static void UpdateGameProgress(void)
{
    if (GameProgress < MAX_GAME_PROGRESS)
    {
        GameProgress++;
    }

    DB_printf("GAME PROGRESS: %d / %d\r\n",
              GameProgress,
              MAX_GAME_PROGRESS);

    if (GameProgress >= MAX_GAME_PROGRESS)
    {
        CurrentState = GAME_WINNING;

        ES_Event_t WinEvent;

        WinEvent.EventType = ES_GAME_WON;
        WinEvent.EventParam = GameProgress;

        PostGameService(WinEvent);
    }
}


static void EnterWelcomeMode(void)
{
    GameProgress = 0;
    GameTime = 60;

    DB_printf("BROTH OF DUTY\r\n");

    // Light the start button while waiting for a player
    START_LED = LED_ON;

    // Coil strip off until the heat task lights it
    ClearNeoPixels();
    UpdateNeoPixels();

    // LED welcome display will be implemented
    // through the LEDService interface.
}


/*******************************
   Public Query Function
 *******************************/

uint8_t QueryGameProgress(void)
{
    return GameProgress;
}
