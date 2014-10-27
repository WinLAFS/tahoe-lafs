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
wchar_t * get_default_destination_dir();
void initialize_com();
void self_extract(wchar_t *destination_dir);
void unzip_from_executable(wchar_t *executable_path, wchar_t *destination_dir);
size_t read_uint32_le(unsigned char *b);
void unzip(wchar_t *zip_path, wchar_t *destination_dir);
bool have_acceptable_python();
void install_python(wchar_t *python_installer_dir);
void execute(wchar_t *command, wchar_t *arguments, wchar_t *stdout_buf, size_t stdout_size);

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

	initialize_com();

	self_extract(destination_dir);
	//unzip(L"C:\\tahoe\\allmydata-tahoe-1.10.0c1.zip", destination_dir);

	if (!have_acceptable_python()) {
		install_python(destination_dir);
	}
	//unlink(python_installer);
	return 0;
}

wchar_t * get_default_destination_dir() {
	// TODO: get Program Files directory from the registry
	return L"C:\\tahoe\\windowstest";
}

void initialize_com() {
	// CoInitializeEx: <http://msdn.microsoft.com/en-gb/library/windows/desktop/ms695279(v=vs.85).aspx>
	HRESULT res = CoInitializeEx(NULL, 0);
	fail_unless(res == S_OK || res == S_FALSE, "Could not initialize COM.");

	// Despite what the documentation says, we don't need to call CoUninitialize because
	// any resources will be cleaned up on process exit, and there will be no pending COM
	// messages at that point.
}

void self_extract(wchar_t *destination_dir) {
	wchar_t executable_path[MAX_PATH];

	HMODULE hModule = GetModuleHandle(NULL);
    fail_unless(hModule != NULL, "Could not get the module handle.");
    GetModuleFileNameW(hModule, executable_path, MAX_PATH); 
    fail_unless(GetLastError() == ERROR_SUCCESS, "Could not get the path of the current executable.");

	unzip_from_executable(executable_path, destination_dir);
}

void unzip_from_executable(wchar_t *executable_path, wchar_t *destination_dir) {
	// shell32's zipped folder implementation is strict about the zip format and
	// does not support unzipping a self-extracting exe directly. So we copy the
	// original zip file that was appended to the exe to a temporary directory,
	// and use shell32 to unzip it from there. To get the length of the zip file,
	// we look at its "end of central directory record", which is documented at
	// <http://www.pkware.com/documents/casestudies/APPNOTE.TXT>.
	// For simplicity we only handle the case of a zip file that has no archive
	// comment, that does not use disk spanning, and that does not have a
	// "Zip64 end of central directory record".

	// APPNOTE.TXT section 4.3.16.
	const size_t sizeof_eocd = 22;
	unsigned char end_data[sizeof_eocd];
	unsigned char eocd_signature[] = {0x50, 0x4B, 0x05, 0x06};
	unsigned char comment_length[] = {0x00, 0x00};
	unsigned char disk_num[] = {0x00, 0x00};

	errno = 0;
	FILE *f = _wfopen(L"C:\\tahoe\\foo.zip", L"rb");
	fail_unless(f != NULL && errno == 0 && ferror(f) == 0,
		        "Could not open executable file.");

	fseek(f, -(off_t) sizeof_eocd, SEEK_END);
	fail_unless(errno == 0 && ferror(f) == 0,
		        "Could not seek to end-of-central-directory record.");

	__int64 eocd_offset = _ftelli64(f);
	fail_unless(errno == 0 && ferror(f) == 0 && eocd_offset >= 0,
		        "Could not read position of end-of-central-directory record.");
	fail_unless(eocd_offset + sizeof_eocd <= 0xFFFFFFFFi64,
		        "Cannot read an executable file >= 4 GiB.");

	size_t n = fread(end_data, sizeof(end_data), 1, f);
	fail_unless(n == 1 && errno == 0 && ferror(f) == 0,
		        "Could not read end records.");

	fail_unless(memcmp(end_data + sizeof(end_data) - sizeof(comment_length),
		               comment_length, sizeof(comment_length)) == 0,
		        "Cannot read a zip file that has an archive comment.");

	fail_unless(memcmp(end_data, eocd_signature, sizeof(eocd_signature)) == 0,
		        "Could not find the end-of-central-directory signature.");

	fail_unless(memcmp(end_data + 4, disk_num, sizeof(disk_num)) == 0 &&
		        memcmp(end_data + 6, disk_num, sizeof(disk_num)) == 0,
		        "Cannot read a zipfile that spans disks.");

	size_t cd_length = read_uint32_le(end_data + 12);
	size_t cd_offset = read_uint32_le(end_data + 16);
	__int64 zip_length = (__int64) cd_offset + cd_length + sizeof_eocd;
	fail_unless(zip_length <= 0x7FFFFFFFi64,
	            "Cannot copy a zip file >= 2 GiB.");
	fseek(f, -(off_t) zip_length, SEEK_END);
	fail_unless(errno == 0 && ferror(f) == 0,
		        "Could not seek to start of embedded zip file.");

	const wchar_t tmp_filename[] = L"tahoe-lafs.zip"; // FIXME make this more unique.
	wchar_t tmp_path[MAX_PATH];
	DWORD len = GetTempPathW(MAX_PATH, tmp_path);
	fail_unless(len > 0, "Could not obtain temporary directory path.");
	fail_unless(len < MAX_PATH - wcslen(tmp_filename), "Temporary directory path is too long.");
	wcscpy(tmp_path + len, tmp_filename);

	FILE *tmp_file = _wfopen(tmp_path, L"wbTD"); // TD => short-lived temporary file
	unsigned char buf[16384];
	size_t remaining_length = (size_t) zip_length;
	while (remaining_length > 0) {
		size_t chunk_length = min(remaining_length, sizeof(buf));
		n = fread(buf, chunk_length, 1, f);
		fail_unless(n == 1 && errno == 0 && ferror(f) == 0,
		            "Could not read from executable file.");
		fwrite(buf, chunk_length, 1, tmp_file);
		fail_unless(n == 1 && errno == 0 && ferror(f) == 0,
		            "Could not write to temporary file.");
		remaining_length -= chunk_length;
	}
	fclose(tmp_file);
	fclose(f);

	unzip(tmp_path, destination_dir);
}

// read unsigned little-endian 32-bit integer
size_t read_uint32_le(unsigned char *b) {
	return ((size_t) b[0]      ) |
		   ((size_t) b[1] <<  8) |
		   ((size_t) b[2] << 16) |
		   ((size_t) b[3] << 24);
}

void unzip(wchar_t *zip_path, wchar_t *destination_dir) {
	// Based loosely on
	// <https://social.msdn.microsoft.com/Forums/vstudio/en-US/45668d18-2840-4887-87e1-4085201f4103/visual-c-to-unzip-a-zip-file-to-a-specific-directory?forum=vclanguage>.

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

	// CoCreateInstance: <http://msdn.microsoft.com/en-gb/library/windows/desktop/ms686615(v=vs.85).aspx>
	IShellDispatch *shell;
	HRESULT res = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void **) &shell);
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

	VARIANT zip_idispatch_var;
	zip_idispatch_var.vt = VT_DISPATCH;
	zip_idispatch_var.pdispVal = NULL;
	zip_folderitems->QueryInterface(IID_IDispatch, (void **) &zip_idispatch_var.pdispVal);
	fail_unless(zip_idispatch_var.pdispVal != NULL, "Could not create IDispatch for zip FolderItems object.");

	// Folder.CopyHere: <http://msdn.microsoft.com/en-us/library/ms723207(v=vs.85).aspx>
	//   (16) Respond with "Yes to All" for any dialog box that is displayed.
	//  (256) Display a progress dialog box but do not show the file names.
	//  (512) Do not confirm the creation of a new directory if the operation requires one to be created.
	// (1024) Do not display a user interface if an error occurs.
	// These options are ignored on Windows XP.
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
}

bool have_acceptable_python() {
	printf("Checking for Python 2.7...");
	// SearchPathW: <http://msdn.microsoft.com/en-gb/library/windows/desktop/aa365527(v=vs.85).aspx>

	wchar_t python_exe_path[MAX_PATH];
	DWORD res = SearchPathW(NULL, L"python.exe", NULL, MAX_PATH, python_exe_path, NULL);
	if (res == 0 || res >= MAX_PATH) {
		return false;
	}
	//execute(python_exe_path, L"--version");
	//execute(python_exe_path, L"setup.py scriptsetup --allusers");
	HKEY environment_key;

	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment",
	                  0, KEY_QUERY_VALUE, &environment_key) != ERROR_SUCCESS) {
		return false;
	}
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
	                  0, KEY_QUERY_VALUE, &environment_key) != ERROR_SUCCESS) {
		return false;
	}

	return false;
}

void execute(wchar_t *command, wchar_t *arguments, wchar_t *stdout_buf, size_t stdout_size) {
	CreateProcessW(command, arguments, NULL, NULL, FALSE, 0, NULL, NULL, NULL, NULL);
}

void install_python(wchar_t *python_installer_dir) {
	wchar_t installer_wildcard[] = L"\\*.msi";
	if (python_installer_dir[wcslen(python_installer_dir)-1] == '\\') {
		wcscpy(installer_wildcard, L"*.msi");
	}
	wchar_t installer_pattern[MAX_PATH];
	fail_unless(wcslen(python_installer_dir) < MAX_PATH - wcslen(installer_wildcard),
	            "Could not construct pattern for Python installer.")
	wcscpy(installer_pattern, python_installer_dir);
	wcscat(installer_pattern, installer_wildcard);

	WIN32_FIND_DATA find_data;
	HANDLE search_handle = FindFirstFileW(installer_pattern, &find_data);
	fail_unless(search_handle != INVALID_HANDLE_VALUE,
	            "Could not find the Python installer.")

	fail_unless(wcslen(python_installer_dir) < MAX_PATH - wcslen(find_data.cFileName),
	            "Could not construct path to Python installer.")

	wchar_t installer_path[MAX_PATH];
	wcscpy(installer_path, python_installer_dir);
	wcscat(installer_path, find_data.cFileName);
	//execute(installer_path, L"");
}

void fail(char *s) {
	// TODO: show dialog box
	puts(s);
	exit(1);
}
