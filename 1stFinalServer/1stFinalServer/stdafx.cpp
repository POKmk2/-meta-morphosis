// stdafx.cpp : 표준 포함 파일만 들어 있는 소스 파일입니다.
// 1stFinalServer.pch는 미리 컴파일된 헤더가 됩니다.
// stdafx.obj에는 미리 컴파일된 형식 정보가 포함됩니다.

#include "stdafx.h"

// TODO: 필요한 추가 헤더는
// 이 파일이 아닌 STDAFX.H에서 참조합니다.
void error_display(const char *msg, int err_no)
{
	std::wcout.imbue(std::locale("kor"));
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L"  에러" << lpMsgBuf << std::endl;
	LocalFree(lpMsgBuf);
	//while (true);
}

void ErrorDisplay(const char * location)
{
	error_display(location, WSAGetLastError());
}