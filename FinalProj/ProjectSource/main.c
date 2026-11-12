/****************************************************************************
 Module
     main.c
 Description
     starter main() function for Events and Services Framework applications
 Notes

*****************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "PIC32_SPI_HAL.h"
#include "ES_Port.h"
#include "dbprintf.h"


void main(void)
{
  ES_Return_t ErrorType = Success;

  _HW_PIC32Init(); // basic PIC hardware init
  // Your hardware initialization function calls go here


  // Configure SPI1
  SPISetup_BasicConfig(SPI_SPI1);
  SPISetup_SetLeader(SPI_SPI1, SPI_SMP_MID);
  
  // Configure clock and transfer format
  SPISetup_SetBitTime(SPI_SPI1, 10000); // 10kHz, 10000ns
  SPISetup_SetClockIdleState(SPI_SPI1, SPI_CLK_HI); // CKP = 1, idle high
  SPISetup_SetActiveEdge(SPI_SPI1, SPI_SECOND_EDGE); // CKE = 0, data change from idle to active
  SPISetup_SetXferWidth(SPI_SPI1, SPI_16BIT);
  SPISetEnhancedBuffer(SPI_SPI1, true);
  
  // Map pins
  SPISetup_MapSDOutput(SPI_SPI1, SPI_RPA1); // SDO to RA1
  SPISetup_MapSSOutput(SPI_SPI1, SPI_RPA0); // SD to RA0
  
  // Enable SPI
  SPISetup_EnableSPI(SPI_SPI1);
  

  DB_printf("\r\nMAIN STARTED\r\n");

  // now initialize the Events and Services Framework and start it running
  ErrorType = ES_Initialize(ES_Timer_RATE_1mS);
  if (ErrorType == Success)
  {
    ErrorType = ES_Run();
  }

  DB_printf("ES_RUN RETURNED\r\n");

  //if we got to here, there was an error
  switch (ErrorType)
  {
    case FailedPost:
    {
      ErrorType = 0; // a hack to get a readable assert message
      assert( FailedPost == ErrorType );
      //DB_printf("Failed on attempt to Post\n");
    }
    break;
    case FailedPointer:
    {
      ErrorType = 0; // a hack to get a readable assert message
      assert( FailedPointer  == ErrorType );
//      DB_printf("Failed on NULL pointer\n");
    }
    break;
    case FailedInit:
    {
      ErrorType = 0; // a hack to get a readable assert message
      assert( FailedInit  == ErrorType );
//      DB_printf("Failed Initialization\n");
    }
    break;
    default:
    {
      ErrorType = 0; // a hack to get a readable assert message
      assert( FailedOther  == ErrorType );
//      DB_printf("Other Failure\n");
    }
    break;
  }
  for ( ; ;)
  {
    ;
  }
}

