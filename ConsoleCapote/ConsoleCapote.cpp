// ConsoleCapote.cpp : コンソール アプリケーションのエントリ ポイントを定義します。
//

#include "stdafx.h"
#include <windows.h>
#include "..\Capote\capote.h"

int _tmain(int argc, _TCHAR* argv[])
{
	ICapote *pCapote = ICapote::Create();
	if (pCapote->Start("sample.wav") != ICapote::ERR_OK) {
		return -1;
	}
	getchar();
	int samples = pCapote->Stop();
	delete pCapote;
	printf("Total: %d[samples]\n", samples);
	return 0;
}

