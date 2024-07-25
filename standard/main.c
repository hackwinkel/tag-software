/*******************************************************************************
* (c) 2024 by Theo Borm
* see LICENSE file in the root directory of this repository
*
*
* ABOUT THE HARDWARE & SOFTWARE
*
* This firmware Controls the LEDs on the LED electronic name-tag
*
* Tags can communicate with each other using infrared two-way communications,
* and adopt the pattern they display accordingly:
*
* Mode 0: tags will "randomly" blink LEDs (PATTERN A) in a slowly changing color.
*
* Mode 1: The tag will display a "chaser" pattern (B). colors will change gradually.
*
* A tag monitors if any other tag(s) in the neighborhood transmits an IR code,
* and will itself transmit an IR code to all other tags in the area approximately
* once every 60 seconds. During the time a tag is transmitting it ignores other
* tags
*
* If a tag is displaying pattern A and receives an IR code, it immediately
* switches to pattern B. If a tag does NOT receive an IR code for about 60
* seconds, it switches back to pattern A, otherwise it continues with pattern B
*
* Uses a timer T16 interrupt for delays and display timing
* Uses timer T2 to generate 38Khz IR modulation signal
*
* 24 RGB LEDs are conected in a charleyplexed array. Every RGB LED is actually 3
* separate LEDs, for a total of 72 LEDs. Only one of these 72 LEDs will be lit
* at a time. If we want to make it appear as if 3 separate RGB LEDs are lit
* with 1 bit per color (for a total of 8 colors including black), then we should
* sequentially juggle switching on/off 9 LEDs each frame. At 1 tick=0.5ms, this
* means a "frame rate" of 222 frames/s
* If we use 2 bits per color and switch on a LED for either 0, 1, 2 or 3 ticks
* per frame, we can maintain a frame rates of 74 frames/s with 3 RGB LEDs
*
* Every interrupt only one of the component LEDs will be lit, so we have to
* figure out which one, then retrieve the bitbang values and subsequently
* program PA, PAC, PB and PBC accordingly. The actual time spent in this code
* should be short enough and (almost) constant, to prevent jitter
*
* The processor uses 9 pins to control the LEDs. Each of these 9 pins can (at
* any one moment) be programmed as input (high impedance state), output high or
* output low. At any time at most one pin should be high, one pin low while all
* the others are in a high impedance state. If we use two 4 bit binary values to
* designate which pin should be high and low for each of the 72 component LEDs,
* then 72 bytes will suffice to store all necessary combinations. These values
* can then be converted into the actual bit-bang values we need to send to the
* appropriate registers of the processor using a switch statement.
* pin <-> 4 bit number mapping is as follows:
*
* none	0000
* B0	0001
* B1	0010
* B3	0011
* B4	0100
* B5	0101
* B6	0110
* B7	0111
* A0	1000
* A7	1001
*
* When viewed on the PCB, going clockwise along the edge, in this file the
* RGB LEDs are numbered L00 - L23 starting from the hole at the top. This 
* differs from the D01 - D24 numbering in the hardware schematic diagram. The 
* following table contains columns for R, G and B respectively. In each row & 
* column there is a PAIR of pins. The first pin must be set to 1 and the second
* to 0 for the corresponding component LED to light up. All other pins must
* be set to High-Z
*
* 	SCHEMA	RED	GREEN	BLUE
* L00	D23	B4-B1	B3-B1	B5-B1 
* L01	D21	B1-B4	B1-B5	B1-B3
* L02	D19	B7-B0	B6-B0	A7-B0
* L03	D17	B0-B7	B0-A7	B0-B6
* L04	D15	B4-B0	B3-B0	B5-B0
* L05	D13	B0-B4	B0-B5	B0-B3
* L06	D02	B7-B1	B6-B1	A7-B1
* L07	D04	B1-B7	B1-A7	B1-B6
* L08	D06	B5-B3	B4-B3	B6-B3
* L09	D08	B3-B5	B3-B6	B3-B4
* L10	D10	A7-B3	B7-B3	B5-B4
* L11	D12	B3-A7	B4-B5	B3-B7
* L12	D11	A7-A0	B7-A0	B1-B0
* L13	D09	A0-A7	B0-B1	A0-B7
* L14	D07	B5-A0	B4-A0	B6-A0
* L15	D05	A0-B5	A0-B6	A0-B4
* L16	D03	B1-A0	B0-A0	B3-A0
* L17	D01	A0-B1	A0-B3	A0-B0
* L18	D14	B7-B4	B6-B4	A7-B4
* L19	D16	B4-B7	B4-A7	B4-B6
* L20	D18	B7-B5	B6-B5	A7-B5
* L21	D20	B5-B7	B5-A7	B5-B6
* L22	D22	A7-B6	B7-B6	A7-B7
* L23	D24	B6-A7	B7-A7	B6-B7
*
* this translates into the following 72 byte values which will be decoded into
* PA/PAC/PB/PBC values
*
*	SCHEMA	RED	GREEN	BLUE
* L00	D23	0x42	0x32	0x52 
* L01	D21	0x24	0x25	0x23
* L02	D19	0x71	0x61	0x91
* L03	D17	0x17	0x19	0x16
* L04	D15	0x41	0x31	0x51
* L05	D13	0x14	0x15	0x13
* L06	D02	0x72	0x62	0x92
* L07	D04	0x27	0x29	0x26
* L08	D06	0x53	0x43	0x63
* L09	D08	0x35	0x36	0x34
* L10	D10	0x93	0x73	0x54
* L11	D12	0x39	0x45	0x37
* L12	D11	0x98	0x78	0x21
* L13	D09	0x89	0x12	0x87
* L14	D07	0x58	0x48	0x68
* L15	D05	0x85	0x86	0x84
* L16	D03	0x28	0x18	0x38
* L17	D01	0x82	0x83	0x81
* L18	D14	0x74	0x64	0x94
* L19	D16	0x47	0x49	0x46
* L20	D18	0x75	0x65	0x95
* L21	D20	0x57	0x59	0x56
* L22	D22	0x96	0x76	0x97
* L23	D24	0x69	0x79	0x67
*
* The code deals with 3 RGB LEDs. Each RGB LED has a position (00-23) and a
* color (0-63). there are 3 component colors to deal with, 3 LEDs and 3 Time-
* phases to deal with for a total of 27 LED-component-time-phases
* 
*/






#include <stdint.h>
#include <device.h>
#include <calibrate.h>

/*
* PA3 and PA5 are used as debug status outputs
*/
volatile uint8_t debugstatus=0;
#define SETPA3 0x08
#define CLEARPA3 0xf7
#define SETPA6 0x40
#define CLEARPA6 0xbf


/*******************************************************************************
* configure/calibrate system clock source
*/
unsigned char _sdcc_external_startup(void)
{
	// use the IHRC oscillator, target 8MHz
	PDK_SET_SYSCLOCK(SYSCLOCK_IHRC_8MHZ);
	// calibrate for 8MHz operation @ 4000 mVolt
	EASY_PDK_CALIBRATE_IHRC(8000000,4000);
	return 0;   // keep SDCC happy
}


/*******************************************************************************
* provide a 16 bit tocks() counter AND controls LED (pattern) timing using T16.
* Each tick is approximately 0.5ms, every tock is 27 ticks
*
* A large part of the functionality is implemented in in an interrupt. The code
* for this below consists of four parts:
* -	Part 1: (global) variables and constants used by the other parts
* -	Part 2: T16 Interrupt setup
* -	Part 3: Handling LED display timing
* - 	Part 4: Handling LED pattern generation
* - 	Part 5: Handling the tocks() counting
*/


/******************************************************************************* 
* Part 1: Global variables and definitions
*/

/*
* ROM-based component LED to pin-pair translation table
* order: 24 red, 24 green, 24 blue + 1 "no LED"
*/
const uint8_t pp[]={
		0x42,0x24,0x71,0x17,0x41,0x14,0x72,0x27,0x53,0x35,0x93,0x39,
		0x98,0x89,0x58,0x85,0x28,0x82,0x74,0x47,0x75,0x57,0x96,0x69,
		0x32,0x25,0x61,0x19,0x31,0x15,0x62,0x29,0x43,0x36,0x73,0x45,
		0x78,0x12,0x48,0x86,0x18,0x83,0x64,0x49,0x65,0x59,0x76,0x79,
		0x52,0x23,0x91,0x16,0x51,0x13,0x92,0x26,0x63,0x34,0x54,0x37,
		0x21,0x87,0x68,0x84,0x38,0x81,0x94,0x46,0x95,0x56,0x97,0x67,
		0x00	};

/*
* ROM-based color sequence table. while 64 colors are possible, only 12/6 are used
* B,G.R
* 0,0,3 0,1,3 0,2,2 0,3,1 0,3,0 1,3,0 2,2,0 3,1,0 3,0,0 3,0,1 2,0,2 1,0,3
* 0,0,3 0,2,2 0,3,0 2,2,0 3,0,0 2,0,2 
* */
const uint8_t colors[]={ 0x03,0x07,0x0a,0x0d,0x0c,0x1c,0x28,0x34,0x30,0x31,0x22,0x13 };

/*const uint8_t colors[]={ 0x03,0x0a,0x0c,0x28,0x30,0x22 };*/

uint8_t colorcount=0;

/*
* LEDs are controlled using a 2kHz "tick rate". Pattern timing and timeouts are
* executed at a "tock rate" of 1/27th (the number of LEDComponentTimePhases)
* 74 Hz. Hence a 1 minute timeout corresponds to a tock counter value of 4444
*/

// The following (global) variable keeps track of the mode (pattern to display)
uint8_t mode=0;
// mode reverts to 0 after 1m timeout without received pulse
uint16_t irwatchdog=0; 
// use a watchdog timeout of 1m
#define irwatchdogtimeout 4444
// transmit a pulse after ~55s
#define transmitirpulseafter 4074
// actual pulse is 27 ms
#define irpulsetime 2
// and after the pulse, the tag is deaf for 27ms as well
#define irdeaftime 2



// The following (global) variables deal with the positions and colors of 3 RGB
// LEDs and the phases of lighting the LEDs - described in Part 3
volatile uint8_t LedPos[3];
volatile uint8_t LedCol[3];
volatile uint8_t LedComTimePhase; // count 0..26

// The following (global) variables and constants deal with the timing of the
// chaser pattern - See Part 4
 // Three position change counters for three different chasers
volatile uint8_t LedChaseCount[3];
// Three color change counter shared between the three different chasers
volatile uint8_t LedColorCount;

// change position ...s
#define chaserpositiontargetcount0 113
#define chaserpositiontargetcount1 11
#define chaserpositiontargetcount2 9
// change color every 3.42 s
#define chasercolortargetcount 253


// The following (global) variables and macro are used in the random pattern -
// See Part 4
volatile uint16_t randomnr=1;
volatile uint8_t randomposns[]={0,0,0}; // this will be kept filled with random positions

// first line of this definition makes sure that randomnr is non-zero
#define makerandom \
	randomnr |= randomnr == 0; \
 	randomnr ^= (randomnr << 13); \
	randomnr ^= (randomnr >> 9); \
	randomnr ^= (randomnr << 7)

// The following 16 bit counter is used to count tocks. overflows after 14m
volatile uint16_t elapsedtocks=0;


uint16_t previoustocks;          // used in waituntiltocks()

/******************************************************************************* 
* Part 2: Interrupt setup
*
* T16 can be clocked from several sources, use a clock divider and can generate
* an interrupt when a certain bit (8..15) changes.
* We assume the IHRC is calibrated to 16MHz, then dividing by 64 will produce
* a 250 KHz input clock to T16. After 256 clock pulses bit 8 will toggle, which
* is almost once a millisecond. To make it exactly twice a millisecond, we should
* preload T16 (which is an up-counter) with the value 134.
* 
*/
void setup_ticks() {
	T16M = (uint8_t)(T16M_CLK_IHRC | T16M_CLK_DIV64 | T16M_INTSRC_8BIT);
	T16C=134;
	elapsedtocks=0;
	INTEN |= INTEN_T16;
}


/*
* variables used in the interrupt routine. These are declared in global scope
* because sdcc apparently doesn't like it if there are too many variables
* that are allocated in the interrupt context
*/
volatile uint8_t intt;
volatile uint8_t intda;
volatile uint8_t intca;
volatile uint8_t intdb;
volatile uint8_t intcb;

/*******************************************************************************
* The interrupt handling code contains Parts 3/4/5, but starts off with a
* function header and generics:
*/
void interrupt(void) __interrupt(0)
{
	if (INTRQ & INTRQ_T16)
	{
		INTRQ &= ~INTRQ_T16; // Mark as processed
		T16C=134;

/******************************************************************************* 
* Part 3: Handling LED display timing
* every interrupt:
*	LedPhase deterines which RGB LED (0,1,2) will display
*	ComPhase deterines which color component of this RGB LED will display
*	TimePhase (0,1,2) determines if we display the lower or upper brightness bit
* when combined, there are 27 LedComTimePhase-s
*/		
		
		// get the RGB component that we should light this phase:
		intt=72; // default: no RGB component
		switch (LedComTimePhase)
		{
			case 0: if (LedCol[0]&0x01) intt=LedPos[0]; break; // red low bit first LED
			case 1: if (LedCol[0]&0x04) intt=LedPos[0]+24; break; // green low bit first LED
			case 2: if (LedCol[0]&0x10) intt=LedPos[0]+48; break; // blue low bit first LED
			case 3: if (LedCol[1]&0x01) intt=LedPos[1]; break; // red low bit second LED
			case 4: if (LedCol[1]&0x04) intt=LedPos[1]+24; break; // green low bit second LED
			case 5: if (LedCol[1]&0x10) intt=LedPos[1]+48; break; // blue low bit second LED
			case 6: if (LedCol[2]&0x01) intt=LedPos[2]; break; // red low bit third LED;
			case 7: if (LedCol[2]&0x04) intt=LedPos[2]+24; break; // green low bit third LED
			case 8: if (LedCol[2]&0x10) intt=LedPos[2]+48; break; // blue low bit third LED
			case 18:
			case 9: if (LedCol[0]&0x02) intt=LedPos[0]; break; // red high bit first LED
			case 19:
			case 10: if (LedCol[0]&0x08) intt=LedPos[0]+24; break; // green high bit first LED
			case 20:
			case 11: if (LedCol[0]&0x20) intt=LedPos[0]+48; break; // blue high bit first LED
			case 21:
			case 12: if (LedCol[1]&0x02) intt=LedPos[1]; break; // red high bit second LED
			case 22:
			case 13: if (LedCol[1]&0x08) intt=LedPos[1]+24; break; // green high bit second LED
			case 23:
			case 14: if (LedCol[1]&0x20) intt=LedPos[1]+48; break; // blue high bit second LED
			case 24:
			case 15: if (LedCol[2]&0x02) intt=LedPos[2]; break; // red high bit third LED;
			case 25:
			case 16: if (LedCol[2]&0x08) intt=LedPos[2]+24; break; // green high bit third LED
			case 26:
			case 17: if (LedCol[2]&0x20) intt=LedPos[2]+48; break; // blue high bit third LED
		}
		
		
		// generate the values to output on port A and B		
		intda=debugstatus;
		intca=0x48; // PA3 and PA6 are debug status outputs
		intdb=0; // no need to preserve IR transmitter output bit on PB as this is controlled by a hardware timer
		intcb=0x04; // PB2 (IR transmitter) is always an output
		switch((pp[intt])>>4)
		{
			case 0: intcb=0x04; break; // some shit must be here otherwise SDCC complains about unreachable code
			
			case 1: intdb|=0x01; intcb=0x05; break;
			case 2: intdb|=0x02; intcb=0x06; break;
			case 3: intdb|=0x08; intcb=0x0c; break;
			case 4: intdb|=0x10; intcb=0x14; break;
			case 5: intdb|=0x20; intcb=0x24; break;
			case 6: intdb|=0x40; intcb=0x44; break;
			case 7: intdb|=0x80; intcb=0x84; break;
			case 8: intda|=0x01; intca=0x49; break;
			case 9: intda|=0x80; intca=0xc8; break;
		}
		switch((pp[intt])&0x0f)
		{
			case 0: intcb=0x04; break; // some code must be here otherwise SDCC complains about unreachable code
			case 1: intcb|=0x01; break;
			case 2: intcb|=0x02; break;
			case 3: intcb|=0x08; break;
			case 4: intcb|=0x10; break;
			case 5: intcb|=0x20; break;
			case 6: intcb|=0x40; break;
			case 7: intcb|=0x80; break;
			case 8: intca|=0x01; break;
			case 9: intca|=0x80; break;
		}
		// actually output the resulting values		
		PAC=intca;
		PA=intda;
		PBC=intcb;
		PB=intdb;		
		
		/*
		* We have handled the display of leds now, for this to work, we still need to increment the phase
		* Coming here we can also do other stuff that needs to be done at the tock rate (which is 1/27th
		* of the tick rate. By doing different things in different phases we can spread this work to keep
		* the amount of work that needs to be done in any one interrupt smaller.
		* What actually needs to be done partially depends on the mode.
		*
		* phase 0-8: update position of led 0-2
		* phase 9-20: update random numbers
		* phase 20-25: update color led 0-2
		* phase 26: make sure LedComTimePhase increments to 0 after this
		*/
		
		
		switch (LedComTimePhase)
		{
			case 0: LedChaseCount[0]=LedChaseCount[0]-1; break;
			case 1: if (LedChaseCount[0]==0)
				{
					if (mode)
					{
						if (LedPos[0]>22) LedPos[0]=0;
						else LedPos[0]++;
					}
					else
					{
						LedPos[0]=randomposns[0];
					}
				}
				break;
			case 2: if (LedChaseCount[0]==0) LedChaseCount[0]=chaserpositiontargetcount0;
				break;
			case 3: LedChaseCount[1]=LedChaseCount[1]-1; break;
			case 4: if (LedChaseCount[1]==0)
				{
					if (mode)
					{
						if (LedPos[1]<1) LedPos[1]=23;
						else LedPos[1]--;
					}
					else
					{
						LedPos[1]=randomposns[1];
					}
				}
				break;
			case 5: if (LedChaseCount[1]==0) LedChaseCount[1]=chaserpositiontargetcount1;
				break;
			case 6: LedChaseCount[2]=LedChaseCount[2]-1; break;
			case 7: if (LedChaseCount[2]==0)
				{
					if (mode)
					{
						if (LedPos[2]>22) LedPos[2]=0;
						else LedPos[2]++;
					}
					else
					{
						LedPos[2]=randomposns[2];
					}
				}
				break;
			case 8: if (LedChaseCount[2]==0) LedChaseCount[2]=chaserpositiontargetcount2;
				break;
			
			
			
			
			case 9:	makerandom;
				break;
			case 10: if ((randomnr & 0x18) != 0x18)
				{
					randomposns[0]=randomnr&0x1f;
				}
				break;
			case 11: makerandom;
				break;
			case 12: if ((randomnr & 0x18) != 0x18)
				{
					randomposns[1]=randomnr&0x1f;
				}
				break;
			case 13: makerandom;
				break;
			case 14: if ((randomnr & 0x18) != 0x18)
				{
					randomposns[2]=randomnr&0x1f;
				}
				break;
			case 15: makerandom;
				 break;
			case 16: if ((randomnr & 0x18) != 0x18)
				{
					randomposns[0]=randomnr&0x1f;
				}
				break;
			case 17: makerandom;
				 break;
			case 18: if ((randomnr & 0x18) != 0x18)
				{
					randomposns[1]=randomnr&0x1f;
				}
				break;
			case 19: makerandom;
				 break;
			case 20: if ((randomnr & 0x18) != 0x18)
				{
					randomposns[2]=randomnr&0x1f;
				}
				break;
			case 21: LedColorCount--; break;
			case 22: if (LedColorCount==0) 
				{
					LedColorCount=chasercolortargetcount;
					if (colorcount>10) { colorcount=0; }
					else { colorcount++; }
				}
				break;
			case 23:  LedCol[0]=colors[colorcount]; break;
			case 24: if (colorcount<8) { LedCol[1]=colors[colorcount+4]; }
				else { LedCol[1]=colors[colorcount-8]; }
				break;
			case 25: if (colorcount<4) { LedCol[2]=colors[colorcount+8]; }
				else { LedCol[2]=colors[colorcount-4]; }
				break;
			case 26:
				LedComTimePhase=0xff;
				if (irwatchdog<irwatchdogtimeout)
				{
					irwatchdog=irwatchdog+1;
					debugstatus |= SETPA6;
				} 
				else
				{
					mode = 0;
					debugstatus =0; // changing to mode 0 clears PA3 and PA6
				}
				elapsedtocks++;
				break; 
		}
		
		LedComTimePhase++;	
		
		
	}
}



/*******************************************************************************
* This function returns the number of tocks since starting the interrupt.
* 16 bit operations are non-atomic on this 8 bit microcontroller, so we must
* disable the T16 interrupt and then re-enable it after reading the value
*/
uint16_t tocks() {
	INTEN &= ~INTEN_T16;
	uint16_t current = elapsedtocks;
	INTEN |= INTEN_T16;
	return(current);
}

/*******************************************************************************
* This function returns true if the IR watchdog timer has expired without
* receiving a new pulse
* 16 bit operations are non-atomic on this 8 bit microcontroller, so we must
* disable the T16 interrupt and then re-enable it after reading the value
*/
uint8_t get_irwatchdog_state() {
	INTEN &= ~INTEN_T16;
	uint16_t current =irwatchdog;
	INTEN |= INTEN_T16;
	
	// this is funky - appears to return true after ~ 1 s instead of 1m
	// DO NOT USE THIS - solved in a different way
	return(current>=irwatchdogtimeout);
}

/*******************************************************************************
* This function resets the value of the ir watchdog timer to zero
* 16 bit operations are non-atomic on this 8 bit microcontroller, so we must
* disable the T16 interrupt and then re-enable it after reading the value
*/
void reset_irwatchdog() {
	INTEN &= ~INTEN_T16;
	irwatchdog=0;
	INTEN |= INTEN_T16;
}
/*******************************************************************************
* This function presets the value of the ir watchdog timer to a timeout
* 16 bit operations are non-atomic on this 8 bit microcontroller, so we must
* disable the T16 interrupt and then re-enable it after reading the value
*/
void preset_irwatchdog() {
	INTEN &= ~INTEN_T16;
	irwatchdog=irwatchdogtimeout;
	INTEN |= INTEN_T16;
}


/*******************************************************************************
* waituntiltocks() waits for <time> tocks. During this time the IR detection
* pin will be monitored if monitor != 0. if the pin is monitored and low,
* then the 16 bit irwatchdog counter will be reset to 0.
*/

void waituntiltocks(uint16_t ttt, uint8_t monitor)
{
	uint16_t currenttocks;
	while (((currenttocks= tocks()) - previoustocks) < ttt)
	{
		if (monitor){
			if ((PA &0x10)==0)
			{
				reset_irwatchdog();
				mode=1;
				debugstatus|=SETPA3; // changing to mode 1 sets PA3
			}
		}
	}
    	previoustocks += ttt;
}



/**********************************

power on

pa6=L, pa3=L   line657
0x00

IR signal

pa6=H, pa3=H
0x48

1s

pa6=H, pa3=L
0x40

IR signal

pa6=H, pa3=H
0x48

1s

pa6=H, pa3=L
0x40

etcetera

***********************************/

/*******************************************************************************
* main program - consists of setup and ... nothing
*/
void main()
{
	// Initialize hardware:
  	// DISABLE pull-ups on PB0-7, PA0, PA7
  	// PA4 is the sync input, which requires the pull-up
  	// PA5 and PA6 are unused (used for programming) PA6 used as debugstatus output
	// PA3 is unused (available on header) 		 PA3 used as debugstatus output
  	// PA1 and PA2 are not available on the package
	
  	PAPH = 0x36;							// xxxx was 0x7e;
	PBPH = 0x00;
  	// set registers low
  	PA=0x00;
  	PB=0x00;
  
	// IR LED is active high source/sink on the PB2 pin - set only PB2 as output
	PAC=0x48;							// xxxx was 0x00;
	PBC=0x04;
  	
	mode=0;
	debugstatus=SETPA6; // initial state = PA6 HIGH, PA3 LOW
	preset_irwatchdog();
	PA=debugstatus;
	
 	// setup the positions and the colors of the three RGB LEDs 
  	LedPos[0]=0;
	LedPos[1]=8;
	LedPos[2]=16;
	LedCol[0]=0x03;
	LedCol[1]=0x0c;
	LedCol[2]=0x30;
	
	// setup the led chaser timing variables with differnt values for speed, but changing color at the same pace
	LedChaseCount[0]=chaserpositiontargetcount0;
	LedChaseCount[1]=chaserpositiontargetcount1;
	LedChaseCount[2]=chaserpositiontargetcount2;
	LedColorCount=chasercolortargetcount;

	// setup and start the timer (and thus the display)
	setup_ticks();
	INTRQ = 0;
	__engint();                     // Enable global interrupts
	
	previoustocks=tocks();

	
	
	while (1)
	{

		waituntiltocks(transmitirpulseafter,1); // do monitor input
		// start transmitting an IR pulse
		/* we want to generate an ~27 ms long 38kHz sync pulse on PB2 using timer 2
		* IHRC 16MHz, 16000000/422=37.914 KHz
		* TM2C [7:4]=0010 -> select IHRC
		* TB2C [3:2]=01 -> output on PB2 (00=disable)
		* TM2C [1] = 0 -> period mode
		* TM2C [0] = 0 -> do not invert
		* TM2S [7] = 0 -> 8 bit resolution
		* TM2S [6:5]=00 -> prescaler 1
		* TM2S [4:0]=00000 -> scaler 1
		* TM2B [7:0] -> 211
		*/
		TM2C=0; // stop
		TM2CT=0;
		TM2B=211;
		TM2S=0; // clear the counter
		TM2C=0b00100100; // go!
		waituntiltocks(irpulsetime,0); // let it run for ~ 27 ms without monitoring IR input
		// stop transmitting the IR pulse
		TM2C=0; // stop PWM
		PB &= 0xfb; // make sure IR LED is off
		waituntiltocks(irdeaftime,0); // be deaf for IR pulses a little longer
	}
}

