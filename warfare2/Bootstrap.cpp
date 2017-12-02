#include "StdInc.h"

#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>

#pragma comment(lib, "wintrust")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")

bool Bootstrap_UpdateEXE(int exeSize)
{
	const char* fn = _tempnam(NULL, "w2");
	//CL_QueueDownload("http://update.rev.iw4play.net/bootstrap/LaunchIW4Play.exe.xz", fn, exeSize, true);
	CL_QueueDownload("http://updater.iw4play.net/updater_iw4/bootstrap/LaunchIW4Play.exe.xz", fn, exeSize, true);
	UI_DoCreation();

	if (!DL_RunLoop())
	{
		UI_DoDestruction();
		return false;
	}

	UI_DoDestruction();

	// verify the signature on the EXE
	wchar_t wfn[512];
	MultiByteToWideChar(CP_ACP, 0, fn, -1, wfn, 512);

	WINTRUST_FILE_INFO info;
	memset(&info, 0, sizeof(info));

	info.cbStruct = sizeof(info);
	info.pcwszFilePath = wfn;
	info.hFile = NULL;
	info.pgKnownSubject = NULL;

	GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;

	WINTRUST_DATA data;
	memset(&data, 0, sizeof(data));

	data.cbStruct = sizeof(data);
	data.pPolicyCallbackData = NULL;
	data.pSIPClientData = NULL;
	data.dwUIChoice = WTD_UI_NONE;
	data.fdwRevocationChecks = WTD_REVOKE_NONE;
	data.dwUnionChoice = WTD_CHOICE_FILE;
	data.dwStateAction = 0;
	data.hWVTStateData = NULL;
	data.pwszURLReference = NULL;
	data.dwUIContext = 0;
	data.pFile = &info;

	LONG status = WinVerifyTrust(NULL, &WVTPolicyGUID, &data);

	if (status != ERROR_SUCCESS)
	{
		//MessageBox(NULL, va(L"A trust chain error occurred in the downloaded LaunchIW4Play.exe. The specific error code is 0x%08x.", status), L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
		//return false;
	}

	wchar_t exePath[512];
	GetModuleFileName(GetModuleHandle(NULL), exePath, sizeof(exePath) / 2);

	STARTUPINFO startupInfo;
	memset(&startupInfo, 0, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.wShowWindow = SW_HIDE;
	startupInfo.dwFlags |= STARTF_USESHOWWINDOW;

	PROCESS_INFORMATION processInfo;

	CreateProcess(wfn, (LPWSTR)va(L"%s -bootstrap \"%s\"", wfn, exePath), NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo);

	return false;
}

bool Bootstrap_DoBootstrap()
{
	FreeConsole();

	// first check the bootstrapper version
	char bootstrapVersion[256];

	//int result = DL_RequestURL("http://update.rev.iw4play.net/bootstrap/version.txt", bootstrapVersion, sizeof(bootstrapVersion));
	int result = DL_RequestURL("http://updater.iw4play.net/updater_iw4/bootstrap/version.txt", bootstrapVersion, sizeof(bootstrapVersion));
	if (result != 0)
	{
		//MessageBox(NULL, va(L"An error (%i) occurred while checking the bootstrapper version. Check if http://update.rev.iw4play.net/ is available in your web browser.", result), L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
		MessageBox(NULL, va(L"An error (%i) occurred while checking the bootstrapper version. Check if http://update.iw4play.net/ is available in your web browser.", result), L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
		return false;
	}

	//int version = atoi(bootstrapVersion);
	int version;
	int exeSize;
	sscanf(bootstrapVersion, "%i %i", &version, &exeSize);

	if (version > LAUNCHER_EXE_VERSION)
	{
		return Bootstrap_UpdateEXE(exeSize);
	}

	return Updater_RunUpdate(1, "iw4m");
}

void Bootstrap_ReplaceExecutable(const wchar_t* fileName)
{
	wchar_t thisFileName[512];
	GetModuleFileName(GetModuleHandle(NULL), thisFileName, sizeof(thisFileName) / 2);

	// try opening the file
	bool opened = false;
	HANDLE hFile;

	while (!opened)
	{
		hFile = CreateFile(fileName, FILE_READ_ACCESS | FILE_WRITE_ACCESS, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hFile != INVALID_HANDLE_VALUE)
		{
			break;
		}

		int error = GetLastError();

		if (error == ERROR_ACCESS_DENIED)
		{
			MessageBox(NULL, L"An 'access denied' error was encountered when updating IW4Play. Please try to run the game as an administrator, or contact support.", L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
			return;
		}
		else if (error != ERROR_SHARING_VIOLATION)
		{
			MessageBox(NULL, va(L"Win32 error %i was encountered when updating IW4Play.", error), L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
			return;
		}

		Sleep(50);
	}

	// move the file
	CloseHandle(hFile);
	DeleteFile(va(L"%s.old", fileName));

	if (!MoveFile(fileName, va(L"%s.old", fileName)))
	{
		int error = GetLastError();

		if (error == ERROR_ACCESS_DENIED)
		{
			MessageBox(NULL, L"An 'access denied' error was encountered when updating IW4Play (moving to .old). Please try to run the game as an administrator, or contact support.", L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
			return;
		}
		else
		{
			MessageBox(NULL, va(L"Win32 error %i was encountered when updating IW4Play (moving to .old).", error), L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
			return;
		}
	}

	// copy our lovely file
	if (!CopyFile(thisFileName, fileName, TRUE))
	{
		int error = GetLastError();

		if (error == ERROR_ACCESS_DENIED)
		{
			MessageBox(NULL, L"An 'access denied' error was encountered when updating IW4Play (copying game executable). Please try to run the game as an administrator, or contact support.", L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
			return;
		}
		else
		{
			MessageBox(NULL, va(L"Win32 error %i was encountered when updating IW4Play (copying game executable).", error), L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
			return;
		}
	}

	DeleteFile(va(L"%s.old", fileName));

	ShellExecute(NULL, L"open", fileName, L"", L"", SW_SHOWDEFAULT);
}

bool Bootstrap_RunInit()
{
	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);

	if (argc == 3)
	{
		if (!wcsicmp(argv[1], L"-bootstrap"))
		{
			FreeConsole();

			Bootstrap_ReplaceExecutable(argv[2]);
			LocalFree(argv);
			return true;
		}
	}

	LocalFree(argv);
}