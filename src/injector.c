/* Start Pivot suspended, inject pivotkit.dll, then resume the process. */
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void die(const char* msg)
{
    fprintf(stderr, "pivotkit-loader: %s (error %lu)\n", msg, GetLastError());
    ExitProcess(1);
}

static int inject_dll(HANDLE hProc, const char* dllPath)
{
    size_t len = strlen(dllPath) + 1;
    void* remote = VirtualAllocEx(hProc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return 0;

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProc, remote, dllPath, len, &written)) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        return 0;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    FARPROC loadLib = GetProcAddress(k32, "LoadLibraryA");
    HANDLE hThread = CreateRemoteThread(hProc, NULL, 0,
                        (LPTHREAD_START_ROUTINE)loadLib, remote, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        return 0;
    }
    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    return 1;
}

int main(int argc, char** argv)
{
    char pivotDir[MAX_PATH];
    char pivotPath[MAX_PATH];
    char dllPath[MAX_PATH];

    GetModuleFileNameA(NULL, pivotPath, MAX_PATH);
    {
        char* slash = strrchr(pivotPath, '\\');
        if (slash) *slash = '\0';
    }
    strncpy(pivotDir, pivotPath, MAX_PATH);

    snprintf(pivotPath, MAX_PATH, "%s\\pivot.exe", pivotDir);
    snprintf(dllPath, MAX_PATH, "%s\\pivotkit.dll", pivotDir);

    /* argv[1] may override the default Pivot path. */
    if (argc >= 2 && strstr(argv[1], ".exe")) {
        snprintf(pivotPath, MAX_PATH, "%s", argv[1]);
        {
            char* slash = strrchr(pivotPath, '\\');
            if (slash) *slash = '\0';
        }
        strncpy(pivotDir, pivotPath, MAX_PATH);
        snprintf(pivotPath, MAX_PATH, "%s", argv[1]);
    }

    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "-console") == 0) {
            SetEnvironmentVariableA("PIVOTKIT_CONSOLE", "1");
            break;
        }
    }

    if (GetFileAttributesA(pivotPath) == INVALID_FILE_ATTRIBUTES)
        die("pivot.exe not found next to loader");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(pivotPath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, pivotDir, &si, &pi))
        die("could not start pivot.exe");

    if (!inject_dll(pi.hProcess, dllPath)) {
        TerminateProcess(pi.hProcess, 1);
        die("injection failed");
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    printf("pivotkit: injected %s into %s\n", dllPath, pivotPath);
    return 0;
}
