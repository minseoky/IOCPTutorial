#pragma once
#pragma comment(lib, "ws2_32")

#include <Ws2tcpip.h>
#include <iostream>
#include <thread>
#include <vector>

#include "Define.h"


using namespace std;

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO);



class IOCompletionPort
{
private:
	SOCKET hServSock = INVALID_SOCKET;	// ���� ����
	vector<PER_CLNT_DATA> clntInfos;	// Ŭ���̾�Ʈ ���� ���� ����ü
	int clntCnt = 0;					// ����� Ŭ���̾�Ʈ ��
	HANDLE IOCPHandle = INVALID_HANDLE_VALUE;	// IOCP �ڵ�
	vector<thread> IOWorkerThreads;		// IO Worker ������
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
		// ��� IO Worker ������ ���� ���
		for (auto& thread : IOWorkerThreads)
		{
			if (thread.joinable())
			{
				thread.join();
				cout << "Worker Thread ����" << endl;
			}
		}
		WSACleanup();
	}
	// ���� �ʱ�ȭ �Լ�
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

		cout << "���� �ʱ�ȭ �Ϸ�" << endl;
		return true;
	}

	// ���� ��� �� ���� �Լ�
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

		cout << "���� ��� ����" << endl;
		return true;
	}

	// ���� ��û ���� �� �޼��� ó�� �Լ�
	bool StartServer(const int maxClntCnt)
	{
		// Ŭ���̾�Ʈ ����
		for (int i = 0; i < maxClntCnt; ++i)
		{
			clntInfos.emplace_back();
		}
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);

		DWORD nCore = sysInfo.dwNumberOfProcessors;
		// IOCP ���
		IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, nCore);
		if (IOCPHandle == NULL)
		{
			cerr << "CreateIoCompletionPort() error" << endl;
			return false;
		}

		// Worker Thread ����
		for (int i = 0; i < nCore; i++)
		{
			// ���ο� ������ �����Ͽ� WorkerThread() ����.
			// emplace_back�� push_back�� �޸� ��ü(���ο� ������)�� ���ο��� ���� ����
			IOWorkerThreads.emplace_back([this]() -> void {
				cout << "������ ����" << endl;
				WorkerThread();
				});
		}

		// Accepter Thread ����
		accepterThread = std::thread([this]() { AccepterThread(); });
		cout << "Accepter Thread ����" << endl;

		cout << "���� ����" << endl;
		return true;
	}

	void DestroyThread(void);
};

// Overlapped IO �۾��� ���� �Ϸ� �뺸�� �޾� �׿� �ش��ϴ� ó���� �ϴ� �Լ�
void IOCompletionPort::WorkerThread()
{
	//CompletionKey�� ���� ������ ����
	LPPER_CLNT_DATA clntInfo = NULL;
	//�Լ� ȣ�� ���� ����
	BOOL success = TRUE;
	//Overlapped IO�۾����� ���۵� ������ ũ��
	DWORD dwIoSize = 0;
	//IO �۾��� ���� ��û�� Overlapped ����ü�� ���� ������
	LPOVERLAPPED lpOverlapped = NULL;

	while (isWorkerRun)
	{
		/*
		 * �� �Լ��� ���� ��������� WaitingThread Queue �� ��� ���·� ���� �ȴ�.
		 * �Ϸ�� Overlapped IO�۾��� �߻��ϸ� IOCP Queue���� �Ϸ�� �۾��� ������ �� ó���� �Ѵ�.
		 * �׸��� PostQueuedCompletionStatus()�Լ��� ���� ����� �޼����� �����Ǹ� �����带 �����Ѵ�.
		*/
		success = GetQueuedCompletionStatus(IOCPHandle,
			&dwIoSize,									// ������ ���۵� ����Ʈ
			(PULONG_PTR)&clntInfo,						// CompletionKey
			&lpOverlapped,								// overlapped ��ü
			INFINITE);									// ���� ���

		//����� ������ ���� �޼��� ó��
		if (success == TRUE && dwIoSize == 0 && lpOverlapped == NULL)
		{
			isWorkerRun = false;
			continue;
		}

		if (lpOverlapped == NULL)
		{
			continue;
		}

		//client�� ������ ������ ��
		if (success == FALSE || (dwIoSize == 0 && success == TRUE))
		{
			cout << "socket(" << (int)clntInfo->clntSockInfo.hClntSock << ") ���� ����" << endl;
			CloseSocket(clntInfo);
			continue;
		}

		LPPER_IO_DATA ioData = (LPPER_IO_DATA)lpOverlapped;

		//OverlappedIO Recv�۾��� ��� ó��
		if (ioData->rwmode == RWMode::RECV)
		{
			ioData->buffer[dwIoSize] = NULL;
			cout << "[����] bytes : " << dwIoSize << ", msg : " << ioData->buffer << endl;

			// Ŭ���̾�Ʈ�� �޼����� �����Ѵ�.
			SendMsg(clntInfo, ioData->buffer, dwIoSize);
			BindRecv(clntInfo);
		}
		//Overlapped IO Send�۾� �� ó��
		else if (ioData->rwmode == RWMode::SEND)
		{
			cout << "[�۽�] bytes : " << dwIoSize << ", msg : " << ioData->buffer << endl;
		}
		//����
		else
		{
			cout << "socket(" << (int)clntInfo->clntSockInfo.hClntSock << ")���� ���ܻ�Ȳ" << endl;
		}
	}

}

void IOCompletionPort::AccepterThread(void)
{
	SOCKADDR_IN clntAdr;
	int clntAdrSz = sizeof(clntAdr);

	while (isAccepterRun)
	{
		//������ ���� ����ü�� �ε����� ���´�.
		LPPER_CLNT_DATA clntInfo = GetEmptyClientInfo();
		if (clntInfo == NULL)
		{
			cout << "[����] Client Full\n" << endl;
			return;
		}

		//Ŭ���̾�Ʈ ���� ��û�� ���� ������ ���
		clntInfo->clntSockInfo.hClntSock = accept(hServSock,
			(SOCKADDR*)&clntAdr,
			&clntAdrSz);
		if (clntInfo->clntSockInfo.hClntSock == INVALID_SOCKET)
			continue;

		//IO Completion Port��ü�� ������ �����Ų��.
		bool bRet = BindIOCompletionPort(clntInfo);
		if (false == bRet)
			return;

		//Recv Overlapped IO�۾��� ��û�� ���´�.
		bRet = BindRecv(clntInfo);
		if (bRet == false)
			return;

		char clientIP[32] = { 0, };
		inet_ntop(AF_INET, &(clntAdr.sin_addr), clientIP, 32 - 1);
		cout << "Ŭ���̾�Ʈ ���� : IP(" << clientIP << ") SOCKET(" << (int)clntInfo->clntSockInfo.hClntSock << ")" << endl;

		//Ŭ���̾�Ʈ ���� ����
		++clntCnt;
	}
}

//������ �����带 �ı��Ѵ�.
void IOCompletionPort::DestroyThread(void)
{
	isWorkerRun = false;
	CloseHandle(IOCPHandle);

	for (auto& th : IOWorkerThreads)
	{
		if (th.joinable())
			th.join();
	}

	// Accepter ������ ����
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
	//socket�� clntInfo�� CompletionPort��ü�� �����Ų��.
	auto hIOCP = CreateIoCompletionPort((HANDLE)clntInfo->clntSockInfo.hClntSock,
		IOCPHandle,
		(ULONG_PTR)(clntInfo), 0);

	if (hIOCP == NULL || hIOCP != IOCPHandle)
	{
		cout << "[����] CreateIoCompletionPort()�Լ� ����" << endl;
		return false;
	}

	return true;
}

void IOCompletionPort::CloseSocket(LPPER_CLNT_DATA clntInfo, bool isForce)
{
	struct linger linger = { 0, 0 };	// SO_DONTLINGER �� ����

	//isForce�� true�̸� SO_LINGER, timeout = 0���� �����Ͽ� ���� ���� ��Ų��. ���� : ������ �ս��� ���� �� ����
	if (isForce == true)
	{
		linger.l_onoff = 1;
	}

	//socketClose������ ������ �ۼ����� ��� �ߴ� ��Ų��.
	shutdown(clntInfo->clntSockInfo.hClntSock, SD_BOTH);

	//���� �ɼ��� �����Ѵ� : closesocket �Լ��� ���� ���� �ð� ����
	setsockopt(clntInfo->clntSockInfo.hClntSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

	//���� ������ ���� ��Ų��.
	closesocket(clntInfo->clntSockInfo.hClntSock);

	clntInfo->clntSockInfo.hClntSock = INVALID_SOCKET;
}

//WSASend Overlapped IO�۾�
bool IOCompletionPort::SendMsg(LPPER_CLNT_DATA clntInfo, char* pMsg, int nLen)
{
	DWORD dwRecvNumBytes = 0;

	//���۵� �޼����� ����
	CopyMemory(clntInfo->sendOverlappedIO.buffer, pMsg, nLen);

	//Overlapped IO�� ���� �� ������ ������ �ش�.
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

	//socket_error�̸� client socket�� �������ɷ� ó���Ѵ�.
	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	{
		cout << "[����] WSASend()�Լ� ����" << endl;
		return false;
	}
	return true;
}

bool IOCompletionPort::BindRecv(LPPER_CLNT_DATA clntInfo)
{
	DWORD dwFlag = 0;
	DWORD dwRecvNumBytes = 0;
	//Overlapped IO�� ���� �� ���� ����
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

	//socket_error�� client socket�� �������ɷ� ó���Ѵ�.
	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	{
		cout << "[����] WSARecv()�Լ� ����" << endl;
		return false;
	}
	return true;
}

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO)
{
	cout << "This is EchoThreadMain()" << endl;
	return 0;
}