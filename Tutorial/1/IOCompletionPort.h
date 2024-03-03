#pragma once
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <iostream>

#include <thread>
#include <vector>

#define MAX_SOCKBUF 1024	//패킷 크기
#define MAX_WORKERTHREAD 4  //쓰레드 풀에 넣을 쓰레드 수

using namespace std;

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO);

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
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[MAX_SOCKBUF];
	RWMode rwmode;
} PER_IO_DATA, *LPPER_IO_DATA;

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

} PER_CLNT_DATA, *LPPER_CLNT_DATA;

class IOCompletionPort
{
private:
	SOCKET hServSock = INVALID_SOCKET;	// 리슨 소켓
	vector<PER_CLNT_DATA> clntInfos;	// 클라이언트 정보 저장 구조체
	int clntCnt = 0;					// 연결된 클라이언트 수
	HANDLE IOCPHandle = INVALID_HANDLE_VALUE;	// IOCP 핸들
	vector<thread> IOWorkerThreads;		// IO Worker 쓰레드

	void WorkerThread(void);
public:
	IOCompletionPort() {}
	~IOCompletionPort(void)
	{
		// 모든 IO Worker 쓰레드 종료 대기
		for (auto& thread : IOWorkerThreads)
		{
			if (thread.joinable())
			{
				thread.join();
				cout << "Worker Thread 종료" << endl;
			}
		}
		WSACleanup();
	}
	// 소켓 초기화 함수
	bool InitSocket()
	{
		WSADATA wsaData;
		HANDLE hComPort;
		SYSTEM_INFO sysInfo;
		SOCKADDR_IN servAdr;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			cerr << "WSAStartup() error" << endl;
			return false;
		}


		hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		GetSystemInfo(&sysInfo);

		for (int i = 0; i < sysInfo.dwNumberOfProcessors; i++)
			_beginthreadex(NULL, 0, (_beginthreadex_proc_type)EchoThreadMain, (LPVOID)hComPort, 0, NULL);

		hServSock = WSASocket(PF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (hServSock == INVALID_SOCKET)
		{
			cerr << "WSASocket() Error" << endl;
			return false;
		}

		cout << "소켓 초기화 완료" << endl;
		return true;
	}

	// 소켓 등록 및 리슨 함수
	bool BindAndListen(const int nBindPort) const
	{
		SOCKADDR_IN servAdr;
		servAdr.sin_family = AF_INET;
		servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
		servAdr.sin_port = htons(nBindPort);

		if (bind(hServSock, (SOCKADDR*)&servAdr, sizeof(SOCKADDR_IN)) != 0)
		{
			cerr << "bind() error" << endl;
			return false;
		}

		if (listen(hServSock, 5) != 0)
		{
			cerr << "listen() error" << endl;
			return false;
		}

		cout << "서버 등록 성공" << endl;
		return true;
	}

	// 접속 요청 수락 및 메세지 처리 함수
	bool StartServer(const int maxClntCnt)
	{
		// 클라이언트 생성
		for (int i = 0; i < maxClntCnt; ++i)
		{
			clntInfos.emplace_back();
		}

		// IOCP 등록
		IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, MAX_WORKERTHREAD);
		if (IOCPHandle == NULL)
		{
			cerr << "CreateIoCompletionPort() error" << endl;
			return false;
		}

		// Worker Thread 생성
		for (int i = 0; i < MAX_WORKERTHREAD; i++)
		{
			// 새로운 쓰레드 생성하여 WorkerThread() 실행.
			// emplace_back은 push_back과 달리 객체(새로운 쓰레드)를 내부에서 직접 생성
			IOWorkerThreads.emplace_back([this]() -> void { WorkerThread(); });
		}
	}
};


void IOCompletionPort::WorkerThread()
{
	cout << "worker thread 생성" << endl;
}

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO)
{
	cout << "This is EchoThreadMain()" << endl;
	return 0;
}