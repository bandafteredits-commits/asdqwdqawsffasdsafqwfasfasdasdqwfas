// cplatency_inject.exe - loads cplatency.dll into Minecraft Bedrock (Minecraft.Windows.exe).
// Exit code: 0 = injected, 1 = Minecraft is not running, 2 = injection failed.
// Same folder as CheesyProxxy.exe. Does not remove or replace any other DLL in the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")

static DWORD FindPid(const wchar_t* name) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e = { sizeof(e) };
    DWORD pid = 0;
    if (Process32FirstW(s, &e)) do { if (!_wcsicmp(e.szExeFile, name)) { pid = e.th32ProcessID; break; } } while (Process32NextW(s, &e));
    CloseHandle(s);
    return pid;
}

// UWP (Minecraft Bedrock) can only load DLLs that "ALL APPLICATION PACKAGES" may read/execute.
static void GrantAppPackages(const wchar_t* path) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &sid)) return;
    EXPLICIT_ACCESSW ea = {};
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = (LPWSTR)sid;
    PACL oldAcl = nullptr, newAcl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &sd) == ERROR_SUCCESS) {
        if (SetEntriesInAclW(1, &ea, oldAcl, &newAcl) == ERROR_SUCCESS) {
            SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr);
            LocalFree(newAcl);
        }
        LocalFree(sd);
    }
    LocalFree(sid);
}

int wmain() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t* slash = wcsrchr(self, L'\\');
    if (slash) *(slash + 1) = 0;
    wchar_t src[MAX_PATH];
    wcscpy_s(src, self);
    wcscat_s(src, L"cplatency.dll");

    DWORD pid = FindPid(L"Minecraft.Windows.exe");
    if (!pid) return 1;

    const wchar_t* dir = L"C:\\Users\\Public\\CheesyProxxy";
    CreateDirectoryW(dir, nullptr);
    wchar_t dst[MAX_PATH];
    wcscpy_s(dst, dir);
    wcscat_s(dst, L"\\cplatency.dll");
    if (!CopyFileW(src, dst, FALSE)) { if (GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES) return 2; }
    GrantAppPackages(dir);
    GrantAppPackages(dst);

    HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!p) return 2;
    SIZE_T bytes = (wcslen(dst) + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(p, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    int rc = 2;
    if (mem && WriteProcessMemory(p, mem, dst, bytes, nullptr)) {
        FARPROC ll = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HANDLE t = CreateRemoteThread(p, nullptr, 0, (LPTHREAD_START_ROUTINE)ll, mem, 0, nullptr);
        if (t) {
            WaitForSingleObject(t, 8000);
            DWORD code = 0;
            GetExitCodeThread(t, &code);
            if (code != 0) rc = 0; // LoadLibraryW returned a module handle (low 32 bits non-zero)
            CloseHandle(t);
        }
    }
    if (mem) VirtualFreeEx(p, mem, 0, MEM_RELEASE);
    CloseHandle(p);
    return rc;
}
