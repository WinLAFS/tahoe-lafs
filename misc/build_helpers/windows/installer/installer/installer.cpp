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
unsigned __int64 read_uint64_le(unsigned char *b);
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

void noop_handler(const wchar_t * expression,
                  const wchar_t * function,
                  const wchar_t * file,
                  unsigned int line,
                  uintptr_t pReserved) {
}

int wmain(int argc, wchar_t *argv[]) {
	_set_invalid_parameter_handler(noop_handler);

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

	// shell32's zipped folder implementation is strict about the zip format and
	// does not support unzipping a self-extracting exe directly. So we copy the
	// original zip file that was appended to the exe to a temporary directory,
	// and use shell32 to unzip it from there. To get the length of the zip file,
	// we look at its "end of central directory record" (not to be confused with
	// a "Zip64 end of central directory record"), which is documented at
	// <http://www.pkware.com/documents/casestudies/APPNOTE.TXT>.
	// For simplicity we only handle the case of a zip file that has no archive
	// comment. This code is based loosely on the _EndRecData function in
	// <https://hg.python.org/cpython/file/2.7/Lib/zipfile.py>.

	// APPNOTE.TXT sections 4.3.15 and 4.3.16.
	const size_t sizeof_zip64eocdl = 20,
		         sizeof_eocd = 22;
	unsigned char end_data[sizeof_zip64eocdl + sizeof_eocd];
	unsigned char zip64eocdl_signature[]  = {0x50, 0x4B, 0x06, 0x07};
	unsigned char eocd_signature[]        = {0x50, 0x4B, 0x05, 0x06};
	unsigned char comment_length[]        = {0x00, 0x00};
	unsigned char zip64eocdl_disk_num[]   = {0x00, 0x00, 0x00, 0x00};
	unsigned char zip64eocdl_disk_count[] = {0x01, 0x00, 0x00, 0x00};

	errno = 0;
	FILE *f = _wfopen(L"C:\\tahoe\\foo.zip", L"rb");
	fail_unless(f != NULL && errno == 0 && ferror(f) == 0,
		        "Could not open executable file.");

	_fseeki64(f, -(__int64) sizeof(end_data), SEEK_END);
	fail_unless(errno == 0 && ferror(f) == 0,
		        "Could not seek to end records.");

	__int64 zip64eocdl_offset = _ftelli64(f);
	fail_unless(errno == 0 && ferror(f) == 0 && zip64eocdl_offset >= 0,
		        "Could not read position of end records.");

	printf("zip64eocdl_offset = %ld\n", zip64eocdl_offset);
	size_t n = fread(end_data, sizeof(end_data), 1, f);
	printf("n = %ld\n", n);
	for (size_t i = 0; i < sizeof(end_data); i++) {
		printf("%02X ", end_data[i]);
	}
	printf("\n");
	fail_unless(n == 1 && errno == 0 && ferror(f) == 0,
		        "Could not read end records.");

	fail_unless(memcmp(end_data + sizeof(end_data) - sizeof(comment_length),
		               comment_length, sizeof(comment_length)) == 0,
		        "Cannot read a zip file that has an archive comment.");

	unsigned char *eocd = end_data + sizeof_zip64eocdl;
	fail_unless(memcmp(eocd, eocd_signature, sizeof(eocd_signature)) == 0,
		        "Could not find the end-of-central-directory signature.");

	fail_unless(memcmp(end_data, zip64eocdl_signature, sizeof(zip64eocdl_signature)) == 0,
		        "Could not find the zip64-end-of-central-directory-locator signature.");

	fail_unless(memcmp(eocd + 4, zip64eocdl_disk_num, sizeof(zip64eocdl_disk_num)) == 0 &&
		        memcmp(eocd + 6, zip64eocdl_disk_num, sizeof(zip64eocdl_disk_num)) == 0 &&
		        memcmp(end_data + 4, zip64eocdl_disk_count, sizeof(zip64eocdl_disk_count)) == 0,
		        "Cannot read a zipfile that spans disks.");

    unsigned __int64 eocd_relative_offset = read_uint64_le(end_data + 8);
	unsigned __int64 eocd_offset = zip64eocdl_offset + sizeof_zip64eocdl;
	fail_unless(eocd_relative_offset <= 0x7FFFFFFFFFFFFFFFi64 && eocd_offset <= 0x7FFFFFFFFFFFFFFFi64,
		        "Could not calculate zipfile offset due to potential integer overflow.");

	__int64 zipfile_offset = eocd_offset - eocd_relative_offset;
	fail_unless(zipfile_offset >= 0 && zipfile_offset <= zip64eocdl_offset,
		        "Unexpected result from zipfile offset calculation.");

	printf("zipfile_offset = %ld\n", zipfile_offset);
	_fseeki64(f, zipfile_offset, SEEK_SET); 
	fail_unless(errno == 0 && ferror(f) == 0,
		        "Could not seek to zipfile offset.");

	printf("%ld\n", zipfile_offset);
	//unzip(L"C:\\tahoe\\foo.zip", destination_dir);
}

// read unsigned little-endian 64-bit integer
unsigned __int64 read_uint64_le(unsigned char *b) {
	return ((unsigned __int64) b[0]      ) |
		   ((unsigned __int64) b[1] <<  8) |
		   ((unsigned __int64) b[2] << 16) |
		   ((unsigned __int64) b[3] << 24) |
		   ((unsigned __int64) b[4] << 32) |
		   ((unsigned __int64) b[5] << 40) |
		   ((unsigned __int64) b[6] << 48) |
		   ((unsigned __int64) b[7] << 56);
}

void read_from_end(FILE *f, size_t offset, unsigned char *dest, size_t length) {
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
	// Based on <https://social.msdn.microsoft.com/Forums/vstudio/en-US/45668d18-2840-4887-87e1-4085201f4103/visual-c-to-unzip-a-zip-file-to-a-specific-directory?forum=vclanguage>

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
	fail_unless(res == S_OK && zip_folder != NULL, "Could not create zip Folder object.");

	Folder *destination_folder = NULL;
	res = shell->NameSpace(destination_dir_var, &destination_folder);
	fail_unless(res == S_OK && destination_folder != NULL, "Could not create destination Folder object.");

	FolderItems *zip_folderitems = NULL;
	zip_folder->Items(&zip_folderitems);
	fail_unless(zip_folderitems != NULL, "Could not create zip FolderItems object.");

	long files_count = 0;
	zip_folderitems->get_Count(&files_count);
	printf("count %d\n", files_count);

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
