// Include files
#include "socketcontextpool.h"
#include <stdio.h>

// Local definitions
#define SetLeft(P,L)    { (P)->left  = L; if (L) (L)->parent = P; }
#define SetRight(P,R)   { (P)->right = R; if (R) (R)->parent = P; }

// Global data

// cSocketContextPool Constructor
cSocketContextPool::cSocketContextPool(CRITICAL_SECTION* cs, u_long bufferLength) : mCs(cs), mBufferLength(bufferLength)
{
	// Pool Usage Pointer
	mPagedPoolUsage         = NULL;
	mNonPagedPoolUsage      = NULL;

	// BST Root Pointer
	mBstRoot                = NULL;

	mQuotaPagedPoolUsage    = 0;
	mQuotaNonPagedPoolUsage = 0;
	mWorkingSetSize         = 0;
}

// ~cSocketContextPool Destructor.
cSocketContextPool::~cSocketContextPool(void)
{
	// cSocketContextPool
	Shutdown( );
}

// DefaultWorkingSize Method
bool cSocketContextPool::DefaultWorkingSize(DWORD workingSize)
{
	cCSLock           lock( mCs );                  // Critical Section
	PerSocketContext* temp = NULL;

	try
	{
		while ( workingSize > 0 )
		{
			temp = GetPool( );

				if ( temp == NULL ) throw false;    // Socket Context

			workingSize--;
		}
		throw true;
	}
	catch ( bool boolean )
	{
		while ( mPagedPoolUsage != NULL )
		{
			ReleasePool( mPagedPoolUsage );
		}
		return boolean;
	}
}

// GetProcessMemoryInfo Method
void cSocketContextPool::GetProcessMemoryInfo(SIZE_T& quotaPagedPoolUsage, SIZE_T& quotaNonPagedPoolUsage, SIZE_T& workingSetSize)
{
	cCSLock lock( mCs );
	quotaPagedPoolUsage    = mQuotaPagedPoolUsage;
	quotaNonPagedPoolUsage = mQuotaNonPagedPoolUsage;
	workingSetSize         = mWorkingSetSize;
}

// Shutdown Method
void cSocketContextPool::Shutdown( )
{
	cCSLock           lock( mCs );
	PerSocketContext* temp;

	// BST Shutdown
	while ( mBstRoot != NULL )
	{
		DetachBst( mBstRoot );
	}
	// Pool Usage Shutdown - PagedPoolUsage
	while ( mPagedPoolUsage != NULL )
	{
		temp               = mPagedPoolUsage;
		mPagedPoolUsage    = mPagedPoolUsage->next;
		FreeSocketContext( &temp );
	}
	// Pool Usage Shutdown - NonPagedPoolUsage
	while ( mNonPagedPoolUsage != NULL )
	{
		temp               = mNonPagedPoolUsage;
		mNonPagedPoolUsage = mNonPagedPoolUsage->next;
		FreeSocketContext( &temp );
	}
}

// AllocSocketContext Method - PerSocketContext
PerSocketContext* cSocketContextPool::AllocSocketContext( )
{
	PerSocketContext* perSocketContext = (PerSocketContext*)GlobalAlloc( GPTR, sizeof(PerSocketContext) );

	if ( perSocketContext != NULL )
	{
		// Socket Context
		perSocketContext->socket       = INVALID_SOCKET;                            
		perSocketContext->statusData   = 0;                                         
		memset( &perSocketContext->addr, 0, sizeof(SOCKADDR_IN) );                   
		perSocketContext->timeToLive   = 0;                                          // Time To Live.
		perSocketContext->buffer       = (char*)GlobalAlloc( GPTR, mBufferLength );  
		perSocketContext->offset       = 0;                                          
		perSocketContext->length       = mBufferLength;                             
		perSocketContext->Internal     = 0;                                       
		perSocketContext->InternalHigh = 0;                                        
		perSocketContext->cid          = 0;                                          // Connection ID.
		perSocketContext->prev         = NULL;                                      
		perSocketContext->next         = NULL;                                     
		perSocketContext->parent       = NULL;                                    
		perSocketContext->left         = NULL;                                   
		perSocketContext->right        = NULL;                                   

		// mWorkingSetSize
		mWorkingSetSize++;
	}

	return perSocketContext;
}

// FreeSocketContext Method - PerSocketContext
void cSocketContextPool::FreeSocketContext(PerSocketContext** perSocketContext)
{
	if ( perSocketContext != NULL )
	{
		if ( (*perSocketContext)->socket != INVALID_SOCKET )
		{
			// force the subsequent closesocket to be abortative.
			//
			LINGER linger;

			linger.l_onoff  = 1;
			linger.l_linger = 0;
			setsockopt( (*perSocketContext)->socket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger) );

			closesocket( (*perSocketContext)->socket );
			(*perSocketContext)->socket = INVALID_SOCKET;
		}
		if ( (*perSocketContext)->buffer != NULL )
		{
			GlobalFree ( (*perSocketContext)->buffer );
			(*perSocketContext)->buffer = NULL;
		}
		GlobalFree ( (*perSocketContext) );
		(*perSocketContext) = NULL;

		// mWorkingSetSize
		mWorkingSetSize--;
	}
}

// CompareCID Method
int cSocketContextPool::CompareCID(PerSocketContext* perSocketContext1, PerSocketContext* perSocketContext2)
{
	return (perSocketContext1->cid - perSocketContext2->cid);
}
int cSocketContextPool::CompareCID(int cid1, int cid2)
{
	return (cid1 - cid2);
}

// AttachPool Method
void cSocketContextPool::AttachPool(PerSocketContext** pool, PerSocketContext* perSocketContext)
{
	if ( (*pool) != NULL )
	{
		(*pool)->prev = perSocketContext;

		perSocketContext->prev = NULL;
		perSocketContext->next = (*pool);
	}
	(*pool) = perSocketContext;
}

// DetachPool Method
void cSocketContextPool::DetachPool(PerSocketContext** pool, PerSocketContext* perSocketContext)
{
	PerSocketContext* prev = perSocketContext->prev;
	PerSocketContext* next = perSocketContext->next;

	if ( prev == NULL && next == NULL )
	{
		(*pool) = NULL;
	}
	else if ( prev == NULL && next != NULL )
	{
		next->prev = NULL;
		(*pool) = next;
	}
	else if ( prev != NULL && next == NULL )
	{
		prev->next = NULL;
	}
	else if ( prev != NULL && next != NULL )
	{
		prev->next = next;
		next->prev = prev;
	}

	perSocketContext->prev = NULL;
	perSocketContext->next = NULL;
}

// AttachBst Method
bool cSocketContextPool::AttachBst(PerSocketContext* perSocketContext)
{
	PerSocketContext* parent = NULL;
	PerSocketContext* child  = mBstRoot;
	int               result;


	//2014-7-28 14:57:34， oiooooio
	//原来是while(child != NULL),因为这里出现过死循环，故加了一个条件判断
	for (int i = mQuotaPagedPoolUsage*2; i > 0 && child != NULL; --i)
	{
		parent = child;
		result = CompareCID( child->cid, perSocketContext->cid );

		if ( result == 0 ) return false;
		else if ( result > 0 ) child = child->left;
		else if ( result < 0 ) child = child->right;
	}

	if ( parent == NULL )
	{
		mBstRoot = perSocketContext;
	}
	else if ( CompareCID( parent, perSocketContext ) > 0 )
	{
		SetLeft( parent, perSocketContext );
	}
	else // if ( CompareCID( parent, perSocketContext ) < 0 )
	{
		SetRight( parent, perSocketContext );
	}
	return true;
}

// DetachBst Method
bool cSocketContextPool::DetachBst(PerSocketContext* perSocketContext)
{
	PerSocketContext* parent = perSocketContext->parent;
	PerSocketContext* left   = perSocketContext->left;
	PerSocketContext* right  = perSocketContext->right;
	PerSocketContext* child  = NULL;

	if ( left == NULL )
	{
		child = right;
	}
	else if ( right == NULL )
	{
		child = left;
	}
	else
	{
		child = right;
		while ( child->left != NULL ) child = child->left;

		if ( child->parent != NULL && child->parent != perSocketContext )
		{
			SetLeft( child->parent, child->right );
			SetRight( child, right );
		}
		child->parent = parent;
		SetLeft( child, left );
	}

	if ( mBstRoot == perSocketContext )
	{
		mBstRoot = child;
		if ( mBstRoot != NULL ) mBstRoot->parent = NULL;
	}
	else if ( perSocketContext == parent->left )
	{
		SetLeft( parent, child );
	}
	else
	{
		SetRight( parent, child );
	}

	perSocketContext->parent = NULL;
	perSocketContext->left   = NULL;
	perSocketContext->right  = NULL;

	return true;
}

// GetPool Method
PerSocketContext* cSocketContextPool::GetPool( )
{
	PerSocketContext* perSocketContext = mNonPagedPoolUsage;

	if ( perSocketContext != NULL )
	{
		DetachPool( &mNonPagedPoolUsage, perSocketContext );
		mQuotaNonPagedPoolUsage--;
	}
	else
	{
		perSocketContext = AllocSocketContext( );
	}

	if ( perSocketContext != NULL )
	{
		AttachPool( &mPagedPoolUsage, perSocketContext );
		mQuotaPagedPoolUsage++;
	}

	return perSocketContext;
}

// ReleasePool Method
void cSocketContextPool::ReleasePool(PerSocketContext* perSocketContext, bool isDelete)
{
	DetachPool( &mPagedPoolUsage, perSocketContext );
	mQuotaPagedPoolUsage--;

	if ( isDelete != true )
	{
		AttachPool( &mNonPagedPoolUsage, perSocketContext );
		mQuotaNonPagedPoolUsage++;

	}
	else
	{
		printf("cSocketContextPool::ReleasePerSocketContext.............error\n");
		FreeSocketContext( &perSocketContext );
	}
}

// GetCID Method
PerSocketContext* cSocketContextPool::GetCID(DWORD cid)
{
	cCSLock           lock( mCs );
	PerSocketContext* perSocketContext = mBstRoot;
	int               result;

	while ( perSocketContext != NULL )
	{
		result = CompareCID( perSocketContext->cid, cid );

		if ( result == 0 )
		{
			return (perSocketContext->socket != INVALID_SOCKET ? perSocketContext : NULL);
		}
		else if ( result > 0 )
		{
			perSocketContext = perSocketContext->left;
		}
		else if ( result < 0 )
		{
			perSocketContext = perSocketContext->right;
		}
	}

	return NULL;
}

// SetCID Method
bool cSocketContextPool::SetCID(PerSocketContext* perSocketContext, DWORD cid)
{
	perSocketContext->cid = cid;
	perSocketContext->status.connectionID = AttachBst( perSocketContext );
	return perSocketContext->status.connectionID;
}

// GetPerSocketContext Method
// ReleasePerSocketContext Context Switching
PerSocketContext* cSocketContextPool::GetPerSocketContext(SOCKET socket, SOCKADDR_IN addr, DWORD ttl)
{
	cCSLock           lock( mCs );                                      // Critical Section
	PerSocketContext* perSocketContext = GetPool( );                    // Socket Context

	if ( perSocketContext != NULL )
	{
		perSocketContext->socket     = socket;                          
		perSocketContext->addr       = addr;                            // IP 
		perSocketContext->timeToLive = GetTickCount( ) + ttl;           // TTL.
	}

	return perSocketContext;
}

// ReleasePerSocketContext Method
// GetPerSocketContext Context Switching
void cSocketContextPool::ReleasePerSocketContext(PerSocketContext* perSocketContext, bool isDelete)
{
	cCSLock lock( mCs );                                                // Critical Section
	LINGER  linger;

	// force the subsequent closesocket to be abortative.
	//
	linger.l_onoff  = 1;
	linger.l_linger = 0;
	setsockopt( perSocketContext->socket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger) );
	closesocket( perSocketContext->socket );
	
	// BST
	if ( perSocketContext->status.connectionID )
		DetachBst( perSocketContext );

	perSocketContext->socket       = INVALID_SOCKET;                   
	perSocketContext->statusData   = 0;                                 
	ZeroMemory( &perSocketContext->addr, sizeof(SOCKADDR_IN) );        
	perSocketContext->timeToLive   = 0;                                 
	ZeroMemory( perSocketContext->buffer, perSocketContext->offset + perSocketContext->InternalHigh );
	                                                                   
	perSocketContext->offset       = 0;                                
	perSocketContext->Internal     = 0;                                
	perSocketContext->InternalHigh = 0;                                
	perSocketContext->cid          = 0;                               

	// Socket Context
	ReleasePool( perSocketContext, isDelete );
}
