// 1stFinalServer.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#include "stdafx.h"
#include "CServerFramework.h"

CServerFramework server;

int main()
{
	server.initiate();
	thread t1{ accept_thread, &server };
	t1.join();
	return 0;
}