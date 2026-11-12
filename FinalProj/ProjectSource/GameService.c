
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
#define INTERACTION_DURATION    500
#define COUNTDOWN_DURATION      1000


/*******************************
   State Definitions
 *******************************/
typedef enum
{
    GAME_INITIALIZING,
    GAME_WELCOME,
    GAME_STARTUP,
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
    TASK_HEAT_COIL
} GameTask_t;


/*******************************
   Module Variables
 *******************************/
static uint8_t MyPriority;

static GameState_t CurrentState = GAME_INITIALIZING;
static GameTask_t CurrentTask;

static uint8_t GameProgress = 0;
static uint8_t GameTime = 60;

static uint8_t TargetButtonPresses = 0;
static uint8_t DetectedButtonPresses = 0;

static uint16_t TargetPotValue = 0;
static uint16_t DetectedPotValue = 0;

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
static void EnterWelcomeMode(void);


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

    ReturnEvent.EventType = ES_NO_EVENT;
    ReturnEvent.EventParam = 0;

    switch (CurrentState)
    {
        case GAME_INITIALIZING:
            if (ThisEvent.EventType == ES_INIT)
            {
                DB_printf("GAME INITIALIZED\r\n");
                EnterWelcomeMode();
                CurrentState = GAME_WELCOME;
            }
            break;

        case GAME_WELCOME:
            switch (ThisEvent.EventType)
            {
                case ES_GAME_START:
                case ES_INPUT_START_PRESSED:
                    DB_printf("GAME STARTED\r\n");
                    StartNewGame();
                    CurrentState = GAME_STARTUP;
                    break;

                case ES_NEW_KEY:
                    if (ThisEvent.EventParam == 't')
                    {
                        CurrentState = GAME_TEST;

                        TestCarrotPressCount = 0;

                        TestPotValue = QueryPotValue();
                        TestIRValue = QueryIRValue();

                        DB_printf("\r\n");
                        DB_printf("============================\r\n");
                        DB_printf("       INPUT TEST MODE      \r\n");
                        DB_printf("============================\r\n");

                        DB_printf("Start button:    RB3\r\n");
                        DB_printf("Carrot button:   RB2\r\n");
                        DB_printf("Potentiometer:   AN12\r\n");
                        DB_printf("IR sensor:       AN11\r\n");

                        DB_printf("Initial POT: %u\r\n", TestPotValue);
                        DB_printf("Initial IR: %u\r\n", TestIRValue);

                        DB_printf("Press X to exit test mode\r\n");
                    }

                    break;

                default:
                    break;
            }
            break;

        case GAME_STARTUP:
            switch (ThisEvent.EventType)
            {
                case ES_GAME_STARTUP_COMPLETE:
                    CurrentState = GAME_COUNTDOWN;
                    break;
                default:
                    break;
            }
            break;

        case GAME_COUNTDOWN:
            if (ThisEvent.EventType == ES_GAME_COUNTDOWN_COMPLETE)
            {
                CurrentState = GAME_SELECT_TASK;

                // Request first task
                ES_Event_t NextEvent;

                NextEvent.EventType = ES_ASSIGN_TASK;
                NextEvent.EventParam = 0;

                PostGameService(NextEvent);
            }
            break;

        case GAME_SELECT_TASK:
            if (ThisEvent.EventType == ES_ASSIGN_TASK)
            {
                AssignNewTask();
                switch (CurrentTask)
                {
                    case TASK_CUT_CARROT:
                        CurrentState = GAME_CUT_CARROT;
                        break;
                    case TASK_STIR_POT:
                        CurrentState = GAME_STIR_POT;
                        break;
                    case TASK_HEAT_COIL:
                        CurrentState = GAME_HEAT_COIL;
                        break;
                }
            }
            break;

        case GAME_CUT_CARROT:
            // Carrot interaction logic will go here
            if (ThisEvent.EventType == ES_CARROT_TASK_COMPLETE)
            {
                UpdateGameProgress();
                CurrentState = GAME_SELECT_TASK;
            }
            break;

        case GAME_STIR_POT:
            // Potentiometer interaction logic will go here
            if (ThisEvent.EventType == ES_STIR_TASK_COMPLETE)
            {
                UpdateGameProgress();
                CurrentState = GAME_SELECT_TASK;
            }
            break;

        case GAME_HEAT_COIL:
            // IR interaction logic will go here
            if (ThisEvent.EventType == ES_HEAT_TASK_COMPLETE)
            {
                UpdateGameProgress();
                CurrentState = GAME_SELECT_TASK;
            }
            break;

        case GAME_WINNING:
            // TREAT dispensing logic will go here
            break;

        case GAME_LOSING:
            // Participation award logic will go here
            break;

        case GAME_INACTIVE:
            // Inactivity timeout logic will go here
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
                    if (ThisEvent.EventParam == 'x')
                    {
                        DB_printf("\r\nEXITING INPUT TEST MODE\r\n");
                        EnterWelcomeMode();
                        CurrentState = GAME_WELCOME;
                    }
                    break;

                default:
                    break;
            }

            break;

        default:
            break;
    }


    /*******************************
       Global Timer Handling
     *******************************/

    if (ThisEvent.EventType == ES_TIMEOUT)
    {
        // Timer handling will be implemented
        // after the individual game states are established.
    }

    return ReturnEvent;
}


/*******************************
   Private Functions
 *******************************/

static void InitHWPins(void)
{
    // Hardware GPIO initialization
    // To be implemented after confirming
    // pin assignments and sensor configuration.
}


static void StartNewGame(void)
{
    GameProgress = 0;
    GameTime = 60;

    DetectedButtonPresses = 0;

    DB_printf("NEW GAME\r\n");

    // Timer initialization will go here.
}


static void AssignNewTask(void)
{
    // Temporary task selection.
    // Replace with a better pseudo-random generator.

    CurrentTask = (GameTask_t)(rand() % 3);

    DB_printf("NEW TASK: %d\r\n", CurrentTask);
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

    DB_printf("WELCOME TO KARL'S KITCHEN\r\n");

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
