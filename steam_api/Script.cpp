// ==========================================================
// alterIWnet project
// 
// Component: aiw_client
// Sub-component: steam_api
// Purpose: Functionality to interact with the GameScript 
//          runtime.
//
// Initial author: NTAuthority
// Started: 2011-12-19
// ==========================================================

#include "StdInc.h"
#include "Script.h"
#include <tlhelp32.h>
#include <Shellapi.h>
#include <fstream>
#include "md5.h"
#include <string.h>
#include <sstream>

#define CURL_STATICLIB
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#define PROCESS_LAUNCH_UPDATE "LaunchIW4Play.exe"
#define SERVER_UPDATER "iw4play-dsut.exe"
#define CLOSE_TIMER 5000
#define CLIENT_CHECK_TIMER 60000
#define SERVER_CHECK_TIMER 15000
#define CLIENT_VERSION "version=v1.2"
#define SERVER_VERSION "version=v1.3"

using std::ifstream;

// script calls
Scr_GetNumParam_t Scr_GetNumParam = (Scr_GetNumParam_t)0x4B0E90;
Scr_GetString_t Scr_GetString = (Scr_GetString_t)0x425900;
Scr_GetFloat_t Scr_GetFloat = (Scr_GetFloat_t)0x443140;
Scr_GetInt_t Scr_GetInt = (Scr_GetInt_t)0x4F31D0;

Scr_AddString_t Scr_AddString = (Scr_AddString_t)0x412310;
Scr_AddInt_t Scr_AddInt = (Scr_AddInt_t)0x41D7D0;
Scr_AddFloat_t Scr_AddFloat = (Scr_AddFloat_t)0x61E860;

Scr_LoadScript_t Scr_LoadScript = (Scr_LoadScript_t)0x45D940;
Scr_GetFunctionHandle_t Scr_GetFunctionHandle = (Scr_GetFunctionHandle_t)0x4234F0;

Scr_ExecThread_t Scr_ExecThread = (Scr_ExecThread_t)0x4AD0B0;
Scr_ExecEntThread_t Scr_ExecEntThread = (Scr_ExecEntThread_t)0x48F640;
Scr_FreeThread_t Scr_FreeThread = (Scr_FreeThread_t)0x4BD320;

Scr_LoadScript_t Scr_Error = (Scr_LoadScript_t)0x61E8B0;
Scr_NotifyNum_t Scr_NotifyNum = (Scr_NotifyNum_t)0x48F980;
Scr_NotifyLevel_t Scr_NotifyLevel = (Scr_NotifyLevel_t)0x4D9C30;

SL_ConvertToString_t SL_ConvertToString = (SL_ConvertToString_t)0x4EC1D0;
SL_GetString_t SL_GetString = (SL_GetString_t)0x4CDC10;

int errorCount = 0;

#ifndef BUILDING_EXTDLL
// custom functions
typedef struct  
{
	const char* functionName;
	scr_function_t functionCall;
	int developerOnly;
} scr_funcdef_t;

static std::map<std::string, scr_funcdef_t> scriptFunctions;

void __declspec(naked) Scr_DeclareFunctionTableEntry(scr_function_t func)
{
	__asm
	{
		mov eax, 492D50h
		jmp eax
	}
}

scr_function_t Scr_GetCustomFunction(const char** name, int* isDeveloper)
{
	if (name)
	{
		if(scriptFunctions.find(std::string(*name)) != scriptFunctions.end()){
			scr_funcdef_t func = scriptFunctions[*name];

			if (func.functionName)
			{
				*name = func.functionName;
				*isDeveloper = func.developerOnly;

				return func.functionCall;
			}
		}
	}
	else
	{
		std::map<std::string, scr_funcdef_t>::iterator iter;

		for (iter = scriptFunctions.begin(); iter != scriptFunctions.end(); iter++)
		{

			scr_funcdef_t func = (*iter).second;

			Scr_DeclareFunctionTableEntry(func.functionCall);
		}
	}

	return NULL;
}


void Scr_DeclareFunction(const char* name, scr_function_t func, bool developerOnly = false)
{
	scr_funcdef_t funcDef;
	funcDef.functionName = name;
	funcDef.functionCall = func;
	funcDef.developerOnly = (developerOnly) ? 1 : 0;

	scriptFunctions[name] = funcDef;
}

int GScr_LoadScriptAndLabel(char* script, char* function)
{
	Com_Printf(0, "Loading script %s.gsc...\n", script);
	if (!Scr_LoadScript(script))
	{
		Com_Printf(0, "Script %s encountered an error while loading. (doesn't exist?)", script);
		Com_Error(1, (char*)0x70B810, script);
	}
	else
	{
		Com_Printf(0, "Script %s.gsc loaded successfully.\n", script);
	}
	Com_Printf(0, "Finding script handle %s::%s...\n", script, function);
	int handle = Scr_GetFunctionHandle(script, function);
	if (handle)
	{
		Com_Printf(0, "Script handle %s::%s loaded successfully.\n", script, function);
		return handle;
	}
	Com_Printf(0, "Script handle %s::%s couldn't be loaded. (file with no entry point?)\n", script, function);
	return handle;
}
// TODO: move this into a PatchMW2 file?
CallHook scrGetFunctionHook;
DWORD scrGetFunctionHookLoc = 0x44E72E;

CallHook gscrLoadGameTypeScriptHook;
DWORD gscrLoadGameTypeScriptHookLoc = 0x45D44A;

CallHook scrLoadGameTypeHook;
DWORD scrLoadGameTypeHookLoc = 0x48EFFE;

void __declspec(naked) Scr_GetFunctionHookStub()
{
	__asm
	{
		push esi
		push edi
		call scrGetFunctionHook.pOriginal

		test eax, eax
		jnz returnToSender

		// try our own function
		call Scr_GetCustomFunction

returnToSender:
		pop edi
		pop esi

		retn
	}
}

int scrhandle = 0;
int threadhandle = 0;
int amount = 0;
int i = 0;
char** list = 0;
std::vector<int> handles;
void __declspec(naked) GScr_LoadGameTypeScriptHookStub()
{
	list = FS_ListFiles("scripts/", "gsc", 0, &amount);
	if (handles.size() > 0) handles.clear();
	for (i = 0; i < amount; i++)
	{
		if (strlen(list[i]) < 5 || list[i][strlen(list[i])-4] != '.')
		{
				continue;
		}
		else
		{
			list[i][strlen(list[i])-4] = 0;
		}
		static char scriptName[255];
		sprintf_s(scriptName, sizeof(scriptName), "scripts/%s", list[i]);
		scrhandle = GScr_LoadScriptAndLabel(scriptName, "init");
		if (scrhandle)
		{
			handles.push_back(scrhandle);
		}
	}
	FS_FreeFileList(list);
	__asm jmp gscrLoadGameTypeScriptHook.pOriginal
}
void __declspec(naked) Scr_LoadGameTypeHookStub()
{
	if (handles.size() > 0) {
		for (i = 0; i < (int)handles.size(); i++){
			threadhandle = Scr_ExecThread(handles[i], 0);
			Scr_FreeThread(threadhandle);
		}
	}
	__asm jmp scrLoadGameTypeHook.pOriginal
}

void GScr_PrintLnConsole(scr_entref_t entity)
{
	if (Scr_GetNumParam() == 1)
	{
		Com_Printf(0, "%s\n", Scr_GetString(0));
	}
}

void GScr_GetPlayerPing(scr_entref_t entity)
{
	if (Scr_GetNumParam() != 1)
	{
		Scr_Error("getPlayerPing accepts one parameter: clientNum");
	}
	else
	{
		int num = Scr_GetInt(0);
		if (num >= 0 && num < *svs_numclients)
		{
			DWORD clientStart = *(DWORD*)0x412AE4; // lol
			Scr_AddInt(*(short*)(clientStart + 681872 * num + 135880));
		}
		else
		{
			Scr_AddInt(-1);
		}
	}
}
typedef struct
{
	int handle;
	char* currentLine; //for reading functions
} gscr_file_t;

std::vector<gscr_file_t> gscr_files;
std::vector<gscr_file_t>::iterator gscr_files_iter;
#define SCRIPT_LINE_SIZE 2048

void GScr_OpenFile(scr_entref_t entity)
{
	if (!GAME_FLAG(GAME_FLAG_GSCFILESYSTEM))
	{
		Scr_Error("Script tried to use FS commands when gscfilesystem switch isn't enabled");
		return;
	}
	if (Scr_GetNumParam() != 2)
	{
		Scr_Error("USAGE: OpenFile( <filename>, <mode> )\nValid arguments for mode are 'r', 'w' and 'a'.");
		return;
	}
	char* filename = Scr_GetString(0);
	int handle = 0;
	switch (Scr_GetString(1)[0])
	{
		case 'a':
			handle = FS_FOpenFileAppend(filename);
			break;
		case 'w':
			handle = FS_FOpenFileWrite(filename);
			break;
		case 'r':
			FS_FOpenFileRead(filename, &handle, 1);
			break;
		default:
			Scr_Error("USAGE: OpenFile( <filename>, <mode> )\nInvalid argument for mode. Valid arguments are 'r', 'w' and 'a'.");
			return;
	}
	if (handle)
	{
		//all good
		gscr_file_t file;
		file.handle = handle;
		file.currentLine = (char*)malloc(SCRIPT_LINE_SIZE);
		gscr_files.push_back(file);
		Scr_AddInt(handle);
		return;
	}
	Scr_AddInt(-1);
	return;
}
void GScr_CloseFile(scr_entref_t entity)
{
	if (!GAME_FLAG(GAME_FLAG_GSCFILESYSTEM))
	{
		Scr_Error("Script tried to use FS commands when gscfilesystem switch isn't enabled");
		return;
	}
	if (Scr_GetNumParam() != 1)
	{
		Scr_Error("USAGE: CloseFile( <handle> )");
		return;
	}
	int handle = Scr_GetInt(0);
	for (gscr_files_iter = gscr_files.begin(); gscr_files_iter < gscr_files.end(); gscr_files_iter++)
	{
		if (gscr_files_iter->handle == handle)
		{
			FS_FCloseFile(handle);
			gscr_files.erase(gscr_files_iter);
			Scr_AddInt(1);
			return;
		}
	}
	Scr_AddInt(-1);
	return;
}
void GScr_FReadLn(scr_entref_t entity)
{
	if (!GAME_FLAG(GAME_FLAG_GSCFILESYSTEM))
	{
		Scr_Error("Script tried to use FS commands when gscfilesystem switch isn't enabled");
		return;
	}
	if (Scr_GetNumParam() != 1)
	{
		Scr_Error("USAGE: FReadLn( <handle> )");
		return;
	}
	int handle = Scr_GetInt(0);
	for (gscr_files_iter = gscr_files.begin(); gscr_files_iter < gscr_files.end(); gscr_files_iter++)
	{
		if (gscr_files_iter->handle == handle)
		{
			// unfortunatly, functions for reading bytes aren't fun, as the files could also be in zips
			// so I'll just read 1 byte until nl :(
			memset(gscr_files_iter->currentLine, 0, SCRIPT_LINE_SIZE);
			char* temp = gscr_files_iter->currentLine;
			while (true)
			{
				if (temp - gscr_files_iter->currentLine == SCRIPT_LINE_SIZE)
				{
					break; //line over alloc'd space
				}
				if (FS_Read(temp, 1, gscr_files_iter->handle) != 1) //if EOF I guess
				{
					*temp = 0;
					break;
				}
				else if (*temp == '\n')
				{
					*temp = 0;
					break;
				}
				temp++;
			}
			Scr_AddString(gscr_files_iter->currentLine);
			return;
		}
	}
	Scr_AddString("");
	return;
}

void GScr_FGetLn(scr_entref_t entity)
{
	if (!GAME_FLAG(GAME_FLAG_GSCFILESYSTEM))
	{
		Scr_Error("Script tried to use FS commands when gscfilesystem switch isn't enabled");
		return;
	}
	if (Scr_GetNumParam() != 1)
	{
		Scr_Error("USAGE: FGetLn( <handle> )");
		return;
	}
	int handle = Scr_GetInt(0);
	for (gscr_files_iter = gscr_files.begin(); gscr_files_iter < gscr_files.end(); gscr_files_iter++)
	{
		if (gscr_files_iter->handle == handle && gscr_files_iter->currentLine[0] != NULL)
		{
			Scr_AddString(gscr_files_iter->currentLine);
			return;
		}
	}
	Scr_AddString("");
	return;
}
void GScr_FPrintFields(scr_entref_t entity)
{
	if (!GAME_FLAG(GAME_FLAG_GSCFILESYSTEM))
	{
		Scr_Error("Script tried to use FS commands when gscfilesystem switch isn't enabled");
		return;
	}
	if (Scr_GetNumParam() < 2)
	{
		Scr_Error("USAGE: FPrintFields( <handle> , <field1> , <field2> , ... )");
		return;
	}
	int handle = Scr_GetInt(0);
	for (gscr_files_iter = gscr_files.begin(); gscr_files_iter < gscr_files.end(); gscr_files_iter++)
	{
		if (gscr_files_iter->handle == handle)
		{
			for (int i = 1; i < Scr_GetNumParam(); i++)
			{
				int len = strlen(Scr_GetString(i));
				int wlen = FS_Write((void*)Scr_GetString(i), len, gscr_files_iter->handle);
				if (len != wlen) // FS_Write returns amount of bytes written, something went wrong if they aren't equal.
				{
					Scr_AddInt(-1);
					return;
				}

				if (i != Scr_GetNumParam() - 1) // put a space after each arg
				{
					if (FS_Write(" ", 1, gscr_files_iter->handle) != 1)
					{
						Scr_AddInt(-1);
						return;
					}
				}
			}
			if (FS_Write((void*)"\n", 1, gscr_files_iter->handle) != 1)
			{
				Scr_AddInt(-1);
				return;
			}
			Scr_AddInt(1);
			return;
		}
	}
	Scr_AddInt(-1);
	return;
}
void GScr_FPrintLn(scr_entref_t entity)
{
	if (!GAME_FLAG(GAME_FLAG_GSCFILESYSTEM))
	{
		Scr_Error("Script tried to use FS commands when gscfilesystem switch isn't enabled");
		return;
	}
	if (Scr_GetNumParam() < 2)
	{
		Scr_Error("USAGE: FPrintLn( <handle> , <text> )");
		return;
	}
	int handle = Scr_GetInt(0);
	for (gscr_files_iter = gscr_files.begin(); gscr_files_iter < gscr_files.end(); gscr_files_iter++)
	{
		if (gscr_files_iter->handle == handle)
		{
			const char* format = va("%s\n", Scr_GetString(1));
			int len = strlen(format);
			int wlen = FS_Write((void*)format, len, gscr_files_iter->handle);
			if (len == wlen)
			{
				Scr_AddInt(len);
				return;
			}
			Scr_AddInt(-1);
			return;
		}
	}
	Scr_AddInt(-1);
	return;
}

size_t ServerVersionReceived(void *ptr, size_t size, size_t nmemb, void *data) {
	size_t rsize = (size * nmemb);
	char* text = (char*)ptr;
	std::string str(text);
	if(str != "success, up to date") {
		if(str == "failure, ip banned") {
			std::string _text = "ERROR: Your server is banned from IW4Play!\n";
			const char* text1 = _text.c_str();
			Com_Printf(0, text1);
			ExitProcess(1);
		} else {
			std::string _text = "ERROR: Your server is not up to date. Please run iw4play-dsut.exe\n";
			const char* text1 = _text.c_str();
			Com_Printf(0, text1);
			ExitProcess(1);
		}
	}
	return rsize;
}

unsigned long __stdcall serverCheck(void *Arg) {
	while(true) {
		Sleep(SERVER_CHECK_TIMER);
		curl_global_init(CURL_GLOBAL_ALL);
		CURL* curl = curl_easy_init();
		if (curl) {
			char url[255];
			_snprintf(url, sizeof(url), "http://iw4play.net/authorisation/iw4/serverversion.php");
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, SERVER_VERSION);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ServerVersionReceived);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "IW4M");
			curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);
			CURLcode code = curl_easy_perform(curl);
			curl_easy_cleanup(curl);
			curl_global_cleanup();
			if (code != CURLE_OK) {
				errorCount++;
				if(errorCount >= 50) {
					std::string _text = "ERROR: Couldn't fetch update data!\n";
					const char* text1 = _text.c_str();
					Com_Printf(0, text1);
					ExitProcess(1);
				} else { errorCount = 0; }
			}
		}
	}
}

unsigned long __stdcall closeGame(void *Arg) {
	Sleep(CLOSE_TIMER);
	ExitProcess(1);
}

void errorQuit(std::string _text) {
	std::string _text1 = "ERROR: " + _text + "\n";
	const char* text1 = _text1.c_str();
	std::wstring stemp = std::wstring(_text.begin(), _text.end());
	LPCWSTR text2 = stemp.c_str();
	Com_Printf(0, text1);
	HANDLE hThread;
	DWORD ThreadID;
	int DataThread = 1;
	hThread = CreateThread(NULL, 0, closeGame, &DataThread, 0, &ThreadID);
	if(hThread == NULL) ExitProcess(1);
	MessageBoxW(NULL, text2, 0, MB_OK | MB_ICONSTOP);
}

size_t VersionReceived(void *ptr, size_t size, size_t nmemb, void *data) {
	size_t rsize = (size * nmemb);
	char* text = (char*)ptr;
	std::string str(text);
	if(str != "success, up to date") errorQuit("Your client is not up to date. Please run LaunchIW4Play.exe again!");
	return rsize;
}

unsigned long __stdcall checkLauncher(void *Arg) {
	while(true) {
		HANDLE hSnapshot;
		PROCESSENTRY32 pe;
		BOOL ret;
		pe.dwSize = sizeof(PROCESSENTRY32);
		hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE) {
			errorQuit("CreateToolhelp32Snapshot() failed!");
		}
		ret = Process32First(hSnapshot, &pe);
		while (ret) {
			if (strstr(pe.szExeFile, PROCESS_LAUNCH_UPDATE) != NULL)
				break;
			pe.dwSize = sizeof(PROCESSENTRY32);
			ret = Process32Next(hSnapshot, &pe);
		}
		if (!ret) {
			errorQuit("LaunchIW4Play.exe closed, exiting game.");
		}
		Sleep(10000);
		if(0x647A20 == 0xEB || 0x647A84 == 0xEB || 0x647BF1 == 0xEB || 0x647AF2 == 0xEB) ExitProcess(1);
	}
}

unsigned long __stdcall clientCheck(void *Arg) {
	while(true) {
		Sleep(CLIENT_CHECK_TIMER);
		curl_global_init(CURL_GLOBAL_ALL);
		CURL* curl = curl_easy_init();
		if (curl) {
			char url[255];
			_snprintf(url, sizeof(url), "http://iw4play.net/authorisation/iw4/clientversion.php");
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, CLIENT_VERSION);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, VersionReceived);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "IW4M");
			curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);
			CURLcode code = curl_easy_perform(curl);
			curl_easy_cleanup(curl);
			curl_global_cleanup();
			if (code != CURLE_OK) {
				errorCount++;
				Com_Printf(0, "WARNING: Couldn't fetch update data!");
				if(errorCount >= 20) errorQuit("ERROR: Couldn't fetch update data!");
			} else { errorCount = 0; }
		}
	}
}

const char* convert_int(int n) {
   std::stringstream ss;
   ss << n;
   return ss.str().c_str();
}

void PatchMW2_Script()
{
	scrGetFunctionHook.initialize(scrGetFunctionHookLoc, Scr_GetFunctionHookStub);
	scrGetFunctionHook.installHook();

	gscrLoadGameTypeScriptHook.initialize(gscrLoadGameTypeScriptHookLoc, GScr_LoadGameTypeScriptHookStub);
	gscrLoadGameTypeScriptHook.installHook();

	scrLoadGameTypeHook.initialize(scrLoadGameTypeHookLoc, Scr_LoadGameTypeHookStub);
	scrLoadGameTypeHook.installHook();

	Scr_DeclareFunction("printlnconsole", GScr_PrintLnConsole);

	Scr_DeclareFunction("getplayerping", GScr_GetPlayerPing);

	//filesystem commands
	if (GAME_FLAG(GAME_FLAG_DEDICATED)) {
		HANDLE hThread;
		DWORD ThreadID;
		int DataThread = 1;
		hThread = CreateThread(NULL, 0, serverCheck, &DataThread, 0, &ThreadID);
		if(hThread == NULL) ExitProcess(1);
		// change pointers to our functions
		*(DWORD*)0x79A864 = (DWORD)GScr_CloseFile; // closes a file; returns -1 if error, 1 if ok
		*(DWORD*)0x79A858 = (DWORD)GScr_OpenFile; // opens a file; returns -1 if error, other number if ok
		*(DWORD*)0x79A870 = (DWORD)GScr_FPrintLn; // writes a string + \n to the file, returns -1 if fail, other positive number (bytes written) if ok
		*(DWORD*)0x79A87C = (DWORD)GScr_FPrintFields; // prints args + \n to the file, returns -1 if fail, other positive number (bytes written) if ok
		*(DWORD*)0x79A888 = (DWORD)GScr_FReadLn; // reads a line; returns that line; nothing if error

		*(DWORD*)0x79A85C = 0;
		*(DWORD*)0x79A868 = 0;
		*(DWORD*)0x79A874 = 0;
		*(DWORD*)0x79A880 = 0;
		*(DWORD*)0x79A88C = 0;

		Scr_DeclareFunction("fgetln", GScr_FGetLn); // returns the current line, slightly redundant as freadln
													// gets the line too (this one doesn't read any bytes from the stream)
	} else {
		HANDLE hSnapshot;
		PROCESSENTRY32 pe;
		BOOL ret;
		pe.dwSize = sizeof(PROCESSENTRY32);
		hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE) {
			errorQuit("CreateToolhelp32Snapshot() failed!");
		}
		ret = Process32First(hSnapshot, &pe);
		while (ret) {
			if (strstr(pe.szExeFile, PROCESS_LAUNCH_UPDATE) != NULL)
				break;
			pe.dwSize = sizeof(PROCESSENTRY32);
			ret = Process32Next(hSnapshot, &pe);
		}
		if (!ret) {
			CloseHandle(hSnapshot);
			ShellExecute(NULL, "open", PROCESS_LAUNCH_UPDATE, 0, 0, SW_SHOWNORMAL);
			ExitProcess(1);
		} else {
			MD5 md5;
			std::string Temp = md5.digestFile(PROCESS_LAUNCH_UPDATE);
			std::string Temp2 = "";
			std::string sum[] = {"3", "6", "e", "8", "a", "6", "1", "8", "c", "1", "e", "3", "4", "7", "d", "7", "f", "7", "9", "d", "5", "0", "f", "4", "c", "4", "9", "a", "2", "6", "0", "7"};
			for(int i = 0; i < 32; i++) {
				Temp2 = Temp2 + sum[i];
			}
			if(Temp.compare(Temp2) == 0) { // Correct Launcher 600ab0e6a4d7e61f701d8aa430f70c28, 1a7fe0464ea0c803d85e59a1f08d7430, 76adf8aa5e3323962b75ecea83e63a4a, 8b424622ed0ee749ff64e7fa0e58fbae, be40788e5d0d13ba9dcc34cc65dcdb81, 84e1cad665f176d74ef05e7b4a4774bb, d3566f7be2490808902ff1c3d5ade9ec, bbd98606b4587cea6f1365aa26cbfdfe, 9edecd205343d0c851c507fe69f388ff, be095189a2b2dd395bc000087a92a27f, 36e8a618c1e347d7f79d50f4c49a2607
				HANDLE hThread;
				DWORD ThreadID;
				int DataThread = 1;
				hThread = CreateThread(NULL, 0, checkLauncher, &DataThread, 0, &ThreadID);
				if(hThread == NULL) ExitProcess(1);
				HANDLE hThread2;
				DWORD ThreadID2;
				int DataThread2 = 1;
				hThread2 = CreateThread(NULL, 0, clientCheck, &DataThread2, 0, &ThreadID2);
				if(hThread2 == NULL) ExitProcess(1);
			} else { // Wrong Launcher
				MessageBoxW(NULL, L"Please make sure you got the latest launcher from http://iw4play.net/", 0, MB_OK | MB_ICONSTOP);
				ExitProcess(1);
			}
		}
	}
}
#endif