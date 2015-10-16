#include "stdio.h"
#include "iocpserver.h"
#include <assert.h>
#include <exception>

// Local definitions
#pragma warning( disable: 4127 )

#define MAX_BUFFER          0x10000	// 64KByte

#define SafeCloseHandle(P)  if (P!=NULL) { CloseHandle(P); (P)=NULL; }
#define SafeDelete(P)       if (P!=NULL) { delete(P);      (P)=NULL; }

void cIocpServer::GameErrorLog( char* format, ... )
{
	LPVOID  msgBuf = NULL;
	DWORD   bufferLength;

	va_list args;

	va_start( args, format );

	bufferLength = _vscprintf( format, args ) + 1;
	msgBuf       = malloc( bufferLength );

	vsprintf( (char*)msgBuf, format, args );

	va_end( args );

	if ( msgBuf != NULL )
	{
		FILE*      stream = NULL;
		char       filename[ MAX_PATH ];
		SYSTEMTIME systemtime;
		char       buffer[ 1024 ];

		GetLocalTime( &systemtime );

		sprintf( filename,
			"GameLog_%04d_%02d_%02d.log",
			systemtime.wYear,
			systemtime.wMonth,
			systemtime.wDay );

		stream = fopen( filename, "at" );
		if ( stream != NULL )
		{
			sprintf( buffer,
				"%04d %02d %02d::%02d %02d %02d : %s\n",
				systemtime.wYear,
				systemtime.wMonth,
				systemtime.wDay,
				systemtime.wHour,
				systemtime.wMinute,
				systemtime.wSecond,
				(char*)msgBuf );

			fputs( buffer, stream );
			fclose( stream );
		}

		free( msgBuf );
	}
}

cIocpServer::cIocpServer(void)
{
	// Critical Section.
	InitializeCriticalSectionAndSpinCount( &mCs, 1024 );

	mIocp                   = NULL;
	mSocket                 = INVALID_SOCKET;
	mIocpAcceptThread       = NULL;
	mIocpWorkerThreadNumber = 0;
	mIocpBackendThread      = NULL;

	mIoContextPool          = NULL;
	mSocketContextPool      = NULL;

	mIoContextFrontBuffer   = NULL;
	mIoContextBackBuffer    = NULL;

	mRunServer              = false;
	mEndServer              = false;
}

// ~cIocpServer Destructor.
cIocpServer::~cIocpServer(void)
{
	Shutdown( );

	// Critical Section.
	DeleteCriticalSection( &mCs );
}

// Initialize Method
bool cIocpServer::Initialize(char* ipAddr, unsigned short port, unsigned short numWorkerThreads, unsigned short bufferLength)
{
	// CreateIoCompletionPort completion port 
	// NumberOfConcurrentThreads 
	//
	// NumberOfConcurrentThreads completion port
	// context switching
	// NumberOfConcurrentThreads
	mIocp = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0 );
	if ( mIocp == NULL )
		return false;

	// CreateIoCompletionPort 
	// NumberOfConcurrentThreads 
	// NumberOfConcurrentThreads
	SYSTEM_INFO si;
	DWORD       threadId;

	GetSystemInfo( &si );

	mIocpWorkerThreadNumber = min( si.dwNumberOfProcessors * numWorkerThreads, IOCP_MAX_WORKER_THREAD );
	for ( int i = 0; i < mIocpWorkerThreadNumber; i++ )
	{
		// Worker Thread.
		mIocpWorkerThread[i] = CreateThread( NULL, 0, WorkerThreadStartingPoint, (LPVOID)this, 0, &threadId );
		if ( mIocpWorkerThread[i] == NULL )
			return false;
	}

	// Socket Context Pool.
	mSocketContextPool = new cSocketContextPool( &mCs );
	if ( mSocketContextPool == NULL )
		return false;

	// I/O Context Pool.
	// Recv/Send
	//
	mIoContextPool = new cIoContextPool( bufferLength );
	if ( mIoContextPool == NULL )
		return false;

	// I/O Context Buffer
	mIoContextFrontBuffer = (IoContextBuffer*)GlobalAlloc( GPTR, sizeof(IoContextBuffer) );
	mIoContextBackBuffer  = (IoContextBuffer*)GlobalAlloc( GPTR, sizeof(IoContextBuffer) );
	if ( !mIoContextFrontBuffer && !mIoContextBackBuffer )
		return false;


	PHOSTENT    phe;
	int         zero;
	LINGER      linger;

	// listen
	mSocket = WSASocket( AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED );
	if ( mSocket == INVALID_SOCKET )
		return false;

	ZeroMemory( (void*)&mAddr, sizeof(mAddr) );
	mAddr.sin_family = AF_INET;
	mAddr.sin_port   = htons( port );
	mAddr.sin_addr.s_addr = strlen( ipAddr ) ? inet_addr( ipAddr ) : INADDR_NONE;

	if ( mAddr.sin_addr.s_addr == INADDR_NONE )
	{
		// the host name for the server is not in dot format, therefore try it just as a string

		if ( (phe = gethostbyname( "" )) != NULL )
			CopyMemory( &mAddr.sin_addr, phe->h_addr_list[0], phe->h_length );
		else
			return false;
	}

	mPort = port;

	if ( bind( mSocket, (LPSOCKADDR)&mAddr, sizeof(mAddr) ) == SOCKET_ERROR )
		return false;

	if ( listen( mSocket, SOMAXCONN ) == SOCKET_ERROR )
		return false;

	// The purpose of this algorithm is to reduce the number of very small segments sent, especially on
	// high-delay (remote) links.
	//
	// Windows Sockets applications can disable the Nagle algorithm for their connections by setting the
	// TCP_NODELAY socket option. However, this practice should be used wisely and only when needed because
	// this practice increases network traffic. Usually, applications that need to disable the Nagle algorithm
	// are applications that need to send small packets and require that the peer application receive them
	// immediately. Some network applications may not perform well if their design does not take into account
	// the effects of transmitting large numbers of small packets and the Nagle algorithm.
	//
	//char nodelay = TRUE;
	//if ( setsockopt( mSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(BOOL) ) == SOCKET_ERROR )
	//	throw 163;


	// Disable receive buffering on the socket. Setting SO_RCVBUF to 0 causes winsock to stop bufferring
	// receive and perform receives directly from our buffers, thereby reducing CPU usage.
	//
	zero = bufferLength;
	if ( setsockopt( mSocket, SOL_SOCKET, SO_RCVBUF, (char*)&zero, sizeof(zero)) == SOCKET_ERROR )
		return false;

	// Disable send buffering on the socket. Setting SO_SNDBUF to 0 causes winsock to stop bufferring
	// sends and perform sends directly from our buffers, thereby reducing CPU usage.
	zero = bufferLength;
	if ( setsockopt( mSocket, SOL_SOCKET, SO_SNDBUF, (char*)&zero, sizeof(zero) ) == SOCKET_ERROR )
		return false;

	// closesocket 
	// linger::u_short l_onoff  : option on/off
	// linger::u_short l_linger : linger time
	linger.l_onoff  = 1;
	linger.l_linger = 0;
	if ( setsockopt( mSocket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger) ) == SOCKET_ERROR )
		return false;

	mEndServer = false;
	mRunServer = true;

	// Accept Thread.
	mIocpAcceptThread = CreateThread( NULL, 0, AcceptThreadStartingPoint, (LPVOID)this, 0, &threadId );
	if ( mIocpAcceptThread == NULL )
		return false;

	// Backend Thread.
	mIocpBackendThread = CreateThread( NULL, 0, BackendThreadStartingPoint, (LPVOID)this, 0, &threadId );
	if ( mIocpBackendThread == NULL )
		return false;

	return true;
}

// Shutdown Method
void cIocpServer::Shutdown(DWORD maxWait)
{
	mEndServer = true;
	mRunServer = false;

	// Backend Thread
	if ( mIocpBackendThread != NULL )
	{
		WaitForSingleObjectEx( mIocpBackendThread, maxWait, TRUE );
		CloseHandle( mIocpBackendThread );
		mIocpBackendThread = NULL;
	}

	SOCKET sockTemp = mSocket;

	mSocket = INVALID_SOCKET;

	if ( sockTemp != INVALID_SOCKET )
	{
		closesocket( sockTemp );
	}

	// Accept Thread
	if ( mIocpAcceptThread != NULL )
	{
		WaitForSingleObjectEx( mIocpAcceptThread, maxWait, TRUE );
		CloseHandle( mIocpAcceptThread );
		mIocpAcceptThread = NULL;
	}

	// Cause worker threads to exit
	if ( mIocp != NULL )
	{
		for ( int i = 0; i < mIocpWorkerThreadNumber; i++ )
		{
			PostQueuedCompletionStatus( mIocp, 0, 0, IOCP_SHUTDOWN );
		}
	}

	// Make sure worker threads exits.
	for ( int i = 0; i < mIocpWorkerThreadNumber; i++ )
	{
		if ( WaitForSingleObject( mIocpWorkerThread[i], 60000 ) != WAIT_OBJECT_0 )
		{
			DWORD exitCode;
			GetExitCodeThread( mIocpWorkerThread[i], &exitCode);
			if ( exitCode == STILL_ACTIVE )
			{
				TerminateThread( mIocpWorkerThread[i], 0 );
			}
		}
		CloseHandle( mIocpWorkerThread[i] );
		mIocpWorkerThread[i] = NULL;
	}

	// Overlapped I/O Model Pool Socket Context Pool.
	if ( mIoContextFrontBuffer )
	{
		GlobalFree( mIoContextFrontBuffer );
		mIoContextFrontBuffer = NULL;
	}
	if ( mIoContextBackBuffer )
	{
		GlobalFree( mIoContextBackBuffer );
		mIoContextBackBuffer = NULL;
	}
	SafeDelete( mIoContextPool     );
	SafeDelete( mSocketContextPool );

	// completion port
	SafeCloseHandle( mIocp );
}

// GetPoolUsage Method
void cIocpServer::GetIoPoolUsage(SIZE_T& quotaPagedPoolUsage, SIZE_T& quotaNonePagedPoolUsage, SIZE_T& workingSetSize)
{
	if ( mIoContextPool != NULL )
	{
		mIoContextPool->GetProcessMemoryInfo( quotaPagedPoolUsage, quotaNonePagedPoolUsage, workingSetSize );
	}
}

// GetPoolUsage Method
void cIocpServer::GetSocketPoolUsage(SIZE_T& quotaPagedPoolUsage, SIZE_T& quotaNonePagedPoolUsage, SIZE_T& workingSetSize)
{
	if ( mSocketContextPool != NULL )
	{
		mSocketContextPool->GetProcessMemoryInfo( quotaPagedPoolUsage, quotaNonePagedPoolUsage, workingSetSize );
	}
}

// QueueRequest Method
BOOL cIocpServer::QueueRequest(ULONG_PTR completionKey, LPOVERLAPPED overlapped, DWORD bytesTransferred)
{
	return PostQueuedCompletionStatus( mIocp, bytesTransferred, completionKey, overlapped );
}

// AcceptComplete Method
bool cIocpServer::AcceptComplete(PerSocketContext* /*perSocketContext*/)
{
	return true;
}

// AcceptThread Method
DWORD cIocpServer::AcceptThread( )
{
	while ( true )
	{
		PerSocketContext* perSocketContext = NULL;
		PerIoContext*     perIoContext     = NULL;
		SOCKET            socket;
		SOCKADDR_IN       addr;
		int               addrlen;

		addrlen = sizeof( addr );
		socket  = WSAAccept( mSocket, (sockaddr*)&addr, &addrlen, NULL, 0 );
		if ( socket == SOCKET_ERROR ) 
			break;

		if ( mEndServer == true )
			break;

		perSocketContext = mSocketContextPool->GetPerSocketContext( socket, addr, MAX_TTL );
		if ( perSocketContext == NULL )
			break;

		// Accept completion port.
		if ( CreateIoCompletionPort( (HANDLE)socket, mIocp, (ULONG_PTR)perSocketContext, 0 ) == NULL )
			break;

		if ( AcceptComplete( perSocketContext ) == false )
		{
			// SocketContext .
			mSocketContextPool->ReleasePerSocketContext( perSocketContext );
			continue;
		}

		// I/O Context.
		perIoContext = mIoContextPool->GetIoContext( perSocketContext->socket, IOCP_REQUEST_READ );
		if ( RecvPost( perIoContext ) == false )
		{
			GameErrorLog("关闭套接字,%d,%s,%s\n",__LINE__,__FILE__,__FUNCTION__);
			Close( perSocketContext );			
			continue;
		}
	}
	return 0;
}

// SendExec Method
bool cIocpServer::SendExec(PerIoContext* perIoContext)
{
	WSABUF wsaBuf;
	DWORD  sendBytes = 0;
	DWORD  flags     = 0;
	int    retcode;

	wsaBuf.len = perIoContext->offset;
	wsaBuf.buf = perIoContext->buffer;

	retcode = WSASend( perIoContext->socket,
					   &wsaBuf,
					   1,
					   &sendBytes,
					   flags,
					   &(perIoContext->wsaOverlapped),
					   NULL );

	if ( (retcode == SOCKET_ERROR) && (WSAGetLastError() != WSA_IO_PENDING) )
	{
		mIoContextPool->ReleaseIoContext( perIoContext );
		return false;
	}
	return true;
}

// SendPost Method
bool cIocpServer::SendPost(PerIoContext* perIoContext)
{
	/*add by oiooooio, 2014-4-5 14:20:28*/
	return SendExec(perIoContext);

	/*
	以下全被注释
	oiooooio, 2014-4-5 14:20:01
	*/
	//cCSLock        lock( &mCs );
	//PerIoContext** buffer = mIoContextBackBuffer->buffer;
	//long&          offset = mIoContextBackBuffer->offset;

	//if ( offset < MAX_IO_CONTEXT_BUFFER_LEN )
	//{
	//	buffer[ offset ] = perIoContext;
	//	offset++;
	//	return true;
	//}

	//////以下if判断 为服务器找错设置 2013.07.22 
	////NETWORK2->PostServerEvent( "WARNING:超过16k错误 - cIoContextPool:%d / %d", root->Category, root->Protocol );  if ( perIoContext->offset + perIoContext->InternalHigh > perIoContext->length )
	////{
	////	MSGROOT *root = (MSGROOT*)perIoContext->buffer;
	////	
	////}

	//mIoContextPool->ReleaseIoContext( perIoContext );
	//return false;
}

// RecvPost Method
bool cIocpServer::RecvPost(PerIoContext* perIoContext)
{
	WSABUF wsaBuf;
	DWORD  recvBytes = 0;
	DWORD  flags     = 0;
	int    retcode;

	wsaBuf.len = (perIoContext->length - perIoContext->offset);
	wsaBuf.buf = (perIoContext->buffer + perIoContext->offset);

	retcode = WSARecv( perIoContext->socket,
					   &wsaBuf,
					   1,
					   &recvBytes,
					   &flags,
					   &(perIoContext->wsaOverlapped),
					   NULL );

	if ( retcode == SOCKET_ERROR )
	{
		retcode = WSAGetLastError();

		if(WSA_IO_PENDING == retcode)
		{
		}
		else if(WSAECONNRESET == retcode){
		}
		else{
			mIoContextPool->ReleaseIoContext( perIoContext );
			return false;
		}
	}
	return true;
}

// Close Method - CompletionKey Overlapped CompletionKey.
void cIocpServer::Close(PerSocketContext* perSocketContext, PerIoContext* perIoContext)
{
	cCSLock lock( &mCs );

	if ( perSocketContext == NULL || perIoContext == NULL )
	{
		return;
	}
	// Socket Context.
	if ( perSocketContext->socket == perIoContext->socket )
	{
		if ( !perSocketContext->status.connectionDead )
		{
			shutdown( perSocketContext->socket, SD_BOTH );
			perSocketContext->status.connectionDead = 1;
		}
	}

	// I/O Context.
	mIoContextPool->ReleaseIoContext( perIoContext );
}

void cIocpServer::Close(PerSocketContext* perSocketContext)
{
	cCSLock lock( &mCs );

	if ( !perSocketContext->status.connectionDead )
	{
		shutdown( perSocketContext->socket, SD_BOTH );
		perSocketContext->status.connectionDead = 1;
	}
}

// BackendThread Method
DWORD cIocpServer::BackendThread( )
{
	DWORD beginTick;
	DWORD endTick;
	DWORD tickDiff;

	while ( true )
	{
		beginTick = GetTickCount( );

		if ( mEndServer == true )
			break;

		if ( mSocketContextPool != NULL )
		{
			cCSLock           lock( &mCs );
			PerSocketContext* socketContext = mSocketContextPool->GetPagedPoolUsage( );
			PerSocketContext* next          = NULL;

			while ( socketContext != NULL )
			{
				next = socketContext->next;

				if ( socketContext->status.connectionDead )
				{
					// Close Socket On.
					socketContext->status.closeSocket = 1;
				}
				else
				{
					// TTL
					if ( beginTick > socketContext->timeToLive )
					{
						PerIoContext* cbIoContext = mIoContextPool->GetIoContext( socketContext->socket, IOCP_REQUEST_CALLBACK );
						PostQueuedCompletionStatus( mIocp, 0, (ULONG_PTR)socketContext, (LPOVERLAPPED)cbIoContext );
					}
				}

				if ( socketContext->status.closeSocket )
				{
					// SocketContext.
					mSocketContextPool->ReleasePerSocketContext( socketContext );
				}

				socketContext = next;
			} // while ( socketContext != NULL )
		}

		// I/O Context.
		IoContextPresent( );

		endTick  = GetTickCount( );
		tickDiff = endTick - beginTick;
		Sleep( 10 );
	}
	return 0;
}

// SendComplete Method
bool cIocpServer::SendComplete(PerSocketContext* /*perSocketContext*/,
							   PerIoContext*     perIoContext,
							   DWORD             /*bytesTransferred*/)
{
	// I/O Context.
	mIoContextPool->ReleaseIoContext( perIoContext );
	return true;
}

// RecvComplete Method (Default Echo Server)
bool cIocpServer::RecvComplete(PerSocketContext* perSocketContext,
							   PerIoContext*     perIoContext,
							   DWORD             bytesTransferred)
{
	perIoContext->offset      = bytesTransferred;
	perIoContext->requestType = IOCP_REQUEST_WRITE;

	if ( SendPost( perIoContext ) == false )
	{
		GameErrorLog("关闭套接字,%d,%s,%s\n",__LINE__,__FILE__,__FUNCTION__);
		Close( perSocketContext );
		return false;
	}

	perIoContext = mIoContextPool->GetIoContext( perSocketContext->socket, IOCP_REQUEST_READ );
	if ( RecvPost( perIoContext ) == false )
	{
		//TEST
		//printf("%d,%s\n",__LINE__,__FILE__);
		//NETWORK2->PostServerEvent("%d,%s",__LINE__,__FILE__);
		GameErrorLog("关闭套接字,%d,%s,%s\n",__LINE__,__FILE__,__FUNCTION__);
		Close( perSocketContext );
		return false;
	}
	return true;
}

// CallbackComplete Method
bool cIocpServer::CallbackComplete(PerSocketContext* /*perSocketContext*/,
								   PerIoContext*     perIoContext,
								   DWORD             /*bytesTransferred*/)
{
	mIoContextPool->ReleaseIoContext( perIoContext );
	return true;
}

/*-- IoContextPresent Method
*/
void cIocpServer::IoContextPresent( )
{
	IoContextBuffer* temp;

	// Double Buffering .
	EnterCriticalSection( &mCs );

		temp                  = mIoContextBackBuffer;
		mIoContextBackBuffer  = mIoContextFrontBuffer;
		mIoContextFrontBuffer = temp;

	LeaveCriticalSection( &mCs );

	PerIoContext** buffer = mIoContextFrontBuffer->buffer;
	long&          offset = mIoContextFrontBuffer->offset;

	while ( offset > 0 )
	{
		SendExec( (*buffer) );
		(*buffer) = NULL;

		buffer++;
		offset--;
	}
}

// WorkerThread Method
DWORD cIocpServer::WorkerThread( )
{
	DWORD             bytesTransferred;
	ULONG_PTR         completionKey;
	OVERLAPPED*       overlapped;
	BOOL              retValue;

	PerSocketContext* perSocketContext;
	PerIoContext*     perIoContext;

	while ( true )
	{
		retValue = GetQueuedCompletionStatus( mIocp, &bytesTransferred, &completionKey, &overlapped, INFINITE );

		if ( overlapped == IOCP_SHUTDOWN )
			return 0;

		perSocketContext = (PerSocketContext*)completionKey;
		perIoContext     = (PerIoContext*)overlapped;

		if ( retValue == FALSE )
		{	
#if defined(_DEBUG)/* && defined(SHOW_INFO)*/
			int tmpErrorCode = WSAGetLastError();

			printf("[%s:%d,errorcode:%d] has been error, the client will be closed.\n", __FUNCTION__, __LINE__, tmpErrorCode);
#endif

			perIoContext->offset = max( perIoContext->offset, bytesTransferred );
			Close( perSocketContext, perIoContext );
			continue;

		}

		if ( bytesTransferred == 0  )
		{
#if defined(_DEBUG)/* && defined(SHOW_INFO)*/
			printf("[%s] user has been closed socket, recv size is 0.\n", __FUNCTION__);
#endif
			Close( perSocketContext, perIoContext );
			continue;
		}

		switch ( perIoContext->requestType )
		{
		case IOCP_REQUEST_READ:
			RecvComplete( perSocketContext, perIoContext, bytesTransferred );
			break; 

		case IOCP_REQUEST_WRITE:
			SendComplete( perSocketContext, perIoContext, bytesTransferred );
			break; 

		case IOCP_REQUEST_CALLBACK:
			assert(false);
			throw new std::exception("Please go back!!!");
			break;

		default:
			{
#if defined(_DEBUG)/* && defined(SHOW_INFO)*/
				printf("[%s:%d] the receivced message error.\n", __FUNCTION__, __LINE__);
#endif
				assert(false);
				Close(perSocketContext, perIoContext);
				break;
			}
		}
	}
	return 0;
}

// AcceptThreadStartingPoint Method ( Accept Thread )
DWORD cIocpServer::AcceptThreadStartingPoint(void* ptr)
{
	cIocpServer* iocpServer = (cIocpServer*)ptr;
	return iocpServer->AcceptThread( );
}

// WorkerThreadStartingPoint Method ( Worker Thread )
DWORD cIocpServer::WorkerThreadStartingPoint(void* ptr)
{
	cIocpServer* iocpServer = (cIocpServer*)ptr;
	return iocpServer->WorkerThread( );
}

// BackendThreadStartingPoint Method ( Backend Thread )
DWORD cIocpServer::BackendThreadStartingPoint(void* ptr)
{
	cIocpServer* iocpServer = (cIocpServer*)ptr;
	return iocpServer->BackendThread( );
}