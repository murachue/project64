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

class CRSP_Plugin
{
	typedef struct {
		/* Menu */
		/* Items should have an ID between 5001 and 5100 */
		MENU_HANDLE hRSPMenu;
		void (__cdecl *ProcessMenuItem) ( int ID );

		/* Break Points */
		BOOL UseBPoints;
		char BPPanelName[20];
		void (__cdecl *Add_BPoint)      ( void );
		void (__cdecl *CreateBPPanel)   ( MENU_HANDLE hDlg, RECT_STRUCT rcBox );
		void (__cdecl *HideBPPanel)     ( void );
		void (__cdecl *PaintBPPanel)    ( WINDOWS_PAINTSTRUCT ps );
		void (__cdecl *ShowBPPanel)     ( void );
		void (__cdecl *RefreshBpoints)  ( MENU_HANDLE hList );
		void (__cdecl *RemoveBpoint)    ( MENU_HANDLE hList, int index );
		void (__cdecl *RemoveAllBpoint) ( void );
		
		/* RSP command Window */
		void (__cdecl *Enter_RSP_Commands_Window) ( void );
	} RSPDEBUG_INFO;

	typedef struct {
		void (__cdecl *UpdateBreakPoints)( void );
		void (__cdecl *UpdateMemory)( void );
		void (__cdecl *UpdateR4300iRegisters)( void );
		void (__cdecl *Enter_BPoint_Window)( void );
		void (__cdecl *Enter_R4300i_Commands_Window)( void );
		void (__cdecl *Enter_R4300i_Register_Window)( void );
		void (__cdecl *Enter_RSP_Commands_Window) ( void );
		void (__cdecl *Enter_Memory_Window)( void );
	} DEBUG_INFO;

public:
	CRSP_Plugin  ( const char * FileName);
	~CRSP_Plugin ( void );

	bool Initiate   ( CPlugins * Plugins, CN64System * System );
	bool Initilized ( void ) { return m_Initilized; }
	void Close      ( void );
	void RomOpened  ( void );
	void RomClose   ( void );
	void GameReset  ( void );
	stdstr PluginName ( void ) const { return m_PluginInfo.Name; }

	void  (__cdecl *Config)          ( DWORD hParent );
	DWORD (__cdecl *DoRspCycles)     ( DWORD );
	void  (__cdecl *EnableDebugging) ( BOOL Enable );

	MENU_HANDLE GetDebugMenu (void ) { return m_RSPDebug.hRSPMenu; }
	void ProcessMenuItem (int id );

private:
	CRSP_Plugin(void);							// Disable default constructor
	CRSP_Plugin(const CRSP_Plugin&);			// Disable copy constructor
	CRSP_Plugin& operator=(const CRSP_Plugin&);	// Disable assignment

	void Init ( const char * FileName );
	bool Initiate_1_0         ( CPlugins * Plugins, CN64System * System );
	void UnloadPlugin         ( void );
	
	RSPDEBUG_INFO m_RSPDebug;
	void        * m_hDll;	
	bool          m_Initilized, m_RomOpen;
	DWORD         m_CycleCount;
	PLUGIN_INFO   m_PluginInfo;

	void (__cdecl *CloseDLL)         ( void );
	void (__cdecl *RomOpen)          ( void );
	void (__cdecl *RomClosed)        ( void );
	void (__cdecl *GetDebugInfo)     ( RSPDEBUG_INFO * GFXDebugInfo );
	void (__cdecl *InitiateDebugger) ( DEBUG_INFO DebugInfo);
	void (__cdecl *PluginOpened)     ( void );
	void (__cdecl *SetSettingInfo)   ( PLUGIN_SETTINGS * info );
	void (__cdecl *SetSettingInfo2)  ( PLUGIN_SETTINGS2 * info );
	void (__cdecl *SetSettingInfo3)  ( PLUGIN_SETTINGS3 * info );
};
