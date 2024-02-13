#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#include <crtdbg.h>
#include <shlwapi.h>

#include <string>
#include <deque>
#include <vector>

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

inline COORD GetConsoleCursorPosition(const HANDLE h)
{
    CONSOLE_SCREEN_BUFFER_INFO bi = {};
    GetConsoleScreenBufferInfo(h, &bi);
    return bi.dwCursorPosition;
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

}

extern "C" {

BOOL RadReadConsole(
    _In_ HANDLE hConsoleInput,
    _Out_writes_bytes_to_(nNumberOfCharsToRead * sizeof(TCHAR), *lpNumberOfCharsRead * sizeof(TCHAR%)) LPVOID lpBuffer,
    _In_ DWORD nNumberOfCharsToRead,
    _Out_ _Deref_out_range_(<= , nNumberOfCharsToRead) LPDWORD lpNumberOfCharsRead,
    _In_opt_ PCONSOLE_READCONSOLE_CONTROL pInputControl
)
{
    DWORD mode = 0;
    if (!GetConsoleMode(hConsoleInput, &mode))
        return FALSE;

    if ((mode & (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)) == 0)
        return ReadConsole(hConsoleInput, lpBuffer, nNumberOfCharsToRead, lpNumberOfCharsRead, pInputControl);

    // TODO Original only return max nNumberOfCharsToRead to buffer even though it accepts the whole line before returning. Next call returns the next characters.
    // TODO If nNumberOfCharsToRead is less than 128 seems to use an internal buffer of 128
    // TODO Original seems to use an internal buffer that is not copied to until returning
    // TODO Handle ctrl characters ie ^Z for Ctrl+Z
    // TODO Check for nNumberOfCharsToRead when inserting
    const HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_CURSOR_INFO cursor = {};
    GetConsoleCursorInfo(hOutput, &cursor);

    LPTSTR lpCharBuffer = (LPTSTR) lpBuffer;
    DWORD offset = 0;
    *lpNumberOfCharsRead = 0;

    if (pInputControl != nullptr)
    {
        _ASSERTE(pInputControl->nLength == sizeof(CONSOLE_READCONSOLE_CONTROL));

        offset = pInputControl->nInitialChars;
        *lpNumberOfCharsRead = offset;

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
        switch (ir.EventType)
        {
        case KEY_EVENT:
        {
            switch (ir.Event.KeyEvent.wVirtualKeyCode)
            {
            case VK_LEFT:
                if (ir.Event.KeyEvent.bKeyDown && offset > 0)
                {
                    if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    {
                        const DWORD newoffset = StrFindPrev(lpCharBuffer, lpNumberOfCharsRead, offset, wordbreak);
                        const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), SHORT(newoffset - offset));
                        SetConsoleCursorPosition(hOutput, pos);
                        offset = newoffset;
                    }
                    else
                    {
                        const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), -1);
                        SetConsoleCursorPosition(hOutput, pos);
                        --offset;
                    }
                }
                // TODO Ctrl+Left move to next word character
                break;

            case VK_RIGHT:
                if (ir.Event.KeyEvent.bKeyDown && offset < *lpNumberOfCharsRead)
                {
                    if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    {
                        const DWORD newoffset = StrFindNext(lpCharBuffer, lpNumberOfCharsRead, offset, wordbreak);
                        const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), SHORT(newoffset - offset));
                        SetConsoleCursorPosition(hOutput, pos);
                        offset = newoffset;
                    }
                    else
                    {
                        const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), 1);
                        SetConsoleCursorPosition(hOutput, pos);
                        ++offset;
                    }
                }
                // TODO Ctrl+Right move to next word character
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
                    const SHORT diff = SHORT(*lpNumberOfCharsRead) - SHORT(g_history_it->size());
                    tmemcpy(lpCharBuffer, g_history_it->data(), g_history_it->size());
                    *lpNumberOfCharsRead = DWORD(g_history_it->size());
                    DWORD written = 0;
                    SetConsoleCursorPosition(hOutput, Move(hOutput, GetConsoleCursorPosition(hOutput), -SHORT(offset)));
                    WriteConsole(hOutput, lpCharBuffer, *lpNumberOfCharsRead, &written, nullptr);
                    if (diff > 0)
                        FillConsoleOutputCharacter(hOutput, TEXT(' '), diff, GetConsoleCursorPosition(hOutput), &written);
                    offset = *lpNumberOfCharsRead;
                }
                break;

            case VK_DOWN:
                if (ir.Event.KeyEvent.bKeyDown
                    && g_history_it != g_history.end() && g_history_it != g_history.begin()
                    && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) == 0))
                {
                    --g_history_it;
                    const SHORT diff = SHORT(*lpNumberOfCharsRead) - SHORT(g_history_it->size());
                    tmemcpy(lpCharBuffer, g_history_it->data(), g_history_it->size());
                    *lpNumberOfCharsRead = DWORD(g_history_it->size());
                    DWORD written = 0;
                    SetConsoleCursorPosition(hOutput, Move(hOutput, GetConsoleCursorPosition(hOutput), -SHORT(offset)));
                    WriteConsole(hOutput, lpCharBuffer, *lpNumberOfCharsRead, &written, nullptr);
                    if (diff > 0)
                        FillConsoleOutputCharacter(hOutput, TEXT(' '), diff, GetConsoleCursorPosition(hOutput), &written);
                    offset = *lpNumberOfCharsRead;
                }
                break;

            case VK_HOME:
                if (ir.Event.KeyEvent.bKeyDown && offset > 0)
                {
                    const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), -(SHORT(offset)));
                    SetConsoleCursorPosition(hOutput, pos);
                    offset = 0;
                }
                else if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED) && offset > 0)
                {
                    const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), -(SHORT(offset)));
                    StrErase(lpCharBuffer, lpNumberOfCharsRead, 0, offset);
                    DWORD written = 0;
                    SetConsoleCursorPosition(hOutput, pos);
                    WriteConsole(hOutput, lpCharBuffer, *lpNumberOfCharsRead, &written, nullptr);
                    FillConsoleOutputCharacter(hOutput, TEXT(' '), offset, GetConsoleCursorPosition(hOutput), &written);
                    SetConsoleCursorPosition(hOutput, pos);
                    offset = 0;
                }
                break;

            case VK_END:
                if (ir.Event.KeyEvent.bKeyDown && offset < *lpNumberOfCharsRead)
                {
                    const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), SHORT(*lpNumberOfCharsRead - offset));
                    SetConsoleCursorPosition(hOutput, pos);
                    offset = *lpNumberOfCharsRead;
                }
                else if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED) && offset > 0)
                {
                    DWORD written = 0;
                    FillConsoleOutputCharacter(hOutput, TEXT(' '), *lpNumberOfCharsRead - offset, GetConsoleCursorPosition(hOutput), &written);
                    StrErase(lpCharBuffer, lpNumberOfCharsRead, offset, *lpNumberOfCharsRead - offset);
                }
                break;

            case VK_INSERT:
                if (ir.Event.KeyEvent.bKeyDown)
                {
                    mode ^= ENABLE_INSERT_MODE; // Does it need to set the handle mode
                    CONSOLE_CURSOR_INFO local = cursor;
                    if ((mode & ENABLE_INSERT_MODE) == 0)
                        local.dwSize = 50;
                    SetConsoleCursorInfo(hOutput, &local);
                }
                break;

            case VK_ESCAPE:
                if (ir.Event.KeyEvent.bKeyDown && *lpNumberOfCharsRead > 0)
                {
                    const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), -(SHORT(offset)));
                    DWORD written = 0;
                    FillConsoleOutputCharacter(hOutput, TEXT(' '), *lpNumberOfCharsRead, pos, &written);
                    SetConsoleCursorPosition(hOutput, pos);
                    StrErase(lpCharBuffer, lpNumberOfCharsRead, 0, *lpNumberOfCharsRead);
                    offset = 0;
                }
                break;

            case VK_BACK:
                if (ir.Event.KeyEvent.bKeyDown && offset > 0)
                {
                    if (ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    {
                        const DWORD newoffset = StrFindPrev(lpCharBuffer, lpNumberOfCharsRead, offset, TEXT("\\/"));
                        const DWORD dist = offset - newoffset;
                        StrErase(lpCharBuffer, lpNumberOfCharsRead, newoffset, dist);
                        const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), -SHORT(dist));
                        DWORD written = 0;
                        SetConsoleCursorPosition(hOutput, pos);
                        WriteConsole(hOutput, BUFFER_X(lpCharBuffer, lpNumberOfCharsRead, newoffset), &written, nullptr);
                        FillConsoleOutputCharacter(hOutput, TEXT(' '), dist, GetConsoleCursorPosition(hOutput), &written);
                        SetConsoleCursorPosition(hOutput, pos);
                        offset = newoffset;
                    }
                    else
                    {
                        StrErase(lpCharBuffer, lpNumberOfCharsRead, --offset, 1);
                        const COORD pos = Move(hOutput, GetConsoleCursorPosition(hOutput), -1);
                        DWORD written = 0;
                        SetConsoleCursorPosition(hOutput, pos);
                        WriteConsole(hOutput, BUFFER_X(lpCharBuffer, lpNumberOfCharsRead, offset), &written, nullptr);
                        FillConsoleOutputCharacter(hOutput, TEXT(' '), 1, GetConsoleCursorPosition(hOutput), &written);
                        SetConsoleCursorPosition(hOutput, pos);
                    }
                }
                break;

            case VK_DELETE:
                if (ir.Event.KeyEvent.bKeyDown && offset < *lpNumberOfCharsRead && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) == 0))
                {
                    StrErase(lpCharBuffer, lpNumberOfCharsRead, offset, 1);
                    const COORD pos = GetConsoleCursorPosition(hOutput);
                    DWORD written = 0;
                    //SetConsoleCursorPosition(hOutput, pos);
                    WriteConsole(hOutput, BUFFER_X(lpCharBuffer, lpNumberOfCharsRead, offset), &written, nullptr);
                    FillConsoleOutputCharacter(hOutput, TEXT(' '), 1, GetConsoleCursorPosition(hOutput), &written);
                    SetConsoleCursorPosition(hOutput, pos);
                }
                break;

            case VK_RETURN:
                if (ir.Event.KeyEvent.bKeyDown && ((ir.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) == 0))
                {
                    if (*lpNumberOfCharsRead > 0)
                        g_history.push_front(std::tstring(lpCharBuffer, *lpNumberOfCharsRead));

                    if (*lpNumberOfCharsRead > 0 && lpCharBuffer[0] != TEXT(' '))
                    {
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
                                case TEXT('1'): case TEXT('2'): case TEXT('3'): case TEXT('4'): case TEXT('5'):
                                case TEXT('6'): case TEXT('7'): case TEXT('8'): case TEXT('9'):
                                {
                                    const int c = lpCharBuffer[r + 1] - TEXT('0');
                                    StrErase(lpCharBuffer, lpNumberOfCharsRead, r, 2);
                                    if (c < args.size())
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

                    const TCHAR text[] = TEXT("\r\n");
                    StrAppend(lpCharBuffer, lpNumberOfCharsRead, text);
                    DWORD written = 0;
                    WriteConsole(hOutput, ARRAY_X(text), &written, nullptr);
                    SetConsoleCursorInfo(hOutput, &cursor);
                    return TRUE;
                }
                break;

            default:
                if (ir.Event.KeyEvent.bKeyDown)
                {
                    if (_istprint(ir.Event.KeyEvent.uChar.tChar))
                    {
                        if (mode & ENABLE_INSERT_MODE)
                            StrInsert(lpCharBuffer, lpNumberOfCharsRead, offset++, ir.Event.KeyEvent.uChar.tChar);
                        else
                            StrOverwrite(lpCharBuffer, lpNumberOfCharsRead, offset++, ir.Event.KeyEvent.uChar.tChar);
                        DWORD written = 0;
                        WriteConsole(hOutput, &ir.Event.KeyEvent.uChar, 1, &written, nullptr);
                        if (mode & ENABLE_INSERT_MODE)
                        {
                            const COORD pos = GetConsoleCursorPosition(hOutput);
                            WriteConsole(hOutput, BUFFER_X(lpCharBuffer, lpNumberOfCharsRead, offset), &written, nullptr);
                            SetConsoleCursorPosition(hOutput, pos);
                        }
                    }
                    else if (pInputControl != nullptr
                        && ir.Event.KeyEvent.uChar.tChar < (8 * sizeof(pInputControl->dwCtrlWakeupMask))
                        && (pInputControl->dwCtrlWakeupMask & (1 << ir.Event.KeyEvent.uChar.tChar)))
                    {
                        // BUG in original ConsoleInput doesn't properly insert the character
                        StrAppend(lpCharBuffer, lpNumberOfCharsRead, TEXT(" "));
                        lpCharBuffer[offset] = ir.Event.KeyEvent.uChar.tChar;
                        return TRUE;
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

}
