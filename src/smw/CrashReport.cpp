#include "CrashReport.h"

// A crash on Windows (the desktop build): its exception, or an abort (a failed assert, the debug
// runtime's checks), and the stack, with the functions and the lines the .pdb knows (a RelWithDebInfo
// or Debug build; Release: the modules and offsets), to stderr, before the process ends. The rest
// of the log (stdout) is flushed first.
#if defined(_MSC_VER) && defined(_M_X64) && !defined(SMW_XBOX)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <crtdbg.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace {
// The stack from `context` (this thread's)
void printStack(CONTEXT context)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    STACKFRAME64 frame {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 48; depth++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
            || frame.AddrPC.Offset == 0)
            break;
        const DWORD64 pc = frame.AddrPC.Offset;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] {};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 offset = 0;
        const bool named = SymFromAddr(process, pc, &offset, symbol);
        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineOffset = 0;
        const bool lined = SymGetLineFromAddr64(process, pc, &lineOffset, &line);
        char module[MAX_PATH] = "?";
        HMODULE handle = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(pc), &handle))
            GetModuleFileNameA(handle, module, MAX_PATH);
        const char* moduleName = strrchr(module, '\\') ? strrchr(module, '\\') + 1 : module;
        if (named && lined)
            fprintf(stderr, "  #%d %s!%s  %s:%lu\n", depth, moduleName, symbol->Name, line.FileName, line.LineNumber);
        else if (named)
            fprintf(stderr, "  #%d %s!%s+0x%llx\n", depth, moduleName, symbol->Name, static_cast<unsigned long long>(offset));
        else
            fprintf(stderr, "  #%d %s+0x%llx\n", depth, moduleName,
                static_cast<unsigned long long>(pc - reinterpret_cast<DWORD64>(handle)));
    }
    fflush(stderr);
}

LONG WINAPI reportCrash(EXCEPTION_POINTERS* info)
{
    fflush(stdout);
    fprintf(stderr, "\ncrash: exception 0x%08lX at %p, thread %lu\n", info->ExceptionRecord->ExceptionCode,
        info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        fprintf(stderr, "crash: %s address %p\n", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
    printStack(*info->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
}

// abort() (a failed assert, the debug runtime's checks): the stack there
void reportAbort(int)
{
    fflush(stdout);
    fprintf(stderr, "\ncrash: abort, thread %lu\n", GetCurrentThreadId());
    CONTEXT context {};
    RtlCaptureContext(&context);
    printStack(context);
    _exit(3);
}
} // namespace

void installCrashReport()
{
    SetUnhandledExceptionFilter(reportCrash);
    signal(SIGABRT, reportAbort);
    //(the debug runtime's asserts and checks: to stderr, not a dialog waiting for a click)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    const int kinds[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
    for (const int kind : kinds) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
}

#else

void installCrashReport() {}

#endif
