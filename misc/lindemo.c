//file lindemo.c
//runs on AITI m162/m88 bd with TH8062 ibus lin driver
//uart0 is kbus from TH8062 9600 8,e,1
//uart1 is console thru rs232 level translator bd 115200bps
//compiles with iccv7avr

//Apr 8 12 Bob G cvt to run on BMW X5, bus on uart0, console on uart1
//Apr 25 2012 Bob G BMW X5 uses ISO9141, 10400bps ? or Kbus 9600 bps?
//May 1 2012 Bob G inital edit BMW version from NavigatorUBP version
//May 22 2012 Bob G
//May 24 2012 Bob G chg to 8,n,1
//May 31 2012 Bob G K bus is I bus, so 9600?
//June 1 2012 Bob G fishing for pkt to clear SELF LEVEL SUP. INACT msg
//June 6 2012 Bob G disab rx by writing to ucsr0b, not a!
//June 11 2012 Bob G back on kbus
//June 13 2012 Bob G add sendloop
//June 15 2012 Bob G seems to be working
//June 18 2012 Bob G leave rx enabled, just turn rxint off and on

#include <iccioavr.h> //mega162 io
#include <avrdef.h>   //stackcheck
#include <stdio.h>    //cprintf
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INTR_OFF() asm("CLI")
#define INTR_ON()  asm("SEI")

//------vars in ram bss-------
char junk;
#define N 96
unsigned char rxbuf0[N]; //uart0 receive interrupt buffers (kbus 9600)
unsigned char rxbuf1[N]; //uart1 receive interrupt buffers (console 115200)
unsigned char rxindx0;
unsigned char rxondx0;
unsigned char rxindx1;
unsigned char rxondx1;
unsigned char fd; //0=uart0 1=uart1
//unsigned char tmpstr[12];
//unsigned char dispon;  //display on
//unsigned char os1sec;  //one shots
//unsigned char os1min;  //one min
//unsigned char os250m;  //250ms
//unsigned char secs,mins,hrs; //running time
//unsigned char tics8;  //incremented in timer2 handler 48usec
//unsigned char initializing; //sending init
//unsigned char initialized;  //0x55 returned
//unsigned char os20ms;
//unsigned char os5ms;
//unsigned char os250ms;
//unsigned char flasher;
//int ms250accum;
//int ms20accum;
//int ms5accum;
unsigned int tics;
//unsigned int ticsl;
//unsigned int deltat100usec;

//------flash rom---------
__flash unsigned char banner[]={"lindemo Feb 8 14  q=menu\n"};

//--------------------
void port_init(void){
	//mega162

	PORTA = 0xff; //pullups on
	DDRA  = 0x00;

	PORTB = 0x0c; //pullups off except rx1,tx1
	DDRB  = 0x08; //pb3=uart 1 tx

	PORTC = 0xff; //pullups on
	DDRC  = 0x00; //inputs

	PORTD = 0x03; //pullup on pd0,1  rx0,tx0
	DDRD  = 0x40; //pd6 led 1  pd3=int1

	PORTE = 0x00;
	DDRE  = 0x00;
}

//--------------------
//TIMER0 initialize - prescale:8
// WGM: Normal
// desired value: 100uSec   (one shot generator)
// actual value: 99.826uSec (0.2%)
void timer0_init(void){
	TCCR0= 0x00; //stop
	TCNT0= 0x48; //set count
	OCR0 = 0xB8; //set compare value
	TCCR0= 0x02; //start timer
}

#if 0
//--------------------
//TIMER2 initialize - prescale:8
// WGM: Normal
// desired value: 20800Hz 48usec   (half bit time of ISO9141 10400 bps)
// actual value: 20945.453Hz (0.7%)
void timer2_init(void){
	TCCR2= 0x00; //stop
	ASSR = 0x00;
	TCNT2= 0xA8; //setup
	OCR2 = 0x55; //set compare value  0x58 theoretical, 0x55 impirical
	TCCR2= 0x02; //start
}
#endif

//--------------------
//UART0 initialize
// desired baud rate: Kbus 9600 8,e,1?
// actual: baud rate:
// char size: 8 bit
// parity: Disabled
void uart0_init(void){
	UCSR0B = 0x00; //disable while setting baud rate
	UCSR0A = 0x00; //disable while setting baud rate
	UBRR0L = 0x5f; //set baud rate  5f=9600 58=10400
	UBRR0H = 0x00;
	UCSR0C = 0xa6; //BIT(URSEL0) | 0x06;  8,n,1   a6=8,e,1
	UCSR0A = 0x00; //enable
	UCSR0B = 0x98; //98=rx int enable, rx,tx enable
}

//--------------------
//UART1 initialize
// desired baud rate:115200 console
// actual baud rate:
// char size: 8 bit
// parity: Disabled
void uart1_init(void){
	UCSR1B = 0x00; //disable while setting baud rate
	UCSR1A = 0x00; //disable while setting baud rate
	UBRR1L = 0x07; //baud rate 7=115200@14  0x5f=9600
	UBRR1H = 0x00;
	UCSR1C = 0x86; //BIT(URSEL1) | 0x06;
	UCSR1A = 0x00; //enable
	UCSR1B = 0x98; //rx int enable
}

//--------------------
void init_devices(void){

	CLI(); //disable all interrupts
	port_init();
	timer0_init();
	// timer2_init();
	uart0_init(); //kbus
	uart1_init(); //console

	MCUCR= 0x00;
	EMCUCR = 0x00;
	TIMSK= 0x00;   //2=t0 comp
	ETIMSK=0x00;
	GIFR=  0x00;
	GICR=  0x00;
	PCMSK0=0x00;
	PCMSK1=0x00;
	SEI(); //re-enable interrupts
}

//-------------------------
void initvars(void){
	//init conversion factors

	MCUCSR=0x80; //jtag off
	MCUCSR=0x80;
}

__flash unsigned char spindat[4]={'-','\\','|','/'};
unsigned char spindex;
//----------------------
void spin(void){
	//progress indicator

	putchar(spindat[spindex]);
	spindex++;
	if(spindex > 3) spindex=0;
	putchar('\r');
}

//-------------------
void delnms(int n){
	//delay n ms by counting (interrupt messes it up)
	int x;

	while(n--){
		x=2600;    //empirically determined fudge factor  14.7456 mhz
		while(x--);
	}
}

#if 0
//-------------------
void delnms(int n){
	//delay n ms by counting 48 usec tics, 21 per ms
	unsigned char trg;

	while(n--){
		trg=tics8 + 21; //1ms is 21 tics from now
		while(tics8 != trg){};
	}
}
#endif

#if 0
//-----------------
void flashled(unsigned char n){
	//flash led n times

	do{
		ledon();
		delnms(50);
		ledoff();
		delnms(50);
	}while(n--);
}
#endif

//----------rs232 subs--------------------
#if 0
unsigned char kbhit(void){
	//return nonzero if char waiting   polled version
	unsigned char b;

	b=0;
	if(UCSR0A & (1<<RXC0)) b=1;
	return b;
}

//-------------------------
unsigned char kbhit1(void){
	//return nonzero if char waiting   polled version
	unsigned char b;

	b=0;
	if(UCSR1A & (1<<RXC1)) b=1;
	return b;
}
#endif

//#if 0
//-------------------------
unsigned char kbhit0(void){
	//return nonzero if char waiting  intr version

	return(rxindx0 != rxondx0);
}

//-------------------------
unsigned char kbhit1(void){
	//return nonzero if char waiting  intr version

	return(rxindx1 != rxondx1);
}

//-------------------------
unsigned char kbhit(void){
	//return nonzero if char waiting  intr version
	unsigned char c;

	if(fd==0){
		c=kbhit0();
	}
	if(fd==1){
		c=kbhit1();
	}
	return c;
}

//-----------------------
int getchar0(void){
	//intr version
	unsigned char c;

	while(!kbhit0()){}; //wait for char
	c=rxbuf0[rxondx0++]; //get char from rxbuf
	if(rxondx0 > N-1) rxondx0=0;
	return c;
}

//-----------------------
int getchar1(void){
	//intr version
	unsigned char c;

	while(!kbhit1()){}; //wait for char
	c=rxbuf1[rxondx1++]; //get char from rxbuf
	if(rxondx1 > N-1) rxondx1=0;
	return c;
}

//-----------------------
int getchar(void){
	//from uart0 or 1
	unsigned char c;

	if(fd==0){
		c=getchar0();
	}
	if(fd==1){
		c=getchar1();
	}
	return c;
}

volatile char fe; //frame err
volatile char pe; //parity err
volatile char dov; //data overrun
//------------------------------
#pragma interrupt_handler uart0_rx_isr:iv_USART0_RX
void uart0_rx_isr(void){
	//uart0 has received a character in UDR0  mega162 version   k bus 9600
	char c,tmpstat;

	tmpstat=UCSR0A;
	if(tmpstat & 0x10) fe=1;     //break detected!
	if(tmpstat & 0x08) dov=1;    //data overrun!
	if(tmpstat & 0x04) pe=1;     //parity error detected!
	c=UDR0;                      //read char
	if(pe){
		cprintf(" PE%02x. ",c);    //report parity err to carbon based units
		pe=0;
	}
	if(fe){
		cprintf(" FE%02x. ",c);    //report framing err to carbon based units
		fe=0;
	}
	if(dov){
		cprintf(" DO%02x. ",c);    //report ovf err to carbon based units
		dov=0;
	}
	rxbuf0[rxindx0++]=c;         //put char in rxbuf0 anyway
	if(rxindx0 > N-1) rxindx0=0; //rewind index if needed
	//	putc1('+'); //debug
}

//------------------------------
#pragma interrupt_handler usart1_rx_isr:iv_USART1_RX
void usart1_rx_isr(void){
	//uart1 has received a character in UDR1  mega162 version  console 115200

	rxbuf1[rxindx1++]=UDR1; //put char in rxbuf
	if(rxindx1 > N-1) rxindx1=0;
}
//#endif

//-----------------------
int putc0(unsigned char c){
	//put char to usart0... dont add lf after cr

	while((UCSR0A & 0x20)==0){};	//20 is UDRE==0 means data register full
	UDR0=c; //send char
	return c;
}

//-----------------------
int putc1(unsigned char c){
	//put char to usart1... dont add lf after cr

	while((UCSR1A & 0x20)==0){};	//==0 means data register full
	UDR1=c; //send char
	return c;
}

//----------------------
int putchar0(unsigned char c)	{
	//adds lf after cr

	if(c=='\n'){
		putc0('\r');
	}
	putc0(c);
	return c;
}

//----------------------
int putchar1(unsigned char c)	{
	//adds lf after cr

	if(c=='\n'){
		putc1('\r');
	}
	putc1(c);
	return c;
}

//----------------------
int putchar(unsigned char c)	{
	//to uart0 or 1

	if(fd==0){
		putchar0(c);
	}
	if(fd==1){
		putchar1(c);
	}
	return c;
}

//----------------------
void crlf(void){
	//output cr lf

	putchar('\n');
}

//-------------------
void space(void){
	//output space

	putchar(' ');
}

//-------------------
void spaces(unsigned char n){
	//output n spaces

	while(n--)
		space();
}

#define ESC 0x1b
//----------------------
void gotoxy(int x, int y){
	//ansi cursor positioning sequence E[y;xH

	putchar(ESC);
	putchar('[');
	cprintf("%d",y);
	putchar(';');
	cprintf("%d",x);
	putchar('H');
}

//----------------------
void clrscr(void){
	//clear ansi terminal screen

	putchar(ESC);
	putchar('[');
	putchar('2');
	putchar('J');
}

//----------------------
void homecrsr(void){
	//home cursor

	putchar(ESC);
	putchar('[');
	putchar('f');
}

//------------------
void initscreen(void){
	//clear screen and print banner

	clrscr();
	homecrsr();
	cprintf(banner);
}

//-------------------------
unsigned char getche(void){
	//get and echo a char from rs232
	char c;

	c=getchar();
	putchar(c);
	return(c);
}

//-------------------------------------
void _StackOverflowed(char n){
	//called from _Stackcheck()  0=sw 1=hw

	INTR_OFF();
	if(n) putchar('H'); else putchar('S');
	while(1); //hang up
}

#define MONITOR
//#undef MONITOR
//#ifdef MONITOR
//-------------------------
unsigned char gethex(void){
	//return a hex char from rs232
	unsigned char b,c;

	b=0xff; //error return value
	c=toupper(getche());
	if(isxdigit(c)) b=c-0x30;      //if c ok, cvt ascii digit to binary
	if((c>='A') && (c<='F')) b-=7; //if c hex, cvt ascii A to binary 10
	return(b);
}

//---------------------------
unsigned char getbyte(void){
	//get 2 nibbles, return a binary byte from rs232
	unsigned char n1,n2;

	n1=gethex();
	n2=gethex();
	return((n1 << 4) + n2);
}

//----------------------------
unsigned int getaddr(void){
	//return addr from rs232
	unsigned int th,tl;

	th=getbyte();
	tl=getbyte();
	return((th << 8) + tl);
}

//-----------------
void getstr(unsigned char *p){
	//get a string and echo
	unsigned char c;

	do{
		c=getche();
		*p++=c;
	}while(c !=0x0d);
	p--;     //roll p back
	*p=0x00; //add null over cr
}

//#endif //monitor
//---------end of rs232 subs-----------

//#ifdef MONITOR
//---------debug subs-----------
void dumpdata256(unsigned char *n){
	//dump 16 rows of 16 bytes from n (data space)
	unsigned char r,c;
	unsigned char *p, *pa, ch;

	p=n;
	crlf();
	spaces(6);
	for(c=0; c<16; c++){
		cprintf("%02x ",c);      //header
		if(c==7) space();
	}
	crlf();
	crlf();
	for(r=0; r<16; r++){
		cprintf("%04x  ",p);     //print addr  at beg of line
		pa=p;                   //remember p for ascii
		for(c=0; c<16; c++){
			cprintf("%02x ",*p++); //print hex
			if(c==7) space();
		}
		for(c=0; c<16; c++){
			ch=*pa++;
			if((ch > 0x20) && (ch != 0x0a) && (ch != 0x8a)) //if printing char
				putchar(ch);   //print ascii
			else
				putchar('.');
			if(c==7) space();
		}
		crlf();
	}
}

//---------------------------
void dumpcode256(__flash char *n){
	//dump 16 rows of 16 bytes from n (code space)
	unsigned char r,c,ch;
	__flash unsigned char *p, *pa;

	p=n;
	crlf();
	spaces(6);
	for(c=0; c<16; c++){
		cprintf("%02x ",c);      //header
		if(c==7) space();
	}
	crlf();
	crlf();
	for(r=0; r<16; r++){
		cprintf("%04x  ",p);     //print addr  at beg of line
		pa=p;                   //remember p for ascii
		for(c=0; c<16; c++){
			cprintf("%02x ",*p++); //print hex
			if(c==7) space();
		}
		for(c=0; c<16; c++){
			ch=*pa++;
			if((ch > 0x20) && (ch != 0x0a) && (ch != 0x8a)) //if printing char
				putchar(ch);   //print ascii
			else
				putchar('.');
			if(c==7) space();
		}
		crlf();
	}
}

//------------------
void examinedata(void){
	//ask for data mem range and dump
	unsigned char *from;
	unsigned char c;

	cprintf("from:");
	from=(unsigned char *)getaddr();
	while(c!='q'){
		dumpdata256(from);
		cprintf("np or q...");
		c=getchar();
		if(c=='n') from+=0x100;
		if(c=='p') from-=0x100;
	}
}

//------------------
void examinecode(void){
	//ask for code mem range and dump
	__flash unsigned char *from;
	unsigned char c;

	cprintf("from:");
	from=(__flash unsigned char *)getaddr();
	while(c!='q'){
		dumpcode256(from);
		cprintf("np or q...");
		c=getchar();
		if(c=='n') from+=0x100;
		if(c=='p') from-=0x100;
	}
}

//------------------
void deposit(void){
	//ask for addr and data
	unsigned char *at;
	unsigned char c;
	unsigned char nh,nl;

	cprintf("at:");
	at=(unsigned char *)getaddr();
	while(1){
		cprintf(" %02x ",*at);
		nh=gethex();
		if(nh==0xff) return;
		nl=gethex();
		if(nl==0xff) return;
		c=((nh << 4) | nl);
		*at++=c;
	}
}

//------------------
void fill(void){
	//ask for data mem range and fill char and fill
	unsigned char *from, *to, with;

	cprintf("from:");
	from=(unsigned char *)getaddr();
	cprintf(" to:");
	to=(unsigned char *)getaddr();
	cprintf(" with:");
	with=getbyte();
	memset(from,with,to-from);
}

//-----------------------------
void (* fn)(void); //fn prototype

void dojsr(void){
	//ask for flash addr, jsr to it
	__flash unsigned char *to;

	cprintf(" to:");
	to=(__flash unsigned char *)getaddr();
	fn=(void *)to;
	(*fn)();  //call function fn and returns
}
//#endif

//#ifdef MONITOR
//------------------
void monitormenu(void){
	//monitor type cmds

	cprintf("cmds:\n"
			" e examine data\n"
			" c examine code\n"
			" d deposit\n"
			" f fill\n"
			" j jsr\n"
			" q quit\n"
			);
}

//------------------
void monitorloop(void){
	//examine and deposit mem regs
	char c;

	monitormenu();
	while(c != 'q'){
		crlf();
		cprintf("cmd:");
		c=getche();
		crlf();
		switch(c){
			case 'e': examinedata(); break;
			case 'c': examinecode(); break;
			case 'd': deposit(); break;
			case 'f': fill();    break;
			case 'j': dojsr();   break;
			default: monitormenu();
		}
	}
}
//#endif
//----end of debug subs----------

#if 0
//--------------------
void updatetime(void){
	//update deltat and time of day
	static unsigned int os250accum,os250cnt;
	unsigned int tmptics;

	INTR_OFF();
	tmptics=tics;
	INTR_ON();
	deltatms=tmptics-ticsl;  //number of 1ms tics in last pass
	ticsl=tmptics;

	os1sec=os500m=os250m=0; //clear oneshots

	os250accum+=deltatms;
	if(os250accum > 250){ //accumulated 250 ms worth of tics yet?
		os250m=1;           //yes, set 250 ms oneshot
		os250accum -= 250;  //adjust accum
	}
	if(os250m){
		os250cnt++;
		flash250= !flash250;
	}
	if(os250m && (os250cnt & 0x01)){ //every other os250m
		os500m=1;                      //set os500m
		flash500= !flash500;
	}
	if(os250cnt > 4){ //counted 4 os250s yet?
		os1sec=1;       //yes, set os1sec
		os250cnt -=4;   //adjust count
	}
	if(os1sec){  //tod clock
		secs++;
		if(secs > 59){
			secs=0;
			mins++;
			if(mins > 59){
				mins=0;
				hrs++;
				if(hrs > 23){
					hrs=0;
				}//hrs
			}//mins
		}//secs
	}//os1sec
}
#endif

//----------------------
#pragma interrupt_handler timer0_comp_isr:iv_TIMER0_COMP
void timer0_comp_isr(void){
	//100usec interrupt for timing... 10 per ms

	tics++;          //every 100usec
}

#if 0
//----------------------------
void generateoneshots(void){
	//generate timing oneshots
	unsigned int tmptics;

	INTR_OFF();
	tmptics=tics; //critical region
	INTR_ON();
	deltat100usec=tmptics-ticsl;  //number of 100usec tics in last pass
	//  if(deltatms > 32767) cprintf("                       negative deltatms!\n");
	ticsl=tmptics;

	ms5accum+=deltat100usec;
	if(ms5accum > 50){
		os5ms=1;
		ms5accum-=50;
	}

	ms20accum+=deltat100usec;
	if(ms20accum > 200){
		os20ms=1;
		ms20accum-=200;
	}

	ms250accum+=deltat100usec;
	if(ms250accum > 2500){
		os250ms=1;
		ms250accum-=2500;
		flasher=!flasher;
	}
}
#endif

#define LEDON()  PORTD &= ~0x40
#define LEDOFF() PORTD |=  0x40

#define SET0()   PORTD &= ~0x02; //K line lo
#define SET1()   PORTD |=  0x02; //K line hi

#define UARTDISAB() UCSR0B=0x00
#define UARTENAB()  UCSR0B=0x98

#define RXINTDISAB() UCSR0B=0x18
#define RXINTENAB()  junk=UCSR0A; junk=UDR0; UCSR0B=0x98

//------------------------------
void sendbreak(void){
	//assert K line lo for 6 char times

	UARTDISAB();
	SET0();    //k line lo
	delnms(6);
	SET1();    //k line hi
	UARTENAB();
}

//--------------
void termmode01(void){
	//uart0 iso 10400 to uart1 console 115200
	char c0,c1;

	cprintf("term mode  uart0 to uart1   ctl-e to exit\n");
	while(1){
		if(kbhit0()){           //iso
			c0=getchar0();
			//			LEDON();
			cprintf("%02x ",c0);
			//			LEDOFF();
		}
		if(kbhit1()){
			c1=getchar1();
			if(c1 == 0x05) return; //ctl-e to exit
			putchar0(c1);
		}
	}
}

#if 0
//-----------------------------------------
unsigned char calcchksum(char n){
	//calc chksum of n chars in txbuf
	char i,result;

	result=0;
	for(i=0; i<n; i++){
		result+=txbuf[i];
	}
	return result;
}

//-----------------------------------------
void sendtxbuf(char n){
	//send n chars from txbuf   is there a space between chars?
	char i;

	for(i=0; i<n; i++){
		putc0(txbuf[i]);
	}
}
#endif

//-----------------Kbus--------------------------------
//05 BMW X5 Kbus electrically same as Ibus
//9600 8,e,1
//example pkts from dump:
//src cnt tgt  data     xor cksm
// 44  05  bf  74 04 03 87       keyoff   44 is ews
// 44  05  bf  74 04 ff 74       keyout
// 44  04  bf  64 00 02 88       keyin

// e8  05  bf  59 21 02 47  rain sensor
// e8  05  d0  59 21 02 47
// e8  05  bf  10 00 05 ??
//0xbc is the |> <| char?

// 00  05  bf  7a 11 13 d2 tailgate open, ehc out   from gm, 7a is door/window status
// 00  05  bf  7a 11 33 e2 tailgate close, ehc out
// 00  05  bf  7a 10 13 c3 tailgate open, ehc in
// 00  05  bf  7a 50 33 a3 tailgate close, ehc out

//80 05 bf 18 00 05 27    mph rpm
//80 06 bf 19 1c 21 00 27 air water oil C

//ac 04 bf 02 00 ck       ehc resp to status poll
//ac 05 bf 61 01 80 03 ck height switch 61=req 01=sw 80=pump 03=led

//how to send msg from kbus to ibus? (theory: just send to tgt addr on ibus)

//------------------------------------
void waitforgap50(void){
	//wait for a 50 ms gap between packets
	unsigned char c2;
	unsigned int gapms;

	gapms=0;
	while(1){
		if(kbhit1()){ //from humans uart1
			return;
		}
		if((kbhit0()) || (gapms > 50)){       //check for char from kbus uart0
			return;
		}
		gapms++;
		delnms(1); //time for next char to arrive
	}//while
}

//------------------------------------
void flush0(void){
	//flush chars from uart0
	char c;

	while(kbhit0()){ //char from uart0
		c=getchar0();  //get the char
		cprintf("%02x ",c); //uart1
	}//while
	crlf();
}

unsigned char chksm; //xor cksm
//----------------------------
void dumpraw(void){
	//dump raw bytes from uart0
	char c1,c2;

	cprintf("dump raw bytes   q=quit\n");
	c1=0;
	while(1){
		if(kbhit1()){           //char from console?
			c1 = getchar1();
			if(c1 == 'q') return;
		}
		if(kbhit0()){           //char waiting in uart0 buffer?
			c2 = getchar0();      //get it
			cprintf("%02x ",c2);  //print to console
			delnms(1);    //time for more chars to arrive (1ms per char at 9600)
			continue;
		}
		//		crlf();
		_StackCheck();
	}//while
	crlf();
}

#if 0
//---------------------------
void setbaud9600(void){

	UBRR0L=0x5f;
}

//---------------------------
void setbaud10400(void){

	UBRR0L=0x58;
}

//---------------------------
void eparityenab(void){
	//  UCSR0C = BIT(URSEL0) | 0x26; //8,e,1

	UCSR0C = 0xa6; //8,e,1
}

//---------------------------
void eparitydisab(void){
	//  UCSR0C = BIT(URSEL0) | 0x06; //8,n,1

	UCSR0C = 0x86; //8,n,1
}

//---------------------------
void databits8(void){

	UCSR0C = 0x86; //8,n,1
}

//---------------------------
void databits7(void){

	UCSR0C = 0x84; //7,n,1
}
#endif

//-----------------------------
void putc0wc(char c){
	//put c to uart, xor c into chksum

	putc0(c);
	chksm ^= c; //xor chksum
}

//-------------------------
void puts0wc(char *s){
	//send string to uart0, calc cksm
	char c;

	while(1){
		c = *s;
		if(c == 0) return;
		putc0wc(c);
		s++;
	}
}

#if 0
//-----------------------------------
void sendchkairsusp(void){
	//send lamp status 5b msg from ihk to ike   key E

	waitforgap50();
	//	flush0();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0x5b); //IHK integrated heating and air conditioning
	putc0wc(0x05);
	putc0wc(0x80); //IKE
	putc0wc(0x83); //unknown opcode
	putc0wc(0x00);
	putc0wc(0x08); //unknown msg
	putc0(chksm);  //xor chksm
	delnms(2);     //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}

//-----------------------------------
void sendchkairsuspoff(void){
	//send lamp status 5b from ihk to ike   key F

	waitforgap50();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0x5b); //IHK
	putc0wc(0x05);
	putc0wc(0x80); //IKE
	putc0wc(0x83);
	putc0wc(0x00);
	putc0wc(0x00);
	putc0(chksm); //xor chksm
	delnms(2);    //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}

//-----------------------------------
void sendac05BF(void){
	//send ehc status report  key H

	waitforgap50();
	LEDON();
	RXDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0xac); //GM
	putc0wc(0x05);
	putc0wc(0xbf); //bcast
	putc0wc(0x7a); //??
	putc0wc(0x63); //??
	putc0wc(0x53); //??
	putc0(chksm);  //xor chksm 0x39?
	delnms(2);     //time for cksm to shift out
	RXENAB();
	LEDOFF();
}

//                12345678901234567890
char testmsg[21]="ARNOTT AIR SUSP INC ";
//-----------------------------
void sendtestmsg(void){
	//try to write to IKE addr 0x80  key T

	waitforgap50();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0xac); //ac is ehc 30->check control module
	putc0wc(0x19); //25 chars after this incl 20 char msg
	putc0wc(0x80); //to the IKE inst cluster 0x80 or 0xe7? bf->broadcast hopefully from kbus over to ibus
	putc0wc(0x1A); //
	putc0wc(0x35); //to the bottom status line
	putc0wc(0x03); //00=text  01=text and gong  02=steady ><  3=flashing ><
	puts0wc(testmsg);
	putc0(chksm);  //check sum
	delnms(2);     //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}

//              1234567
char obcmsg[7]="HIBOB ";
//-----------------------------
void sendobcmsg(void){
	//try to write to IKE status line  key 0

	waitforgap50();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0xac); //ac=ehc 80=IKE
	putc0wc(0x0b); //11 chars after this incl 6 char msg
	putc0wc(0x80); //e7=OBC inst cluster
	putc0wc(0x24); //ANZV
	putc0wc(0x08); //count
	putc0wc(0x03); //1 text, 2 gong, 3 ><
	puts0wc(obcmsg);
	putc0(chksm);  //check sum
	delnms(2);     //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}

//-----------------------------
void sendAC04BF(void){
	//send ac 04 bf 02 00 ck  (turn off inact msg)  key G

	waitforgap50();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0xac); //ac=air susp panel  00=GM 80=IKE do=LCM
	putc0wc(0x04); //byte count
	putc0wc(0xbf); //
	putc0wc(0x02); //good status
	putc0wc(0x00); //good status
	putc0(chksm);  //check sum
	delnms(2);     //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}

//-----------------------------
void sendAC05BF(void){
	//key M  ac 05 bf 61 01 01

	waitforgap50();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0xac); //ac=ehc
	putc0wc(0x05); //byte count
	putc0wc(0xbf); //bcast
	putc0wc(0x61); //
	putc0wc(0x01); //switch
	putc0wc(0x01); //leds
	putc0(chksm);  //check sum
	delnms(2);     //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}

//-------------------------
void sendloop(void){
	//send ac 04 bf 02 00 ck

	//was
	//wait for d0 08 bf 5b 00 01 00 00 00 ck
	//send     d0 08 bf 5b 00 00 00 00 00 ck

	//wait for e8 05 d0 59 xx xx xx
	//send     e8 05 d0 59 00 00 ck

	//now
	//send       ac 04 bf 02 01 ck
	//send       ac 05 bf 61 01 00 ck

	unsigned char c;
	unsigned char rsrc,rlen,rtgt,rpid;

	cprintf("send loop    q=quit\n");
	flush0();
	cprintf("send ac04bf\n");
	sendAC04BF();              //send     ac 04 bf 02 00 ck
	flush0();
	cprintf("send ac05bf\n");
	sendAC05BF();              //send     ac 05 bf 61 01 00 ck
	c=0;
	while(c != 'q'){
		putchar1('.');
		if(kbhit1()){
			c=getchar1();
		}
		flush0();                 //print what came in
		cprintf("send ac04bf\n");
		sendAC04BF();             //send     ac 04 bf 02 00 ck

		flush0();                 //print what came in
		cprintf("send ac05bf\n");
		sendAC05BF();             //send     ac 05 bf 61 01 00 ck
		delnms(100);
	}//while
}
#endif

//----------------------------
void dumpkbus(void){
	//dump kbus packets
	//chars arrive 1 per ms in uart0
	//print out is 12 per ms on uart1
	char c1,c2,i;
	unsigned int gapms;

	cprintf("dump Kbus pkts    q=quit\n");
	c1=0;
	while(1){
		if(kbhit1()){         //check for console input
			c1 = getchar1();
			if(c1 == 'q') return;
		}
		if(kbhit0()){         //check for char from uart0 iso
			LEDON();
			if(gapms > 0){
				cprintf("           %4d ms\n",gapms);   //ms since last char
			}
			gapms=0;
			c2=getchar0();         //get the char
			cprintf("%02x ",c2);   //print it
			LEDOFF();
		}else{
			gapms++;
		}
		if((PINC & 0x01)==0){
			cprintf(" BR!\n");
		}
		delnms(1); //time for next char to arrive
		_StackCheck();
	}//while
	crlf();
}

//----------------------------
void filterkbus(void){
	//filter kbus packets
	char c1,c2,i;
	unsigned int gapms;
	char cnt,srcaddr,dstaddr,srcok,dstok;

	cprintf("filter Kbus pkts to from addr ac    q=quit\n");
	c1=0;
	while(1){
		if(kbhit1()){         //check for console input
			c1 = getchar1();
			if(c1 == 'q') return;
		}
		if(kbhit0()){         //check for char from uart0 kbus
			LEDON();
			if(gapms > 0){
				cprintf("           %4d ms\n",gapms);   //
				cnt=srcok=dstok=0;
			}
			gapms=0;
			c2=getchar0();         //get the char
			if((c2 == 0xac)&&(cnt==0)){
				srcok=1;
				cnt++;
				cprintf("%02x ",c2);   //print it
				continue;
			}
			if(cnt==1){  //length
				cprintf("%02x ",c2);   //print it
				cnt++;
				continue;
			}
			if((c2 == 0xac)&&(cnt==2)){
				dstok=1;
				cprintf("%02x ",c2);   //print it
				cnt++;
				continue;
			}
			if(srcok || dstok){
				cprintf("%02x ",c2);   //print it
			}
			LEDOFF();
		}else{
			if(gapms < 255) gapms++;
		}
		if((PINC & 0x01)==0){
			cprintf(" BR!\n");
		}
		delnms(1); //time for next char to arrive
		_StackCheck();
	}//while
	crlf();
}

//--------------lin temperature stuff-----------------------
//lin master addr 0
//lin slaves addr 1=59
//pkt: src cnt tgt data cksum
//send temp opcode is 1

unsigned int t1,t2,t3,t4; //four temps to send back to master
unsigned char myaddr=7; //this should get saved and restored from eeprom ea slv
//----------------------------
void readtemps(void){
	//read 4 temperature sensors into t1-t4

}

//-----------------------------
void sendtempresp(){
	//lin slave would send this pkt to master
	//mst bc slv t1 t2 t3 t4
	//00 0a slvaddr hi lo  hi lo  hi lo hi lo  ck

	waitforgap50();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0x00); //master addr
	putc0wc(0x0a); //byte count
	putc0wc(myaddr);  //slave addr
	putc0wc(t1 >> 8);   //t1 hi
	putc0wc(t1 & 0xff); //t1 lo
	putc0wc(t2 >> 8);   //t2 hi
	putc0wc(t2 & 0xff); //t2 lo
	putc0wc(t3 >> 8);   //t3 hi
	putc0wc(t3 & 0xff); //t3 lo
	putc0wc(t4 >> 8);   //t4 hi
	putc0wc(t4 & 0xff); //t4 lo
	putc0(chksm);  //check sum
	delnms(2);     //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}


//-----------------------------
void sendtempreq(unsigned char slv){
	//mstaddr bc slvaddr oc ck
	//00      03 slvaddr 01 ck

	waitforgap50();
	LEDON();
	RXINTDISAB();
	chksm=0;       //start calc chksm
	putc0wc(0x00); //master addr
	putc0wc(0x03); //byte count
	putc0wc(slv);  //slave addr
	putc0wc(0x01); //send temp opcode
	putc0(chksm);  //check sum
	delnms(2);     //time for cksm to shift out
	RXINTENAB();
	LEDOFF();
}

//----------------------------
void temploop(void){
	//lin temperature pkts
	//master sends 'read temp opcode/ to ea addr
	//slaves respond with 4 temps
	unsigned char c;
	char c1,c2,i;
	unsigned int gapms;
	char cnt,srcaddr,dstaddr,srcok,dstok;
	unsigned char slv;

	cprintf("polling lin slave boards  q=quit\n");
	c1=0;
	while(1){
		if(kbhit1()){         //check for console input
			c1 = getchar1();
			if(c1 == 'q') return;
		}
		for(slv=1; slv <=59; slv++){
			sendtempreq(slv);   //master tells ea slv to send temps

		}
		if(kbhit0()){         //check for char from uart0 kbus
			LEDON();
			if(gapms > 0){
				cprintf("           %4d ms\n",gapms);   //
				cnt=srcok=dstok=0;
			}
			gapms=0;
			c2=getchar0();         //get the char
			if((c2 == 0xac)&&(cnt==0)){
				srcok=1;
				cnt++;
				cprintf("%02x ",c2);   //print it
				continue;
			}
			if(cnt==1){  //length
				cprintf("%02x ",c2);   //print it
				cnt++;
				continue;
			}
			if((c2 == 0xac)&&(cnt==2)){
				dstok=1;
				cprintf("%02x ",c2);   //print it
				cnt++;
				continue;
			}
			if(srcok || dstok){
				cprintf("%02x ",c2);   //print it
			}
			LEDOFF();
		}else{
			if(gapms < 255) gapms++;
		}
		if((PINC & 0x01)==0){
			cprintf(" BR!\n");
		}
		delnms(1); //time for next char to arrive
		_StackCheck();
	}//while
	crlf();
}

//--------------------------
void mainmenu(void){
	//cmds

	cprintf("lindemo cmds:\n"
			" t temp loop\n"
			" r dump raw\n"
			" d dump all K bus pkts\n"
			" f filter to from 0xac\n"
			" e term mode uart 0 to uart 1\n"
			" m monitor\n"
			);
}

//-----------------
void main(void){
	//main program
	unsigned char c;

	init_devices();  //init uart, spi, a/d turn on interrupts
	LEDON();
	initvars();
	fd=1;            //print on console on uart1
	initscreen();    //signon banner on crt uart1
	LEDOFF();
	temploop();      //right into temp loop
	c=0;
	while(1){        //main loop
		crlf();
		mainmenu();
		crlf();
		putchar('>');  //prompt for input
		c=getche();
		crlf();
		switch(c){
			case 't': temploop();          break; //ask for temperatures from slaves
			case 'r': dumpraw();           break; //dump raw
			case 'd': dumpkbus();          break; //dump kbus pkts
			case 'f': filterkbus();        break; //filter kbus pkts
			case 'e': termmode01();        break;
			case 'm': monitorloop();       break;
			default: mainmenu();
		}
		_StackCheck();
	}
}

//---strings embedded in rom--------------
__flash char copyrightstring[]={"lindemo Programmer Bob Gardner at aol"};
//--------------------eof---------------------------

