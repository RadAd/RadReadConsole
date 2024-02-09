#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
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

int _tmain(const int argc, const TCHAR* const argv[])
{
    const HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);

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
        DWORD written = 0;
        WriteConsole(hOutput, prompt, DWORD(_tcslen(prompt)), &written, nullptr);

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
            WriteConsole(hOutput, buffer, read, &written, nullptr);
            SetConsoleTextAttribute(hOutput, wAttributes);
        }
    }
    return EXIT_SUCCESS;
}
