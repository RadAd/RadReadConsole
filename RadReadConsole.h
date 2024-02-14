#pragma once

#ifdef __cplusplus
extern "C" {
#endif

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
