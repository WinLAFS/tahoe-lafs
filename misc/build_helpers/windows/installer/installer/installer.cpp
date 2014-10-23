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
bool unzip(wchar_t *zip_path, wchar_t *destination_dir);

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
	HMODULE hModule = GetModuleHandle(NULL);
    assert(hModule != NULL);
	WCHAR executable_path[MAX_PATH];
    GetModuleFileNameW(hModule, executable_path, MAX_PATH); 
    assert(GetLastError() == ERROR_SUCCESS);

	wchar_t executable_dir[_MAX_DRIVE-1 + _MAX_DIR], dir_on_drive[_MAX_DIR];
	errno = 0;
	_wsplitpath(executable_path, executable_dir, dir_on_drive, NULL, NULL); // can't overflow
	assert(errno == 0);
	wcscat(executable_dir, dir_on_drive); // can't overflow
	assert(errno == 0);
	assert(SetCurrentDirectoryW(executable_dir) != 0);
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
	return L"";
}

bool unzip(LPWSTR zipFile, LPWSTR folder) {
	// Essentially something like:
	// Shell.NameSpace(folder).CopyHere(Shell.NameSpace(zipFile))

	BSTR bstrZipFile = SysAllocString(zipFile);
	assert(bstrZipFile);
	BSTR bstrFolder = SysAllocString(folder);
	assert(bstrFolder);

	HRESULT res = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	assert(res == S_OK || res == S_FALSE);
	__try {
		IShellDispatch *pISD;
		assert(CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void **) &pISD) == S_OK);

		VARIANT InZipFile;
		InZipFile.vt = VT_BSTR;
		InZipFile.bstrVal = bstrZipFile;
		Folder *pZippedFile = NULL;
		pISD->NameSpace(InZipFile, &pZippedFile);
		assert(pZippedFile);

		VARIANT OutFolder;
		OutFolder.vt = VT_BSTR;
		OutFolder.bstrVal = SysAllocString(folder);
		assert(OutFolder.bstrVal);
		Folder *pDestination = NULL;
		pISD->NameSpace(OutFolder, &pDestination);
		assert(pDestination);

		FolderItems *pFilesInside = NULL;
		pZippedFile->Items(&pFilesInside);
		assert(pFilesInside);

		IDispatch *pItem = NULL;
		pFilesInside->QueryInterface(IID_IDispatch, (void **) &pItem);
		assert(pItem);

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
		return retval;

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