/*
 * xio_usart.c	- General purpose USART device driver for xmega family
 * 				- works with avr-gcc stdio library
 *
 * Part of TinyG project
 * Copyright (c) 2010 Alden S. Hart, Jr.
 *
 * TinyG is free software: you can redistribute it and/or modify it under the 
 * terms of the GNU General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later 
 * version.
 *
 * TinyG is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for details.
 *
 * You should have received a copy of the GNU General Public License along 
 * with TinyG  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---- Non-threadsafe code ----
 *
 *	WARNING: The getc() and gets() code is not thread safe. 
 *	I had to use a global variable (gdev) to pass the device number to the 
 *	character dispatch handlers. Any suggestions on how to do this better?
 *
 * ---- Efficiency ----
 *
 *	Originally structures were accessed using "floating" accessor macros - e.g:
 *
 *    #define DEV (ds[dev])							// device struct accessor
 *    #define DEVx ((struct xioUSART *)(ds[dev].x))	// USART extended struct accessor
 *
 *	It turned out to be more efficient to initialize pointers in each routine and 
 *	use them instead - e.g:
 *
 *	  struct xioDEVICE *d = &ds[dev];					// setup device struct ptr
 *    struct xioUSART *dx = (struct xioUSART *)ds[dev].x; // setup USART struct ptr
 *
 *	There are other examples of this approch as well (e.g. xio_set_baud_usart())
 *
 * ---- HACK ALERT ----
 *
 *	In a Q&D effort to get RS485 working there are device specific if statements
 *	(labeled HACK) in xio_putc_usart(). This should be cleaned up at some point.
 */

#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>			// needed for blocking character reads & writes
#include <util/delay.h>

#include "xio.h"				// includes for all devices are in here
#include "xmega_interrupts.h"
#include "signals.h"			// application specific signal handlers

#include "encoder.h"			// ++++ DEBUG

static uint8_t gdev;			// global device # variable (yuk)

// baud rate lookup tables - indexed by enum xioBAUDRATES (see xio_usart.h)
const uint8_t bsel[] PROGMEM = { 0, 207, 103, 51, 34, 33, 31, 27, 19, 1, 1 };
const uint8_t bscale[] PROGMEM =  				// more baud rate data
		{ 0, 0, 0, 0, 0, (-1<<4), (-2<<4), (-3<<4), (-4<<4), (1<<4), 1 };

// local function prototypes
static int _xio_readc_usart(const uint8_t dev, const char *buf);

static int _getc_char(void);		// getc character dispatch routines
static int _getc_NEWLINE(void);
static int _getc_SEMICOLON(void);
static int _getc_DELETE(void);

static int _gets_char(void);		// gets character dispatch routines
static int _gets_NEWLINE(void);
static int _gets_SEMICOLON(void);
static int _gets_DELETE(void);

/* 
 *	xio_init_usart() - general purpose USART initialization (shared)
 */

void xio_init_usart(const uint8_t dev, 			// index into device array (ds)
					const uint8_t offset,		// index into USART array (us)
					const uint16_t control,
					const struct USART_struct *usart_addr,
					const struct PORT_struct *port_addr,
					const uint8_t dirclr, 
					const uint8_t dirset, 
					const uint8_t outclr, 
					const uint8_t outset) 
{
	// do all the bindings first (and in this order)
	struct xioDEVICE *d = &ds[dev];					// setup device struct pointer
	d->x = &us[offset];								// bind USART struct to device
	struct xioUSART *dx = (struct xioUSART *)d->x;	// setup USART struct pointer
	dx->usart = (struct USART_struct *)usart_addr;	// bind USART 
	dx->port = (struct PORT_struct *)port_addr;		// bind PORT

	// set flags
	xio_cntrl(dev, control);		// generic setflags - doesn't validate flags

	// setup internal RX/TX buffers
	dx->rx_buf_head = 1;			// can't use location 0 in circular buffer
	dx->rx_buf_tail = 1;
	dx->tx_buf_head = 1;
	dx->tx_buf_tail = 1;

	// baud rate and USART setup
	uint8_t baud = (uint8_t)(control & XIO_BAUD_gm);
	if (baud == XIO_BAUD_UNSPECIFIED) { baud = XIO_BAUD_DEFAULT; }
	xio_set_baud_usart(dev, baud);					// usart must be bound first

	dx->usart->CTRLB = USART_TXEN_bm | USART_RXEN_bm;// enable tx and rx
	dx->usart->CTRLA = CTRLA_RXON_TXON;			   // enable tx and rx IRQs

	dx->port->DIRCLR = dirclr;
	dx->port->DIRSET = dirset;
	dx->port->OUTCLR = outclr;
	dx->port->OUTSET = outset;
}

void xio_set_baud_usart(const uint8_t dev, const uint8_t baud)
{
	((struct xioUSART *)(ds[dev].x))->usart->BAUDCTRLA = (uint8_t)pgm_read_byte(&bsel[baud]);
	((struct xioUSART *)(ds[dev].x))->usart->BAUDCTRLB = (uint8_t)pgm_read_byte(&bscale[baud]);
}

/* 
 * xio_putc_usb() - stdio compatible char writer for USB device
 *
 *	Supports both blocking and non-blocking write behaviors
 *
 *	Note: Originally I had the routine advancing the buffer head and comparing 
 *		  against the buffer tail to detect buffer full (it would sleep if the 
 *		  buffer was full). This unfortunately collides with the buffer empty 
 *		  detection in the dequeue routine - causing the dequeing ISR to lock up
 *		  when the buffer was full. Using a local next_tx_buffer_head prevents this
 */

int xio_putc_usart(const uint8_t dev, const char c, FILE *stream)
{
	struct xioDEVICE *d = &ds[dev];					// init device struct pointer
	struct xioUSART *dx = ((struct xioUSART *)(ds[dev].x));	// init USART pointer

	if ((dx->next_tx_buf_head = (dx->tx_buf_head)-1) == 0) { // adv. head & wrap
		dx->next_tx_buf_head = TX_BUFFER_SIZE-1;	 // -1 avoids the off-by-one
	}
	while(dx->next_tx_buf_head == dx->tx_buf_tail) { // buf full. sleep or ret
		if (BLOCKING(d->flags)) {
			sleep_mode();
		} else {
			d->sig = XIO_SIG_EAGAIN;
			return(_FDEV_ERR);
		}
	};
	// write to data register
	dx->tx_buf_head = dx->next_tx_buf_head;			// accept next buffer head
	dx->tx_buf[dx->tx_buf_head] = c;				// ...write char to buffer

	if (CRLF(d->flags) && (c == '\n')) {			// detect LF & add CR
		return d->x_putc('\r', stream);				// recurse
	}

	// dequeue the buffer if DATA register is ready
	if (dx->usart->STATUS & 0x20) {
		if (dx->tx_buf_head == dx->tx_buf_tail) {// buf might be empty if IRQ got it
			return (XIO_OK);
		}
		d->flags |= XIO_FLAG_TX_MUTEX_bm;			// mutual exclusion from ISR
		if (--(dx->tx_buf_tail) == 0) {				// advance tail & wrap if needed
			dx->tx_buf_tail = TX_BUFFER_SIZE-1;		// -1 avoid off-by-one err (OBOE)
		}
		if (dev == XIO_DEV_RS485) {					// ++++ HACK ++++
			dx->port->OUTSET = (RS485_DE_bm | RS485_RE_bm);	// enable DE, disable RE
		}
		dx->usart->DATA = dx->tx_buf[dx->tx_buf_tail];// write to TX DATA reg
		d->flags &= ~XIO_FLAG_TX_MUTEX_bm;			// release mutual exclusion lock
	}
	// enable interrupts regardless
	if (dev == XIO_DEV_RS485) {						// ++++ HACK ++++
		dx->usart->CTRLA = CTRLA_RXON_TXON_TXCON;	// doesn't work if you just |= it
	} else {
		dx->usart->CTRLA = CTRLA_RXON_TXON;			// doesn't work if you just |= it
	}
	PMIC_EnableLowLevel(); 							// enable USART TX interrupts
	sei();											// enable global interrupts
	return (XIO_OK);
}

/* 
 *  dispatch table for xio_getc_usart
 *
 *  Helper functions take no input but use global gdev to resolve variables.
 *  Returns c (may be translated depending on the function)
 */

static int (*getcFuncs[])(void) PROGMEM = { 	// use if you want it in FLASH
//static int (*getcFuncs[])(void) = {			// ALTERNATE: put table in SRAM

							// dec  hex symbol
		_getc_NEWLINE, 		//	0	00	NUL	(Null char)		(TREATED AS NEWLINE)
		_getc_char, 		//	1	01	SOH	(Start of Header)
		_getc_char, 		//	2	02	STX	(Start of Text)
		_getc_char, 	 	//	3	03	ETX (End of Text) ^c
		_getc_char, 		//	4	04	EOT	(End of Transmission)
		_getc_char, 		//	5	05	ENQ	(Enquiry)
		_getc_char, 		//	6	06	ACK	(Acknowledgment)
		_getc_char, 		//	7	07	BEL	(Bell)
		_getc_DELETE, 		//	8	08	BS	(Backspace)
		_getc_char, 		//	9	09	HT	(Horizontal Tab)
		_getc_NEWLINE, 		//	10	0A	LF	(Line Feed)
		_getc_char, 		//	11	0B	VT	(Vertical Tab)
		_getc_char, 		//	12	0C	FF	(Form Feed)
		_getc_NEWLINE, 		//	13	0D	CR	(Carriage Return)
		_getc_char,			//	14	0E	SO	(Shift Out)
		_getc_char, 		//	15	0F	SI	(Shift In)
		_getc_char, 		//	16	10	DLE	(Data Link Escape)
		_getc_char,  		//	17	11	DC1 (XON) (Device Control 1) ^q	
		_getc_char, 		//	18	12	DC2	(Device Control 2)
		_getc_char, 		//	19	13	DC3 (XOFF)(Device Control 3) ^s	
		_getc_char, 		//	20	14	DC4	(Device Control 4)
		_getc_char, 		//	21	15	NAK (Negativ Acknowledgemnt)	
		_getc_char, 		//	22	16	SYN	(Synchronous Idle)
		_getc_char, 		//	23	17	ETB	(End of Trans. Block)
		_getc_char,  		//	24	18	CAN	(Cancel) ^x
		_getc_char,			//	25	19	EM	(End of Medium)
		_getc_char, 		//	26	1A	SUB	(Substitute)
		_getc_char, 		//	27	1B	ESC	(Escape)
		_getc_char, 		//	28	1C	FS	(File Separator)
		_getc_char, 		//	29	1D	GS	(Group Separator)
		_getc_char, 		//	30	1E	RS  (Reqst to Send)(Record Sep.)	
		_getc_char, 		//	31	1F	US	(Unit Separator)
		_getc_char, 		//	32	20	SP	(Space)
		_getc_char, 		//	33	21	!	(exclamation mark)
		_getc_char, 		//	34	22	,	(double quote)	
		_getc_char, 		//	35	23	#	(number sign)
		_getc_char, 		//	36	24	$	(dollar sign)
		_getc_char, 		//	37	25	%	(percent)
		_getc_char, 		//	38	26	&	(ampersand)
		_getc_char, 		//	39	27	'	(single quote)
		_getc_char, 		//	40	28	(	(left/open parenthesis)
		_getc_char, 		//	41	29	)	(right/closing parenth.)
		_getc_char, 		//	42	2A	*	(asterisk)
		_getc_char, 		//	43	2B	+	(plus)
		_getc_char, 		//	44	2C		(comma)
		_getc_char,	 		//	45	2D	-	(minus or dash)
		_getc_char, 		//	46	2E	.	(dot)
		_getc_char,	 		//	47	2F	/	(forward slash)
		_getc_char, 		//	48	30	0	
		_getc_char, 		//	49	31	1	
		_getc_char, 		//	50	32	2	
		_getc_char, 		//	51	33	3	
		_getc_char, 		//	52	34	4	
		_getc_char, 		//	53	35	5	
		_getc_char, 		//	54	36	6	
		_getc_char, 		//	55	37	7	
		_getc_char, 		//	56	38	8	
		_getc_char, 		//	57	39	9	
		_getc_char, 		//	58	3A	:	(colon)
		_getc_SEMICOLON,	//	59	3B	;	(semi-colon)
		_getc_char, 		//	60	3C	<	(less than)
		_getc_char, 		//	61	3D	=	(equal sign)
		_getc_char, 		//	62	3E	>	(greater than)
		_getc_char, 		//	63	3F	?	(question mark)
		_getc_char, 		//	64	40	@	(AT symbol)
		_getc_char,			//	65	41	A	
		_getc_char,			//	66	42	B	
		_getc_char,			//	67	43	C	
		_getc_char,			//	68	44	D	
		_getc_char,			//	69	45	E	
		_getc_char,			//	70	46	F	
		_getc_char,			//	71	47	G	
		_getc_char,			//	72	48	H	
		_getc_char,			//	73	49	I	
		_getc_char,			//	74	4A	J	
		_getc_char,			//	75	4B	K	
		_getc_char,			//	76	4C	L	
		_getc_char,			//	77	4D	M	
		_getc_char,			//	78	4E	N	
		_getc_char,			//	79	4F	O	
		_getc_char,			//	80	50	P	
		_getc_char,			//	81	51	Q	
		_getc_char,			//	82	52	R	
		_getc_char,			//	83	53	S	
		_getc_char,			//	84	54	T	
		_getc_char,			//	85	55	U	
		_getc_char,			//	86	56	V	
		_getc_char,			//	87	57	W	
		_getc_char,			//	88	58	X	
		_getc_char,			//	89	59	Y	
		_getc_char,			//	90	5A	Z	
		_getc_char,			//	91	5B	[	(left/opening bracket)
		_getc_char,			//	92	5C	\	(back slash)
		_getc_char,			//	93	5D	]	(right/closing bracket)
		_getc_char,			//	94	5E	^	(caret/circumflex)
		_getc_char,			//	95	5F	_	(underscore)
		_getc_char,			//	96	60	`	
		_getc_char,			//	97	61	a	
		_getc_char,			//	98	62	b	
		_getc_char,			//	99	63	c	
		_getc_char,			//	100	64	d	
		_getc_char,			//	101	65	e	
		_getc_char,			//	102	66	f	
		_getc_char,			//	103	67	g	
		_getc_char,			//	104	68	h	
		_getc_char,			//	105	69	i	
		_getc_char,			//	106	6A	j	
		_getc_char,			//	107	6B	k	
		_getc_char,			//	108	6C	l	
		_getc_char,			//	109	6D	m	
		_getc_char,			//	110	6E	n	
		_getc_char,			//	111	6F	o	
		_getc_char,			//	112	70	p	
		_getc_char,			//	113	71	q	
		_getc_char,			//	114	72	r	
		_getc_char,			//	115	73	s	
		_getc_char,			//	116	74	t	
		_getc_char,			//	117	75	u	
		_getc_char,			//	118	76	v	
		_getc_char,			//	119	77	w	
		_getc_char,			//	120	78	x	
		_getc_char,			//	121	79	y	
		_getc_char,			//	122	7A	z	
		_getc_char,			//	123	7B	{	(left/opening brace)
		_getc_char,			//	124	7C	|	(vertical bar)
		_getc_char,			//	125	7D	}	(right/closing brace)
		_getc_char,			//	126	7E	~	(tilde)
		_getc_DELETE		//	127	7F	DEL	(delete)
};

/*
 *  xio_getc_usart() - generic char reader for USART devices
 *
 *	Compatible with stdio system - may be bound to a FILE handle
 *
 *  Get next character from RX buffer.
 *	See "Notes on the circular buffers" at end of xio.h for buffer details.
 *
 *	This routine returns a single character from the RX buffer to the caller.
 *	It's typically called by fgets() and is useful for single-threaded IO cases.
 *	Cases with multiple concurrent IO streams may want to use the gets() function
 *	which is incompatible with the stdio system. 
 *
 *  Flags that affect behavior:
 *
 *  BLOCKING behaviors
 *	 	- execute blocking or non-blocking read depending on controls
 *		- return character or -1 & XIO_SIG_WOULDBLOCK if non-blocking
 *		- return character or sleep() if blocking
 *
 *  ECHO behaviors
 *		- if ECHO is enabled echo character to stdout
 *		- echo all line termination chars as newlines ('\n')
 *		- Note: putc is responsible for expanding newlines to <cr><lf> if needed
 *
 *  SPECIAL CHARACTERS 
 *		- special characters such as EOL and control chars are handled by the
 *		  character helper routines. See them for behaviors
 */

int xio_getc_usart(const uint8_t dev, FILE *stream)
{
	struct xioDEVICE *d = &ds[dev];					// init device struct pointer
	struct xioUSART *dx = ((struct xioUSART *)(ds[dev].x));	// init USART pointer

	gdev = dev;										// set dev number global var

	while (dx->rx_buf_head == dx->rx_buf_tail) {	// RX ISR buffer empty
		if (BLOCKING(d->flags)) {
			sleep_mode();
		} else {
			d->sig = XIO_SIG_EAGAIN;
			return(_FDEV_ERR);
		}
	}
	if (--(dx->rx_buf_tail) == 0) {				// advance RX tail (RXQ read ptr)
		dx->rx_buf_tail = RX_BUFFER_SIZE-1;		// -1 avoids off-by-one error (OBOE)
	}
	d->c = (dx->rx_buf[dx->rx_buf_tail] & 0x007F);// get char from RX buf & mask MSB
	// 	call action procedure from dispatch table in FLASH (see xio.h for typedef)
	return (((fptr_int_void)(pgm_read_word(&getcFuncs[d->c])))());
	//return (getcFuncs[c]()); // call action procedure from dispatch table in RAM
}

// xio_getc_usart helper routines

static int _getc_char(void)
{
	if (ECHO(ds[gdev].flags)) ds[gdev].x_putc(ds[gdev].c, stdout);
	return(ds[gdev].c);
}

static int _getc_NEWLINE(void)		// convert CRs and LFs to newlines if line mode
{
	if (LINEMODE(ds[gdev].flags)) ds[gdev].c = '\n';
	if (ECHO(ds[gdev].flags)) ds[gdev].x_putc(ds[gdev].c, stdout);
	return(ds[gdev].c);
}

static int _getc_SEMICOLON(void)
{
	if (SEMICOLONS(ds[gdev].flags)) {
		return (_getc_NEWLINE());			// if semi mode treat as an EOL
	} 
	return (_getc_char());					// else treat as any other character
}

static int _getc_DELETE(void)				// can't handle a delete very well
{
	ds[gdev].sig = XIO_SIG_DELETE;
	return(_FDEV_ERR);
}

/* 
 *  dispatch table for xio_gets_usart
 *
 *  Functions take no input but use static 'c', d->signals, and others
 *  Returns c (may be translated depending on the function)
 *
 *  NOTE: As of build 203 the signal dispatchers (KILL, SHIFTOUT...) are unused. 
 *	Signal chars are trapped in the ISR and are never inserted into the RX queue
 *	Their dispatchers are left in for clarity and stubbed out
 */

static int (*getsFuncs[])(void) PROGMEM = { 	// use if you want it in FLASH
//static int (*getsFuncs[])(void) = {		// ALTERNATE: put table in SRAM

							// dec  hex symbol
		_gets_NEWLINE,	 	//	0	00	NUL	(Null char)  	(TREAT AS NEWLINE)
		_gets_char, 		//	1	01	SOH	(Start of Header)
		_gets_char, 		//	2	02	STX	(Start of Text)
		_gets_char,			//	3	03	ETX (End of Text) ^c
		_gets_char, 		//	4	04	EOT	(End of Transmission)
		_gets_char, 		//	5	05	ENQ	(Enquiry)
		_gets_char, 		//	6	06	ACK	(Acknowledgment)
		_gets_char, 		//	7	07	BEL	(Bell)
		_gets_DELETE,	 	//	8	08	BS	(Backspace)
		_gets_char, 		//	9	09	HT	(Horizontal Tab)
		_gets_NEWLINE,	 	//	10	0A	LF	(Line Feed)
		_gets_char, 		//	11	0B	VT	(Vertical Tab)
		_gets_char, 		//	12	0C	FF	(Form Feed)
		_gets_NEWLINE,	 	//	13	0D	CR	(Carriage Return)
		_gets_char,			//	14	0E	SO	(Shift Out)
		_gets_char, 		//	15	0F	SI	(Shift In)
		_gets_char, 		//	16	10	DLE	(Data Link Escape)
		_gets_char, 		//	17	11	DC1 (XON) (Device Control 1) ^q	
		_gets_char, 		//	18	12	DC2	(Device Control 2)
		_gets_char,			//	19	13	DC3 (XOFF)(Device Control 3) ^s	
		_gets_char, 		//	20	14	DC4	(Device Control 4)
		_gets_char, 		//	21	15	NAK (Negativ Acknowledgemnt)	
		_gets_char, 		//	22	16	SYN	(Synchronous Idle)
		_gets_char, 		//	23	17	ETB	(End of Trans. Block)
		_gets_char,		 	//	24	18	CAN	(Cancel) ^x
		_gets_char, 		//	25	19	EM	(End of Medium)
		_gets_char, 		//	26	1A	SUB	(Substitute)
		_gets_char,			//	27	1B	ESC	(Escape)
		_gets_char, 		//	28	1C	FS	(File Separator)
		_gets_char, 		//	29	1D	GS	(Group Separator)
		_gets_char, 		//	30	1E	RS  (Reqst to Send)(Record Sep.)	
		_gets_char, 		//	31	1F	US	(Unit Separator)
		_gets_char, 		//	32	20	SP	(Space)
		_gets_char, 		//	33	21	!	(exclamation mark)
		_gets_char, 		//	34	22	,	(double quote)	
		_gets_char, 		//	35	23	#	(number sign)
		_gets_char, 		//	36	24	$	(dollar sign)
		_gets_char, 		//	37	25	%	(percent)
		_gets_char, 		//	38	26	&	(ampersand)
		_gets_char, 		//	39	27	'	(single quote)
		_gets_char, 		//	40	28	(	(left/open parenthesis)
		_gets_char, 		//	41	29	)	(right/closing parenth.)
		_gets_char, 		//	42	2A	*	(asterisk)
		_gets_char, 		//	43	2B	+	(plus)
		_gets_char, 		//	44	2C		(comma)
		_gets_char,		 	//	45	2D	-	(minus or dash)
		_gets_char, 		//	46	2E	.	(dot)
		_gets_char,		 	//	47	2F	/	(forward slash)
		_gets_char, 		//	48	30	0	
		_gets_char, 		//	49	31	1	
		_gets_char, 		//	50	32	2	
		_gets_char, 		//	51	33	3	
		_gets_char, 		//	52	34	4	
		_gets_char, 		//	53	35	5	
		_gets_char, 		//	54	36	6	
		_gets_char, 		//	55	37	7	
		_gets_char, 		//	56	38	8	
		_gets_char, 		//	57	39	9	
		_gets_char, 		//	58	3A	:	(colon)
		_gets_SEMICOLON, //	59	3B	;	(semi-colon)
		_gets_char, 			//	60	3C	<	(less than)
		_gets_char, 		//	61	3D	=	(equal sign)
		_gets_char, 		//	62	3E	>	(greater than)
		_gets_char, 		//	63	3F	?	(question mark)
		_gets_char, 		//	64	40	@	(AT symbol)
		_gets_char,			//	65	41	A	
		_gets_char,			//	66	42	B	
		_gets_char,			//	67	43	C	
		_gets_char,			//	68	44	D	
		_gets_char,			//	69	45	E	
		_gets_char,			//	70	46	F	
		_gets_char,			//	71	47	G	
		_gets_char,			//	72	48	H	
		_gets_char,			//	73	49	I	
		_gets_char,			//	74	4A	J	
		_gets_char,			//	75	4B	K	
		_gets_char,			//	76	4C	L	
		_gets_char,			//	77	4D	M	
		_gets_char,			//	78	4E	N	
		_gets_char,			//	79	4F	O	
		_gets_char,			//	80	50	P	
		_gets_char,			//	81	51	Q	
		_gets_char,			//	82	52	R	
		_gets_char,			//	83	53	S	
		_gets_char,			//	84	54	T	
		_gets_char,			//	85	55	U	
		_gets_char,			//	86	56	V	
		_gets_char,			//	87	57	W	
		_gets_char,			//	88	58	X	
		_gets_char,			//	89	59	Y	
		_gets_char,			//	90	5A	Z	
		_gets_char,			//	91	5B	[	(left/opening bracket)
		_gets_char,			//	92	5C	\	(back slash)
		_gets_char,			//	93	5D	]	(right/closing bracket)
		_gets_char,			//	94	5E	^	(caret/circumflex)
		_gets_char,			//	95	5F	_	(underscore)
		_gets_char,			//	96	60	`	
		_gets_char,			//	97	61	a	
		_gets_char,			//	98	62	b	
		_gets_char,			//	99	63	c	
		_gets_char,			//	100	64	d	
		_gets_char,			//	101	65	e	
		_gets_char,			//	102	66	f	
		_gets_char,			//	103	67	g	
		_gets_char,			//	104	68	h	
		_gets_char,			//	105	69	i	
		_gets_char,			//	106	6A	j	
		_gets_char,			//	107	6B	k	
		_gets_char,			//	108	6C	l	
		_gets_char,			//	109	6D	m	
		_gets_char,			//	110	6E	n	
		_gets_char,			//	111	6F	o	
		_gets_char,			//	112	70	p	
		_gets_char,			//	113	71	q	
		_gets_char,			//	114	72	r	
		_gets_char,			//	115	73	s	
		_gets_char,			//	116	74	t	
		_gets_char,			//	117	75	u	
		_gets_char,			//	118	76	v	
		_gets_char,			//	119	77	w	
		_gets_char,			//	120	78	x	
		_gets_char,			//	121	79	y	
		_gets_char,			//	122	7A	z	
		_gets_char,			//	123	7B	{	(left/opening brace)
		_gets_char,			//	124	7C	|	(vertical bar)
		_gets_char,			//	125	7D	}	(right/closing brace)
		_gets_char,			//	126	7E	~	(tilde)
		_gets_DELETE		//	127	7F	DEL	(delete)
};

/* 
 *	xio_gets_usart() - read a complete line from the usart device
 *
 *	Retains line context across calls - so it can be called multiple times.
 *	Reads as many characters as it can until any of the following is true:
 *
 *	  - RX buffer is empty on entry (return XIO_EAGAIN)
 *	  - no more chars to read from RX buffer (return XIO_EAGAIN)
 *	  - read would cause output buffer overflow (return XIO_BUFFER_FULL)
 *	  - read returns complete line (returns XIO_OK)
 *
 *	Note: LINEMODE flag in device struct is ignored. It's ALWAYS LINEMODE here.
 */

int xio_gets_usart(const uint8_t dev, char *buf, const uint8_t size)
{
	struct xioDEVICE *d = &ds[dev];				// init device struct pointer
	
	gdev = dev;									// set the global device number

	if (!IN_LINE(d->flags)) {					// first time thru initializations
		d->len = 0;								// zero buffer
		d->status = 0;
		d->size = size;
		d->buf = buf;
		d->sig = XIO_SIG_OK;					// reset signal register
		d->flags |= XIO_FLAG_IN_LINE_bm;		// yes, we are busy getting a line
	}
	while (TRUE) { 
		switch (d->status = _xio_readc_usart(dev, d->buf)) {
			case (XIO_BUFFER_EMPTY): return (XIO_EAGAIN);		// empty condition
			case (XIO_BUFFER_FULL_NON_FATAL): return (d->status);// overrun err
			case (XIO_EOL): return (XIO_OK);					// got complete line
			case (XIO_EAGAIN): break;							// loop
		}
		// +++ put a size check here of buffers can overrun.
	}
	return (XIO_OK);
}

/*
 * _xio_readc_usart() - non-blocking character getter for gets
 */

static int _xio_readc_usart(const uint8_t dev, const char *buf)
{
	struct xioDEVICE *d = &ds[dev];					// init device struct pointer
	struct xioUSART *dx = ((struct xioUSART *)(ds[dev].x));	// init USART pointer

	if (dx->rx_buf_head == dx->rx_buf_tail) {		// RX ISR buffer empty
		return(XIO_BUFFER_EMPTY);
	}
	if (--(dx->rx_buf_tail) == 0) {			// advance RX tail (RX q read ptr)
		dx->rx_buf_tail = RX_BUFFER_SIZE-1;	// -1 avoids off-by-one error (OBOE)
	}
	d->c = (dx->rx_buf[dx->rx_buf_tail] & 0x007F);	// get char from RX Q & mask MSB
	return (((fptr_int_void)(pgm_read_word(&getsFuncs[d->c])))()); // dispatch char
}

/* xio_usb_gets helper routines */

static int _gets_char(void)
{
	if (ds[gdev].len > ds[gdev].size) {			// trap buffer overflow
		ds[gdev].sig = XIO_SIG_EOL;
		ds[gdev].buf[ds[gdev].size] = NUL;		// size is zero based
		return (XIO_BUFFER_FULL_NON_FATAL);
	}
	ds[gdev].buf[ds[gdev].len++] = ds[gdev].c;
	if (ECHO(ds[gdev].flags)) ds[gdev].x_putc(ds[gdev].c, stdout);// conditional echo
	return (XIO_EAGAIN);						// line is still in process
}

static int _gets_NEWLINE(void)				// handles any valid newline char
{
	ds[gdev].sig = XIO_SIG_EOL;
	ds[gdev].buf[ds[gdev].len] = NUL;
	ds[gdev].flags &= ~XIO_FLAG_IN_LINE_bm;			// clear in-line state (reset)
	if (ECHO(ds[gdev].flags)) ds[gdev].x_putc('\n',stdout);// echo a newline
	return (XIO_EOL);							// return for end-of-line
}

static int _gets_SEMICOLON(void)				// semicolon is a conditional newln
{
	if (SEMICOLONS(ds[gdev].flags)) {
		return (_gets_NEWLINE());				// if semi mode treat as an EOL
	} else {
		return (_gets_char());				// else treat as any other character
	}
}

static int _gets_DELETE(void)
{
	if (--ds[gdev].len >= 0) {
		if (ECHO(ds[gdev].flags)) ds[gdev].x_putc(ds[gdev].c, stdout);
	} else {
		ds[gdev].len = 0;
	}
	return (XIO_EAGAIN);						// line is still in process
}

/*
 * xio_queue_RX_char_usart() - fake ISR to put a char in the RX buffer
 */

void xio_queue_RX_char_usart(const uint8_t dev, const char c)
{
	struct xioDEVICE *d = &ds[dev];				// init device struct pointer
	struct xioUSART *dx = ((struct xioUSART *)(ds[dev].x));// init USART pointer

	// trap signals - do not insert into RX queue
	if (c == SIG_KILL_CHAR) {	 				// trap Kill signal
		d->sig = XIO_SIG_KILL;					// set signal value
		sig_kill();								// call app-specific sig handler
		return;
	}
	if (c == SIG_TERM_CHAR) {					// trap Terminate signal
		d->sig = XIO_SIG_KILL;
		sig_term();
		return;
	}
	if (c == SIG_PAUSE_CHAR) {					// trap Pause signal
		d->sig = XIO_SIG_PAUSE;
		sig_pause();
		return;
	}
	if (c == SIG_RESUME_CHAR) {					// trap Resume signal
		d->sig = XIO_SIG_RESUME;
		sig_resume();
		return;
	}

	// normal path
	if ((--dx->rx_buf_head) == 0) { 			// wrap condition
		dx->rx_buf_head = RX_BUFFER_SIZE-1;		// -1 avoids the off-by-one error
	}
	if (dx->rx_buf_head != dx->rx_buf_tail) {	// write char unless buffer full
		dx->rx_buf[dx->rx_buf_head] = c;		// FAKE INPUT DATA
		return;
	}
	// buffer-full handling
	if ((++dx->rx_buf_head) > RX_BUFFER_SIZE-1) { // reset the head
		dx->rx_buf_head = 1;
	}
}

/*
 * xio_queue_RX_string_usart() - fake ISR to put a string in the RX buffer
 */

void xio_queue_RX_string_usart(const uint8_t dev, const char *buf)
{
	char c;
	uint8_t i=0;

	while ((c = buf[i++]) != NUL) {
		xio_queue_RX_char_usart(dev, c);
	}
}