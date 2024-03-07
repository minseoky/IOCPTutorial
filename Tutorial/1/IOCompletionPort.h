#pragma once
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <iostream>

#include <thread>
#include <vector>

#define MAX_SOCKBUF 1024	//패킷 크기

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
	WSAOVERLAPPED overlapped;
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
	thread accepterThread;
	bool isWorkerRun = true;
	bool isAccepterRun = true;

	void WorkerThread(void);
	void AccepterThread(void);
	LPPER_CLNT_DATA GetEmptyClientInfo(void);
	void CloseSocket(LPPER_CLNT_DATA clntInfo, bool isForce = false);
	bool SendMsg(LPPER_CLNT_DATA clntInfo, char* pMsg, int nLen);
	bool BindRecv(LPPER_CLNT_DATA clntInfo);
	bool BindIOCompletionPort(LPPER_CLNT_DATA clntInfo);
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
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			cerr << "WSAStartup() error" << endl;
			return false;
		}

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
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);

		DWORD nCore = sysInfo.dwNumberOfProcessors;
		// IOCP 등록
		IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, nCore);
		if (IOCPHandle == NULL)
		{
			cerr << "CreateIoCompletionPort() error" << endl;
			return false;
		}

		// Worker Thread 생성
		for (int i = 0; i < nCore; i++)
		{
			// 새로운 쓰레드 생성하여 WorkerThread() 실행.
			// emplace_back은 push_back과 달리 객체(새로운 쓰레드)를 내부에서 직접 생성
			IOWorkerThreads.emplace_back([this]() -> void { 
				cout << "쓰레드 생성" << endl;
				WorkerThread(); 
				});
		}
		
		// Accepter Thread 생성
		accepterThread = std::thread([this]() { AccepterThread(); });
		cout << "Accepter Thread 시작" << endl;

		cout << "서버 시작" << endl;
		return true;
	}

	void DestroyThread(void);
};

// Overlapped IO 작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
void IOCompletionPort::WorkerThread()
{
	//CompletionKey를 받을 포인터 변수
	LPPER_CLNT_DATA clntInfo = NULL;
	//함수 호출 성공 여부
	BOOL success = TRUE;
	//Overlapped IO작업에서 전송된 데이터 크기
	DWORD dwIoSize = 0;
	//IO 작업을 위해 요청한 Overlapped 구조체를 받을 포인터
	LPOVERLAPPED lpOverlapped = NULL;

	while (isWorkerRun)
	{
		/*
		 * 이 함수로 인해 쓰레드들은 WaitingThread Queue 에 대기 상태로 들어가게 된다.
		 * 완료된 Overlapped IO작업이 발생하면 IOCP Queue에서 완료된 작업을 가져와 뒤 처리를 한다.
		 * 그리고 PostQueuedCompletionStatus()함수에 의해 사용자 메세지가 도착되면 쓰레드를 종료한다.
		*/
		success = GetQueuedCompletionStatus(IOCPHandle,
			&dwIoSize,									// 실제로 전송된 바이트
			(PULONG_PTR)&clntInfo,						// CompletionKey
			&lpOverlapped,								// overlapped 객체
			INFINITE);									// 무한 대기

		//사용자 쓰레드 종료 메세지 처리
		if (success == TRUE && dwIoSize == 0 && lpOverlapped == NULL)
		{
			isWorkerRun = false;
			continue;
		}

		if (lpOverlapped == NULL)
		{
			continue;
		}

		//client가 접속을 끊었을 때
		if (success == FALSE || (dwIoSize == 0 && success == TRUE))
		{
			cout << "socket(" << (int)clntInfo->clntSockInfo.hClntSock << ") 접속 끊김" << endl;
			CloseSocket(clntInfo);
			continue;
		}

		LPPER_IO_DATA ioData = (LPPER_IO_DATA)lpOverlapped;

		//OverlappedIO Recv작업일 경우 처리
		if (ioData->rwmode == RWMode::RECV)
		{
			ioData->buffer[dwIoSize] = NULL;
			cout << "[수신] bytes : " << dwIoSize << ", msg : " << ioData->buffer << endl;

			// 클라이언트에 메세지를 에코한다.
			SendMsg(clntInfo, ioData->buffer, dwIoSize);
			BindRecv(clntInfo);
		}
		//Overlapped IO Send작업 뒤 처리
		else if (ioData->rwmode == RWMode::SEND)
		{
			cout << "[송신] bytes : " << dwIoSize << ", msg : " << ioData->buffer << endl;
		}
		//예외
		else
		{
			cout << "socket(" << (int)clntInfo->clntSockInfo.hClntSock << ")에서 예외상황" << endl;
		}
	}

}

void IOCompletionPort::AccepterThread(void)
{
	SOCKADDR_IN clntAdr;
	int clntAdrSz = sizeof(clntAdr);
	
	while (isAccepterRun)
	{
		//접속을 받을 구조체의 인덱스를 얻어온다.
		LPPER_CLNT_DATA clntInfo = GetEmptyClientInfo();
		if (clntInfo == NULL)
		{
			cout << "[에러] Client Full\n" << endl;
			return;
		}

		//클라이언트 접속 요청이 들어올 때까지 대기
		clntInfo->clntSockInfo.hClntSock = accept(hServSock,
			(SOCKADDR*)&clntAdr,
			&clntAdrSz);
		if (clntInfo->clntSockInfo.hClntSock == INVALID_SOCKET)
			continue;

		//IO Completion Port객체와 소켓을 연결시킨다.
		bool bRet = BindIOCompletionPort(clntInfo);
		if (false == bRet)
			return;

		//Recv Overlapped IO작업을 요청해 놓는다.
		bRet = BindRecv(clntInfo);
		if (bRet == false)
			return;

		char clientIP[32] = { 0, };
		inet_ntop(AF_INET, &(clntAdr.sin_addr), clientIP, 32 - 1);
		cout << "클라이언트 접속 : IP(" << clientIP << ") SOCKET(" << (int)clntInfo->clntSockInfo.hClntSock << ")" << endl;

		//클라이언트 갯수 증가
		++clntCnt;
	}
}

//생성된 쓰레드를 파괴한다.
void IOCompletionPort::DestroyThread(void)
{
	isWorkerRun = false;
	CloseHandle(IOCPHandle);

	for (auto& th : IOWorkerThreads)
	{
		if (th.joinable())
			th.join();
	}

	// Accepter 쓰레드 종료
	isAccepterRun = false;
	closesocket(hServSock);

	if (accepterThread.joinable())
		accepterThread.join();
}

LPPER_CLNT_DATA IOCompletionPort::GetEmptyClientInfo(void)
{
	for (auto& client : clntInfos)
	{
		if (client.clntSockInfo.hClntSock == INVALID_SOCKET)
			return &client;
	}
	return nullptr;
}

bool IOCompletionPort::BindIOCompletionPort(LPPER_CLNT_DATA clntInfo)
{
	//socket과 clntInfo를 CompletionPort객체와 연결시킨다.
	auto hIOCP = CreateIoCompletionPort((HANDLE)clntInfo->clntSockInfo.hClntSock,
		IOCPHandle,
		(ULONG_PTR)(clntInfo), 0);

	if (hIOCP == NULL || hIOCP != IOCPHandle)
	{
		cout << "[에러] CreateIoCompletionPort()함수 실패" << endl;
		return false;
	}

	return true;
}

void IOCompletionPort::CloseSocket(LPPER_CLNT_DATA clntInfo, bool isForce)
{
	struct linger linger = { 0, 0 };	// SO_DONTLINGER 로 설정

	//isForce가 true이면 SO_LINGER, timeout = 0으로 설정하여 강제 종료 시킨다. 주의 : 데이터 손실이 있을 수 있음
	if (isForce == true)
	{
		linger.l_onoff = 1;
	}

	//socketClose소켓의 데이터 송수신을 모두 중단 시킨다.
	shutdown(clntInfo->clntSockInfo.hClntSock, SD_BOTH);

	//소켓 옵션을 설정한다 : closesocket 함수의 리턴 지연 시간 제어
	setsockopt(clntInfo->clntSockInfo.hClntSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

	//소켓 연결을 종료 시킨다.
	closesocket(clntInfo->clntSockInfo.hClntSock);

	clntInfo->clntSockInfo.hClntSock = INVALID_SOCKET;
}

//WSASend Overlapped IO작업
bool IOCompletionPort::SendMsg(LPPER_CLNT_DATA clntInfo, char* pMsg, int nLen)
{
	DWORD dwRecvNumBytes = 0;

	//전송될 메세지를 복사
	CopyMemory(clntInfo->sendOverlappedIO.buffer, pMsg, nLen);

	//Overlapped IO를 위해 각 정보를 셋팅해 준다.
	clntInfo->sendOverlappedIO.wsaBuf.len = nLen;
	clntInfo->sendOverlappedIO.wsaBuf.buf = clntInfo->sendOverlappedIO.buffer;
	clntInfo->sendOverlappedIO.rwmode = RWMode::SEND;

	int nRet = WSASend(clntInfo->clntSockInfo.hClntSock,
		&(clntInfo->sendOverlappedIO.wsaBuf),
		1,
		&dwRecvNumBytes,
		0,
		(LPWSAOVERLAPPED)&clntInfo->sendOverlappedIO,
		NULL);

	//socket_error이면 client socket이 끊어진걸로 처리한다.
	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	{
		cout << "[에러] WSASend()함수 실패" << endl;
		return false;
	}
	return true;
}

bool IOCompletionPort::BindRecv(LPPER_CLNT_DATA clntInfo)
{
	DWORD dwFlag = 0;
	DWORD dwRecvNumBytes = 0;
	//Overlapped IO을 위해 각 정보 세팅
	clntInfo->recvOverlappedIO.wsaBuf.len = MAX_SOCKBUF;
	clntInfo->recvOverlappedIO.wsaBuf.buf = clntInfo->recvOverlappedIO.buffer;
	clntInfo->recvOverlappedIO.rwmode = RWMode::RECV;

	int nRet = WSARecv(clntInfo->clntSockInfo.hClntSock,
		&(clntInfo->recvOverlappedIO.wsaBuf),
		1,
		&dwRecvNumBytes,
		&dwFlag,
		(LPWSAOVERLAPPED)&clntInfo->recvOverlappedIO,
		NULL);

	//socket_error면 client socket이 끊어진걸로 처리한다.
	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	{
		cout << "[에러] WSARecv()함수 실패" << endl;
		return false;
	}
	return true;
}

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO)
{
	cout << "This is EchoThreadMain()" << endl;
	return 0;
}