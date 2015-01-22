/****************************************************************************
*                                                                           *
* Project 64 - A Nintendo 64 emulator.                                      *
* http://www.pj64-emu.com/                                                  *
* Copyright (C) 2012 Project64. All rights reserved.                        *
*                                                                           *
* License:                                                                  *
* GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html                        *
*                                                                           *
****************************************************************************/
#pragma once

class CGDBStub {
public:
	enum REASON {
		REASON_STEP,
		REASON_TRAP,
		REASON_BUS,
		REASON_TLB,
		REASON_PERMLOOP,
	};

	static bool Open ( bool breakAtStart );
	static bool Enter ( REASON reason );
	static void Close ( void );
	static void CloseWithSigkill ( void );
	static void ResumeFromOp ( void );
	static void BreakAtNext ( void );

private:
	CGDBStub(void);							// Disable default constructor
	CGDBStub(const CGDBStub&);				// Disable copy constructor
	CGDBStub& operator=(const CGDBStub&);	// Disable assignment

	static bool AcceptClient ( void );
	static void CloseClient ( void );
	static bool ReacceptClient ( void );
	static bool recvcmd ( char *buf, size_t len );
	static bool sendcmd ( const char *str, size_t len );

	static bool gdb_readregs64 ( const char *arg );
	static bool gdb_writeregs64 ( const char *arg );
	static bool gdb_readregs32 ( const char *arg );
	static bool gdb_writeregs32 ( const char *arg );
	static bool gdb_readmem ( const char *arg );
	static bool gdb_writemem ( const char *arg );

	static SOCKET m_ListenSocket;
	static SOCKET m_Socket;
	static int m_Stepping;
	static bool m_Initial;
	static bool m_WinsockInitialized;
};
