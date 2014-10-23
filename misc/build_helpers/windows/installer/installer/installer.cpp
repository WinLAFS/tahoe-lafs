// installer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

// Turn off the warnings nagging you to use the more complicated *_s
// "secure" functions that are actually more difficult to use securely.
#pragma warning(disable:4996)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wtypes.h>
#include <objbase.h>
#include <shldisp.h>

int wmain(int argc, wchar_t *argv[]);
void self_extract(wchar_t *destination_dir);
bool have_acceptable_python();
void install_python();
wchar_t * get_default_destination_dir();
void unzip(wchar_t *zip_path, wchar_t *destination_dir);

#define fail_unless(x, s) if (!(x)) { fail(s); }
void fail(char *s);

#define MINIMUM_PYTHON_VERSION L"2.7.0"
#define INSTALL_PYTHON_VERSION L"2.7.8"
#define PYTHON_INSTALLER_32BIT (L"python-" INSTALL_PYTHON_VERSION L".msi")
#define PYTHON_INSTALLER_64BIT (L"python-" INSTALL_PYTHON_VERSION L".amd64.msi")

int wmain(int argc, wchar_t *argv[]) {
	if (argc >= 2 && wcscmp(argv[1], L"--help") == 0) {
		printf("installer <destination_dir>\n");
	}
	wchar_t *destination_dir = (argc >= 2) ? argv[1] : get_default_destination_dir();

	self_extract(destination_dir);
	if (!have_acceptable_python()) {
		install_python();
	}
	//unlink(python_installer);
	return 0;
}

void self_extract(wchar_t *destination_dir) {
	wchar_t executable_path[MAX_PATH];

	HMODULE hModule = GetModuleHandle(NULL);
    assert(hModule != NULL);
    GetModuleFileNameW(hModule, executable_path, MAX_PATH); 
    assert(GetLastError() == ERROR_SUCCESS);

	unzip(executable_path, destination_dir);
}

bool have_acceptable_python() {
	printf("Checking for Python 2.7...");
	//key = OpenKey(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE)
	//key = OpenKey(HKEY_CURRENT_USER, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_QUERY_VALUE)
	//...
	return false;
}

void install_python() {
	// The current directory should be the root of ...
	//CreateProcessW(".msi");
}

wchar_t * get_default_destination_dir() {
	// TODO: get Program Files directory from the registry
	return L"C:\\tahoe\\windowstest";
}

void unzip(wchar_t *zip_path, wchar_t *destination_dir) {
	// Essentially something like:
	// Shell.NameSpace(destination_dir).CopyHere(Shell.NameSpace(zip_path))

	BSTR bstrZipFile = SysAllocString(zip_path);
	fail_unless(bstrZipFile, "Could not allocate string for zip file path.");
	BSTR bstrFolder = SysAllocString(destination_dir);
	fail_unless(bstrFolder, "Could not allocate string for destination directory path.");

	HRESULT res = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	assert(res == S_OK || res == S_FALSE);
	__try {
		IShellDispatch *pISD;
		fail_unless(CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void **) &pISD) == S_OK,
			        "Could not create Shell instance.");

		VARIANT InZipFile;
		InZipFile.vt = VT_BSTR;
		InZipFile.bstrVal = bstrZipFile;
		Folder *pZippedFile = NULL;
		pISD->NameSpace(InZipFile, &pZippedFile);
		fail_unless(pZippedFile, "Could not create NameSpace for zip file.");

		VARIANT OutFolder;
		OutFolder.vt = VT_BSTR;
		OutFolder.bstrVal = bstrFolder;
		Folder *pDestination = NULL;
		pISD->NameSpace(OutFolder, &pDestination);
		fail_unless(pDestination, "Could not create NameSpace for destination directory.");

		FolderItems *pFilesInside = NULL;
		pZippedFile->Items(&pFilesInside);
		fail_unless(pFilesInside, "Could not create FolderItems for zip file contents.");

		IDispatch *pItem = NULL;
		pFilesInside->QueryInterface(IID_IDispatch, (void **) &pItem);
		fail_unless(pItem, "Could not create Item for zip file contents.");

		VARIANT Item;
		Item.vt = VT_DISPATCH;
		Item.pdispVal = pItem;

		VARIANT Options;
		Options.vt = VT_I4;
		// http://msdn.microsoft.com/en-us/library/bb787866(VS.85).aspx
		//    (4) Do not display a progress dialog box.
		//   (16) Respond with "Yes to All" for any dialog box that is displayed.
		//  (256) Display a progress dialog box but do not show the file names.
		//  (512) Do not confirm the creation of a new directory if the operation requires one to be created.
		// (1024) Do not display a user interface if an error occurs.

		Options.lVal = 512 | 256 | 16;

		bool retval = pDestination->CopyHere(Item, Options) == S_OK;
		fail_unless(retval, "CopyHere failed.");

	} __finally {
		//if (bstrInZipFile) SysFreeString(bstrInZipFile);
		//if (bstrOutFolder) SysFreeString(bstrOutFolder);
		//if (pItem)         pItem->Release();
		//if (pFilesInside)  pFilesInside->Release();
		//if (pDestination)  pDestination->Release();
		//if (pZippedFile)   pZippedFile->Release();
		//if (pISD)          pISD->Release();
		CoUninitialize();
	}
}

void fail(char *s) {
	// TODO: show dialog box
	puts(s);
	exit(1);
}
