#ifndef __IOCP_SERVER_H__
#define __IOCP_SERVER_H__

//#ifndef WINVER
//#define WINVER          0x0500
//#endif
//
//#ifndef _WIN32_WINNT
//#define _WIN32_WINNT    0x0500
//#endif

#ifndef IOCP_SERVER_VER
#define IOCP_SERVER_VER 0x0001
#else
#if defined(IOCP_SERVER_VER) && (IOCP_SERVER_VER < 0x0001)
#error IOCP_SERVER_VER setting conflicts
#endif
#endif

#pragma once

#include <winsock2.h>
#include <windows.h>
#include <mstcpip.h>
#include <lmerr.h>
#include <process.h>
#include <stdlib.h>
#include <stdarg.h>

#include "../GameSrv-Common/iocontextpool.h"
#include "../GameSrv-Common/criticalsectionlock.h"
#include "socketcontextpool.h"

#define IOCP_MAX_WORKER_THREAD  16
#define IOCP_SHUTDOWN           ((OVERLAPPED*)((__int64)-1))

class cIocpServer
{
protected:
	CRITICAL_SECTION    mCs;
	HANDLE              mIocp;
	SOCKET              mSocket;
	SOCKADDR_IN         mAddr;
	unsigned short      mPort;
	HANDLE              mIocpAcceptThread;
	int                 mIocpWorkerThreadNumber;
	HANDLE              mIocpWorkerThread[IOCP_MAX_WORKER_THREAD];
	HANDLE              mIocpBackendThread;

	cIoContextPool*     mIoContextPool;
	cSocketContextPool* mSocketContextPool;

	IoContextBuffer*    mIoContextFrontBuffer;
	IoContextBuffer*    mIoContextBackBuffer;

	bool                mRunServer;
	bool                mEndServer;

protected:
	virtual bool        SendExec           ( PerIoContext* perIoContext );
	virtual bool        SendPost           ( PerIoContext* perIoContext );
	virtual bool        RecvPost           ( PerIoContext* perIoContext );

	virtual void        Close              ( PerSocketContext* perSocketContext, PerIoContext* perIoContext );
	virtual void        Close              ( PerSocketContext* perSocketContext );

	virtual bool        AcceptComplete     ( PerSocketContext* perSocketContext );

	virtual bool        SendComplete       ( PerSocketContext* perSocketContext, PerIoContext* perIoContext, DWORD bytesTransferred );
	virtual bool        RecvComplete       ( PerSocketContext* perSocketContext, PerIoContext* perIoContext, DWORD bytesTransferred );
	virtual bool        CallbackComplete   ( PerSocketContext* perSocketContext, PerIoContext* perIoContext, DWORD bytesTransferred );

	virtual void        IoContextPresent     ( );

public:
	cIocpServer (void);

	virtual bool        Initialize         ( char* ipAddr="", unsigned short port=5001, unsigned short numWorkerThreads=2, unsigned short bufferLength=DEF_TCP_PACKET_SIZE );
	virtual void        Shutdown           ( DWORD maxWait=INFINITE );

	virtual void        GetIoPoolUsage     ( SIZE_T& quotaPagedPoolUsage, SIZE_T& quotaNonePagedPoolUsage, SIZE_T& workingSetSize );
	virtual void        GetSocketPoolUsage ( SIZE_T& quotaPagedPoolUsage, SIZE_T& quotaNonePagedPoolUsage, SIZE_T& workingSetSize );
	virtual BOOL        QueueRequest       ( ULONG_PTR completionKey, LPOVERLAPPED overlapped, DWORD bytesTransferred );

	virtual DWORD       AcceptThread       ( );
	virtual DWORD       WorkerThread       ( );
	virtual DWORD       BackendThread      ( );
	virtual void		GameErrorLog	   ( char* format, ... );

public:
	virtual ~cIocpServer (void);

public:
	static DWORD WINAPI AcceptThreadStartingPoint  ( void* ptr );
	static DWORD WINAPI WorkerThreadStartingPoint  ( void* ptr );
	static DWORD WINAPI BackendThreadStartingPoint ( void* ptr );
};

#endif // __IOCP_SERVER_H__