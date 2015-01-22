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
#include "stdafx.h"

SOCKET CGDBStub::m_ListenSocket = INVALID_SOCKET;
SOCKET CGDBStub::m_Socket = INVALID_SOCKET;
int CGDBStub::m_Stepping;
bool CGDBStub::m_Initial;
bool CGDBStub::m_WinsockInitialized = false;

bool CGDBStub::AcceptClient ( void )
{
	SOCKET s;

	if (g_System->m_EndEmulation)
	{
		return false;
	}

	g_Notify->DisplayMessage(1, "GDB Stub is listening to GDB connection");

	if ((s = accept(m_ListenSocket, NULL, NULL)) == INVALID_SOCKET)
	{
		if (!g_System->m_EndEmulation)
		{
			g_Notify->DisplayError("GDBStub: AcceptClient: Failed to accept.");
		}
		return false;
	}
	m_Socket = s;

	// note: "GNU gdb (GDB) 7.8.50.20150103-cvs" sends "+" on connect
	//       but "IDA Pro 6.7.141229"'s remote GDB does not.
	/*
	char cbuf;
	if (recv(m_Socket, &cbuf, 1, 0) != 1)
	{
		return false;
	}
	if (cbuf != '+')
	{
		return false;
	}
	*/

	m_Initial = true;

	return true;
}

bool CGDBStub::Open ( bool breakAtStart )
{
	SOCKET gs;
	sockaddr_in sin = { 0 };
	int val;

	if (!m_WinsockInitialized)
	{
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0)
		{
			g_Notify->DisplayError("GDBStub: Open: Failed to initialize winsock.");
			return false;
		}
		m_WinsockInitialized = true;
	}

	Close(); // if sockets not closed by some reason, ensure closed.

	gs = socket(AF_INET, SOCK_STREAM, 0);
	if (gs == INVALID_SOCKET) {
		g_Notify->DisplayError("GDBStub: Open: Failed to create a socket.");
		return false;
	}

	val = 1;
	setsockopt(gs, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&val), sizeof val);

	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_family = PF_INET;
	sin.sin_port = htons(23946);	// TODO make configurable
	if (::bind(gs, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) != 0)
	{
		g_Notify->DisplayError("GDBStub: Open: Failed to bind the socket.");
		return false;
	}
	if (listen(gs, 1) != 0)
	{
		g_Notify->DisplayError("GDBStub: Open: Failed to listen the socket.");
		return false;
	}
	m_ListenSocket = gs;

	if (breakAtStart)
	{
		if (!AcceptClient())
		{
			return false;
		}

		// break on top of boot.
		m_Stepping = 1;
	} else {
		// no break on top of boot.
		m_Stepping = 0;
	}

	return true;
}

static int hexc2i ( char c )
{
	if ('0' <= c && c <= '9')
	{
		return c - '0';
	}
	if ('A' <= c && c <= 'F') // maybe not required
	{
		return c - 'A' + 10;
	}
	if ('a' <= c && c <= 'f') // maybe not required
	{
		return c - 'a' + 10;
	}
	return -1;
}

static char nibblei2hexc ( char val )
{
	if (0 <= val && val <= 9)
	{
		return '0' + val;
	}
	if (10 <= val && val <= 15)
	{
		return 'a' + (val - 10);
	}
	return 0;
}

/*
static int readhex2 (const char ** const pbuf, int * const plen)
{
	int val = 0;

	if (pbuf == NULL || plen == NULL)
	{
		return -1;
	}

	for (int i = 0; i < 2; i++)
	{
		int nibble;

		if (*plen <= 0)
		{
			return -1;
		}
		val = val << 4;
		nibble = hexc2i(**pbuf);
		if (nibble == -1)
		{
			return -1;
		}
		val |= nibble;

		(*pbuf)++;
		(*plen)--;
	}

	return val;
}
*/

bool CGDBStub::recvcmd ( char *buf, size_t len )
{
	// TODO: if buffer is insufficient, it should still read from client, but not store.
	//       and when xmit is done, return false.
	enum {
		RESET,
		DOLLAR,
		DATA,
		CKSUM1,
		CKSUM2,
		FINISH
	} state = RESET;
	char *p = NULL /* initialize to choke warning */, * const e = buf + len;
	unsigned char cksum = 0 /* initialize to choke warning */, oksum;
	const char cnak[] = {'-'}, cack[] = {'+'};

	for (;;)
	{
		int r;
		if (state == RESET)
		{
			p = buf;
			cksum = 0;
			oksum = 0;
			state = DOLLAR;
			continue;
		}
		if (e <= p) // force check whenever any state.
		{
			return false;
		}
		if (state == FINISH)
		{
			*p = '\0';
			return true;
		}
		r = recv(m_Socket, p, 1, 0);
		if (r == SOCKET_ERROR)
		{
			if (g_System->m_EndEmulation)
			{
				CGDBStub::Close();
				return false;
			}
			g_Notify->DisplayError("GDBStub: recvcmd: Failed to recv. I'm about to reaccept.");
			r = 0;
		}
		if (r == 0)
		{
			if (!ReacceptClient())
			{
				return false;
			}
			// reconnected. redo from start.
			state = RESET;
			// note: first turn is from client; continue without response is ok.
			continue;
		}
		if (state == DOLLAR) /* note: p == buf */
		{
			if (*p != '$')
			{
				// desync! continue without advancing p.
				continue;
			}
			//p++; // no need to store. overwrite by next one.
			state = DATA;
			continue;
		}
		if (state == DATA)
		{
			if (*p == '#')
			{
				// end of stream marker. 2 * hex will follow.
				//p++; // no need to store. overwrite by next one.
				state = CKSUM1;
				continue;
			}
			cksum = (cksum + *p) & 0xFF/*squelch RTC*/;
			p++;
			// note: no state change.
			continue;
		}
		if (state == CKSUM1)
		{
			int nibble = hexc2i(*p);
			if (nibble == -1)
			{
				// character error.
				if (send(m_Socket, cnak, sizeof cnak, 0) == SOCKET_ERROR)
				{
					return false;
				}
				state = RESET;
				continue;
			}
			oksum = static_cast<char>(nibble << 4); // cast to choke warning.
			//p++; // no need to store. overwrite by next one.
			state = CKSUM2;
			continue;
		}
		if (state == CKSUM2)
		{
			int nibble = hexc2i(*p);
			if (nibble == -1)
			{
				// character error.
				if (send(m_Socket, cnak, sizeof cnak, 0) == SOCKET_ERROR)
				{
					return false;
				}
				state = RESET;
				continue;
			}
			oksum |= nibble;
			if (cksum != oksum)
			{
				// checksum error.
				if (send(m_Socket, cnak, sizeof cnak, 0) == SOCKET_ERROR)
				{
					return false;
				}
				state = RESET;
				continue;
			}
			// checksum ok, send ack.
			if (send(m_Socket, cack, sizeof cack, 0) == SOCKET_ERROR)
			{
				return false;
			}
			//p++; // no need to store. overwrite by next one.
			state = FINISH;
			continue;
		}
		// state error.
		return false;
	}
}

bool CGDBStub::sendcmd ( const char *str, size_t len )
{
	char buf[2048], *p, cbuf;
	unsigned char cksum = 0;

	if (len == -1)
	{
		len = strlen(str);
	}

	if (sizeof buf < 1+len+1+2)
	{
		return false;
	}

	p = buf;

	*p++ = '$';
	memcpy(p, str, len);
	p += len;
	for (size_t i = 0; i < len; i++)
	{
		cksum = (cksum + str[i]) & 0xFF/*squelch RTC*/;
	}
	*p++ = '#';
	*p++ = nibblei2hexc(cksum >> 4);
	*p++ = nibblei2hexc(cksum & 0x0F);

	*p++ = '\0'; // for DEBUG
	p++; // for DEBUG

	//Sleep(500);

	for (int i = 0; ; i++)
	{
		if (send(m_Socket, buf, 1+len+1+2, 0) == SOCKET_ERROR)
		{
			return false;
		}

		if (recv(m_Socket, &cbuf, 1, 0) != 1)
		{
			return false;
		}
		if (cbuf == '+')
		{
			return true;
		}
		if (cbuf == '-')
		{
			if (3 < i)
			{
				return true; // abort...
			}
			continue; // resend
		}
		// unknown response...
		return false;
	}
}

static DWORD readdhex ( const char **str )
{
	DWORD val = 0;
	for (; **str; (*str)++) {
		int i = hexc2i(**str);
		if (i == -1) {
			break;
		}
		val = (val << 4) | i;
	}
	return val;
}

static bool readudwhex ( const char **p, QWORD *pval )
{
	unsigned __int64 val = 0;

	for (int i = 0; i < 8*2; i++)
	{
		if (!**p)
		{
			return false;
		}

		int t = hexc2i(**p);
		if (t == -1)
		{
			return false;
		}
		val = (val << 4) | t;
		(*p)++;
	}

	*pval = val;

	return true;
}

static bool writeudwhex ( char **p, size_t *plen, QWORD val )
{
	for (int i = 0; i < 8*2; i++)
	{
		if (*plen <= 0)
		{
			return false;
		}

		char c = nibblei2hexc((val >> 60) & 0x0F);
		if (c == 0)
		{
			return false;
		}
		val <<= 4;
		*(*p)++ = c;
		(*plen)--;
	}

	return true;
}

static bool readuwhex ( const char **p, DWORD *pval )
{
	DWORD val = 0;

	for (int i = 0; i < 4*2; i++)
	{
		if (!**p)
		{
			return false;
		}

		int t = hexc2i(**p);
		if (t == -1)
		{
			return false;
		}
		val = (val << 4) | t;
		(*p)++;
	}

	*pval = val;

	return true;
}

static bool writeuwhex ( char **p, size_t *plen, DWORD val )
{
	for (int i = 0; i < 4*2; i++)
	{
		if (*plen <= 0)
		{
			return false;
		}

		char c = nibblei2hexc((val >> 28) & 0x0F);
		if (c == 0)
		{
			return false;
		}
		val <<= 4;
		*(*p)++ = c;
		(*plen)--;
	}

	return true;
}

bool CGDBStub::gdb_readregs64 ( const char * /*arg*/ )
{
	// 8[bytes](=64bits) * (32(GPR)+1(SR)+2(LO/HI)+2(BV/CR)+1(PC)+32(FPR)+2(FPCSR/FIR))+1(Res) * 2(chars/bytes:hexstring) = 1168[bytes] least required buffer.
	const size_t clen = 1168;
	size_t len = clen;
	char sbuf[clen];
	char *p = sbuf;
	for (int i = 0; i < 32; i++) { if (!writeudwhex(&p, &len, g_Reg->m_GPR[i].UDW)) { goto enomem; } }
	if (!writeudwhex(&p, &len, /*zero-ext*/g_Reg->STATUS_REGISTER)) { goto enomem; }
	if (!writeudwhex(&p, &len, g_Reg->m_LO.UDW)) { goto enomem; }
	if (!writeudwhex(&p, &len, g_Reg->m_HI.UDW)) { goto enomem; }
	if (!writeudwhex(&p, &len, /*sign-ext*/static_cast<QWORD>(static_cast<__int32>(g_Reg->BAD_VADDR_REGISTER)))) { goto enomem; }
	if (!writeudwhex(&p, &len, /*zero-ext*/g_Reg->CAUSE_REGISTER)) { goto enomem; }
	if (!writeudwhex(&p, &len, /*sign-ext*/static_cast<QWORD>(static_cast<__int32>(g_Reg->m_PROGRAM_COUNTER)))) { goto enomem; }
	for (int i = 0; i < 32; i++) { if (!writeudwhex(&p, &len, *reinterpret_cast<QWORD*>(g_Reg->m_FPR_D[i]))) { goto enomem; } }
	if (!writeudwhex(&p, &len, /*zero-ext*/g_Reg->m_FPCR[32])) { goto enomem; }
	if (!writeudwhex(&p, &len, /*zero-ext*/g_Reg->m_FPCR[0])) { goto enomem; }
	if (!writeudwhex(&p, &len, 0)) { goto enomem; } // Restart: Linux specific.

	if (!sendcmd(sbuf, p - sbuf))
	{
		g_Notify->DisplayError("GDBStub: gdb_readregs64: Failed to send response.");
		return false;
	}
	return true;
enomem:
	if (!sendcmd("E0c", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_readregs64: Failed to send error response.");
		return false;
	}
	return true;
}

bool CGDBStub::gdb_writeregs64 ( const char *arg )
{
	QWORD tmp;
	for (int i = 0; i < 32; i++) { if (!readudwhex(&arg, &g_Reg->m_GPR[i].UDW)) { goto esrch; } }
	if (!readudwhex(&arg, &tmp)) { goto esrch; } g_Reg->STATUS_REGISTER = tmp & 0xFFFFffff;
	if (!readudwhex(&arg, &g_Reg->m_LO.UDW)) { goto esrch; }
	if (!readudwhex(&arg, &g_Reg->m_HI.UDW)) { goto esrch; }
	if (!readudwhex(&arg, &tmp)) { goto esrch; } g_Reg->BAD_VADDR_REGISTER = tmp & 0xFFFFffff;
	if (!readudwhex(&arg, &tmp)) { goto esrch; } g_Reg->CAUSE_REGISTER = tmp & 0xFFFFffff;
	if (!readudwhex(&arg, &tmp)) { goto esrch; }
	if (g_Reg->m_PROGRAM_COUNTER != (tmp & 0xFFFFffff))
	{
		/*
		R4300iOp::m_NextInstruction = JUMP;
		R4300iOp::m_JumpToLocation = tmp & 0xFFFFffff;
		*/
		// note: should do "NI=Jump, JTL=tmp", but if stepping it cause stepping same position...
		g_Reg->m_PROGRAM_COUNTER = tmp & 0xFFFFffff;
		R4300iOp::m_NextInstruction = NORMAL;
	}
	// XXX should be problematic. if you use writeregs with caution (use f_n where n%2=1 NOT =0), it is ok.
	for (int i = 0; i < 32; i++) { if (!readudwhex(&arg, reinterpret_cast<QWORD*>(g_Reg->m_FPR_D[i]))) { goto esrch; } }
	if (!readudwhex(&arg, &tmp)) { goto esrch; } g_Reg->m_FPCR[31] = tmp & 0xFFFFffff;
	if (!readudwhex(&arg, &tmp)) { goto esrch; } g_Reg->m_FPCR[0] = tmp & 0xFFFFffff;
	if (!readudwhex(&arg, &tmp)) { goto esrch; } // Restart: Linux specific. ignore it.

	if (!sendcmd("OK", 2))
	{
		g_Notify->DisplayError("GDBStub: gdb_writeregs64: Failed to send response.");
		return false;
	}
	return true;
esrch:
	if (!sendcmd("E03", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_writeregs64: Failed to send error response.");
		return false;
	}
	return true;
}

bool CGDBStub::gdb_readregs32 ( const char * /*arg*/ )
{
	// 4[bytes](=32bits) * (32(GPR)+1(SR)+2(LO/HI)+2(BV/CR)+1(PC)+32(FPR)+2(FPCSR/FIR))+1(Res) * 2(chars/bytes:hexstring) = 1168[bytes] least required buffer.
	const size_t clen = 584;
	size_t len = clen;
	char sbuf[clen];
	char *p = sbuf;
	for (int i = 0; i < 32; i++) { if (!writeuwhex(&p, &len, g_Reg->m_GPR[i].UW[0])) { goto enomem; } }
	if (!writeuwhex(&p, &len, /*zero-ext*/g_Reg->STATUS_REGISTER)) { goto enomem; }
	if (!writeuwhex(&p, &len, g_Reg->m_LO.UW[0])) { goto enomem; }
	if (!writeuwhex(&p, &len, g_Reg->m_HI.UW[0])) { goto enomem; }
	if (!writeuwhex(&p, &len, static_cast<unsigned __int32>(g_Reg->BAD_VADDR_REGISTER))) { goto enomem; }
	if (!writeuwhex(&p, &len, /*zero-ext*/g_Reg->CAUSE_REGISTER)) { goto enomem; }
	if (!writeuwhex(&p, &len, static_cast<unsigned __int32>(g_Reg->m_PROGRAM_COUNTER))) { goto enomem; }
	for (int i = 0; i < 32; i++) { if (!writeuwhex(&p, &len, *reinterpret_cast<unsigned __int32*>(g_Reg->m_FPR_S[i]))) { goto enomem; } }
	if (!writeuwhex(&p, &len, /*zero-ext*/g_Reg->m_FPCR[32])) { goto enomem; }
	if (!writeuwhex(&p, &len, /*zero-ext*/g_Reg->m_FPCR[0])) { goto enomem; }
	if (!writeuwhex(&p, &len, 0)) { goto enomem; } // Restart: Linux specific.

	if (!sendcmd(sbuf, p - sbuf))
	{
		g_Notify->DisplayError("GDBStub: gdb_readregs32: Failed to send response.");
		return false;
	}
	return true;
enomem:
	if (!sendcmd("E0c", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_readregs32: Failed to send error response.");
		return false;
	}
	return true;
}

bool CGDBStub::gdb_writeregs32 ( const char *arg )
{
	DWORD tmp;
	for (int i = 0; i < 32; i++) { if (!readuwhex(&arg, &g_Reg->m_GPR[i].UW[0])) { goto esrch; } }
	if (!readuwhex(&arg, &g_Reg->STATUS_REGISTER)) { goto esrch; }
	if (!readuwhex(&arg, &g_Reg->m_LO.UW[0])) { goto esrch; }
	if (!readuwhex(&arg, &g_Reg->m_HI.UW[0])) { goto esrch; }
	if (!readuwhex(&arg, &g_Reg->BAD_VADDR_REGISTER)) { goto esrch; }
	if (!readuwhex(&arg, &g_Reg->CAUSE_REGISTER)) { goto esrch; }
	if (!readuwhex(&arg, &tmp)) { goto esrch; }
	if (g_Reg->m_PROGRAM_COUNTER != tmp)
	{
		/*
		R4300iOp::m_NextInstruction = JUMP;
		R4300iOp::m_JumpToLocation = tmp & 0xFFFFffff;
		*/
		// note: should do "NI=Jump, JTL=tmp", but if stepping it cause stepping same position...
		g_Reg->m_PROGRAM_COUNTER = tmp;
		R4300iOp::m_NextInstruction = NORMAL;
	}
	// XXX should be problematic. if you use writeregs with caution (use f_n where n%2=1 NOT =0), it is ok.
	for (int i = 0; i < 32; i++) { if (!readuwhex(&arg, reinterpret_cast<DWORD*>(g_Reg->m_FPR_S[i]))) { goto esrch; } }
	if (!readuwhex(&arg, &g_Reg->m_FPCR[31])) { goto esrch; }
	if (!readuwhex(&arg, &g_Reg->m_FPCR[0])) { goto esrch; }
	if (!readuwhex(&arg, &tmp)) { goto esrch; } // Restart: Linux specific. ignore it.

	if (!sendcmd("OK", 2))
	{
		g_Notify->DisplayError("GDBStub: gdb_writeregs32: Failed to send response.");
		return false;
	}
	return true;
esrch:
	if (!sendcmd("E03", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_writeregs32: Failed to send error response.");
		return false;
	}
	return true;
}

bool CGDBStub::gdb_readmem ( const char *arg )
{
	DWORD addr = 0, length = 0, endaddr;
	DWORD tmp;
	char sbuf[2048], *q;
	const char *p = arg;

	addr = readdhex(&p);
	if (*p != ',') {
		goto eperm;
	}

	p++; // skip ","

	length = readdhex(&p);
	if (*p != '\0') {
		goto eperm;
	}

	if (sizeof sbuf < length * 2)
	{
		goto enomem;
	}

	endaddr = addr + length; // note: endaddr may wrap. ex. addr=0xFFFFffff,length=4
	for (q = sbuf; addr != endaddr; addr++)
	{
		if ((q == sbuf) || ((addr & 3) == 0))
		{
			if (!g_MMU->LW_VAddr(addr & ~3, tmp)) // must use LW otherwise can't access NonMemory area.
			{
				break;
			}
		}
		*q++ = nibblei2hexc(((tmp >> (((addr ^ 3) & 3) * 8)) >> 4) & 0x0F);
		*q++ = nibblei2hexc(((tmp >> (((addr ^ 3) & 3) * 8)) >> 0) & 0x0F);
	}

	// note: http://ftp.gnu.org/old-gnu/Manuals/gdb/html_node/gdb_129.html says "Can be fewer bytes than requested if able to read only part of the data."
	//       but i386-stub returns "E03" if mem_err.
	//       and if something went wrong and returns empty, "GNU gdb (GDB) 7.8.50.20150103-cvs" says "target.c:1379: internal-error: target_xfer_partial: Assertion `*xfered_len > 0' failed."
	//       so make error here.
	if (addr != endaddr)
	{
		goto esrch;
	}

	if (!sendcmd(sbuf, q - sbuf))
	{
		g_Notify->DisplayError("GDBStub: gdb_readmem: Failed to send response.");
		return false;
	}
	return true;
eperm:
	if (!sendcmd("E01", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_readmem: Failed to send error response.");
		return false;
	}
	return true;
esrch:
	if (!sendcmd("E03", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_readmem: Failed to send error response.");
		return false;
	}
	return true;
enomem:
	if (!sendcmd("E0c", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_readmem: Failed to send error response.");
		return false;
	}
	return true;
}

bool CGDBStub::gdb_writemem ( const char *arg )
{
	DWORD addr = 0, length = 0;
	DWORD tmp = 0xABADF00D/*squelching warning*/;
	const char *p = arg;

	addr = readdhex(&p);
	if (*p != ',') {
		goto enoent; // XXX not eperm??
	}

	p++; // skip ","

	length = readdhex(&p);
	if (*p != ':') {
		goto enoent; // XXX not eperm??
	}

	p++; // skip ":"

	arg = p; // let arg as head of data hexstring.

	// note: i386-stub uses hex2mem that writes converted bytes directly.
	//       so when some error occured, data are partially written.
	while (*p)
	{
		if ((p != arg) && ((addr & 3) == 0))
		{
			if (!g_MMU->SW_VAddr(addr - 4, tmp)) // must use SW otherwise can't access NonMemory area.
			{
				goto esrch; // FIXME what is most suitable?
			}
		}
		if ((p == arg) || ((addr & 3) == 0))
		{
			if (!g_MMU->LW_VAddr(addr & ~3, tmp)) // must use LW otherwise can't access NonMemory area.
			{
				goto esrch; // FIXME what is most suitable?
			}
		}
		int i = hexc2i(*p++);
		if (i == -1)
		{
			goto enoent; // FIXME what is most suitable?
		}
		int j = hexc2i(*p++);
		if (j == -1)
		{
			goto enoent; // FIXME what is most suitable?
		}
		int o = ((addr ^ 3) & 3) * 8;
		tmp = (tmp & ~(0xFF << o)) | (((i << 4) | j) << o);
		addr++;
	}
	if ((p != arg))
	{
		if (!g_MMU->SW_VAddr((addr & ~3) - (((addr & 3) == 0) ? 4 : 0), tmp)) // must use SW otherwise can't access NonMemory area.
		{
			goto esrch; // FIXME what is most suitable?
		}
	}

	if (!sendcmd("OK", 2))
	{
		g_Notify->DisplayError("GDBStub: gdb_writemem: Failed to send response.");
		return false;
	}
	return true;
enoent:
	if (!sendcmd("E02", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_writemem: Failed to send error response.");
		return false;
	}
	return true;
esrch:
	if (!sendcmd("E03", 3))
	{
		g_Notify->DisplayError("GDBStub: gdb_writemem: Failed to send error response.");
		return false;
	}
	return true;
}

// http://ftp.gnu.org/old-gnu/Manuals/gdb/html_node/gdb_129.html
// http://www.cs.columbia.edu/~%20sedwards/classes/2002/w4995-02/tan-final.pdf
// http://kozos.jp/kozos/porting04.html
bool CGDBStub::Enter ( REASON reason )
{
	char sigbuf[3];

	if (reason == REASON_STEP)
	{
		if (!m_Stepping)
		{
			return true;
		}
		if (--m_Stepping)
		{
			return true;
		}
	}

	if (m_Socket == INVALID_SOCKET)
	{
		if (!AcceptClient())
		{
			if (!g_System->m_EndEmulation)
			{
				g_Notify->DisplayError("GDBStub: Enter: Failed to accept client.");
			}
			return false;
		}
	}

	g_Notify->DisplayMessage(1, "GDB Stub is in active");

	if (!m_Initial)
	{
		// reply current status.
		sigbuf[0] = 'S';
		switch(reason) // gdb/mips-linux-tdep.c
		{
		case REASON_STEP: sigbuf[1] = '0'; sigbuf[2] = '5'; break; // SIGTRAP
		case REASON_TRAP: sigbuf[1] = '0'; sigbuf[2] = '5'; break; // SIGTRAP
		case REASON_BUS: sigbuf[1] = '0'; sigbuf[2] = 'a'; break; // SIGBUS
		case REASON_TLB: sigbuf[1] = '0'; sigbuf[2] = 'b'; break; // SIGSEGV
		case REASON_PERMLOOP: sigbuf[1] = '0'; sigbuf[2] = '3'; break; // SIGQUIT
		}
		if (!sendcmd(sigbuf, 3))
		{
			g_Notify->DisplayError("GDBStub: Enter: Failed to send status.");
			return false;
		}
	}
	else
	{
		sigbuf[0] = 'S';
		// dummy number.
		sigbuf[1] = '0';
		sigbuf[2] = '0';
		m_Initial = false;
	}

	for (;;)
	{
		char buf[2048];
		if (!recvcmd(buf, sizeof buf))
		{
			return false;
		}
		switch (buf[0])
		{
		case 'g': // read all regs
			if (!gdb_readregs64(&buf[1]))
			{
				return false;
			}
			break;
		case 'G': // write all regs
			if (!gdb_writeregs64(&buf[1]))
			{
				return false;
			}
			break;
		case 'm': // read mem
			if (!gdb_readmem(&buf[1]))
			{
				return false;
			}
			break;
		case 'M': // write mem
			if (!gdb_writemem(&buf[1]))
			{
				return false;
			}
			break;
		case '?': // get last signal
			if (!sendcmd(sigbuf, 3))
			{
				g_Notify->DisplayError("GDBStub: Enter: Failed to send current status.");
				return false;
			}
			break;
		case 's': // step
			m_Stepping = 1;
			g_Notify->DisplayMessage(1, "GDB Stub will step");
			return true;
		case 'c': // continue
			m_Stepping = 0;
			g_Notify->DisplayMessage(1, "GDB Stub will continue");
			return true;
		default:
			// send empty response if command is unknown.
			if (!sendcmd("", 0))
			{
				g_Notify->DisplayError("GDBStub: Enter: Failed to send empty (=unsupported) response.");
				return false;
			}
			break;
		}
	}
}

void CGDBStub::CloseClient ( void )
{
	if (m_Socket != INVALID_SOCKET)
	{
		closesocket(m_Socket);
		m_Socket = INVALID_SOCKET;
	}
}

bool CGDBStub::ReacceptClient ( void )
{
	CloseClient();
	return AcceptClient();
}

void CGDBStub::Close ( void )
{
	CloseClient();
	if (m_ListenSocket != INVALID_SOCKET)
	{
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;
	}
}

void CGDBStub::CloseWithSigkill ( void )
{
	if (m_Socket != INVALID_SOCKET)
	{
		sendcmd("X09", 3); // SIGKILL. note: ignoreing sendcmd error that not needed.
	}
	Close();
}

void CGDBStub::ResumeFromOp ( void )
{
	if (m_Stepping)
	{
		m_Stepping = 2;
	}
}

void CGDBStub::BreakAtNext ( void )
{
	m_Stepping = 1;
}
