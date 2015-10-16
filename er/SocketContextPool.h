#ifndef __SOCKET_CONTEXT_POOL_H__
#define __SOCKET_CONTEXT_POOL_H__

//#ifndef WINVER
//#define WINVER          0x0500
//#endif
//
//#ifndef _WIN32_WINNT
//#define _WIN32_WINNT    0x0500
//#endif

#ifndef SOCKET_CONTEXT_VER
#define SOCKET_CONTEXT_VER 0x0001
#else
#if defined(SOCKET_CONTEXT_VER) && (SOCKET_CONTEXT_VER < 0x0001)
#error SOCKET_CONTEXT_VER setting conflicts
#endif
#endif

#pragma once

#include <winsock2.h>
#include <windows.h>

#include "../GameSrv-Common/criticalsectionlock.h"


#ifndef __MIN_MAX_TTL__
#define __MIN_MAX_TTL__

#define MAX_TTL     3600000               
#define CHAT_TTL	60000*1000	//*100是测试时，避免客户端再开线程发送心跳包，所以*1000
#define MIN_TTL     60000
#define HEARTBEAT	30000		//30*1000

#endif // __MIN_MAX_TTL__


#ifndef PER_SOCKET_CONTEXT
#define PER_SOCKET_CONTEXT

struct PerSocketContext
{
	SOCKET                   socket;           

	union
	{
		struct
		{
			u_char connectionID   : 1;          // CID       ( 0 Off / 1 On )
			u_char connectionDead : 1;          // ( 0 Off / 1 On )
			u_char closeSocket    : 1;          // ( 0 Off / 1 On )
		} status;

		BYTE statusMask;
		BYTE statusData;
	};

	SOCKADDR_IN              addr;              
	DWORD                    timeToLive;        // Time To Live (TTL)

	char*                    buffer;            
	u_long                   offset;            
	u_long                   length;          

	u_long                   Internal;          
	u_long                   InternalHigh;     

	DWORD                    cid;               // Connection ID

	struct PerSocketContext* next;              // Dual Linked List 
	struct PerSocketContext* prev;              // Dual Linked List 

	struct PerSocketContext* parent;            // BST
	struct PerSocketContext* left;              // BST (Left  = Less than)
	struct PerSocketContext* right;             // BST (Right = Greater than)
};

#endif // PER_SOCKET_CONTEXT


class cSocketContextPool
{
protected:
	CRITICAL_SECTION* mCs;                    
	u_long            mBufferLength;           

protected:
	PerSocketContext* mPagedPoolUsage;        
	PerSocketContext* mNonPagedPoolUsage;      
	PerSocketContext* mBstRoot;                // BST.

protected:
	SIZE_T            mQuotaPagedPoolUsage;   
	SIZE_T            mQuotaNonPagedPoolUsage; 
	SIZE_T            mWorkingSetSize;        

protected:
	PerSocketContext* AllocSocketContext      ( );
	void              FreeSocketContext       ( PerSocketContext** perSocketContext );

	int               CompareCID              ( PerSocketContext* perSocketContext1, PerSocketContext* perSocketContext2 );
	int               CompareCID              ( int cid1, int cid2 );

protected:
	void              AttachPool              ( PerSocketContext** pool, PerSocketContext* perSocketContext );
	void              DetachPool              ( PerSocketContext** pool, PerSocketContext* perSocketContext );

	bool              AttachBst               ( PerSocketContext* perSocketContext );
	bool              DetachBst               ( PerSocketContext* perSocketContext );

	PerSocketContext* GetPool                 ( );
	void              ReleasePool             ( PerSocketContext* perSocketContext, bool isDelete=false );

public:
	cSocketContextPool(CRITICAL_SECTION* cs, u_long bufferLength=0XFFFF);

	bool              DefaultWorkingSize      ( DWORD workingSize );
	void              GetProcessMemoryInfo    ( SIZE_T& quotaPagedPoolUsage, SIZE_T& quotaNonPagedPoolUsage, SIZE_T& workingSetSize );
	void              Shutdown                ( );

	PerSocketContext* GetPagedPoolUsage       ( ) { return mPagedPoolUsage; }

	PerSocketContext* GetCID                  ( DWORD cid );
	bool              SetCID                  ( PerSocketContext* perSocketContext, DWORD cid );

	PerSocketContext* GetPerSocketContext     ( SOCKET socket, SOCKADDR_IN addr, DWORD ttl=MAX_TTL );
	void              ReleasePerSocketContext ( PerSocketContext* perSocketContext, bool isDelete=false );

public:
	virtual ~cSocketContextPool(void);
};

#endif // __SOCKET_CONTEXT_POOL_H__