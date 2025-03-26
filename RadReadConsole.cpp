#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#include <crtdbg.h>
#include <shlwapi.h>

#include <string>
#include <deque>
#include <vector>
#include <memory>

#include "RadReadConsole.h"

#define ARRAY_X(a) (a), ARRAYSIZE(a)
#define BUFFER_X(p, s, o) (p) + (o), (*s) - (o)

#ifdef UNICODE
#define tmemmove wmemmove
#define tmemcpy wmemcpy
#define tmemset wmemset
#define tChar UnicodeChar
#define tstring wstring
#else
#define tmemmove memmove
#define tmemcpy memcpy
#define tmemset memset
#define tChar AsciiChar
#define tstring string
#endif

namespace
{
    class SaveConsoleMode
    {
    public:
        SaveConsoleMode(HANDLE hConsoleHandle)
            : m_hConsoleHandle(hConsoleHandle), m_dwMode(0)
        {
            if (!GetConsoleMode(m_hConsoleHandle, &m_dwMode))
                OutputDebugString(TEXT("Error GetConsoleMode\n"));
        }

        ~SaveConsoleMode()
        {
            if (!SetConsoleMode(m_hConsoleHandle, m_dwMode))
                OutputDebugString(TEXT("Error SetConsoleMode\n"));
        }

        DWORD mode() const { return m_dwMode; }

    private:
        HANDLE m_hConsoleHandle;
        DWORD m_dwMode;
    };

    struct HANDLE_Deleter
    {
        typedef HANDLE pointer;
        void operator()(HANDLE h) { CloseHandle(h); }
    };

    template <class T, class U>
    class unique_ptr_ptr
    {
    public:
        typedef std::unique_ptr<T, U> unique_ptr;

        unique_ptr_ptr(unique_ptr& up)
            : up(up), p(nullptr)
        {}

        ~unique_ptr_ptr()
        {
            up.reset(p);
        }

        operator typename unique_ptr::pointer* ()
        {
            return &p;
        }

    private:
        unique_ptr& up;
        typename unique_ptr::pointer p;
    };

    template<class T, class U>
    typename unique_ptr_ptr<T, U> operator&(std::unique_ptr<T, U>& up)
    {
        return unique_ptr_ptr<T, U>(up);
    }

    BOOL WriteFileANSI(HANDLE hFile, _In_NLS_string_(cchWideChar)LPCWCH lpWideCharStr, int cchWideChar = -1)
    {
        CHAR szAnsiCharStr[1024];
        int bytes = WideCharToMultiByte(CP_ACP, 0, lpWideCharStr, cchWideChar, szAnsiCharStr, ARRAYSIZE(szAnsiCharStr), nullptr, nullptr);
        DWORD dwWritten;
        return WriteFile(hFile, szAnsiCharStr, bytes, &dwWritten, NULL);
    }

    BOOL ReadFileANSI(HANDLE hFile, _Out_writes_to_opt_(cchWideChar, return) LPWCH lpWideCharStr, int cchWideChar, LPDWORD pchars)
    {
        CHAR szAnsiCharStr[1024];
        DWORD dwRead;
        if (!ReadFile(hFile, szAnsiCharStr, ARRAYSIZE(szAnsiCharStr), &dwRead, NULL))
            return FALSE;
        int chars = MultiByteToWideChar(CP_ACP, 0, szAnsiCharStr, dwRead, lpWideCharStr, cchWideChar);
        if (lpWideCharStr)
            lpWideCharStr[chars] = L'\0';
        if (pchars)
            *pchars = chars;
        return TRUE;
    }

std::deque<std::tstring> g_history;

std::vector<std::tstring> split(const std::tstring& str, const TCHAR delim)
{
    std::vector<std::tstring> result;
    size_t start = 0;

    for (size_t found = str.find(delim); found != std::tstring::npos; found = str.find(delim, start))
    {
        result.emplace_back(str.begin() + start, str.begin() + found);
        start = found + 1;
    }
    if (start != str.size())
        result.emplace_back(str.begin() + start, str.end());
    return result;
}

inline bool IsDoubleWidth(const TCHAR ch)
{
    switch (ch)
    {
    case TEXT('\0'):
    case TEXT('\r'):
    case TEXT('\n'):
        return false;
    default: return ch <= 26;
    }
}

inline DWORD GetPrintWidth(LPCTSTR lpStr, const DWORD begin, const DWORD end)
{
    _ASSERTE(begin <= end);
    DWORD w = 0;
    for (DWORD i = begin; i < end; ++i)
        w += IsDoubleWidth(lpStr[i]) ? 2 : 1;
    return w;
}

inline COORD GetConsoleCursorPosition(const HANDLE h)
{
    CONSOLE_SCREEN_BUFFER_INFO bi = {};
    GetConsoleScreenBufferInfo(h, &bi);
    return bi.dwCursorPosition;
}

inline SMALL_RECT GetConsoleWindow(const HANDLE h)
{
    CONSOLE_SCREEN_BUFFER_INFO bi = {};
    GetConsoleScreenBufferInfo(h, &bi);
    return bi.srWindow;
}

inline COORD Move(const HANDLE h, COORD p, SHORT d)
{
    CONSOLE_SCREEN_BUFFER_INFO bi = {};
    GetConsoleScreenBufferInfo(h, &bi);

    p.X += d;
    while (p.X < 0)
    {
        p.X += bi.dwSize.X;
        --p.Y;
    }
    while (p.X >= bi.dwSize.X)
    {
        p.X -= bi.dwSize.X;
        ++p.Y;
    }
    return p;
}

inline DWORD StrFind(LPCTSTR lpStr, LPDWORD lpLength, DWORD offset, TCHAR ch)
{
    _ASSERTE(offset <= *lpLength);
    while (offset < *lpLength && lpStr[offset] != ch)
        ++offset;
    return offset;
}

inline void StrErase(LPTSTR lpStr, LPDWORD lpLength, DWORD offset, DWORD length)
{
    _ASSERTE(offset <= *lpLength);
    _ASSERTE((offset + length) <= *lpLength);
    tmemmove(lpStr + offset, BUFFER_X(lpStr, lpLength, offset + length));
    *lpLength -= length;
}

inline void StrInsert(LPTSTR lpStr, LPDWORD lpLength, DWORD offset, TCHAR ch)
{
    _ASSERTE(offset <= *lpLength);
    tmemmove(lpStr + offset + 1, BUFFER_X(lpStr, lpLength, offset));
    lpStr[offset] = ch;
    ++(*lpLength);
}

inline DWORD StrInsert(LPTSTR lpStr, LPDWORD lpLength, DWORD offset, LPCTSTR lpInsert)
{
    _ASSERTE(offset <= *lpLength);
    const DWORD length = DWORD(_tcslen(lpInsert));
    tmemmove(lpStr + offset + length, BUFFER_X(lpStr, lpLength, offset));
    tmemcpy(lpStr + offset, lpInsert, length);
    *lpLength += length;
    return length;
}

inline void StrOverwrite(LPTSTR lpStr, LPDWORD lpLength, DWORD offset, TCHAR ch)
{
    _ASSERTE(offset <= *lpLength);
    lpStr[offset] = ch;
    if (offset >= *lpLength)
        *lpLength = offset + 1;
}

inline void StrAppend(LPTSTR lpStr, LPDWORD lpLength, LPCTSTR text)
{
    int i = 0;
    while (text[i] != TEXT('\0'))
        lpStr[(*lpLength)++] = text[i++];
}

inline void StrCopy(LPTSTR lpStr, LPDWORD lpLength, LPCTSTR text, DWORD length)
{
    tmemcpy(lpStr, text, length);
    *lpLength = length;
}

inline DWORD StrFindPrev(LPTSTR lpStr, LPDWORD lpLength, DWORD offset, LPCTSTR find)
{
    _ASSERTE(offset <= *lpLength);
    _ASSERTE(offset > 0);

    while (offset > 0 && _tcschr(TEXT(" "), lpStr[offset - 1]) != nullptr)
        --offset;

    if (offset > 0 && _tcschr(find, lpStr[offset - 1]) != nullptr)
    {
        while (offset > 0 && _tcschr(find, lpStr[offset - 1]) != nullptr)
            --offset;
    }
    else
    {
        while (offset > 0 && lpStr[offset - 1] != TEXT(' ') && _tcschr(find, lpStr[offset - 1]) == nullptr)
            --offset;
    }
    return offset;
}

inline DWORD StrFindNext(LPTSTR lpStr, LPDWORD lpLength, DWORD offset, LPCTSTR find)
{
    _ASSERTE(offset <= *lpLength);

    if (offset <= *lpLength && _tcschr(find, lpStr[offset]) != nullptr)
    {
        while (offset < *lpLength && _tcschr(find, lpStr[offset]) != nullptr)
            ++offset;
    }
    else
    {
        while (offset < *lpLength && lpStr[offset] != TEXT(' ') && _tcschr(find, lpStr[offset]) == nullptr)
            ++offset;
    }

    while (offset < *lpLength && _tcschr(TEXT(" "), lpStr[offset]) != nullptr)
        ++offset;

    return offset;
}

inline SHORT ScreenMoveCursor(HANDLE hOutput, LPCTSTR lpCharBuffer, LPDWORD poffset, const DWORD newoffset)
{
    const SHORT diff = newoffset < *poffset
        ? -SHORT(GetPrintWidth(lpCharBuffer, newoffset, *poffset))
        : SHORT(GetPrintWidth(lpCharBuffer, *poffset, newoffset));
    const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), diff);
    SetConsoleCursorPosition(hOutput, pos);
    *poffset = newoffset;
    return diff;
}

inline void ScreenEraseBack(HANDLE hOutput, LPTSTR lpCharBuffer, LPDWORD lpNumberOfCharsRead, LPDWORD poffset, const DWORD length)
{
    _ASSERTE(*poffset <= *lpNumberOfCharsRead);
    _ASSERTE(length <= *poffset);
    const DWORD newoffset = *poffset - length;
    const SHORT diff = ScreenMoveCursor(hOutput, lpCharBuffer, poffset, newoffset);
    StrErase(lpCharBuffer, lpNumberOfCharsRead, newoffset, length);
    const COORD pos = GetConsoleCursorPosition(hOutput);
    DWORD written = 0;
    RadWriteConsoleOutputCharacter(hOutput, lpCharBuffer + newoffset, *lpNumberOfCharsRead - newoffset, pos, &written);
    FillConsoleOutputCharacter(hOutput, TEXT(' '), -diff, Move(hOutput, pos, SHORT(written)), &written);
    _ASSERTE(*poffset <= *lpNumberOfCharsRead);
}

inline void ScreenEraseForward(HANDLE hOutput, LPTSTR lpCharBuffer, LPDWORD lpNumberOfCharsRead, const DWORD offset, const DWORD length)
{
    _ASSERTE(offset <= *lpNumberOfCharsRead);
    _ASSERTE(length <= (*lpNumberOfCharsRead  - offset));
    const DWORD newoffset = offset + length;
    const DWORD width = GetPrintWidth(lpCharBuffer, offset, newoffset);
    StrErase(lpCharBuffer, lpNumberOfCharsRead, offset, length);
    const COORD pos = GetConsoleCursorPosition(hOutput);
    DWORD written = 0;
    RadWriteConsoleOutputCharacter(hOutput, lpCharBuffer + offset, *lpNumberOfCharsRead - offset, pos, &written);
    FillConsoleOutputCharacter(hOutput, TEXT(' '), width, Move(hOutput, pos, SHORT(written)), &written);
    _ASSERTE(offset <= *lpNumberOfCharsRead);
}

inline void ScreenReplace(HANDLE hOutput, LPTSTR lpCharBuffer, LPDWORD lpNumberOfCharsRead, LPDWORD poffset, LPCTSTR lpText, const DWORD length)
{
    _ASSERTE(*poffset <= *lpNumberOfCharsRead);
    const SHORT diff = SHORT(GetPrintWidth(lpCharBuffer, 0, *lpNumberOfCharsRead)) - SHORT(GetPrintWidth(lpText, 0, length));
    ScreenMoveCursor(hOutput, lpCharBuffer, poffset, 0);
    StrCopy(lpCharBuffer, lpNumberOfCharsRead, lpText, length);
    RadWriteConsole(hOutput, lpCharBuffer, *lpNumberOfCharsRead, nullptr, nullptr);
    if (diff > 0)
    {
        DWORD written = 0;
        FillConsoleOutputCharacter(hOutput, TEXT(' '), diff, GetConsoleCursorPosition(hOutput), &written);
    }
    *poffset = *lpNumberOfCharsRead;
    _ASSERTE(*poffset <= *lpNumberOfCharsRead);
}

inline void ScreenInsert(HANDLE hOutput, LPTSTR lpCharBuffer, LPDWORD lpNumberOfCharsRead, LPDWORD poffset, LPCTSTR lpText)
{
    const DWORD length = StrInsert(lpCharBuffer, lpNumberOfCharsRead, *poffset, lpText);
    RadWriteConsole(hOutput, lpText, length, nullptr, nullptr);
    *poffset += length;

    COORD pos = GetConsoleCursorPosition(hOutput);
    RadWriteConsole(hOutput, BUFFER_X(lpCharBuffer, lpNumberOfCharsRead, *poffset), nullptr, nullptr);
    const COORD endpos = Move(hOutput, pos, (SHORT) (*lpNumberOfCharsRead - *poffset));
    const SMALL_RECT srWindow = GetConsoleWindow(hOutput);
    if (endpos.Y > srWindow.Bottom)
        pos.Y -= endpos.Y - srWindow.Bottom;
    SetConsoleCursorPosition(hOutput, pos);
}

}

extern "C" {

BOOL WriteHistoryANSI(_In_ HANDLE hOutput);

BOOL RadWriteConsole(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nNumberOfCharsToWrite) CONST VOID* lpBuffer,
    _In_ DWORD nNumberOfCharsToWrite,
    _Out_opt_ LPDWORD lpNumberOfCharsWritten,
    _Reserved_ LPVOID lpReserved
)
{
    LPCTSTR lpCharBuffer = (LPCTSTR) lpBuffer;
    if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten = 0;
    DWORD NumberOfCharsWritten;
    DWORD begin = 0;
    for (DWORD end = begin; end < nNumberOfCharsToWrite; ++end)
        if (IsDoubleWidth(lpCharBuffer[end]))
        {
            if (!WriteConsole(hConsoleOutput, lpCharBuffer + begin, end - begin, &NumberOfCharsWritten, lpReserved)) return FALSE;
            if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten += NumberOfCharsWritten;
            const TCHAR chevron[] = { TEXT('^'), TCHAR(TEXT('A') + lpCharBuffer[end] - 1) };
            if (!WriteConsole(hConsoleOutput, ARRAY_X(chevron), &NumberOfCharsWritten, lpReserved)) return FALSE;
            if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten += NumberOfCharsWritten;
            begin = end + 1;
        }
    if (!WriteConsole(hConsoleOutput, lpCharBuffer + begin, nNumberOfCharsToWrite - begin, &NumberOfCharsWritten, lpReserved)) return FALSE;
    if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten += NumberOfCharsWritten;
    return TRUE;
}

BOOL RadWriteConsoleOutputCharacter(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nLength) LPCWSTR lpCharacter,
    _In_ DWORD nLength,
    _In_ COORD dwWriteCoord,
    _Out_ LPDWORD lpNumberOfCharsWritten
)
{
    *lpNumberOfCharsWritten = 0;
    DWORD NumberOfCharsWritten;
    DWORD begin = 0;
    for (DWORD end = begin; end < nLength; ++end)
        if (IsDoubleWidth(lpCharacter[end]))
        {
            if (!WriteConsoleOutputCharacter(hConsoleOutput, lpCharacter + begin, end - begin, dwWriteCoord, &NumberOfCharsWritten)) return FALSE;
            dwWriteCoord = Move(hConsoleOutput, dwWriteCoord, SHORT(NumberOfCharsWritten));
            *lpNumberOfCharsWritten += NumberOfCharsWritten;
            const TCHAR chevron[] = { TEXT('^'), TCHAR(TEXT('A') + lpCharacter[end] - 1) };
            if (!WriteConsoleOutputCharacter(hConsoleOutput, ARRAY_X(chevron), dwWriteCoord, &NumberOfCharsWritten)) return FALSE;
            dwWriteCoord = Move(hConsoleOutput, dwWriteCoord, SHORT(NumberOfCharsWritten));
            *lpNumberOfCharsWritten += NumberOfCharsWritten;
            begin = end + 1;
        }
    if (!WriteConsoleOutputCharacter(hConsoleOutput, lpCharacter + begin, nLength - begin, dwWriteCoord, &NumberOfCharsWritten)) return FALSE;
    //dwWriteCoord = Move(hConsoleOutput, dwWriteCoord, NumberOfCharsWritten);
    *lpNumberOfCharsWritten += NumberOfCharsWritten;
    return TRUE;
}

void ExpandAlias(LPDWORD lpNumberOfCharsRead, LPTSTR lpCharBuffer, DWORD nNumberOfCharsToRead)
{
    if (*lpNumberOfCharsRead <= 0 || lpCharBuffer[0] == TEXT(' '))
        return;

    // Find alias
    static LPTSTR lpExeName = nullptr;
    if (lpExeName == nullptr)
    {
        static TCHAR filename[MAX_PATH] = TEXT("");
        GetModuleFileName(NULL, ARRAY_X(filename));
        lpExeName = PathFindFileName(filename);
    }

    const std::vector<std::tstring> args = split(std::tstring(lpCharBuffer, *lpNumberOfCharsRead), TEXT(' ')); // TODO Avoid copy of lpCharBuffer into temp buffer

    if (GetConsoleAlias(const_cast<LPTSTR>(args[0].c_str()), lpCharBuffer, nNumberOfCharsToRead, lpExeName))
    {
        *lpNumberOfCharsRead = DWORD(_tcslen(lpCharBuffer));

        DWORD r = 0;
        while ((r = StrFind(lpCharBuffer, lpNumberOfCharsRead, r, TEXT('$'))) < *lpNumberOfCharsRead)
        {
            switch (lpCharBuffer[r + 1])
            {
            case TEXT('G'): case TEXT('g'):
                StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, TEXT('>'));
                ++r;
                break;

            case TEXT('L'): case TEXT('l'):
                StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, TEXT('<'));
                ++r;
                break;

            case TEXT('B'): case TEXT('b'):
                StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, TEXT('|'));
                ++r;
                break;

            case TEXT('T'): case TEXT('t'):
                // TODO This is not correct behaviour - it should return here and then on the next call return the rest
                StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, TEXT('&'));
                ++r;
                break;

#if 0 // Documentation claims this to be true, my tests show otherwise
            case TEXT('$'):
                StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, TEXT('$'));
                ++r;
                break;
#endif

            case TEXT('1'): case TEXT('2'): case TEXT('3'): case TEXT('4'): case TEXT('5'):
            case TEXT('6'): case TEXT('7'): case TEXT('8'): case TEXT('9'):
            {
                const unsigned int c = lpCharBuffer[r + 1] - TEXT('0');
                StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                if (c < args.size())
                    r += StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, args[c].c_str());
            }
            break;

            case TEXT('*'):
                StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                for (size_t c = 1; c < args.size(); ++c)
                {
                    if (c != 1)
                    {
                        StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, TEXT(' '));
                        ++r;
                    }
                    r += StrInsert(lpCharBuffer, lpNumberOfCharsRead, r, args[c].c_str());
                }
                break;

            default:
                ++r;
                break;
            }
        }
    }
}

BOOL RadReadConsole(
    _In_ HANDLE hConsoleInput,
    _Inout_updates_bytes_to_(nNumberOfCharsToRead * sizeof(TCHAR), *lpNumberOfCharsRead * sizeof(TCHAR%)) LPVOID lpBuffer,
    _In_ DWORD nNumberOfCharsToRead,
    _Out_ _Deref_out_range_(<= , nNumberOfCharsToRead) LPDWORD lpNumberOfCharsRead,
    _In_opt_ PCONSOLE_READCONSOLE_CONTROL pInputControl
)
{
    *lpNumberOfCharsRead = 0;

    DWORD mode_input;
    if (!GetConsoleMode(hConsoleInput, &mode_input))
        OutputDebugString(TEXT("Error GetConsoleMode hConsoleInput\n"));

    if ((mode_input & (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)) == 0)
    {
        OutputDebugString(TEXT("Error Invalid input console mode reverting to default\n"));
        return ReadConsole(hConsoleInput, lpBuffer, nNumberOfCharsToRead, lpNumberOfCharsRead, pInputControl);
    }

    mode_input |= ENABLE_INSERT_MODE;

    // TODO Original only return max nNumberOfCharsToRead to buffer even though it accepts the whole line before returning. Next call returns the next characters.
    // TODO If nNumberOfCharsToRead is less than 128 seems to use an internal buffer of 128
    // TODO Original seems to use an internal buffer that is not copied to until returning
    // TODO Check for nNumberOfCharsToRead when inserting

    const HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    SaveConsoleMode save_mode_output(hOutput);

    {
        DWORD mode_output = save_mode_output.mode();

        // ENABLE_VIRTUAL_TERMINAL_PROCESSING stops ENABLE_WRAP_AT_EOL_OUTPUT from working
        mode_output &= ~ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        mode_output |= ENABLE_WRAP_AT_EOL_OUTPUT;
        if (!SetConsoleMode(hOutput, mode_output))
            OutputDebugString(TEXT("Error SetConsoleMode hOutput\n"));

        DWORD mode_output_check;
        if (!GetConsoleMode(hOutput, &mode_output_check))
            OutputDebugString(TEXT("Error GetConsoleMode hOutput\n"));

        if (mode_output_check != mode_output)
            OutputDebugString(TEXT("Error setting output console mode\n"));
    }

    CONSOLE_CURSOR_INFO cursor = {};
    GetConsoleCursorInfo(hOutput, &cursor);

    LPTSTR lpCharBuffer = (LPTSTR) lpBuffer;
    DWORD offset = 0;

    if (pInputControl != nullptr)
    {
        _ASSERTE(pInputControl->nLength == sizeof(CONSOLE_READCONSOLE_CONTROL));

        offset = pInputControl->nInitialChars;
        *lpNumberOfCharsRead = offset;

        _ASSERT(pInputControl->dwControlKeyState == 0);
        // TODO pInputControl->dwControlKeyState
    }

    //lpCharBuffer[*lpNumberOfCharsRead] = TEXT('\0');
    LPCTSTR wordbreak = TEXT("/\\=[]{}()");

    auto g_history_it = g_history.end();

    INPUT_RECORD ir = {};
    DWORD read = 0;
    while (ReadConsoleInput(hConsoleInput, &ir, 1, &read))
    {
        _ASSERTE(*lpNumberOfCharsRead >= offset);
        _ASSERTE(*lpNumberOfCharsRead <= nNumberOfCharsToRead);
        switch (ir.EventType)
        {
        case KEY_EVENT:
        {
            switch (ir.Event.KeyEvent.wVirtualKeyCode)
            {
            case VK_SHIFT:
            case VK_CONTROL:
            case VK_MENU:
                break;

            case VK_LEFT:
                if (ir.Event.KeyEvent.bKeyDown && offset > 0)
                {
                    if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    {
                        const DWORD newoffset = StrFindPrev(lpCharBuffer, lpNumberOfCharsRead, offset, wordbreak);
                        ScreenMoveCursor(hOutput, lpCharBuffer, &offset, newoffset);
                    }
                    else
                    {
                        ScreenMoveCursor(hOutput, lpCharBuffer, &offset, offset - 1);
                    }
                }
                break;

            case VK_RIGHT:
                if (ir.Event.KeyEvent.bKeyDown && offset < *lpNumberOfCharsRead)
                {
                    if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    {
                        const DWORD newoffset = StrFindNext(lpCharBuffer, lpNumberOfCharsRead, offset, wordbreak);
                        ScreenMoveCursor(hOutput, lpCharBuffer, &offset, newoffset);
                    }
                    else
                    {
                        ScreenMoveCursor(hOutput, lpCharBuffer, &offset, offset + 1);
                    }
                }
                break;

            case VK_UP:
                if (ir.Event.KeyEvent.bKeyDown
                    && (g_history_it == g_history.end() || std::next(g_history_it) != g_history.end())
                    && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) == 0))
                {
                    if (g_history_it == g_history.end())
                        g_history_it = g_history.begin();
                    else
                        ++g_history_it;
                    if (g_history_it != g_history.end())
                        ScreenReplace(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, g_history_it->data(), DWORD(g_history_it->size()));
                }
                break;

            case VK_DOWN:
                if (ir.Event.KeyEvent.bKeyDown
                    && g_history_it != g_history.end() && g_history_it != g_history.begin()
                    && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) == 0))
                {
                    --g_history_it;
                    ScreenReplace(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, g_history_it->data(), DWORD(g_history_it->size()));
                }
                break;

            case VK_HOME:
                if (ir.Event.KeyEvent.bKeyDown && offset > 0)
                {
                    ScreenMoveCursor(hOutput, lpCharBuffer, &offset, 0);
                }
                else if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED) && offset > 0)
                {
                    ScreenEraseBack(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, offset);
                }
                break;

            case VK_END:
                if (ir.Event.KeyEvent.bKeyDown && offset < *lpNumberOfCharsRead)
                {
                    ScreenMoveCursor(hOutput, lpCharBuffer, &offset, *lpNumberOfCharsRead);
                }
                else if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED) && offset > 0)
                {
                    ScreenEraseForward(hOutput, lpCharBuffer, lpNumberOfCharsRead, offset, *lpNumberOfCharsRead - offset);
                }
                break;

            case VK_INSERT:
                if (ir.Event.KeyEvent.bKeyDown)
                {
                    mode_input ^= ENABLE_INSERT_MODE; // Does it need to set the handle mode
                    CONSOLE_CURSOR_INFO local = cursor;
                    if ((mode_input & ENABLE_INSERT_MODE) == 0)
                        local.dwSize = 50;
                    SetConsoleCursorInfo(hOutput, &local);
                }
                break;

            case VK_ESCAPE:
                if (ir.Event.KeyEvent.bKeyDown && *lpNumberOfCharsRead > 0)
                {
                    ScreenReplace(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, TEXT(""), 0);
                }
                break;

            case VK_BACK:
                if (ir.Event.KeyEvent.bKeyDown && offset > 0)
                {
                    if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    {
                        const DWORD newoffset = StrFindPrev(lpCharBuffer, lpNumberOfCharsRead, offset, wordbreak);
                        ScreenEraseBack(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, offset - newoffset);
                    }
                    else
                        ScreenEraseBack(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, 1);
                }
                break;

            case VK_DELETE:
                if (ir.Event.KeyEvent.bKeyDown && offset < *lpNumberOfCharsRead)
                {
                    if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    {
                        const DWORD newoffset = StrFindNext(lpCharBuffer, lpNumberOfCharsRead, offset, wordbreak);
                        ScreenEraseForward(hOutput, lpCharBuffer, lpNumberOfCharsRead, offset, newoffset - offset);
                    }
                    else
                        ScreenEraseForward(hOutput, lpCharBuffer, lpNumberOfCharsRead, offset, 1);
                }
                break;

            case VK_F7:
                if (ir.Event.KeyEvent.bKeyDown && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) == 0))
                {
                    TCHAR command[1024] = TEXT("");

                    if (GetEnvironmentVariable(TEXT("RAD_HISTORY_PIPE"), command, ARRAYSIZE(command)))
                    {
                        const COORD pos = GetConsoleCursorPosition(hOutput);
                        const TCHAR text[] = TEXT("\r\n");
                        RadWriteConsole(hOutput, ARRAY_X(text) - 1, nullptr, nullptr);

                        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES) };
                        sa.bInheritHandle = TRUE;

                        std::unique_ptr<HANDLE, HANDLE_Deleter> hInputWritePipe;
                        std::unique_ptr<HANDLE, HANDLE_Deleter> hInputReadPipe;
                        if (!CreatePipe(&hInputReadPipe, &hInputWritePipe, &sa, 0))
                            break;
                        if (!SetHandleInformation(hInputWritePipe.get(), HANDLE_FLAG_INHERIT, 0))
                            break;

                        std::unique_ptr<HANDLE, HANDLE_Deleter> hOutputWritePipe;
                        std::unique_ptr<HANDLE, HANDLE_Deleter> hOutputReadPipe;
                        if (!CreatePipe(&hOutputReadPipe, &hOutputWritePipe, &sa, 0))
                            break;
                        if (!SetHandleInformation(hOutputReadPipe.get(), HANDLE_FLAG_INHERIT, 0))
                            break;

                        STARTUPINFO si = { sizeof(STARTUPINFO) };
                        si.dwFlags |= STARTF_USESTDHANDLES;
                        si.hStdInput = hInputReadPipe.get();
                        si.hStdOutput = hOutputWritePipe.get();
                        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                        PROCESS_INFORMATION pi = {};
                        if (!CreateProcess(nullptr, command, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
                            break;

                        std::unique_ptr<HANDLE, HANDLE_Deleter> hThread(pi.hThread);
                        std::unique_ptr<HANDLE, HANDLE_Deleter> hProcess(pi.hProcess);

                        hInputReadPipe.reset();
                        hOutputWritePipe.reset();

                        if (!WriteHistoryANSI(hInputWritePipe.get()))
                            break;
                        hInputWritePipe.reset();

                        WaitForSingleObject(hProcess.get(), INFINITE);


                        COORD resetpos = GetConsoleCursorPosition(hOutput);
                        --resetpos.Y;
                        resetpos.X = pos.X;
                        SetConsoleCursorPosition(hOutput, resetpos);

                        TCHAR buffer[1024];
                        DWORD chars = 0;
                        if (!ReadFileANSI(hOutputReadPipe.get(), buffer, ARRAYSIZE(buffer) - 1, &chars))
                            break;
                        buffer[--chars] = TEXT('\0');
                        hOutputReadPipe.reset();

                        hThread.reset();
                        hProcess.reset();

                        if (chars > 0)
                            ScreenReplace(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, buffer, chars);
                    }
                }
                break;

            case VK_RETURN:
                if (ir.Event.KeyEvent.bKeyDown && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) == 0))
                {
                    if (offset < *lpNumberOfCharsRead)
                        ScreenMoveCursor(hOutput, lpCharBuffer, &offset, *lpNumberOfCharsRead);

                    if (*lpNumberOfCharsRead > 0)
                        g_history.push_front(std::tstring(lpCharBuffer, *lpNumberOfCharsRead));

                    ExpandAlias(lpNumberOfCharsRead, lpCharBuffer, nNumberOfCharsToRead);

                    const TCHAR text[] = TEXT("\r\n");
                    StrAppend(lpCharBuffer, lpNumberOfCharsRead, text);
                    RadWriteConsole(hOutput, ARRAY_X(text) - 1, nullptr, nullptr);
                    SetConsoleCursorInfo(hOutput, &cursor);
                    return TRUE;
                }
                break;

            default:
                if (ir.Event.KeyEvent.bKeyDown)
                {
                    if (pInputControl != nullptr
                        && ir.Event.KeyEvent.uChar.tChar < (8 * sizeof(pInputControl->dwCtrlWakeupMask))
                        && (pInputControl->dwCtrlWakeupMask & (1 << ir.Event.KeyEvent.uChar.tChar)))
                    {
                        // BUG in original ConsoleInput doesn't properly insert the character
                        StrAppend(lpCharBuffer, lpNumberOfCharsRead, TEXT(" "));
                        lpCharBuffer[offset] = ir.Event.KeyEvent.uChar.tChar;
                        return TRUE;
                    }
                    else if (ir.Event.KeyEvent.wVirtualKeyCode == TEXT('V') && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0))
                    {
                        const HWND hWnd = ::GetConsoleWindow();
                        int retry = 10;
                        while (retry && !OpenClipboard(hWnd))
                            --retry;
                        if (retry <= 0)
                            break;

                        const HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                        if (hData)
                        {
                            const wchar_t* pClip = (const wchar_t*) GlobalLock(hData);
                            // TODO if (mode_input & ENABLE_INSERT_MODE)
                            ScreenInsert(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, pClip);
                            GlobalUnlock(hData);
                        }

                        CloseClipboard();
                    }
                    else if (ir.Event.KeyEvent.uChar.tChar != TEXT('\0'))
                    {
                        if (mode_input & ENABLE_INSERT_MODE)
                        {
                            TCHAR buffer[] = TEXT("_");
                            buffer[0] = ir.Event.KeyEvent.uChar.tChar;
                            ScreenInsert(hOutput, lpCharBuffer, lpNumberOfCharsRead, &offset, buffer);
                        }
                        else
                        {
                            StrOverwrite(lpCharBuffer, lpNumberOfCharsRead, offset++, ir.Event.KeyEvent.uChar.tChar);
                            RadWriteConsole(hOutput, &ir.Event.KeyEvent.uChar.tChar, 1, nullptr, nullptr);
                            if (IsDoubleWidth(ir.Event.KeyEvent.uChar.tChar))
                            {
                                COORD pos = GetConsoleCursorPosition(hOutput);
                                RadWriteConsole(hOutput, BUFFER_X(lpCharBuffer, lpNumberOfCharsRead, offset), nullptr, nullptr);
                                const COORD endpos = Move(hOutput, pos, (SHORT) (*lpNumberOfCharsRead - offset));
                                const SMALL_RECT srWindow = GetConsoleWindow(hOutput);
                                if (endpos.Y > srWindow.Bottom)
                                    pos.Y -= endpos.Y - srWindow.Bottom;
                                SetConsoleCursorPosition(hOutput, pos);
                            }
                        }
                    }
                }
                break;
            }

            break;
        }
        }
    }

    return TRUE;
}

BOOL WriteHistory(_In_ HANDLE hOutput)
{
    for (auto it = g_history.rbegin(); it != g_history.rend(); ++it)
    {
        const auto& s = *it;
        const DWORD cbBytesToWrite = DWORD(s.length() * sizeof(TCHAR));
        DWORD cbBytesWritten;
        BOOL fSuccess = WriteFile(hOutput, s.data(), cbBytesToWrite, &cbBytesWritten, nullptr);
        if (!fSuccess || cbBytesToWrite != cbBytesWritten)
            return FALSE;

        fSuccess = WriteFile(hOutput, TEXT("\n"), sizeof(TCHAR), &cbBytesWritten, nullptr);
        if (!fSuccess || sizeof(TCHAR) != cbBytesWritten)
            return FALSE;
    }
    return TRUE;
}

BOOL WriteHistoryANSI(_In_ HANDLE hOutput)
{
    for (auto it = g_history.rbegin(); it != g_history.rend(); ++it)
    {
        const auto& s = *it;
        BOOL fSuccess = WriteFileANSI(hOutput, s.data(), static_cast<int>(s.length()));
        if (!fSuccess)
            return FALSE;

        fSuccess = WriteFileANSI(hOutput, TEXT("\n"), 1);
        if (!fSuccess)
            return FALSE;
    }
    return TRUE;
}

}
