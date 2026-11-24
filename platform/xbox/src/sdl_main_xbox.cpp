// Xbox SDL3 compatibility layer: the title's entry point.
//
// The RXDK startup calls main() without arguments, so main can't take the
// usual argc/argv. As with SDL on other platforms, the game's main is renamed
// to SDL_main (SDL_main.h) and called from here with a proper argv. When the
// game returns, the console goes back to the dashboard.
//
// The console has no command line: arguments can come from D:\smw_args.txt on
// the disc instead (separated by spaces or new lines; "quotes" keep spaces in
// one). A test build puts one there (the determinism test, src/smw/Determinism.h).
//
// A crash (an exception nothing handled: a bad pointer, a division by zero; or a C++ exception
// nobody caught) shows the crash screen (sdlx_fatal): what happened and where, until a button.

#include <xtl.h>

#include "sdl_internal.h"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

extern "C" int SDL_main(int argc, char* argv[]);

namespace {
// The arguments in the file, if there is one
std::vector<std::string> argsFromFile(const char* path)
{
    std::vector<std::string> args;
    FILE* file = fopen(path, "rb");
    if (!file)
        return args;
    std::string text;
    char buf[512];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0)
        text.append(buf, n);
    fclose(file);

    std::string arg;
    bool quoted = false, any = false;
    for (char c : text) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
            if (any)
                args.push_back(arg);
            arg.clear();
            any = false;
        } else {
            arg += c;
            any = true;
        }
    }
    if (any)
        args.push_back(arg);
    return args;
}
const char* exceptionName(DWORD code)
{
    switch (code) {
        case 0xC0000005: return "Access violation (a bad pointer)";
        case 0xC0000094: return "Integer division by zero";
        case 0xC0000095: return "Integer overflow";
        case 0xC00000FD: return "Stack overflow";
        case 0xC000001D: return "Illegal instruction";
        case 0xC0000096: return "Privileged instruction";
        case 0xC0000017: return "Out of memory";
        case 0xC000008C: return "Array bounds exceeded";
        case 0xC000008E: return "Float division by zero";
        case 0xC0000090: return "Invalid float operation";
        case 0xC0000091: return "Float overflow";
        case 0x80000003: return "Breakpoint";
        case 0x80000002: return "Misaligned data";
        default: return "Exception";
    }
}

// An exception nothing handled, on any thread: the crash screen (it never returns)
LONG __attribute__((__stdcall__)) onUnhandledException(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
    const CONTEXT* context = info ? info->ContextRecord : nullptr;
    const DWORD code = record ? static_cast<DWORD>(record->ExceptionCode) : 0;

    char title[64];
    snprintf(title, sizeof(title), "%s", exceptionName(code));
    char message[512];
    int n = snprintf(message, sizeof(message), "Code %08lX at %08lX (in the game's XBE: look it up in its map file)",
                     static_cast<unsigned long>(code), record ? reinterpret_cast<unsigned long>(record->ExceptionAddress) : 0ul);
    if (code == 0xC0000005 && record && record->NumberParameters >= 2) {
        const ULONG_PTR kind = record->ExceptionInformation[0];
        n += snprintf(message + n, sizeof(message) - n, "\n%s address %08lX%s",
                      kind == 1 ? "Writing to" : kind == 8 ? "Running code at" : "Reading from",
                      static_cast<unsigned long>(record->ExceptionInformation[1]),
                      record->ExceptionInformation[1] < 0x10000 ? " (a null pointer)" : "");
    }
    if (context && n > 0 && n < static_cast<int>(sizeof(message))) {
        snprintf(message + n, sizeof(message) - n, "\nThread %lu, stack %08lX, frame %08lX", GetCurrentThreadId(),
                 static_cast<unsigned long>(context->Esp), static_cast<unsigned long>(context->Ebp));
    }
    sdlx_fatal(title, message);
}

// A C++ exception nobody caught (the game's main catches its own: this is another thread's)
[[noreturn]] void onTerminate()
{
    const char* what = "A C++ exception nobody caught";
    std::string text;
    if (std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            text = ex.what();
        } catch (...) {
            text = "(not a std::exception)";
        }
    } else {
        what = "std::terminate called";
    }
    sdlx_fatal(what, text.c_str());
}
} // namespace

int main(void)
{
    SetUnhandledExceptionFilter(onUnhandledException);
    std::set_terminate(onTerminate);

    static char arg0[] = "D:\\default.xbe";
    std::vector<std::string> fileArgs = argsFromFile("D:\\smw_args.txt");
    std::vector<char*> argv {arg0};
    for (size_t k = 0; k < fileArgs.size(); k++) {
        //(the layer's own: the back buffers, 1 or 2; not the game's)
        if (fileArgs[k] == "--xbox-buffers" && k + 1 < fileArgs.size()) {
            g_sdlx_wanted_back_buffers = fileArgs[k + 1] == "2" ? 2 : 1;
            k++;
            continue;
        }
        argv.push_back(fileArgs[k].data());
    }
    if (!fileArgs.empty())
        sdlx_log("[xbox] %d arguments from D:\\smw_args.txt\n", static_cast<int>(fileArgs.size()));
    argv.push_back(nullptr);

    sdlx_set_fpu_precision();

    // The network starts now: its address (DHCP) comes while the menus are up
    SDL_XboxStartNetwork();

    const int result = SDL_main(static_cast<int>(argv.size()) - 1, argv.data());
    sdlx_log("[xbox] The game exited with %d\n", result);

    SDL_XboxReturnToDashboard();
    return result;
}
