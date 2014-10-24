// installer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
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
	//unzip(L"C:\\tahoe\\allmydata-tahoe-1.10.0c1.zip", destination_dir);

	if (!have_acceptable_python()) {
		install_python();
	}
	//unlink(python_installer);
	return 0;
}

void self_extract(wchar_t *destination_dir) {
	wchar_t executable_path[MAX_PATH];

	HMODULE hModule = GetModuleHandle(NULL);
    fail_unless(hModule != NULL, "Could not get the module handle.");
    GetModuleFileNameW(hModule, executable_path, MAX_PATH); 
    fail_unless(GetLastError() == ERROR_SUCCESS, "Could not get the path of the current executable.");

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
	wprintf(L"Extracting %ls\nto %ls\n", zip_path, destination_dir);

	// SysAllocString: <http://msdn.microsoft.com/en-gb/library/windows/desktop/ms221458(v=vs.85).aspx>
	// BSTR: <http://msdn.microsoft.com/en-us/library/windows/desktop/ms221069(v=vs.85).aspx>

	VARIANT zip_path_var;
	zip_path_var.vt = VT_BSTR;
	zip_path_var.bstrVal = SysAllocString(zip_path);
	fail_unless(zip_path_var.bstrVal != NULL, "Could not allocate string for zip file path.");

	VARIANT destination_dir_var;
	destination_dir_var.vt = VT_BSTR;
	destination_dir_var.bstrVal = SysAllocString(destination_dir);
	fail_unless(destination_dir_var.bstrVal != NULL, "Could not allocate string for destination directory path.");

	// CoInitializeEx: <http://msdn.microsoft.com/en-gb/library/windows/desktop/ms695279(v=vs.85).aspx>
	HRESULT res = CoInitializeEx(NULL, 0);
	fail_unless(res == S_OK || res == S_FALSE, "Could not initialize COM.");

	// CoCreateInstance: <http://msdn.microsoft.com/en-gb/library/windows/desktop/ms686615(v=vs.85).aspx>
	IShellDispatch *shell;
	res = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void **) &shell);
	fail_unless(res == S_OK, "Could not create Shell instance.");

	// Folder.NameSpace: <http://msdn.microsoft.com/en-gb/library/windows/desktop/gg537721(v=vs.85).aspx>
	Folder *zip_folder = NULL;
	res = shell->NameSpace(zip_path_var, &zip_folder);
	printf("%d", res);
	fail_unless(zip_folder != NULL, "Could not create zip Folder object.");

	Folder *destination_folder = NULL;
	shell->NameSpace(destination_dir_var, &destination_folder);
	fail_unless(destination_folder != NULL, "Could not create destination Folder object.");

	FolderItems *zip_folderitems = NULL;
	zip_folder->Items(&zip_folderitems);
	fail_unless(zip_folderitems != NULL, "Could not create zip FolderItems object.");

	VARIANT zip_idispatch_var;
	zip_idispatch_var.vt = VT_DISPATCH;
	zip_idispatch_var.pdispVal = NULL;
	zip_folderitems->QueryInterface(IID_IDispatch, (void **) &zip_idispatch_var.pdispVal);
	fail_unless(zip_idispatch_var.pdispVal != NULL, "Could not create IDispatch for zip FolderItems object.");

	// Folder.CopyHere: <http://msdn.microsoft.com/en-us/library/ms723207(v=vs.85).aspx>
	//    (4) Do not display a progress dialog box.
	//   (16) Respond with "Yes to All" for any dialog box that is displayed.
	//  (256) Display a progress dialog box but do not show the file names.
	//  (512) Do not confirm the creation of a new directory if the operation requires one to be created.
	// (1024) Do not display a user interface if an error occurs.
	VARIANT options_var;
	options_var.vt = VT_I4;
	options_var.lVal = 16 | 256 | 512 | 1024;

	res = destination_folder->CopyHere(zip_idispatch_var, options_var);
	fail_unless(res == S_OK, "Could not extract zip file contents to destination directory.");

	// We don't bother to free/release stuff unless we succeed, since we exit on failure.

	// SysFreeString: <http://msdn.microsoft.com/en-gb/library/windows/desktop/ms221481(v=vs.85).aspx>
	SysFreeString(zip_path_var.bstrVal);
	SysFreeString(destination_dir_var.bstrVal);
	zip_idispatch_var.pdispVal->Release();
	zip_folderitems->Release();
	destination_folder->Release();
	zip_folder->Release();
	shell->Release();

	// CoUninitialize: <http://msdn.microsoft.com/en-us/library/windows/desktop/ms688715(v=vs.85).aspx>
	CoUninitialize();
}

void fail(char *s) {
	// TODO: show dialog box
	puts(s);
	exit(1);
}
