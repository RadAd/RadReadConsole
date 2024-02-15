#pragma once

#ifdef __cplusplus
extern "C" {
#endif

BOOL RadWriteConsole(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nNumberOfCharsToWrite) CONST VOID* lpBuffer,
    _In_ DWORD nNumberOfCharsToWrite,
    _Out_opt_ LPDWORD lpNumberOfCharsWritten,   // Could be more than nNumberOfCharsToWrite due to double-width characters
    _Reserved_ LPVOID lpReserved
);

BOOL RadWriteConsoleOutputCharacter(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nLength) LPCWSTR lpCharacter,
    _In_ DWORD nLength,
    _In_ COORD dwWriteCoord,
    _Out_ LPDWORD lpNumberOfCharsWritten        // Could be more than nNumberOfCharsToWrite due to double-width characters
);

BOOL RadReadConsole(
    _In_ HANDLE hConsoleInput,
    _Inout_updates_bytes_to_(nNumberOfCharsToRead * sizeof(TCHAR), *lpNumberOfCharsRead * sizeof(TCHAR%)) LPVOID lpBuffer,
    _In_ DWORD nNumberOfCharsToRead,
    _Out_ _Deref_out_range_(<= , nNumberOfCharsToRead) LPDWORD lpNumberOfCharsRead,
    _In_opt_ PCONSOLE_READCONSOLE_CONTROL pInputControl
);

#ifdef __cplusplus
}
#endif
