#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#include <shlwapi.h>
#include <cstdlib>
#include <cstdio>

#include "..\RadReadConsole.h"

#define ARRAY_X(a) (a), ARRAYSIZE(a)

inline WORD GetConsoleTextAttribute(const HANDLE h)
{
    CONSOLE_SCREEN_BUFFER_INFO bi = {};
    GetConsoleScreenBufferInfo(h, &bi);
    return bi.wAttributes;
}

// Defined incorrectly in consoleapi3.h
inline BOOL AddConsoleAlias(
    _In_ LPCTSTR Source,
    _In_ LPCTSTR Target,
    _In_ LPCTSTR ExeName
)
{
    return AddConsoleAlias(const_cast<LPTSTR>(Source), const_cast<LPTSTR>(Target), const_cast<LPTSTR>(ExeName));
}


int _tmain(const int argc, const TCHAR* const argv[])
{
    const HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    LPCTSTR strExeName = PathFindFileName(argv[0]);
    AddConsoleAlias(TEXT("x"), TEXT("exit"), strExeName);
    AddConsoleAlias(TEXT("test1"), TEXT("--- [$1] [$2] [$3] [$4] [$5] [$6] [$7] [$8] [$9]"), strExeName);
    AddConsoleAlias(TEXT("test2"), TEXT("--- $G $L $B $T $$"), strExeName);
    AddConsoleAlias(TEXT("test3"), TEXT("--- $$ $$ $$"), strExeName);
    AddConsoleAlias(TEXT("test4"), TEXT("--- [$*]"), strExeName);

    SetEnvironmentVariable(TEXT("RAD_HISTORY_PIPE"), TEXT(R"(fzf --tac --height=~10 --exact --scheme=history)"));

    typedef BOOL(*ReadConsoleT)(
        _In_ HANDLE hConsoleInput,
        _Out_writes_bytes_to_(nNumberOfCharsToRead * sizeof(TCHAR), *lpNumberOfCharsRead * sizeof(TCHAR%)) LPVOID lpBuffer,
        _In_ DWORD nNumberOfCharsToRead,
        _Out_ _Deref_out_range_(<= , nNumberOfCharsToRead) LPDWORD lpNumberOfCharsRead,
        _In_opt_ PCONSOLE_READCONSOLE_CONTROL pInputControl
        );

    ReadConsoleT pReadConsole = RadReadConsole;
    LPCTSTR prompt = TEXT("R> ");

    while (true)
    {
        WriteConsole(hOutput, prompt, DWORD(_tcslen(prompt)), nullptr, nullptr);

        TCHAR buffer[128] = TEXT("Filled_in_the_buffer_before");
        DWORD read = 0;

        CONSOLE_READCONSOLE_CONTROL ctrl = { sizeof(CONSOLE_READCONSOLE_CONTROL) };
        //ctrl.nInitialChars = 2;
        ctrl.dwCtrlWakeupMask = 1 << '\t';

        if (!pReadConsole(hInput, ARRAY_X(buffer), &read, &ctrl))
        {
            const DWORD error = GetLastError();
            fprintf(stderr, "Error: %08X\n", error);
        }

        if (_tcsncmp(buffer, TEXT("exit"), 4) == 0)
            break;
        else if (_tcsncmp(buffer, TEXT("switch"), 4) == 0)
        {
            if (pReadConsole == RadReadConsole)
            {
                pReadConsole = ReadConsole;
                prompt = TEXT("W> ");
            }
            else
            {
                pReadConsole = RadReadConsole;
                prompt = TEXT("R> ");
            }
        }
        else
        {
            const WORD wAttributes = GetConsoleTextAttribute(hOutput);
            SetConsoleTextAttribute(hOutput, FOREGROUND_RED);
            WriteConsole(hOutput, buffer, read, nullptr, nullptr);
            SetConsoleTextAttribute(hOutput, wAttributes);
        }
    }
    return EXIT_SUCCESS;
}
