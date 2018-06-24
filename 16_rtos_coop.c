// RTOS Framework - Spring 2017
// J Losh

// Student Name: Ayushee Shah
// TO DO: Add your name on this line.  Do not include your ID number.

// Submit only two .c files in an e-mail to me (not in a compressed file):
// xx_rtos_coop.c   Cooperative version of your project
// xx_rtos_prempt.c Premptive version of your project
// (xx is a unique number that will be issued in class)

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL Evaluation Board
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// 4 Pushbuttons and 5 LEDs, UART

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

//_putSP.asm:
/*
  		.thumb
		.text
   	   .def PUTSP
		PUTSP:
   	   	   mov sp,r0
   	   	   bx lr
		.endm

//_getSP.asm:

 		.thumb
		.text
   	   	.def GETSP
		GETSP:
   	   	   mov r0,sp
  	  	   bx lr
		.endm
*/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tm4c123gh6pm.h"

// REQUIRED: correct these bitbanding references for the off-board LEDs
#define BLUE_LED     (*((volatile uint32_t *)(0x42000000 + (0x400253FC-0x40000000)*32 + 2*4))) // on-board blue LED
#define RED_LED      (*((volatile uint32_t *)(0x42000000 + (0x400073FC-0x40000000)*32 + 3*4))) // off-board red LED
#define ORANGE_LED   (*((volatile uint32_t *)(0x42000000 + (0x400073FC-0x40000000)*32 + 0*4))) // off-board orange LED
#define YELLOW_LED   (*((volatile uint32_t *)(0x42000000 + (0x400073FC-0x40000000)*32 + 2*4))) // off-board yellow LED
#define GREEN_LED    (*((volatile uint32_t *)(0x42000000 + (0x400073FC-0x40000000)*32 + 1*4))) // off-board green LED


//-----------------------------------------------------------------------------
// RTOS Defines and Kernel Variables
//-----------------------------------------------------------------------------

// function pointer
typedef void (*_fn)();

// semaphore
#define MAX_SEMAPHORES 5
#define MAX_QUEUE_SIZE 5

extern int GETSP (void);
extern void PUTSP (uint32_t *stackptr);

char str[15];
uint32_t *systemSP,Count=0;
int *temp;
struct semaphore
{
  uint16_t count;
  uint16_t queueSize;
  uint32_t processQueue[MAX_QUEUE_SIZE]; // store task index here
  char name[20];

} semaphores[MAX_SEMAPHORES];
uint8_t semaphoreCount = 0;

struct semaphore *keyPressed, *keyReleased, *flashReq, *resource;

// task
#define STATE_INVALID    0 // no task
#define STATE_READY      1 // ready to run
#define STATE_BLOCKED    2 // has run, but now blocked by semaphore
#define STATE_DELAYED    3 // has run, but now awaiting timer

#define MAX_TASKS 10       // maximum number of valid tasks
uint8_t taskCurrent = 0;   // index of last dispatched task
uint8_t taskCount = 0;     // total number of valid tasks

struct _tcb
{
  uint8_t state;                 // see STATE_ values above
  void *pid;                     // used to uniquely identify thread
  void *sp;                      // location of stack pointer for thread
  uint8_t skipCount;             // Used for scheduling
  uint8_t priority;              // 0=highest, 7=lowest
  uint8_t currentPriority;       // used for priority inheritance
  uint32_t ticks;                // ticks until sleep complete
  char name[16];                 // name of task used in ps command
  void *semaphore;               // pointer to the semaphore that is blocking the thread
  uint32_t startCount;
  uint32_t endCount;
  uint32_t totalCount;
  bool semaphoreUsing;
} tcb[MAX_TASKS];

uint32_t stack[MAX_TASKS][256];

//-----------------------------------------------------------------------------
// RTOS Kernel Functions
//-----------------------------------------------------------------------------

void rtosInit()
{
  uint8_t i;
  // no tasks running
  taskCount = 0;
  // clear out tcb records
  for (i = 0; i < MAX_TASKS; i++)
  {
    tcb[i].state = STATE_INVALID;
    tcb[i].pid = 0;
  }

  // REQUIRED: initialize systick for 1ms system timer
  NVIC_ST_CTRL_R=0; //Disabling the system timer
  NVIC_ST_RELOAD_R = 0x9C3F; // 40M/1K - 1
  NVIC_ST_CURRENT_R=0; //Clears current
  NVIC_ST_CTRL_R=0x07;

}

int rtosScheduler()
{
  // REQUIRED: Implement prioritization to 16 levels
  bool ok;
  static uint8_t task = 0xFF;
  ok = false;
  tcb[taskCurrent].endCount=Count;
  tcb[taskCurrent].totalCount=tcb[taskCurrent].endCount-tcb[taskCurrent].startCount;
  while (!ok)
  {
	  task++;
	 if (task >= MAX_TASKS)
	   	  task = 0;
	 if((tcb[task].state == STATE_READY) && (tcb[task].skipCount >= tcb[task].priority))
	 {
		 ok=1;
		 tcb[task].skipCount=0;
		 tcb[task].startCount=Count;
	 }
	 else
	 {
		 tcb[task].skipCount=tcb[task].skipCount+1;
	 }
  }
  //NVIC_ST_CURRENT_R=0;

  return task;
}

void timer1Isr(void)
{
	Count++;
	TIMER1_ICR_R =TIMER_ICR_TATOCINT;
}
void init_timer()
{
	 SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R1;
	 TIMER1_CTL_R &= ~TIMER_CTL_TAEN;                 // turn-off timer before reconfiguring
	 TIMER1_CFG_R = TIMER_CFG_32_BIT_TIMER;           		// configure as 16-bit timer (A)
	 TIMER1_TAMR_R = TIMER_TAMR_TAMR_PERIOD;          // configure for periodic mode (count down)
	 TIMER1_TAILR_R = 400;
	 TIMER1_IMR_R = TIMER_IMR_TATOIM;                 // turn-on interrupts
	 NVIC_EN0_R |= 1 << (INT_TIMER1A-16);             // turn-on interrupt 37 (TIMER1A)
	 TIMER1_CTL_R |= TIMER_CTL_TAEN;
}

void rtosStart()
{
  // REQUIRED: add code to call the first task to be run, restoring the preloaded context

	_fn fn;
	extern void PUTSP (uint32_t*);
//	__asm(" POP {r3,lr}");
	systemSP=GETSP();
	init_timer();
  taskCurrent = rtosScheduler();

  // Add code to initialize the SP with tcb[task_current].sp;

 // uint32_t *a=tcb[taskCurrent].sp;
  PUTSP(tcb[taskCurrent].sp);
  // Restore the stack to run the first process
  	__asm(" POP {r4-r11 , pc}");


}

bool createThread(_fn fn, char name[], int priority)
{
  bool ok = false;
  uint8_t i = 0;
  uint16_t j=0;
  bool found = false;
  // REQUIRED: store the thread name


  // REQUIRED: take steps to ensure a task switch cannot occur

  // add task if room in task list
  if (taskCount < MAX_TASKS)
  {
    // make sure fn not already in list (prevent reentrancy)
    while (!found && (i < MAX_TASKS))
    {
      found = (tcb[i++].pid ==  fn);
    }
    if (!found)
    {
      // find first available tcb record
      i = 0;
      while (tcb[i].state != STATE_INVALID) {i++;}
      tcb[i].state = STATE_READY;
      tcb[i].pid = fn;

      // REQUIRED: preload stack to look like the task had run before
      for(j=254;j>246;j--)
    	  stack[i][j]=j;
      stack[i][255]=tcb[i].pid;
      tcb[i].sp = &stack[i][247]; // REQUIRED: + offset as needed for the pre-loaded stack
      tcb[i].priority = priority;
      tcb[i].currentPriority = priority;
      tcb[i].skipCount = 0;
      strcpy(tcb[i].name,name);
      // increment task count
      taskCount++;
      ok = true;
    }
    else
    {
    	tcb[i-1].state= STATE_READY;
    	tcb[i-1].priority = priority;
    	tcb[i-1].currentPriority = priority;
    	tcb[i-1].skipCount = 0;
    }
  }
  // REQUIRED: allow tasks switches again
  return ok;
}

// REQUIRED: modify this function to destroy a thread
void destroyThread(_fn fn)
{
	uint8_t i;
	for(i=0;i<taskCount;i++)
	{
		if(fn==tcb[i].pid)
		{
			//tcb[i].pid=0;
			tcb[i].state=STATE_INVALID;
		}
	}
}

struct semaphore* createSemaphore(int count, char name[])
{
  struct semaphore *pSemaphore = 0;

  if (semaphoreCount < MAX_SEMAPHORES)
  {

	  pSemaphore = &semaphores[semaphoreCount++];
	  pSemaphore->count = count;
	  strcpy(pSemaphore->name,name);
  }
  return pSemaphore;
}




// REQUIRED: modify this function to yield execution back to scheduler
void yield()
{
	__asm(" POP {r3, lr}");
	__asm(" PUSH {r4-r11 , lr}");

	//REFERENCE:http://stackoverflow.com/questions/32953949/saving-value-of-stack-pointer-in-c-variable-in-code-composer-studio-for-arm-cort

	tcb[taskCurrent].sp=GETSP();
	PUTSP(systemSP);
	taskCurrent=rtosScheduler();
	PUTSP(tcb[taskCurrent].sp);
	__asm(" POP {r4-r11 , pc}");
}

// REQUIRED: modify this function to support 1ms system timer
// execution yielded back to scheduler until time elapses
void sleep(uint32_t tick)
{
  // push registers, set state to delayed, store timeout, call scheduler, pop registers,
  // return to new function (separate unrun or ready processing)
	tcb[taskCurrent].ticks=tick;
	__asm(" POP {r3, lr}");
	__asm(" PUSH {r4-r11 , lr}");

		//REFERENCE:http://stackoverflow.com/questions/32953949/saving-value-of-stack-pointer-in-c-variable-in-code-composer-studio-for-arm-cort

	tcb[taskCurrent].sp=GETSP();
	tcb[taskCurrent].state=STATE_DELAYED;

	// wait for count flag
	PUTSP(systemSP);
	taskCurrent=rtosScheduler();
	PUTSP(tcb[taskCurrent].sp);
	__asm(" POP {r4-r11 , pc}");
}
void systickIsr()
{
	uint8_t i;

	for(i=0;i<MAX_TASKS;i++)
	{
		if(tcb[i].state==STATE_DELAYED)
		{
			tcb[i].ticks=tcb[i].ticks-1;
		if(tcb[i].ticks==0)
			tcb[i].state=STATE_READY;
		}
	}
}

uint8_t getSpWait()
{
	__asm(" mov r0,sp");
	__asm(" add r0, #0x08");
}

// REQUIRED: modify this function to wait a semaphore with priority inheri n tance
// return if avail (separate unrun or ready processing), else yield to scheduler
void wait(struct semaphore *pSemaphore)
{
	//REFERENCE:http://stackoverflow.com/questions/32953949/saving-value-of-stack-pointer-in-c-variable-in-code-composer-studio-for-arm-cort

	/*	if(pSemaphore->count >0)
		{
			pSemaphore->count=pSemaphore->count-1;

		}
		else
		{
			tcb[taskCurrent].state= STATE_BLOCKED;
			tcb[taskCurrent].semaphore = pSemaphore;
			pSemaphore->processQueue[pSemaphore->queueSize]=tcb[taskCurrent].pid;
			pSemaphore->queueSize=pSemaphore->queueSize+1;
			__asm(" POP {r3, lr}");
			__asm(" PUSH {r4-r11 , lr}");
			tcb[taskCurrent].sp=GETSP();
			PUTSP(systemSP);
			taskCurrent=rtosScheduler();
			PUTSP(tcb[taskCurrent].sp);
			__asm(" POP {r4-r11 , pc}");
		}*/

	temp=pSemaphore;
	__asm(" POP {r3, lr}");
	__asm(" POP {r3,lr}");
	__asm(" PUSH {r4-r11 , lr}");
	tcb[taskCurrent].sp=getSpWait();
	PUTSP(systemSP);
	uint8_t i=0;
	pSemaphore=temp;
	if(pSemaphore->count >0)
		pSemaphore->count=pSemaphore->count-1;
	else
	{
		tcb[taskCurrent].state= STATE_BLOCKED;
		tcb[taskCurrent].semaphore = pSemaphore;
		pSemaphore->processQueue[pSemaphore->queueSize]=tcb[taskCurrent].pid;
		pSemaphore->queueSize=pSemaphore->queueSize+1;
		while(i<taskCount)
		{
			if(tcb[taskCurrent].semaphore == tcb[i].semaphore)
			{
					if(tcb[i].priority>tcb[taskCurrent].priority)
						tcb[i].priority=tcb[taskCurrent].priority;
					tcb[i].skipCount=tcb[i].priority;
			}
			i++;
		}


		taskCurrent=rtosScheduler();
	}
		PUTSP(tcb[taskCurrent].sp);
		__asm(" POP {r4-r11 , pc}");

}

// REQUIRED: modify this function to signal a semaphore is available
void post(struct semaphore *pSemaphore)
{
	/*temp = pSemaphore;
	__asm(" PUSH {r4-r11, lr}");
	__asm( " POP {r3,lr}");
	tcb[taskCurrent].sp=GETSP();
	*/
	uint8_t i;

	// pSemaphore=temp;
	pSemaphore->count=pSemaphore->count+1;
	if(pSemaphore->count==1)
	{
		for(i=0;i<taskCount;i++)
		{
			tcb[i].priority=tcb[i].currentPriority;
			tcb[i].skipCount=tcb[i].currentPriority;
			if(pSemaphore->processQueue[0]==tcb[i].pid)
			{
				tcb[i].state=STATE_READY;
				pSemaphore->count--;
				//tcb[taskCurrent].semaphoreUsing=false;
				//taskCurrent=i;
			}
		}
		for(i=0;i<MAX_QUEUE_SIZE-1;i++)
			pSemaphore->processQueue[i]=pSemaphore->processQueue[i+1];
		if(pSemaphore->queueSize>0)
			pSemaphore->queueSize=pSemaphore->queueSize-1;
			//pSemaphore->processQueue[pSemaphore->queueSize]=pSemaphore->processQueue[pSemaphore->queueSize];

	}
	/*PUTSP(tcb[taskCurrent].sp);
	__asm(" PUSH {r3,lr}");
	__asm(" POP {r4-r11, lr}");*/
}
/*
// REQUIRED: modify this function to add support for the system timer

// REQUIRED: modify this function to add support for the service call
void svCallIsr()
{
}

// REQUIRED: modify this function to add support for the service call
void pendSvIsr()
{
}

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
*/
// Initialize Hardware
void initHw()
{
  // REQUIRED: Add initialization for blue, orange, red, green, and yellow LEDs
  //           4 pushbuttons, and uart

	// Configure HW to work with 16 MHz XTAL, PLL enabled, system clock of 40 MHz
	    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);

	    // Set GPIO ports to use APB (not needed since default configuration -- for clarity)
	    // Note UART on port A must use APB
	    SYSCTL_GPIOHBCTL_R = 0;

	    // Enable GPIO port A and F peripherals
	    SYSCTL_RCGC2_R = SYSCTL_RCGC2_GPIOA | SYSCTL_RCGC2_GPIOC | SYSCTL_RCGC2_GPIOD | SYSCTL_RCGC2_GPIOF;

	    // Configure LED and pushbutton pins
	    GPIO_PORTD_DIR_R = 0x0F;  // bits 1 and 3 are outputs, other pins are inputs
	    GPIO_PORTD_DR2R_R = 0x0F; // set drive strength to 2mA (not needed since default configuration -- for clarity)
	    GPIO_PORTD_DEN_R = 0x0F;  // enable LEDs

	    GPIO_PORTF_DIR_R = 0x04;  // bits 1 and 3 are outputs, other pins are inputs
	    GPIO_PORTF_DR2R_R = 0x04; // set drive strength to 2mA (not needed since default configuration -- for clarity)
	    GPIO_PORTF_DEN_R = 0x04;  // enable LEDs

	    // Configure LED and pushbutton pins
		GPIO_PORTC_DEN_R = 0xF0;  // enable LEDs and pushbuttons
	    GPIO_PORTC_PUR_R = 0xF0;  // enable internal pull-up for push button

	    // Configure UART0 pins
		SYSCTL_RCGCUART_R |= SYSCTL_RCGCUART_R0;         // turn-on UART0, leave other uarts in same status
	    GPIO_PORTA_DEN_R |= 3;                           // default, added for clarity
		GPIO_PORTA_AFSEL_R |= 3;                         // default, added for clarity
	    GPIO_PORTA_PCTL_R = GPIO_PCTL_PA1_U0TX | GPIO_PCTL_PA0_U0RX;

	   	// Configure UART0 to 115200 baud, 8N1 format (must be 3 clocks from clock enable and config writes)
	    UART0_CTL_R = 0;                                 // turn-off UART0 to allow safe programming
		UART0_CC_R = UART_CC_CS_SYSCLK;                  // use system clock (40 MHz)
	    UART0_IBRD_R = 21;                               // r = 40 MHz / (Nx115.2kHz), set floor(r)=21, where N=16
	    UART0_FBRD_R = 45;                               // round(fract(r)*64)=45
	    UART0_LCRH_R = UART_LCRH_WLEN_8 | UART_LCRH_FEN; // configure for 8N1 w/ 16-level FIFO
	    UART0_CTL_R = UART_CTL_TXE | UART_CTL_RXE | UART_CTL_UARTEN; // enable TX, RX, and module
}
void putcUart0(char c)
{
	while (UART0_FR_R & UART_FR_TXFF);
	UART0_DR_R = c;
}

// Blocking function that writes a string when the UART buffer is not full
void putsUart0(char* str)
{
	int i;
    for (i = 0; i < strlen(str); i++)
	  putcUart0(str[i]);
}

// Blocking function that returns with serial data once the buffer is not empty
char getcUart0()
{
	while (UART0_FR_R & UART_FR_RXFE)
		yield();
	return UART0_DR_R & 0xFF;
}
uint8_t isbks(char s)
{
	if(s==8)
		return 1;
	else
		return 0;
}
uint8_t isenter(char s)
{
	if(s==13)
		return 1;
	else
		return 0;
}
char iscapital(char s)
{
	if(s>=65 && s<=90)
		s=s+32;
	return s;
}
uint8_t isdelim(char s)
{
	if((s>=48 && s<=57)||s==46||(s>=97 && s<=122)||s==32||s==45)
		return 0;
	else return 1;
}
uint8_t isvalid(char s)
{
	if(s>=32)
		return 1;
	else
		return 0;
}

void getsUart0()
{
	uint8_t i,j,count=0;

	str[count]=getcUart0();
	while(!isenter(str[count]))
	{
		if(count<strlen(str))
		{
		    if((isbks(str[count]) && count!=0))
		    {
   				count--;
  				str[count]=NULL;
  	  			putsUart0("\r");
   	  			putsUart0(str);
		    }
	    	else if(isvalid(str[count]))
	    	{
	    		putcUart0(str[count]);
			    count++;
		   	}
	     	str[count]=getcUart0();
	  }
	else
		break;
	}
	for(j=count;j<=strlen(str);j++)
		str[j]=NULL;
	for(i=0;(i<strlen(str) && str[i]!=NULL);i++)
	{
	   	str[i]=iscapital(str[i]);
	   	if(isdelim(str[i]))
	   	{
	   		for(j=i;str[j]!=NULL;j++)
	   			str[j]=str[j+1];
	  		str[j]=NULL;
	  		i--;
	 	}
	}
}
// Approximate busy waiting (in units of microseconds), given a 40 MHz system clock
void waitMicrosecond(uint32_t us)
{
	                                            // Approx clocks per us
  __asm("WMS_LOOP0:   MOV  R1, #6");          // 1
  __asm("WMS_LOOP1:   SUB  R1, #1");          // 6
  __asm("             CBZ  R1, WMS_DONE1");   // 5+1*3
  __asm("             NOP");                  // 5
  __asm("             B    WMS_LOOP1");       // 5*3
  __asm("WMS_DONE1:   SUB  R0, #1");          // 1
  __asm("             CBZ  R0, WMS_DONE0");   // 1
  __asm("             B    WMS_LOOP0");       // 1*3
  __asm("WMS_DONE0:");                        // ---
                                              // 40 clocks/us + error
}

// REQUIRED: add code to return a value from 0-15 indicating which of 4 PBs are pressed
uint8_t readPbs()
{
	while(!(GPIO_PORTC_DATA_R & 0x10))
	{
		return 1;
	}
	while(!(GPIO_PORTC_DATA_R & 0x20))
	{
		return 2;
	}
	while(!(GPIO_PORTC_DATA_R & 0x40))
	{
		return 4;
	}
	while(!(GPIO_PORTC_DATA_R & 0x80))
	{
		return 8;
	}

		return 0;
}

// ------------------------------------------------------------------------------
//  Task functions
// ------------------------------------------------------------------------------

// one task must be ready at all times or the scheduler will fail
// the idle task is implemented for this purpose
void idle()
{
  while(true)
  {
    ORANGE_LED = 1;
    waitMicrosecond(1000);
    ORANGE_LED = 0;
    yield();
  }
}

void idle2()
{
  while(true)
  {
    GREEN_LED = 1;
    waitMicrosecond(1000);
    GREEN_LED = 0;
    yield();
  }
}

void flash4Hz()
{
  while(true)
  {
    GREEN_LED ^= 1;
    sleep(125);
  }
}

void oneshot()
{
  while(true)
  {
    wait(flashReq);
    YELLOW_LED = 1;
    sleep(1000);
    YELLOW_LED = 0;
  }
}
void important()
{
	while(true)
	{
		wait(resource);
		BLUE_LED = 1;
		sleep(1000);
		BLUE_LED = 0;
		post(resource);
	}
}

void partOfLengthyFn()
{
  // represent some lengthy operation
  waitMicrosecond(1000);
  // give another process a chance
  yield();
}

void lengthyFn()
{
  uint16_t i;
  while(true)
  {
    wait(resource);
    for (i = 0; i < 4000; i++)
    {
      partOfLengthyFn();
    }
    RED_LED ^= 1;
    post(resource);
  }
}

void readKeys()
{
  uint8_t buttons;
  while(true)
  {
    wait(keyReleased);
    buttons = 0;
    while (buttons == 0)
    {
      buttons = readPbs();
      yield();
    }
    post(keyPressed);
    if ((buttons & 1) != 0)
    {
      YELLOW_LED ^= 1;
      RED_LED = 1;
    }
    if ((buttons & 2) != 0)
    {
      post(flashReq);
      RED_LED = 0;
    }
    if ((buttons & 4) != 0)
    {
      createThread(flash4Hz, "Flash4hz", 0);
    }
    if ((buttons & 8) != 0)
    {
      destroyThread(flash4Hz);
	}

    yield();
  }
}

void debounce()
{
  uint8_t count;
  while(true)
  {
    wait(keyPressed);
    count = 10;
    while (count != 0)
    {
      sleep(10);
      if (readPbs() == 0)
        count--;
      else
        count = 10;
    }
    post(keyReleased);
  }
}

void uncooperative()
{
  while(true)
  {
    while (readPbs() == 8)
    {
    }
    yield();
  }
}

void ps()
{
	uint8_t i,j;
	uint32_t totalTime=0;
	char pid[10],cpuTime[10];
	for(i=0;i<taskCount;i++)
	{
		totalTime=totalTime+tcb[i].totalCount;
	}
	putsUart0("\r\n");
	putsUart0("\n\rPID\tName\t\t%CPU\n\r");
	for(i=0;i<taskCount;i++)
	{
		if(tcb[i].state!=STATE_INVALID)
		{
			sprintf(pid,"%d",tcb[i].pid);
			sprintf(cpuTime,"%d",(tcb[i].totalCount*100/totalTime));
			putsUart0(pid);
			putsUart0("\t");
			for(j=(strlen(tcb[i].name)+1);j<10;j++)
					strcat(tcb[i].name, " ");
			putsUart0(tcb[i].name);
			putsUart0("\t");
			putsUart0(cpuTime);
			putsUart0("\r\n");
		}
	}

}
void ipcs()
{
	uint8_t i,j,k;
	char count[10],queueSize[10],queueElement[10];
	putsUart0("\n\rName\t\t\tCount QueueSize Process\tQueue\n\r");
	for(i=0;i<semaphoreCount;i++)
	{
		putsUart0(semaphores[i].name);
		putsUart0("\t");
		sprintf(count,"%d",semaphores[i].count);
		putsUart0("\t");
		putsUart0(count);
		sprintf(queueSize,"%d",semaphores[i].queueSize);
		putsUart0("\t");
		putsUart0(queueSize);
		for(j=0;j<MAX_QUEUE_SIZE;j++)
		{
			for(k=0;k<10;k++)
				queueElement[k]=0;
			putsUart0("\t");
			sprintf(queueElement,"%d",semaphores[i].processQueue[j]);
			putsUart0(" ");
			putsUart0(queueElement);
		}
		putsUart0("\n\r");
	}
}
void kill()
{
	uint32_t pid;
	uint8_t i;
	for(i=0;i<strlen(str);i++)
		if( (str[i] >= '0' && str[i] <= '9'))
			break;
	pid=atoi(&str[i]);
	for(i=0;i<taskCount;i++)
	{
		if(tcb[i].name=="Shell")
			break;
	}
	if(tcb[i-1].pid==pid)
	{
		putsUart0("Can't kill Shell thread!");
		putsUart0("\n\r");
	}
	else
		destroyThread(pid);
	putsUart0("\n\r");
}
void pidof()
{

	uint8_t i,j;
	char pid[10];
	char tcb_temp[10];
	for(i=0;i<taskCount;i++)
	{
		for(j=0;j<strlen(tcb[i].name);j++)
			tcb_temp[j]=iscapital(tcb[i].name[j]);
		if(strstr(str,tcb_temp))
			{
				putsUart0("\t");
				sprintf(pid,"%d",tcb[i].pid);
				putsUart0(pid);
				putsUart0("\n\r");
				break;
			}
		else
			for(j=0;j<strlen(tcb[i].name);j++)
				tcb_temp[j]=0;
	}
}
uint8_t process()
{
	 char tcb_temp[15];
	 uint8_t i,j;
	 for(i=0;i<taskCount;i++)
	 {
		for(j=0;j<strlen(tcb[i].name);j++)
			tcb_temp[j]=iscapital(tcb[i].name[j]);
		if(strstr(tcb_temp,str))
		{
	 		if(tcb[i].state==STATE_INVALID)
	 		{
	 			tcb[i].state=STATE_READY;
	 			tcb[i].skipCount=tcb[i].priority;
	 			putsUart0("\n\r");
	 			putsUart0("Process Created!");
	 			putsUart0("\n\r");
	 			return(1);
	 		}
	 		else
	 		{
	 			putsUart0("\n\r");
	 			putsUart0("Process already running!");
	 			putsUart0("\n\r");
	 			return(1);
	 		}

		}

		else
			for(j=0;j<strlen(tcb[i].name);j++)
		 			tcb_temp[j]=0;
	}
	 return(0);
}
void shell()
{
uint8_t i,Process_temp;
while (true)
  {
    // REQUIRED: add processing for the shell commands here through the UART
	for(i=0;i<15;i++)
			str[i]=NULL;
		getsUart0();
	  if(strstr(str,"ps"))
		  ps();
	  else if(strstr(str,"ipcs"))
	  {
		  putsUart0("\r\n");
		 ipcs();
	  }
	  else if(strstr(str,"kill"))
		  kill();
	  else if(strstr(str,"reboot"))
	  {
		  putsUart0("\r\n");
		  __asm("    .global _c_int00\n"
		  		"    b.w     _c_int00");
		  putsUart0("\r\n");
	  }
	  else if(strstr(str,"pidof"))
		  pidof();
	  else
	  {
		  Process_temp=process();
		  if(Process_temp==0)
		  {
			  putsUart0("\r\n");
			  putsUart0("Invalid Command!");
		  }
	  }
  }
}


//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(void)
{
  bool ok;

  // Initialize hardware
  initHw();
  rtosInit();

  // Power-up flash
  RED_LED = 1;
  waitMicrosecond(250000);
  RED_LED = 0;
  waitMicrosecond(250000);

  // Initialize semaphores
  keyPressed = createSemaphore(1, "KeyPressed");
  keyReleased = createSemaphore(0, "keyReleased");
  flashReq = createSemaphore(5, "flashReq");
  resource = createSemaphore(1, "resource");

  // Add required idle process
  ok =  createThread(idle, "Idle", 15);
  //ok =  createThread(idle2, "Idle2", 0);

  // Add other processes
 ok &= createThread(lengthyFn, "LengthyFn", 12);
 ok &= createThread(flash4Hz, "Flash4hz", 4);
 ok &= createThread(oneshot, "OneShot", 4);
  ok &= createThread(readKeys, "ReadKeys", 8);
  ok &= createThread(debounce, "Debounce", 8);
  ok &= createThread(important, "Important", 0);
 ok &= createThread(uncooperative, "Uncoop", 10);
 ok &= createThread(shell, "Shell", 8);

  // Start up RTOS
  if (ok)
    rtosStart(); // never returns
  else
    RED_LED = 1;

return 0;
  // don't delete this unreachable code
  // if a function is only called once in your code, it will be
  // accessed with two goto instructions instead of call-return,
  // so any stack-based code will not function correctly
  yield(); sleep(0); wait(0); post(0);
}

