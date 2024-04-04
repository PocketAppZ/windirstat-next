// FileFindEnhanced.h - Declaration of CFileFindEnhanced
//
// WinDirStat - Directory Statistics
// Copyright (C) 2003-2005 Bernhard Seifert
// Copyright (C) 2004-2024 WinDirStat Team (windirstat.net)
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

#pragma once

#include <stdafx.h>

class FileFindEnhanced final
{
    typedef struct FILE_DIRECTORY_INFORMATION {
        ULONG         NextEntryOffset;
        ULONG         FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG         FileAttributes;
        ULONG         FileNameLength;
        WCHAR         FileName[1];
    } FILE_DIRECTORY_INFORMATION;

    CStringW m_search;
    CStringW m_base;
    CStringW m_name;
    HANDLE m_handle = nullptr;
    bool m_firstrun = true;
    FILE_DIRECTORY_INFORMATION* m_current_info = nullptr;

public:

    FileFindEnhanced();
    ~FileFindEnhanced();

    bool FindNextFile();
    bool FindFile(const CStringW& strFolder,const CStringW& strName = L"");
    bool IsDirectory() const;
    bool IsDots() const;
    bool IsHidden() const;
    bool IsHiddenSystem() const;
    bool IsProtectedReparsePoint() const;
    DWORD GetAttributes() const;
    CStringW GetFileName() const;
    ULONGLONG GetFileSize() const;
    FILETIME GetLastWriteTime() const;
    CStringW GetFilePath() const;
    static bool DoesFileExist(const CStringW& folder, const CStringW& file);
    static CStringW GetLongPathCompatible(const CStringW& path);
};
