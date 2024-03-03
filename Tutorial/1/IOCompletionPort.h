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

class IOCompletionPort
{
public:
	IOCompletionPort() {}
	~IOCompletionPort(void)
	{
		WSACleanup();
	}
	bool InitSocket()
	{
		WSADATA wsaData;
		HANDLE hComPort;
		SYSTEM_INFO sysInfo;
		SOCKET hServSock;
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
};


DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO)
{
	cout << "This is EchoThreadMain()" << endl;
	return 0;
}