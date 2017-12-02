// ==========================================================
// IW4M project
// 
// Component: clientdll
// Sub-component: steam_api
// Purpose: support code for the main menu news ticker
//
// Initial author: Alex
// Started: 2013-01-23
// ==========================================================

#include "StdInc.h"

CallHook newsTickerGetTextHook;
DWORD newsTickerGetTextHookLoc = 0x6388C1;

char ticker[1024];
char motdFile[1024];
bool newsUpdated;

const char* NewsTicker_GetText(const char* garbage)
{
	if (newsUpdated)
	{
		return ticker; 
	}

	// Get the current language
	DWORD getLang = 0x45CBA0;
	char* language;

	__asm {
		call getLang
		mov language, eax
	}

	// Grab the message of the day file
	NPAsync<NPGetPublisherFileResult>* readResult = NP_GetPublisherFile(va("motd-%s.txt", language), (uint8_t*)&motdFile, sizeof(motdFile));
	NPGetPublisherFileResult* rResult = readResult->Wait();

	if (rResult->result != GetFileResultOK)
	{
		strcpy(ticker, va("^1Failed to retrieve latest news! Please contact an IW4Play staff, thanks."));
		newsUpdated = true;
	}
	else
	{
		strcpy(ticker, va("%s", rResult->buffer));
		newsUpdated = true;
	}


	return ticker;
}

void PatchMW2_NewsTicker()
{
	// hook for getting the news ticker string
	*(WORD*)0x6388BB = 0x9090; // skip the "if (item->text[0] == '@')" localize check

	// replace the localize function
	newsTickerGetTextHook.initialize(newsTickerGetTextHookLoc, NewsTicker_GetText);
	newsTickerGetTextHook.installHook();

	// make newsfeed (ticker) menu items not cut off based on safe area
	memset((void*)0x63892D, 0x90, 5);
}

/*CallHook newsTickerGetTextHook;
DWORD newsTickerGetTextHookLoc = 0x6388C1;

char ticker[1024];
char motdFile[1024];
bool newsUpdated;

const char* NewsTicker_GetText(const char* garbage)
{
	if (newsUpdated)
	{
		return ticker; 
	}

	// Get the current language
	DWORD getLang = 0x45CBA0;
	char* language;

	__asm {
		call getLang
		mov language, eax
	}

	// Grab the message of the day file
	NPAsync<NPGetPublisherFileResult>* readResult = NP_GetPublisherFile(va("motd-%s.txt", language), (uint8_t*)&motdFile, sizeof(motdFile));
	NPGetPublisherFileResult* rResult = readResult->Wait();

	if (rResult->result != GetFileResultOK)
	{
		strcpy(ticker, va("^1Failed to retrieve latest news!"));
		newsUpdated = true;
	}
	else
	{
		strcpy(ticker, va("%s", rResult->buffer));
		newsUpdated = true;
	}


	return ticker;
}

void PatchMW2_NewsTicker()
{
	// hook for getting the news ticker string
	*(WORD*)0x6388BB = 0x9090; // skip the "if (item->text[0] == '@')" localize check

	// replace the localize function
	newsTickerGetTextHook.initialize(newsTickerGetTextHookLoc, NewsTicker_GetText);
	newsTickerGetTextHook.installHook();

	// make newsfeed (ticker) menu items not cut off based on safe area
	memset((void*)0x63892D, 0x90, 5);
}*/