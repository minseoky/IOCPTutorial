#pragma once

#include <WinSock2.h>

#define MAX_SOCKBUF 1024	//패킷 크기

enum RWMode
{
	RECV,
	SEND
};

typedef struct // socket info
{
	SOCKET hClntSock;
	SOCKADDR_IN clntAdr;
} PER_HANDLE_DATA, * LPPER_HANDLE_DATA;

typedef struct // buffer info
{
	WSAOVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[MAX_SOCKBUF];
	RWMode rwmode;
} PER_IO_DATA, * LPPER_IO_DATA;

typedef struct S// client info
{
	PER_HANDLE_DATA clntSockInfo;
	PER_IO_DATA recvOverlappedIO;
	PER_IO_DATA sendOverlappedIO;

	S()
	{
		memset(&clntSockInfo, 0, sizeof(clntSockInfo));
		memset(&recvOverlappedIO, 0, sizeof(recvOverlappedIO));
		memset(&sendOverlappedIO, 0, sizeof(sendOverlappedIO));
		clntSockInfo.hClntSock = INVALID_SOCKET;
	}

} PER_CLNT_DATA, * LPPER_CLNT_DATA;