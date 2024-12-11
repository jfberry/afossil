#include <windows.h>

extern "C" {
#include <vddsvc.h>
}

#include <stdio.h>

//#define LOGGING

#define MAX_PORTS	8

struct PortInfo {
  HANDLE hFile;
  int peek;
  BOOL enabled;

  PortInfo() {
	  enabled = FALSE;
	  peek = -1;
  }
};

void __cdecl Log(LPCTSTR lpszFormat, ...);
void F_activate_port(PortInfo* port, short fossilPort);
void F_set_parameters(PortInfo* port, short params);
void F_putch(PortInfo* port, short byte);
void F_getch(PortInfo* port);
void F_peek(PortInfo* port);
void F_deactivate_port(PortInfo* port);
void F_flush_output(PortInfo* port);
void F_purge_output(PortInfo* port);
void F_purge_input(PortInfo* port);
void F_set_flowcontrol(PortInfo* port, short status);
void F_get_status(PortInfo* port);
short F_line_status(PortInfo* port);
void F_set_dtr(PortInfo* port, short state);

void A_get_time();
void A_get_date();

/*
 *
 * VDDInitialize
 *
 * Arguments:
 *     DllHandle - Identifies this VDD
 *     Reason - Attach or Detach
 *     Context - Not Used
 *
 * Return Value:
 *     TRUE: SUCCESS
 *     FALSE: FAILURE
 *
 */
BOOL VDDInitialize(
    IN PVOID DllHandle,
    IN ULONG Reason,
    IN PCONTEXT Context OPTIONAL
    ) {
   switch(Reason)
   {

      case DLL_PROCESS_ATTACH:
		  // Clear all ports. Hmm.
//         OutputDebugString("ATTACH\n");
         break;

      case DLL_PROCESS_DETACH:
//         OutputDebugString("DETACH\n");
         break;

      default:
         break;
   }

   return TRUE;
}


/*
 *
 * VDDTerminateVDM
 *
 */
VOID VDDTerminateVDM( VOID )
{
//   OutputDebugString("VDDTERMINATE\n");
   return;
}


/*
 *
 * VDDInit
 *
 */
VOID VDDInit( VOID )
{
 //  OutputDebugString("VDDINIT\n");
   setCF( 0 );
}



/*
 *
 * VDDDispatch
 *
 * Arguments:
 *     BX=fossil function (AX)
 *     DX=fossil port
 * Return Value:
 *     clears carry flag
 *     buffer filled with results of GetVersionEx
 *
 */

VOID VDDDispatch( VOID )
{
   USHORT  PassAX;
   static PortInfo piPorts[MAX_PORTS];

   setCF(0);

   PassAX = getBX();

   short fossilFunc = (PassAX & 0xFF00) >> 8;
   short fossilPort = getDX();
   short fossilParam = (PassAX & 0xFF);

   if (fossilPort > MAX_PORTS-1) {
	   // Check we're not trying to do fossil access to ports higher than we're supporting
	   return;
   }

   switch(fossilFunc) {

	   case 0x00:
		   F_set_parameters(&piPorts[fossilPort], fossilParam);
		   break;
	   case 0x01:
		   F_putch(&piPorts[fossilPort], fossilParam);
		   break;
	   case 0x02:
		   F_getch(&piPorts[fossilPort]);
		   break;
	   case 0x03:
		   F_get_status(&piPorts[fossilPort]);
		   break;
	   case 0x04:	
		   F_activate_port(&piPorts[fossilPort], fossilPort);
		   break;
	   case 0x05:
	   case 0x1d:
			F_deactivate_port(&piPorts[fossilPort]);
			break;
	   case 0x06:
		   F_set_dtr(&piPorts[fossilPort], fossilParam);
		   break;
	   case 0x08:
		   F_flush_output(&piPorts[fossilPort]);
		   break;
	   case 0x0a:
		   F_purge_input(&piPorts[fossilPort]);
		   break;
	   case 0x0c:
		   F_peek(&piPorts[fossilPort]);
		   break;
	   case 0x0f:
		   F_set_flowcontrol(&piPorts[fossilPort], fossilParam);
		   break;
   }
}


void F_activate_port(PortInfo* port, short comPort) {

	if (port->enabled == TRUE) {
		setAX(0x1954);
		return;
	}

	TCHAR portName[5];
	wsprintf(portName, TEXT("COM%d"), comPort+1);

#ifdef LOGGING
	Log(TEXT("Attempting to open %s\r\n"), portName);
#endif

	port->hFile = CreateFile(portName, 
						GENERIC_READ | GENERIC_WRITE,
	                    0,    // comm devices must be opened w/exclusive-access
						NULL, // no security attributes
						OPEN_EXISTING, // comm devices must use OPEN_EXISTING
						0,    // not overlapped I/O
						NULL);  // hTemplate must be NULL for comm devices
    if (port->hFile == INVALID_HANDLE_VALUE) {
#ifdef LOGGING
		Log(TEXT("Cannot open port %s\r\n"), portName);
#endif
		return;
	}

	// Set timeouts so that reads return quickly

	COMMTIMEOUTS timeouts;

	timeouts.ReadIntervalTimeout = MAXDWORD;
	timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
	timeouts.ReadTotalTimeoutConstant = 100;
	timeouts.WriteTotalTimeoutConstant = 0;
	timeouts.WriteTotalTimeoutMultiplier = 0;

	if (!SetCommTimeouts(port->hFile, &timeouts)) {
#ifdef LOGGING
		Log(TEXT("activate_port: SetCommTimeouts returned %d\r\n"), GetLastError());
#endif
		CloseHandle(port->hFile);
		return;
	}

	DCB dcb;
	GetCommState(port->hFile, &dcb);
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	SetCommState(port->hFile, &dcb);

	// Raise DTR

	if (!EscapeCommFunction(port->hFile, SETDTR)) {
#ifdef LOGGING
		Log(TEXT("activate_port: EscapeCommFunction returned %d\r\n"), GetLastError());
#endif
		CloseHandle(port->hFile);
		return;
	}

	setAX(0x1954);
	// For full compliance, should return fossil info in BX

	port->enabled = TRUE;
}

/*
          Function 00h - Set communications parameters, baud, parity etc.

               Input:    AH = 00h
                         AL = Communications parameters (defined below)
                         DX = Port number

               Output:   AX = Port status (see function 03h)

          This function is identical to the IBM PC INT 14h BIOS call except
          that  110 baud and 150 baud have  been replaced by 19200 baud and
          38400 baud respectively.  The high order 3 bits of AL specify the
          baud rate  as follows:  See functions 1Eh and 1Fh for an enhanced
          version of this function.

                         010 =   300 baud
                         011 =   600  ''
                         100 =  1200  ''
                         101 =  2400  ''
                         110 =  4800  ''
                         111 =  9600  ''
                         000 = 19200  '' (Replaces IBM 110 baud setting)
                         001 = 38400  '' (Replaces IBM 150 baud setting)

          The low order 5 of AL specify the following:

               Bits 4-3 define parity:       0 0  no parity
                                             1 0  no parity
                                             0 1  odd parity
                                             1 1  even parity

               Bit 2 defines stop bits:      0    1 stop bit;
                                             1    1.5 bits for 5-bit codes,
                                                  2 for others

               Bits 1-0 character length:    0 0  5 bits
                                             0 1  6 bits
                                             1 0  7 bits
                                             1 1  8 bits

*/

void F_set_parameters(PortInfo* port, short params) {

	if (port->enabled == FALSE) return;
#ifdef LOGGING
	Log(TEXT("set_parameters: %d\r\n"), params);
#endif

	BOOL fSuccess;
	DCB dcb;

	fSuccess = GetCommState(port->hFile, &dcb);
	if (!fSuccess) {
#ifdef LOGGING
		Log(TEXT("set_parameters: GetCommState returned %d\r\n"), GetLastError());
#endif
		return;
	}

	// baud rate

	switch((params >> 5) & 0x7) {
		case 0:		// 19200
					dcb.BaudRate = CBR_19200;
					break;
		case 1:		dcb.BaudRate = CBR_38400;
					break;
		case 2:		dcb.BaudRate = CBR_300;
					break;
		case 3:		dcb.BaudRate = CBR_600;
					break;
		case 4:		dcb.BaudRate = CBR_1200;
					break;
		case 5:		dcb.BaudRate = CBR_2400;
					break;
		case 6:		dcb.BaudRate = CBR_4800;
					break;
		case 7:		dcb.BaudRate = CBR_9600;
					break;
	}

	// parity

	switch ((params >> 3) & 0x3) {
		case 0:
		case 2:		dcb.Parity = NOPARITY;
					break;
		case 1:		dcb.Parity = ODDPARITY;
					break;
		case 3:		dcb.Parity = EVENPARITY;
					break;
	}

	// word length

	switch(params & 0x03) {
		case 0:		dcb.ByteSize = 5;
					break;
		case 1:		dcb.ByteSize = 6;
					break;
		case 2:		dcb.ByteSize = 7;
					break;
		case 3:		dcb.ByteSize = 8;
					break;
	}

	// stop bits

	switch ((params >> 2) & 0x01) {
		case 0:		dcb.StopBits = ONESTOPBIT;
					break;
		case 1:		if (dcb.ByteSize != 5) {
						dcb.StopBits = TWOSTOPBITS;
					} else {
						dcb.StopBits = ONE5STOPBITS;
					}
					break;
	}
#ifdef LOGGING
	Log(TEXT("Speed: %d word: %d parity: %d stop: %d\r\n"), dcb.BaudRate, dcb.ByteSize, dcb.Parity, dcb.StopBits);
#endif
	fSuccess = SetCommState(port->hFile, &dcb);
	if (!fSuccess) {
#ifdef LOGGING
		Log(TEXT("set_parameters: SetCommState returned %d\r\n"), GetLastError());
#endif
		return;
	}

	F_get_status(port);
}

/*
          Function 01h - Transmit character and wait.

               Input:    AH = 01h
                         AL = Character
                         DX = Port number

               Output:   AX = Port status (see function 03h)

          Upon  entry to this function, AL must contain a character that is
          to be transmitted.   If there is room in the transmit buffer, the
          character  buffered.   Otherwise, this  function will  loop until
          their is  room in the buffer or until  a timeout occurs.  If set,
          the high  order bit of  the returned status indicates  a time out
          occurred.   See function 03h.   At the time of  this writing, the
          timeout is set to 30 seconds.
*/

void F_putch(PortInfo *port, short byte) {
	if (port->enabled == FALSE) return;

#ifdef LOGGING
	Log(TEXT("putch: %d\r\n"), byte);
#endif

	TCHAR szOut[1] = { byte };
	DWORD dwNumWritten;

	BOOL fSuccess = WriteFile(port->hFile, szOut, 1, &dwNumWritten, NULL);
	if (!fSuccess) {
#ifdef LOGGING
		Log(TEXT("putch: WriteFile returned %d\r\n"), GetLastError());
#endif
	}

	F_get_status(port);
}

/*
         Function 02h - Get received character with wait.

              Receive character with wait

               Input:    AH = 02h
                         DX = Port number

               Output:   AH = Line status (same AH returned by function 3)
                         AL = Input character

          If  a character is available in the receive buffer, this function
          returns the next character from the  receive buffer in AL.  If no
          receive character is available, this  function will wait until  a
          character is received or  until a timeout occurs.   If a  timeout
          occurs,  the high order bit of  AH will be set  upon return.  See
          function 03h.   At the time of this writing, the timeout value is
          set to 30 seconds.

          Note:  X00 executes functions with  the interrupt mask  set as it
          was when  entered.   That is,  if X00  is called  will interrupts
          masked, interrupts will  remain masked during  the processing  of
          the function.  If this function is called with interrupts masked,
          it may never return.

          Function 20h is a get character with no wait.
*/
void F_getch(PortInfo *port) {
	if (port->enabled == FALSE) return;

	if (port->peek != -1) {
		// character has been "peeked"
		setAX(port->peek + (F_line_status(port) << 8));
#ifdef LOGGING
		Log(TEXT("getch (from peek): %d\r\n"), port->peek);
#endif

		port->peek = -1;
		return;
	}

	TCHAR szIn[1];
	DWORD dwNumRead;

	// wait for 30 seconds for character

	DWORD dwStart = GetTickCount();

	do {

		BOOL fSuccess = ReadFile(port->hFile, szIn, 1, &dwNumRead, NULL);

		if (fSuccess && dwNumRead > 0) {
			setAX(szIn[0] + (F_line_status(port) << 8));
#ifdef LOGGING
		    Log(TEXT("getch: %d\r\n"), szIn[0]);
#endif

			return;
		}

		Sleep(10);
	} while ((GetTickCount() - dwStart) < 30
		&& GetTickCount() > dwStart);			// cater for time wrap-around

#ifdef LOGGING
	Log(TEXT("getch: timeout\r\n"), szIn[0]);
#endif
	setAX((F_line_status(port) | 0x80) << 8);
}

/*
          Function 0Ch - Non-destructive read-ahead (Peek).


               Input:    AH = 0Ch    
                         DX = Port number

               Output:   AH = 00h if character is available
                         AL = Next character if available
                         AX = 0FFFFh = If no character is available

          This function returns the next character from the receive buffer.
          If the  receive buffer is empty,  0FFFFh is returned in  AX.  The
          character   returned  remains   in  the   receive   buffer.  Some
          programmers call this function "peek".

          Function 20h is a destructive read with no wait.
*/

/*
 * Actually, this peek function does not return instantaneously.  This could
 * cause problems for applications that are relying on this.
 */

void F_peek(PortInfo *port) {
	if (port->enabled == FALSE) return;

	/* Attempt to determine if characters are waiting to be read */

	DWORD lpErrors;
	COMSTAT lpStat;
	ClearCommError(port->hFile, &lpErrors, &lpStat);

	if (lpStat.cbInQue == 0) {
		// No characters to read
		setAX(0xffff);
		return;
	}

	TCHAR szIn[1];
	DWORD dwNumRead;

	BOOL fSuccess = ReadFile(port->hFile, szIn, 1, &dwNumRead, NULL);

	if (fSuccess) {
		if (dwNumRead == 0) {
			setAX(0xffff);
			return;
		} else {
			setAX(szIn[0] & 0xff);
			port->peek = szIn[0] & 0xff;
#ifdef LOGGING
			Log(TEXT("peek: read %d\r\n"), szIn[0]);
#endif
		}
	} else {
		// Error occurred, so indicate no characters
		setAX(0xffff);
	}
}

/*

          Function 05h - Deactivate Port.

               Input:    AH = 05h
                         DX = Port number

               Output:   Nothing

          This function  instructs X00  that it  should  no longer  process
          calls for specified port (in  DX) using the FOSSIL specification.
          If the port was never activated by function 4, then this function
          is ignored.  Any  subsequent function calls, that require  a port
          number  in  DX, will  be  passed  to BIOS  or  the  BIOS INT  14h
          emulator.

          NOTICE:  At some  future date  (July 1991  or later),  the author
          intends to make this function identical to the PS/2's function 5.
          Function 1Dh is  a duplicate of this  function.  New  or modified
          application programs should discontinue use of this function  and
          use the duplicate (1Dh) function.

          A section is the  application notes describes a simple  method of
          determining  which Activate/Deactivate  function numbers  to use.
          The described method should work on all FOSSIL implementations.
*/

void F_deactivate_port(PortInfo *port) {
	if (port->enabled == FALSE) return;

#ifdef LOGGING
	Log(TEXT("deactivate port\r\n"));
#endif
	CloseHandle(port->hFile);
	port->enabled = FALSE;
	port->peek = -1;
}


/*
          Function 06h - Raise/lower DTR.

               Input:    AH = 06h
                         AL = DTR state to set
                         DX = Port number

               Output:   Nothing

          This function is used to control the DTR signal.   AL = 00h means
          lower DTR (turn it off), and AL = 01h means to raise DTR (turn it
          on).  No other function (except functions 04h and 1Ch) will alter
          DTR.
*/

void F_set_dtr(PortInfo *port, short state) {
	if (port->enabled == FALSE) return;
#ifdef LOGGING
	Log(TEXT("set_dtr: %d\r\n"), state);
#endif
	EscapeCommFunction(port->hFile, (state == 1) ? SETDTR : CLRDTR);
}

/*
          Function 08h - Flush output buffer.

               Input:    AH = 08h
                         DX = Port number

               Output:   Nothing

          This  function  is  used to  wait  until  any  pending output  is
          transmitted.   It does not  return until all  buffered output has
          been sent.  This function should be used with care.  Flow control
          (documented  below) can  cause your  system hang  in a  tight un-
          interruptable loop given the right circumstances.

          Note:  X00 executes functions with  the interrupt mask  set as it
          was when  entered.   That is,  if X00  is called will  interrupts
          masked, interrupts  will remain  masked during the  processing of
          the function.  If this function is called with interrupts masked,
          it may never return.
*/

void F_flush_output(PortInfo *port) {
	if (port->enabled == FALSE) return;

#ifdef LOGGING
	Log(TEXT("flush_output\r\n"));
#endif

	FlushFileBuffers(port->hFile);
}

/* 
        Function 09h - Purge output buffer.

               Input:    AH = 09h
                         DX = Port number

               Output:   Nothing

          This function  is used to  remove any buffered  output (transmit)
          data.   Any  output  data remaining  in  the output  buffer  (not
          transmitted yet) is discarded.
*/

void F_purge_output(PortInfo *port) {
	if (port->enabled == FALSE) return;

#ifdef LOGGING
	Log(TEXT("purge_output\r\n"));
#endif

	PurgeComm(port->hFile, PURGE_TXCLEAR);
}

/*
          Function 0Ah - Purge input buffer.

               Input:    AH = 0Ah
                         DX = Port number

               Output:   Nothing

          This function is  used to  remove any  buffered input  (received)
          data.    Any  input data  which  is  in  the  receive  buffer  is
          discarded.
*/

void F_purge_input(PortInfo *port) {
	if (port->enabled == FALSE) return;

#ifdef LOGGING
	Log(TEXT("purge_input\r\n"));
#endif
	PurgeComm(port->hFile, PURGE_RXCLEAR);
}

/*
          Function 0Fh - Flow Control for serial I/O.

               Input:    AH = 0Fh
                         AL = Bit defining the flow control to use
                         DX = Port number

               Output:   Nothing

          Two kinds of basic flow control are supported.  One is  software,
          called  Xon/Xoff  flow control.   The  other is  hardware, called
          RTS/CTS flow  control.  Both  software and hardware  flow control
          may be enabled at the same time.

          The bits in the parameter passed in AL are as follows:

                         Bit 0 = 1      Enable receiving of Xon/Xoff
                         Bit 1 = 1      Enable RTS/CTS flow control

                         Bit 2          Reserved (should be zero)
                         Bit 3 = 1      Enable transmitting of Xon/Xoff

          Setting  an appropriate bit to  zero will cause  the flow control
          associated with that bit to be disabled.  

          Enabling  the  receiving  of  Xon/Xoff  will  cause  X00  to stop
          transmitting   upon  receiving   an  Xoff.     X00   will  resume
          transmitting when an Xon is received.

          Enabling the sending of  Xon/Xoff will cause X00 to send  an Xoff
          when its buffers are 3/4  full.  When the buffers are  emptied to
          1/4 full, X00 will send an Xon.

          X00  WILL ALWAYS CEASE  TRANSMITTING WHEN CTS  IS LOWERED (TURNED
          OFF).  TRANSMISSION WILL RESUME WHEN CTS IS RAISED (TURNED ON).

          When  RTS/CTS flow control is  enabled, X00 will  drop (turn off)
          RTS when  the receive buffer is  3/4 full.  X00  will raise (turn
          on) RTS  when the  receive buffer empties  to 1/4 full.   RTS/CTS
          flow control is almost  always used by modems that  operate above
          2400  baud.    Some  2400  baud  modems  also  use/allow  RTS/CTS
          handshaking

          If the  baud rate is locked  in the command line  that loads X00,
          RTS/CTS  handshaking is  forced.   In this case,  the application
          program can not disable RTS/CTS handshaking.
*/

void F_set_flowcontrol(PortInfo *port, short status) {
	if (port->enabled == FALSE) return;

	BOOL fSuccess;
	DCB dcb;
#ifdef LOGGING
	Log(TEXT("set_flowcontrol status = %d\r\n"), status);
#endif

	fSuccess = GetCommState(port->hFile, &dcb);
	if (!fSuccess) {
#ifdef LOGGING
		Log(TEXT("set_flowcontrol: GetCommState returned %d\r\n"), GetLastError());
#endif
		return;
	}

	if (status & 0x01) {
		// Enable receiving of xon/xoff
		dcb.fOutX = TRUE;
	} else {
		dcb.fOutX = FALSE;
	}

	if (status & 0x08) {
		// Enable transmitting of xon/xoff
		dcb.fInX = TRUE;
	} else {
		dcb.fInX = FALSE;
	}

	if (status & 0x02) {
		// Enable RTS/CTS
		dcb.fOutxCtsFlow = TRUE;
		dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
	} else {
		dcb.fOutxCtsFlow = FALSE;
		dcb.fRtsControl = RTS_CONTROL_ENABLE;
	}

	fSuccess = SetCommState(port->hFile, &dcb);
	if (!fSuccess) {
#ifdef LOGGING
		Log(TEXT("set_flowcontrol: SetCommState returned %d\r\n"), GetLastError());
#endif
		return;
	}
}

/*
          Function 03h - Return Serial Port Status.

               Input:    AH = 03h
                         DX = Port number

               Output:   AX = Status bits as follows:

                    AH = Line Status, where:
                         Bit 0 = RDA  - input data is available in buffer
                         Bit 1 = OVRN - the input buffer has been overrun
                         Bit 2 = Reserved (Parity error in BIOS INT 14h)
                         Bit 3 = Reserved (Framing error in BIOS INT 14h)
                         Bit 4 = Reserved (Break detect in BIOS INT 14h)
                         Bit 5 = THRE - room is available in output buffer
                         Bit 6 = TSRE - output buffer is empty
                         Bit 7 = Timeout (set by functions 1 and 2 only)

          Bit 7 of AH IS ALWAYS ZERO WHEN RETURNED BY FUNCTION 3.  Bit 7 of
          AH may be  set to one by other functions (like 1 and 2) that also
          return status.  When bit 7 of AH is set to one, none of the other
          15 bits returned in AX are valid.

                    AL = Modem status, where:
                         Bit 0 = Delta clear to send (not reliable)
                         Bit 1 = Delta data set ready (not reliable)
                         Bit 2 = Delta data carrier detect (not reliable)
                         Bit 3 = Always set to 1 upon return (DUMMY DCD)
                         Bit 4 = Clear to send (CTS)
                         Bit 5 = Data set ready (DSR)
                         Bit 6 = Ring indicator (RI)
                         Bit 7 = Data carrier detect (DCD)

          Bit  3  of AL  is  always  returned  set  to  1.    This  enables
          application programs  to use  bit 3  as a  carrier detect bit  on
          hardwired (null modem) links.
*/

void F_get_status(PortInfo *port) {
	if (port->enabled == FALSE) return;

	short status = 0x08;	// bit 3 is always on

	DWORD winCommStatus;
	GetCommModemStatus(port->hFile, &winCommStatus);

	if (winCommStatus & MS_CTS_ON) {
		status |= 0x10;
	}

	if (winCommStatus & MS_DSR_ON) {
		status |= 0x20;
	}
	if (winCommStatus & MS_RING_ON) {
		status |= 0x40;
	}
	if (winCommStatus & MS_RLSD_ON) {
		status |= 0x80;
	}

	status |= (F_line_status(port) << 8);
#ifdef LOGGING
	Log(TEXT("get_status ret= %d\r\n"), status);
#endif

	setAX(status);
}

short F_line_status(PortInfo *port) {
	// return AH (line status)

	// ** HACK ATTACK **

	return 16;
}


void A_gettime() {
  
}

#define MAX_PRINT_STRING 100

#ifdef LOGGING
void __cdecl Log(LPCTSTR lpszFormat, ...)
{
   static TCHAR szOutput[MAX_PRINT_STRING]; // max printable string length
   va_list v1;
   DWORD dwSize, dwNumWritten;
   HANDLE hFile = NULL;

   va_start(v1, lpszFormat);

   dwSize = wvsprintf(szOutput, lpszFormat, v1); 

//  OutputDebugString(szOutput);

//  MessageBox(NULL, szOutput, "AFOSSIL", MB_OK);

   hFile = CreateFile(TEXT("C:\\AFOSSIL.LOG"), GENERIC_WRITE, FILE_SHARE_READ, NULL, 
		   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

   if (hFile != INVALID_HANDLE_VALUE) {
       SetFilePointer(hFile, 0, NULL, FILE_END);
	   WriteFile(hFile, szOutput, dwSize*sizeof(TCHAR), &dwNumWritten, NULL);
	   CloseHandle(hFile);
   }
}
#endif