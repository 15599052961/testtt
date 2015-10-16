#ifndef __WSA_STARTUP_H__
#define __WSA_STARTUP_H__

//#ifndef WINVER
//#define WINVER          0x0500
//#endif
//
//#ifndef _WIN32_WINNT
//#define _WIN32_WINNT    0x0500
//#endif

#ifndef WSA_STARTUP_VER
#define WSA_STARTUP_VER 0x0001
#else
#if defined(WSA_STARTUP_VER) && (WSA_STARTUP_VER < 0x0001)
#error WSA_STARTUP_VER setting conflicts
#endif
#endif

#pragma once

#include <winsock2.h>
#include <windows.h>
// IP - link "iphlpapi.lib"
#include <iphlpapi.h>
#if(WSA_STARTUP_VER >= 0x0002)
// IP - link "iphlpapi.lib"
#include <iphlpapi.h>
#endif

class cWSAStartup
{
protected:
	WORD    m_versionRequested;
	WSADATA m_wsaData;

#if(WSA_STARTUP_VER >= 0x0002)
protected:
	PFIXED_INFO      mFixedInfo;
	PMIB_IFTABLE     mIfTable;
	PIP_ADAPTER_INFO mAdapterInfo;
#endif // #if(WSA_STARTUP_VER >= 0x0002)

public:
	cWSAStartup(void)
	{
		// WS2_32.DLL
		m_versionRequested = MAKEWORD(2,2);
		::WSAStartup( m_versionRequested, &m_wsaData );
#if(WSA_STARTUP_VER >= 0x0002)
		mFixedInfo   = NULL;
		mIfTable     = NULL;
		mAdapterInfo = NULL;
#endif // #if(WSA_STARTUP_VER >= 0x0002)
	}

public:
	virtual ~cWSAStartup(void)
	{
#if(WSA_STARTUP_VER >= 0x0002)
		GlobalFree( mFixedInfo );
		GlobalFree( mIfTable );
		GlobalFree( mAdapterInfo );
#endif // #if(WSA_STARTUP_VER >= 0x0002)

		// WS2_32.DLL
		::WSACleanup( );
	}

#if(WSA_STARTUP_VER >= 0x0002)
public:
	bool GetLocalHostInfo ( void );
	bool GetMacAddress1   ( void );
	bool GetMacAddress2   ( void );
#endif // #if(WSA_STARTUP_VER >= 0x0002)
};

#if(WSA_STARTUP_VER >= 0x0002)
inline bool cWSAStartup::GetLocalHostInfo(void)
{
	DWORD fixedInfoSize   = 0;
	DWORD fixedInfoRetVal = 0;

	// Get the main IP configuration information for this machine using a FIXED_INFO structure
	fixedInfoRetVal = GetNetworkParams( mFixedInfo, &fixedInfoSize );
	if ( fixedInfoRetVal == ERROR_BUFFER_OVERFLOW )
	{
		GlobalFree( mFixedInfo );
		mFixedInfo = (PFIXED_INFO)GlobalAlloc( GPTR, fixedInfoSize );
	}

	fixedInfoRetVal = GetNetworkParams( mFixedInfo, &fixedInfoSize );
	return (fixedInfoRetVal == ERROR_SUCCESS) ? true : false;
}

inline bool cWSAStartup::GetMacAddress1(void)
{
	DWORD ifTableSize   = 0;
	DWORD ifTableRetVal = 0;

	// Make an initial call to GetIfTable to get the necessary size into the dwSize variable
	if ( GetIfTable( mIfTable, &ifTableSize, 0 ) == ERROR_INSUFFICIENT_BUFFER )
	{
		GlobalFree( mIfTable );
		mIfTable = (MIB_IFTABLE*)GlobalAlloc( GPTR, ifTableSize );
	}
	// Make a second call to GetIfTable to get the actual data we want
	ifTableRetVal = GetIfTable( mIfTable, &ifTableSize, 0 );
	return (ifTableRetVal == NO_ERROR) ? true : false;
}

inline bool cWSAStartup::GetMacAddress2(void)
{
	DWORD            infoSize    = 0;
	DWORD            infoRetVal  = 0;

	if ( GetAdaptersInfo( mAdapterInfo, &infoSize ) == ERROR_BUFFER_OVERFLOW )
	{
		GlobalFree( mAdapterInfo );
		mAdapterInfo = (PIP_ADAPTER_INFO)GlobalAlloc( GPTR, infoSize );
	}
	infoRetVal = GetAdaptersInfo( mAdapterInfo, &infoSize );
	return infoRetVal == ERROR_SUCCESS ? true : false;
}
#endif // #if(WSA_STARTUP_VER >= 0x0002)

#endif // __WSA_STARTUP_H__
