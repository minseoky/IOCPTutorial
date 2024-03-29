#include "IOCompletionPort.h"

#define MAX_CLNT_CNT 5

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		cout << "Usage : " << argv[0] << " <port>" << endl;
		exit(1);
	}
	IOCompletionPort IOCP;
	int port = atoi(argv[1]);

	IOCP.InitSocket();

	IOCP.BindAndListen(port);

	IOCP.StartServer(MAX_CLNT_CNT);

	cout << "아무 키나 누르면 서버를 종료합니다." << endl;
	getchar();

	IOCP.DestroyThread();

	return 0;
}