// GlobalHelpers.cpp - Implementation of global helper functions
//
// WinDirStat - Directory Statistics
// Copyright (C) 2003-2005 Bernhard Seifert
// Copyright (C) 2004-2024 WinDirStat Team (windirstat.net)
// Copyright (C) 2010 Chris Wimmer
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include "stdafx.h"
#include "WinDirStat.h"
#include <common/MdExceptions.h>
#include <common/SmartPointer.h>
#include <common/CommonHelpers.h>
#include "GlobalHelpers.h"
#include "Options.h"
#include "Localization.h"

#include <array>
#include <algorithm>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
    CStringW FormatLongLongNormal(ULONGLONG n)
    {
        // Returns formatted number like "123.456.789".

        ASSERT(n >= 0);

        CStringW all;

        do
        {
            const int rest = static_cast<int>(n % 1000);
            n /= 1000;

            CStringW s;
            if (n > 0)
            {
                s.Format(L"%s%03d", GetLocaleThousandSeparator().GetString(), rest);
            }
            else
            {
                s.Format(L"%d", rest);
            }

            all = s + all;
        }
        while (n > 0);

        return all;
    }
}

CStringW GetLocaleString(LCTYPE lctype, LANGID langid)
{
    const LCID lcid = MAKELCID(langid, SORT_DEFAULT);

    const int len = ::GetLocaleInfo(lcid, lctype, nullptr, 0);
    CStringW s;

    ::GetLocaleInfo(lcid, lctype, s.GetBuffer(len), len);
    s.ReleaseBuffer();

    return s;
}

CStringW GetLocaleLanguage(LANGID langid)
{
    const CStringW s = GetLocaleString(LOCALE_SLOCALIZEDLANGUAGENAME, langid);
    const CStringW n = GetLocaleString(LOCALE_SNATIVELANGNAME, langid);
    return s + L" (" + n + L")";
}

CStringW GetLocaleThousandSeparator()
{
    static LANGID cached_lang = static_cast<LANGID>(-1);
    static CStringW cached_string;
    if (cached_lang != COptions::GetEffectiveLangId())
    {
        cached_lang = COptions::GetEffectiveLangId();
        cached_string = GetLocaleString(LOCALE_STHOUSAND, cached_lang);
    }
    return cached_string;
}

CStringW GetLocaleDecimalSeparator()
{
    static LANGID cached_lang = static_cast<LANGID>(-1);
    static CStringW cached_string;
    if (cached_lang != COptions::GetEffectiveLangId())
    {
        cached_lang = COptions::GetEffectiveLangId();
        cached_string = GetLocaleString(LOCALE_SDECIMAL, cached_lang);
    }
    return cached_string;
}

CStringW FormatBytes(const ULONGLONG& n)
{
    if (COptions::HumanFormat)
    {
        return FormatLongLongHuman(n);
    }

    return FormatLongLongNormal(n);

}

CStringW FormatLongLongHuman(ULONGLONG n)
{
    // Returns formatted number like "12,4 GB".
    ASSERT(n >= 0);
    constexpr int base = 1024;
    constexpr int half = base / 2;

    CStringW s;

    const double B = static_cast<int>(n % base);
    n /= base;

    const double KB = static_cast<int>(n % base);
    n /= base;

    const double MB = static_cast<int>(n % base);
    n /= base;

    const double GB = static_cast<int>(n % base);
    n /= base;

    const double TB = static_cast<int>(n);

    if (TB != 0.0 || GB == base - 1 && MB >= half)
    {
        s.Format(L"%s %s", FormatDouble(TB + GB / base).GetString(), GetSpec_TB().GetString());
    }
    else if (GB != 0.0 || MB == base - 1 && KB >= half)
    {
        s.Format(L"%s %s", FormatDouble(GB + MB / base).GetString(), GetSpec_GB().GetString());
    }
    else if (MB != 0.0 || KB == base - 1 && B >= half)
    {
        s.Format(L"%s %s", FormatDouble(MB + KB / base).GetString(), GetSpec_MB().GetString());
    }
    else if (KB != 0.0)
    {
        s.Format(L"%s %s", FormatDouble(KB + B / base).GetString(), GetSpec_KB().GetString());
    }
    else if (B != 0.0)
    {
        s.Format(L"%d %s", static_cast<int>(B), GetSpec_Bytes().GetString());
    }
    else
    {
        s = L"0";
    }

    return s;
}

CStringW FormatCount(const ULONGLONG& n)
{
    return FormatLongLongNormal(n);
}

CStringW FormatDouble(double d)
{
    ASSERT(d >= 0);

    d += 0.05;

    const int i = static_cast<int>(floor(d));
    const int r = static_cast<int>(10 * fmod(d, 1));

    CStringW s;
    s.Format(L"%d%s%d", i, GetLocaleDecimalSeparator().GetString(), r);

    return s;
}

CStringW PadWidthBlanks(CStringW n, int width)
{
    const int blankCount = width - n.GetLength();
    if (blankCount > 0)
    {
        CStringW b;
        const LPWSTR psz = b.GetBuffer(blankCount + 1);
        int i = 0;
        for (; i < blankCount; i++)
        {
            psz[i] = L' ';
        }
        psz[i] = 0;
        b.ReleaseBuffer();

        n = b + n;
    }
    return n;
}

CStringW FormatFileTime(const FILETIME& t)
{
    SYSTEMTIME st;
    if (FILETIME ft;
        ::FileTimeToLocalFileTime(&t, &ft) == 0 ||
        ::FileTimeToSystemTime(&ft, &st) == 0)
    {
        return L"";
    }

    const LCID lcid = MAKELCID(COptions::LanguageId.Obj(), SORT_DEFAULT);

    CStringW date;
    VERIFY(0 < ::GetDateFormat(lcid, DATE_SHORTDATE, &st, nullptr, date.GetBuffer(64), 64));
    date.ReleaseBuffer();

    CStringW time;
    VERIFY(0 < GetTimeFormat(lcid, TIME_NOSECONDS, &st, nullptr, time.GetBuffer(64), 64));
    time.ReleaseBuffer();

    return date + L"  " + time;
}

CStringW FormatAttributes(DWORD attr)
{
    // order:
    // strAttributeReadonly
    // strAttributeHidden
    // strAttributeSystem
    // strAttributeArchive
    // strAttributeTemporary
    // strAttributeCompressed
    // strAttributeEncrypted
    // strAttributeIntegrityStream
    // strAttributeVirtual
    // strAttributeReparsePoint
    // strAttributeSparse
    // strAttributeOffline
    // strAttributeNotContentIndexed
    // strAttributeEA
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return wds::strInvalidAttributes;
    }

    CStringW attributes;
    if (attr & FILE_ATTRIBUTE_READONLY)
    {
        attributes.Append(wds::strAttributeReadonly);
    }

    if (attr & FILE_ATTRIBUTE_HIDDEN)
    {
        attributes.Append(wds::strAttributeHidden);
    }

    if (attr & FILE_ATTRIBUTE_SYSTEM)
    {
        attributes.Append(wds::strAttributeSystem);
    }

    if (attr & FILE_ATTRIBUTE_ARCHIVE)
    {
        attributes.Append(wds::strAttributeArchive);
    }

    if (attr & FILE_ATTRIBUTE_COMPRESSED)
    {
        attributes.Append(wds::strAttributeCompressed);
    }

    if (attr & FILE_ATTRIBUTE_ENCRYPTED)
    {
        attributes.Append(wds::strAttributeEncrypted);
    }

    return attributes;
}

CStringW FormatMilliseconds(const ULONGLONG ms)
{
    CStringW ret;
    const ULONGLONG sec = (ms + 500) / 1000;

    const ULONGLONG s   = sec % 60;
    const ULONGLONG min = sec / 60;

    const ULONGLONG m = min % 60;


    if (const ULONGLONG h = min / 60; h > 0)
    {
        ret.Format(L"%I64u:%02I64u:%02I64u", h, m, s);
    }
    else
    {
        ret.Format(L"%I64u:%02I64u", m, s);
    }
    return ret;
}

bool GetVolumeName(LPCWSTR rootPath, CStringW& volumeName)
{
    const bool b = FALSE != GetVolumeInformation(rootPath, volumeName.GetBuffer(256), 256, nullptr, nullptr, nullptr, nullptr, 0);
    volumeName.ReleaseBuffer();

    if (!b)
    {
        VTRACE(L"GetVolumeInformation(%s) failed: %u", rootPath, ::GetLastError());
    }

    return b;
}

// Given a root path like "C:\", this function
// obtains the volume name and returns a complete display string
// like "BOOT (C:)".
CStringW FormatVolumeNameOfRootPath(const CStringW& rootPath)
{
    CStringW ret;
    CStringW volumeName;
    if (GetVolumeName(rootPath, volumeName))
    {
        ret = FormatVolumeName(rootPath, volumeName);
    }
    else
    {
        ret = rootPath;
    }
    return ret;
}

CStringW FormatVolumeName(const CStringW& rootPath, const CStringW& volumeName)
{
    CStringW ret;
    ret.Format(L"%s (%s)", volumeName.GetString(), rootPath.Left(2).GetString());
    return ret;
}

// The inverse of FormatVolumeNameOfRootPath().
// Given a name like "BOOT (C:)", it returns "C:" (without trailing backslash).
// Or, if name like "C:\", it returns "C:".
CStringW PathFromVolumeName(const CStringW& name)
{
    const int i = name.ReverseFind(wds::chrBracketClose);
    if (i == -1)
    {
        ASSERT(name.GetLength() == 3);
        return name.Left(2);
    }

    ASSERT(i != -1);
    const int k = name.ReverseFind(wds::chrBracketOpen);
    ASSERT(k != -1);
    ASSERT(k < i);
    CStringW path = name.Mid(k + 1, i - k - 1);
    ASSERT(path.GetLength() == 2);
    ASSERT(path[1] == wds::chrColon);

    return path;
}

// Retrieve the "fully qualified parse name" of "My Computer"
CStringW GetParseNameOfMyComputer()
{
    CComPtr<IShellFolder> sf;
    HRESULT hr = ::SHGetDesktopFolder(&sf);
    MdThrowFailed(hr, L"::SHGetDesktopFolder");

    SmartPointer<LPITEMIDLIST> pidl(CoTaskMemFree);
    hr = ::SHGetSpecialFolderLocation(nullptr, CSIDL_DRIVES, &pidl);
    MdThrowFailed(hr, L"SHGetSpecialFolderLocation(CSIDL_DRIVES)");

    STRRET name;
    ZeroMemory(&name, sizeof(name));
    name.uType = STRRET_CSTR;
    hr = sf->GetDisplayNameOf(pidl, SHGDN_FORPARSING, &name);
    MdThrowFailed(hr, L"GetDisplayNameOf(My Computer)");

    return MyStrRetToString(pidl, &name);
}

void GetPidlOfMyComputer(LPITEMIDLIST* ppidl)
{
    CComPtr<IShellFolder> sf;
    HRESULT hr = ::SHGetDesktopFolder(&sf);
    MdThrowFailed(hr, L"SHGetDesktopFolder");

    hr = ::SHGetSpecialFolderLocation(nullptr, CSIDL_DRIVES, ppidl);
    MdThrowFailed(hr, L"SHGetSpecialFolderLocation(CSIDL_DRIVES)");
}

CStringW GetFolderNameFromPath(const LPCWSTR path)
{
    CStringW s  = path;
    const int i = s.ReverseFind(wds::chrBackslash);
    if (i < 0)
    {
        return s;
    }
    return s.Left(i);
}

CStringW GetCOMSPEC()
{
    CStringW cmd;
    const DWORD dw = ::GetEnvironmentVariable(L"COMSPEC", cmd.GetBuffer(_MAX_PATH), _MAX_PATH);
    cmd.ReleaseBuffer();

    if (dw == 0)
    {
        VTRACE(L"COMSPEC not set.");
        cmd = L"cmd.exe";
    }
    return cmd;
}

void WaitForHandleWithRepainting(const HANDLE h, const DWORD TimeOut)
{
    while (true)
    {
        // Read all messages in this next loop, removing each message as we read it.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE))
        {
            ::DispatchMessage(&msg);
        }

        // Wait for WM_PAINT message sent or posted to this queue
        // or for one of the passed handles be set to signal.
        const DWORD r = ::MsgWaitForMultipleObjects(1, &h, FALSE, TimeOut, QS_PAINT);

        // The result tells us the type of event we have.
        if (r == WAIT_OBJECT_0 + 1)
        {
            // New messages have arrived.
            // Continue to the top of the always while loop to dispatch them and resume waiting.
            continue;
        }

        // The handle became signaled.
        break;
    }
}

bool FolderExists(LPCWSTR path)
{
    CFileFind finder;
    if (finder.FindFile(path))
    {
        finder.FindNextFile();
        return FALSE != finder.IsDirectory();
    }

    // Here we land, if path is a UNC drive. In this case we
    // try another FindFile:
    return finder.FindFile(CStringW(path) + L"\\*.*") != false;
}

bool DriveExists(const CStringW& path)
{
    if (path.GetLength() != 3 || path[1] != wds::chrColon || path[2] != wds::chrBackslash)
    {
        return false;
    }

    CStringW letter = path.Left(1);
    letter.MakeLower();
    const int d = letter[0] - wds::chrSmallA;
    if (const DWORD mask = 0x1 << d; (mask & ::GetLogicalDrives()) == 0)
    {
        return false;
    }

    CStringW dummy;
    if (!::GetVolumeName(path, dummy))
    {
        return false;
    }

    return true;
}

// drive is a drive spec like C: or C:\ or C:\path (path is ignored).
//
// This function returns
// "", if QueryDosDevice is unsupported or drive doesn't begin with a drive letter,
// 'Information about MS-DOS device names' otherwise:
// Something like
//
// \Device\Harddisk\Volume1                               for a local drive
// \Device\LanmanRedirector\;T:0000000011e98\spock\temp   for a network drive
// \??\C:\programme                                       for a SUBSTed local path
// \??\T:\Neuer Ordner                                    for a SUBSTed SUBSTed path
// \??\UNC\spock\temp                                     for a SUBSTed UNC path
//
// As always, I had to experimentally determine these strings, Microsoft
// didn't think it was necessary to document them. (Sometimes I think, they
// even don't document such things internally...)
//
// I hope that a drive is SUBSTed iff this string starts with \??\.
//
// assarbad:
//   It cannot be safely determined whether a path is or is not SUBSTed on NT
//   via this API. You would have to look up the volume mount points because
//   SUBST only works per session by definition whereas volume mount points
//   work across sessions (after restarts).
//
CStringW MyQueryDosDevice(const LPCWSTR drive)
{
    CStringW d = drive;

    if (d.GetLength() < 2 || d[1] != wds::chrColon)
    {
        return wds::strEmpty;
    }

    d = d.Left(2);

    CStringW info;
    const DWORD dw = ::QueryDosDevice(d, info.GetBuffer(512), 512);
    info.ReleaseBuffer();

    if (dw == 0)
    {
        VTRACE(L"QueryDosDevice(%s) failed: %s", d.GetString(), MdGetWinErrorText(::GetLastError()).GetString());
        return wds::strEmpty;
    }

    return info;
}

// drive is a drive spec like C: or C:\ or C:\path (path is ignored).
// 
// This function returnes true, if QueryDosDevice() is supported
// and drive is a SUBSTed drive.
//
bool IsSUBSTedDrive(LPCWSTR drive)
{
    const CStringW info = MyQueryDosDevice(drive);
    return info.GetLength() >= 4 && info.Left(4) == "\\??\\";
}

CStringW GetSpec_Bytes()
{
    static CStringW s = Localization::Lookup(IDS_SPEC_BYTES, L"Bytes");
    return s;
}

CStringW GetSpec_KB()
{
    static CStringW s = Localization::Lookup(IDS_SPEC_KB, L"KiB");
    return s;
}

CStringW GetSpec_MB()
{
    static CStringW s = Localization::Lookup(IDS_SPEC_MB, L"MiB");
    return s;
}

CStringW GetSpec_GB()
{
    static CStringW s = Localization::Lookup(IDS_SPEC_GB, L"GiB");
    return s;
}

CStringW GetSpec_TB()
{
    static CStringW s = Localization::Lookup(IDS_SPEC_TB, L"TiB");
    return s;
}

BOOL IsAdmin()
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (SmartPointer<PSID> pSid(FreeSid); ::AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pSid))
    {
        BOOL bResult = FALSE;
        if (!::CheckTokenMembership(nullptr, pSid, &bResult))
        {
            return FALSE;
        }
        return bResult;
    }

    return FALSE;
}

bool EnableReadPrivileges()
{
    // Open a connection to the currently running process token and request
    // we have the ability to look at and adjust our privileges
    SmartPointer<HANDLE> token(CloseHandle);
    if (OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token) == 0)
    {
        return false;
    }

    // Fetch a list of privileges we currently have
    std::vector<BYTE> privs_bytes(64 * sizeof(LUID_AND_ATTRIBUTES) + sizeof(DWORD), 0);
    const PTOKEN_PRIVILEGES privs_available = reinterpret_cast<PTOKEN_PRIVILEGES>(privs_bytes.data());
    DWORD priv_length = 0;
    if (GetTokenInformation(token, TokenPrivileges, privs_bytes.data(),
        static_cast<DWORD>(privs_bytes.size()), &priv_length) == 0)
    {
        return false;
    }
    
    bool ret = true;
    for (const LPCWSTR priv : { SE_RESTORE_NAME, SE_BACKUP_NAME })
    {
        // Populate the privilege adjustment structure
        TOKEN_PRIVILEGES priv_entry = {};
        priv_entry.PrivilegeCount = 1;
        priv_entry.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // Translate the privilege name into the binary representation
        if (LookupPrivilegeValue(nullptr, priv, &priv_entry.Privileges[0].Luid) == 0)
        {
            ret = false;
            continue;
        }

        // Check if privilege is in the list of ones we have
        if (std::count_if(privs_available[0].Privileges, &privs_available->Privileges[privs_available->PrivilegeCount],
            [&](const LUID_AND_ATTRIBUTES& element) {
                return element.Luid.HighPart == priv_entry.Privileges[0].Luid.HighPart &&
                    element.Luid.LowPart == priv_entry.Privileges[0].Luid.LowPart;}) == 0)
        {
            ret = false;
            continue;
        }

        // Adjust the process to change the privilege
        if (AdjustTokenPrivileges(token, FALSE, &priv_entry,
            sizeof(TOKEN_PRIVILEGES), nullptr, nullptr) == 0)
        {
            ret = false;
            break;
        }

        // Error if not all items were assigned
        if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
        {
            ret = false;
            break;
        }
    }

    return ret;
}
