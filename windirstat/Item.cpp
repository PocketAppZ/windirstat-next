// Item.cpp - Implementation of CItem
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

#include "stdafx.h"
#include "aclapi.h"
#include "sddl.h"

#include "WinDirStat.h"
#include "DirStatDoc.h"
#include "MainFrame.h"
#include <common/CommonHelpers.h>
#include "GlobalHelpers.h"
#include "SelectObject.h"
#include "Item.h"
#include "BlockingQueue.h"
#include "Localization.h"
#include "SmartPointer.h"

#include <string>
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <queue>
#include <shared_mutex>
#include <stack>
#include <array>

CItem::CItem(ITEMTYPE type, LPCWSTR name) : m_name(name), m_type(type)
{
    if (IsType(IT_DRIVE))
    {
        m_name = FormatVolumeNameOfRootPath(m_name);
    }

    if (IsType(IT_FILE))
    {
        if (const LPCWSTR ext = wcsrchr(name, L'.'); ext != nullptr)
        {
            std::wstring exttoadd(&ext[0]);
            _wcslwr_s(exttoadd.data(), exttoadd.size() + 1);

            static std::shared_mutex extlock;
            static std::unordered_set<std::wstring> extcache;
            std::lock_guard lock(extlock);
            const auto cached = extcache.insert(std::move(exttoadd));
            m_extension = cached.first->c_str();
        }
        else
        {
            static LPCWSTR noext = L"";
            m_extension = noext;
        }
    }
    else
    {
        m_ci = new CHILDINFO;
        m_extension = m_name.GetString();
    }
}

CItem::CItem(ITEMTYPE type, LPCWSTR name, FILETIME lastChange,
    ULONGLONG size, DWORD attributes, ULONG files, ULONG subdirs) : CItem(type, name)
{
    m_lastChange = lastChange;
    m_size = size;
    m_attributes = attributes;
    if (m_ci != nullptr)
    {
        m_ci->m_subdirs = subdirs;
        m_ci->m_files = files;
    }
}

CItem::~CItem()
{
    if (m_ci != nullptr)
    {
        for (const auto& m_child : m_ci->m_children)
        {
            delete m_child;
        }
        delete m_ci;
    }
}

CRect CItem::TmiGetRectangle() const
{
    return m_rect;
}

void CItem::TmiSetRectangle(const CRect& rc)
{
    m_rect = rc;
}

bool CItem::DrawSubitem(int subitem, CDC* pdc, CRect rc, UINT state, int* width, int* focusLeft) const
{
    if (subitem == COL_NAME)
    {
        return CTreeListItem::DrawSubitem(subitem, pdc, rc, state, width, focusLeft);
    }
    if (subitem != COL_SUBTREEPERCENTAGE)
    {
        return false;
    }

    const bool showReadJobs = MustShowReadJobs();

    if (showReadJobs && !COptions::PacmanAnimation)
    {
        return false;
    }

    if (showReadJobs && IsDone())
    {
        return false;
    }

    if (width != nullptr)
    {
        *width = GetSubtreePercentageWidth();
        return true;
    }

    DrawSelection(CFileTreeControl::Get(), pdc, rc, state);

    if (showReadJobs)
    {
        constexpr SIZE sizeDeflatePacman = { 1, 2 };
        rc.DeflateRect(sizeDeflatePacman);
        DrawPacman(pdc, rc, CFileTreeControl::Get()->GetItemSelectionBackgroundColor(this));
    }
    else
    {
        rc.DeflateRect(2, 5);
        for (int i = 0; i < GetIndent(); i++)
        {
            rc.left += rc.Width() / 10;
        }

        DrawPercentage(pdc, rc, GetFraction(), GetPercentageColor());
    }
    return true;
}

CStringW CItem::GetText(int subitem) const
{
    CStringW s;
    switch (subitem)
    {
    case COL_NAME:
        {
            s = m_name;
        }
        break;

    case COL_SUBTREEPERCENTAGE:
        if (!IsDone())
        {
            if (GetReadJobs() == 1)
                s = Localization::Lookup(IDS_ONEREADJOB);
            else
                s.FormatMessage(Localization::Lookup(IDS_sREADJOBS), FormatCount(GetReadJobs()).GetString());
        }
        break;

    case COL_PERCENTAGE:
        if (COptions::ShowTimeSpent && MustShowReadJobs() || IsRootItem())
        {
            s.Format(L"[%s s]", FormatMilliseconds(GetTicksWorked() * 1000).GetString());
        }
        else
        {
            s.Format(L"%s%%", FormatDouble(GetFraction() * 100).GetString());
        }
        break;

    case COL_SIZE:
        {
            s = FormatBytes(GetSize());
        }
        break;

    case COL_ITEMS:
        if (!IsType(IT_FILE | IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatCount(GetItemsCount());
        }
        break;

    case COL_FILES:
        if (!IsType(IT_FILE | IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatCount(GetFilesCount());
        }
        break;

    case COL_SUBDIRS:
        if (!IsType(IT_FILE | IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatCount(GetSubdirsCount());
        }
        break;

    case COL_LASTCHANGE:
        if (!IsType(IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatFileTime(m_lastChange);
        }
        break;

    case COL_ATTRIBUTES:
        if (!IsType(IT_FREESPACE | IT_UNKNOWN | IT_MYCOMPUTER))
        {
            s = FormatAttributes(GetAttributes());
        }
        break;

    case COL_OWNER:
        if (IsType(IT_FILE | IT_DIRECTORY))
        {
            s = GetOwner();
        }
        break;

    default:
        {
            ASSERT(0);
        }
        break;
    }
    return s;
}

COLORREF CItem::GetItemTextColor() const
{
    // Get the file/folder attributes
    const DWORD attr = GetAttributes();

    // This happens e.g. on a Unicode-capable FS when using ANSI APIs
    // to list files with ("real") Unicode names
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return CTreeListItem::GetItemTextColor();
    }

    // Check for compressed flag
    if (attr & FILE_ATTRIBUTE_COMPRESSED)
    {
        return CDirStatApp::Get()->AltColor();
    }

    if (attr & FILE_ATTRIBUTE_ENCRYPTED)
    {
        return CDirStatApp::Get()->AltEncryptionColor();
    }

    // The rest is not colored
    return CTreeListItem::GetItemTextColor();
}

int CItem::CompareSibling(const CTreeListItem* tlib, int subitem) const
{
    const CItem* other = reinterpret_cast<const CItem*>(tlib);

    switch (subitem)
    {
        case COL_NAME:
        {
            if (IsType(IT_DRIVE))
            {
                ASSERT(other->IsType(IT_DRIVE));
                return signum(GetPath().CompareNoCase(other->GetPath()));
            }
            else
            {
                return signum(m_name.CompareNoCase(other->m_name));
            }
        }

        case COL_SUBTREEPERCENTAGE:
        {
            if (MustShowReadJobs())
            {
                return usignum(GetReadJobs(), other->GetReadJobs());
            }
            else
            {
                return signum(GetFraction() - other->GetFraction());
            }
        }

        case COL_PERCENTAGE:
        {
            return signum(GetFraction() - other->GetFraction());
        }

        case COL_SIZE:
        {
            return usignum(GetSize(), other->GetSize());
        }

        case COL_ITEMS:
        {
            return usignum(GetItemsCount(), other->GetItemsCount());
        }

        case COL_FILES:
        {
            return usignum(GetFilesCount(), other->GetFilesCount());
        }

        case COL_SUBDIRS:
        {
            return usignum(GetSubdirsCount(), other->GetSubdirsCount());
        }

        case COL_LASTCHANGE:
        {
            if (m_lastChange < other->m_lastChange)
            {
                return -1;
            }
            if (m_lastChange == other->m_lastChange)
            {
                return 0;
            }

            return 1;
        }

        case COL_ATTRIBUTES:
        {
            return signum(GetSortAttributes() - other->GetSortAttributes());
        }

        case COL_OWNER:
        {
            return signum(GetOwner().CompareNoCase(other->GetOwner()));
        }

        default:
        {
            return 0;
        }
    }
}

int CItem::GetTreeListChildCount()const
{
    if (m_ci == nullptr) return 0;
    return static_cast<int>(GetChildren().size());
}

CTreeListItem* CItem::GetTreeListChild(int i) const
{
    return GetChildren()[i];
}

short CItem::GetImageToCache() const
{
    // (Caching is done in CTreeListItem)

    if (IsType(IT_MYCOMPUTER))
    {
        return GetIconImageList()->getMyComputerImage();
    }
    if (IsType(IT_FREESPACE))
    {
        return GetIconImageList()->getFreeSpaceImage();
    }
    if (IsType(IT_UNKNOWN))
    {
        return GetIconImageList()->getUnknownImage();
    }

    const CStringW path = GetPath();
    if (IsType(IT_DIRECTORY) && CDirStatApp::Get()->IsMountPoint(path, m_attributes))
    {
        return GetIconImageList()->getMountPointImage();
    }
    if (IsType(IT_DIRECTORY) && CDirStatApp::Get()->IsJunction(path, m_attributes))
    {
        constexpr DWORD mask = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
        const bool os_file = (GetAttributes() & mask) == mask;
        return os_file ? GetIconImageList()->getJunctionProtectedImage() : GetIconImageList()->getJunctionImage();
    }

    return GetIconImageList()->getFileImage(path);
}

void CItem::DrawAdditionalState(CDC* pdc, const CRect& rcLabel) const
{
    if (!IsRootItem() && this == GetDocument()->GetZoomItem())
    {
        CRect rc = rcLabel;
        rc.InflateRect(1, 0);
        rc.bottom++;

        CSelectStockObject sobrush(pdc, NULL_BRUSH);
        CPen pen(PS_SOLID, 2, GetDocument()->GetZoomColor());
        CSelectObject sopen(pdc, &pen);

        pdc->Rectangle(rc);
    }
}

int CItem::GetSubtreePercentageWidth()
{
    return 105;
}

CItem* CItem::FindCommonAncestor(const CItem* item1, const CItem* item2)
{
    for (auto parent = item1; parent != nullptr; parent = parent->GetParent())
    {
        if (parent->IsAncestorOf(item2)) return const_cast<CItem*>(parent);
    }

    ASSERT(FALSE);
    return nullptr;
}

ULONGLONG CItem::GetProgressRange() const
{
    if (IsType(IT_MYCOMPUTER))
    {
        return GetProgressRangeMyComputer();
    }
    if (IsType(IT_DRIVE))
    {
        return GetProgressRangeDrive();
    }
    if (IsType(IT_FILE | IT_DIRECTORY))
    {
        return 0;
    }

    ASSERT(FALSE);
    return 0;
}

ULONGLONG CItem::GetProgressPos() const
{
    if (IsType(IT_MYCOMPUTER))
    {
        ULONGLONG pos = 0;
        for (const auto& child : GetChildren())
        {
            pos += child->GetProgressPos();
        }
        return pos;
    }
    if (IsType(IT_DRIVE))
    {
        ULONGLONG pos = GetSize();
        const CItem* fs = FindFreeSpaceItem();
        pos -= (fs != nullptr) ? fs->GetSize() : 0;
        return pos;
    }

    return 0;
}

void CItem::UpdateStatsFromDisk()
{
    if (IsType(IT_DIRECTORY | IT_FILE))
    {
        FileFindEnhanced finder;
        if (finder.FindFile(GetFolderPath(),IsType(ITF_ROOTITEM) ? CString() : GetName()))
        {
            SetLastChange(finder.GetLastWriteTime());
            SetAttributes(finder.GetAttributes());

            if (IsType(IT_FILE))
            {
                UpwardSubtractSize(m_size);
                UpwardAddSize(finder.GetFileSize());
            }
        }
    }
    else if (IsType(IT_DRIVE))
    {
        SmartPointer<HANDLE> handle(CloseHandle, CreateFile(GetPath(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (handle != INVALID_HANDLE_VALUE)
        {
            GetFileTime(handle, nullptr, nullptr, &m_lastChange);
        }
    }
}

const std::vector<CItem*>& CItem::GetChildren() const
{
    return m_ci->m_children;
}

CItem* CItem::GetParent() const
{
    return reinterpret_cast<CItem*>(CTreeListItem::GetParent());
}

void CItem::AddChild(CItem* child, bool add_only)
{
    if (!add_only)
    {
        UpwardAddSize(child->m_size);
        UpwardUpdateLastChange(child->m_lastChange);
    }

    child->SetParent(this);

    std::lock_guard m_guard(m_ci->m_protect);
    m_ci->m_children.push_back(child);

    if (IsVisible() && IsExpanded())
    {
        CMainFrame::Get()->InvokeInMessageThread([this, child]
        {
            CFileTreeControl::Get()->OnChildAdded(this, child);
        });
    }
}

void CItem::RemoveChild(CItem* child)
{
    std::lock_guard m_guard(m_ci->m_protect);
    std::erase(m_ci->m_children, child);

    if (IsVisible())
    {
        CMainFrame::Get()->InvokeInMessageThread([this, child]
        {
            CFileTreeControl::Get()->OnChildRemoved(this, child);
        });
    }

    delete child;
}

void CItem::RemoveAllChildren()
{
    if (m_ci == nullptr) return;
    CMainFrame::Get()->InvokeInMessageThread([this]
    {
        CFileTreeControl::Get()->OnRemovingAllChildren(this);
    });

    std::lock_guard m_guard(m_ci->m_protect);
    for (const auto& child : m_ci->m_children)
    {
        delete child;
    }
    m_ci->m_children.clear();
}

void CItem::UpwardAddSubdirs(const ULONG dirCount)
{
    if (dirCount == 0) return;
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (p->IsType(IT_FILE)) continue;
        p->m_ci->m_subdirs += dirCount;
    }
}

void CItem::UpwardSubtractSubdirs(const ULONG dirCount)
{
    if (dirCount == 0) return;
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        ASSERT(p->m_ci->m_subdirs - dirCount >= 0);
        if (p->IsType(IT_FILE)) continue;
        p->m_ci->m_subdirs -= dirCount;
    }
}

void CItem::UpwardAddFiles(const ULONG fileCount)
{
    if (fileCount == 0) return;
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (p->IsType(IT_FILE)) continue;
        p->m_ci->m_files += fileCount;
    }
}

void CItem::UpwardSubtractFiles(const ULONG fileCount)
{
    if (fileCount == 0) return;
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (p->IsType(IT_FILE)) continue;
        ASSERT(p->m_ci->m_files - fileCount >= 0);
        p->m_ci->m_files -= fileCount;
    }
}

void CItem::UpwardAddSize(const ULONGLONG bytes)
{
    if (bytes == 0) return;
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        p->m_size += bytes;
    }
}

void CItem::UpwardSubtractSize(const ULONGLONG bytes)
{
    if (bytes == 0) return;
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        ASSERT(p->m_size - bytes >= 0);
        p->m_size -= bytes;
    }
}

void CItem::UpwardAddReadJobs(const ULONG count)
{
    if (m_ci == nullptr || count == 0) return;
    if (m_ci->m_jobs == 0) m_ci->m_tstart = static_cast<ULONG>(GetTickCount64() / 1000ull);
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (p->IsType(IT_FILE)) continue;
        p->m_ci->m_jobs += count;
    }
}

void CItem::UpwardSubtractReadJobs(const ULONG count)
{
    if (count == 0 || IsType(IT_FILE)) return;
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        ASSERT(p->m_ci->m_jobs - count >= 0);
        p->m_ci->m_jobs -= count;
        if (p->m_ci->m_jobs == 0) p->SetDone();
    }
}

// This method increases the last change
void CItem::UpwardUpdateLastChange(const FILETIME& t)
{
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (CompareFileTime(&t, &p->m_lastChange) == 1) p->m_lastChange = t;
    }
}

void CItem::UpwardRecalcLastChange(bool without_item)
{
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        p->UpdateStatsFromDisk();

        if (p->m_ci == nullptr) continue;
        for (const auto& child : p->GetChildren())
        {
            if (without_item && child == this) continue;
            if (CompareFileTime(&child->m_lastChange, &p->m_lastChange) == 1)
                p->m_lastChange = child->m_lastChange;
        }
    }
}

ULONGLONG CItem::GetSize() const
{
    return m_size;
}

void CItem::SetSize(const ULONGLONG ownSize)
{
    ASSERT(ownSize >= 0);
    m_size = ownSize;
}

ULONG CItem::GetReadJobs() const
{
    if (m_ci == nullptr) return 0;
    return m_ci->m_jobs;
}

FILETIME CItem::GetLastChange() const
{
    return m_lastChange;
}

void CItem::SetLastChange(const FILETIME& t)
{
    m_lastChange = t;
}

void CItem::SetAttributes(const DWORD attr)
{
    m_attributes = attr;
}

// Decode the attributes encoded by SetAttributes()
DWORD CItem::GetAttributes() const
{
    return m_attributes;
}

// Returns a value which resembles sorting of RHSACE considering gaps
unsigned short CItem::GetSortAttributes() const
{
    unsigned short ret = 0;

    // We want to enforce the order RHSACE with R being the highest priority
    // attribute and E being the lowest priority attribute.
    ret |= m_attributes & FILE_ATTRIBUTE_READONLY   ? 1 << 5 : 0; // R
    ret |= m_attributes & FILE_ATTRIBUTE_HIDDEN     ? 1 << 4 : 0; // H
    ret |= m_attributes & FILE_ATTRIBUTE_SYSTEM     ? 1 << 3 : 0; // S
    ret |= m_attributes & FILE_ATTRIBUTE_ARCHIVE    ? 1 << 2 : 0; // A
    ret |= m_attributes & FILE_ATTRIBUTE_COMPRESSED ? 1 << 1 : 0; // C
    ret |= m_attributes & FILE_ATTRIBUTE_ENCRYPTED  ? 1 << 0 : 0; // E

    return ret;
}

double CItem::GetFraction() const
{
    if (!GetParent() || GetParent()->GetSize() == 0)
    {
        return 1.0;
    }
    return static_cast<double>(GetSize()) /
        static_cast<double>(GetParent()->GetSize());
}

bool CItem::IsRootItem() const
{
    return (m_type & ITF_ROOTITEM) != 0;
}

CStringW CItem::GetPath() const
{
    CStringW path = UpwardGetPathWithoutBackslash();
    if (IsType(IT_DRIVE))
    {
        path += L"\\";
    }
    return path;
}


CStringW CItem::GetOwner(bool force) const
{
    if (!IsVisible() && !force)
    {
        return L"";
    }

    // If visible, use cached variable
    CStringW tmp;
    CStringW & ret = (force) ? tmp : m_vi->owner;
    if (!ret.IsEmpty()) return ret;

    // Fetch owner information from disk
    SmartPointer<PSECURITY_DESCRIPTOR> ps(LocalFree);
    PSID sid = nullptr;
    GetNamedSecurityInfo(GetPath(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
        &sid, nullptr, nullptr, nullptr, &ps);
    ret = GetNameFromSid(sid).c_str();
    return ret;
}

bool CItem::HasUncPath() const
{
    const CStringW path = GetPath();
    return path.GetLength() >= 2 && path.Left(2) == L"\\\\";
}

CStringW CItem::GetFindPattern() const
{
    CStringW pattern = GetPath();
    if (pattern.Right(1) != wds::chrBackslash)
    {
        pattern += L"\\";
    }
    pattern += L"*.*";
    return pattern;
}

// Returns the path for "Explorer here" or "Command Prompt here"
CStringW CItem::GetFolderPath() const
{
    CStringW path;

    if (IsType(IT_MYCOMPUTER))
    {
        path = GetParseNameOfMyComputer();
    }
    else
    {
        path = GetPath();
        if (IsType(IT_FILE))
        {
            const int i = path.ReverseFind(wds::chrBackslash);
            ASSERT(i != -1);
            path = path.Left(i + 1);
        }
    }
    return path;
}

// returns the path for the mail-report
CStringW CItem::GetReportPath() const
{
    CStringW path = UpwardGetPathWithoutBackslash();
    if (IsType(IT_DRIVE))
    {
        path += L"\\";
    }
    if (IsType(IT_FREESPACE | IT_UNKNOWN))
    {
        path += GetName();
    }

    return path;
}

CStringW CItem::GetName() const
{
    return m_name;
}

CStringW CItem::GetExtension() const
{
    return m_extension;
}

ULONG CItem::GetFilesCount() const
{
    if (m_ci == nullptr) return 0;
    return m_ci->m_files;
}

ULONG CItem::GetSubdirsCount() const
{
    if (m_ci == nullptr) return 0;
    return m_ci->m_subdirs;
}

ULONGLONG CItem::GetItemsCount() const
{
    if (m_ci == nullptr) return 0;
    return static_cast<ULONGLONG>(m_ci->m_files) + static_cast<ULONGLONG>(m_ci->m_subdirs);
}

void CItem::SetDone()
{
    if (IsDone())
    {
        return;
    }

    if (IsType(IT_DRIVE))
    {
        UpdateFreeSpaceItem();
        UpdateUnknownItem();
    }

    // Sort and set finish time
    if (m_ci != nullptr)
    {
        SortItemsBySize();
        m_ci->m_tfinish = static_cast<ULONG>(GetTickCount64() / 1000ull);
    }

    m_rect = { 0,0,0,0 };
    SetType(ITF_DONE, true);
}

void CItem::SortItemsBySize() const
{
    if (m_ci == nullptr) return;
    
    // sort by size for proper treemap rendering
    std::lock_guard m_guard(m_ci->m_protect);
    m_ci->m_children.shrink_to_fit();
    std::ranges::sort(m_ci->m_children, [](auto item1, auto item2)
    {
        return item1->GetSize() > item2->GetSize(); // biggest first
    });
}

ULONGLONG CItem::GetTicksWorked() const
{
    if (m_ci == nullptr) return 0;
    return m_ci->m_tfinish > 0 ? (m_ci->m_tfinish - m_ci->m_tstart) :
        (m_ci->m_tstart > 0) ? ((GetTickCount64() / 1000ull) - m_ci->m_tstart) : 0;
}

void CItem::ScanItemsFinalize(CItem* item)
{
    if (item == nullptr) return;
    std::stack<CItem*> queue({item});
    while (!queue.empty())
    {
        const auto & qitem = queue.top();
        queue.pop();
        qitem->SetDone();
        if (qitem->IsType(IT_FILE)) continue;
        for (const auto& child : qitem->GetChildren())
        {
            if (!child->IsDone()) queue.push(child);
        }
    }
}

void CItem::ScanItems(BlockingQueue<CItem*> * queue)
{
    while (CItem * item = queue->pop())
    {
        // Used to trigger thread exit condition
        if (item == nullptr) return;

        // Mark the time we started evaluating this node
        if (item->m_ci) item->m_ci->m_tstart = static_cast<ULONG>(GetTickCount64() / 1000ull);

        if (item->IsType(IT_DRIVE | IT_DIRECTORY))
        {
            FileFindEnhanced finder;
            for (BOOL b = finder.FindFile(item->GetPath()); b; b = finder.FindNextFile())
            {
                if (finder.IsDots())
                {
                    continue;
                }
                if (COptions::SkipHidden && finder.IsHidden() ||
                    COptions::SkipProtected && finder.IsHiddenSystem())
                {
                    continue;
                }

                if (finder.IsDirectory())
                {
                    item->UpwardAddSubdirs(1);
                    if (CItem* newitem = item->AddDirectory(finder); newitem->GetReadJobs() > 0)
                    {
                        queue->push(newitem, false);
                    }
                }
                else
                {
                    item->UpwardAddFiles(1);
                    CItem* newitem = item->AddFile(finder);

                    CFileDupeControl::Get()->ProcessDuplicate(newitem);
                }

                // Update pacman position
                item->UpwardDrivePacman();
            }
        }
        else if (item->IsType(IT_FILE))
        {
            // Only used for refreshes
            item->UpdateStatsFromDisk();
            item->SetDone();
        }
        else if (item->IsType(IT_MYCOMPUTER))
        {
            for (const auto & child : item->GetChildren())
            {
                child->UpwardAddReadJobs(1);
                queue->push(child, false);
            }
        }
        item->UpwardSubtractReadJobs(1);
        item->UpwardDrivePacman();
    }
}

void CItem::UpwardSetDone()
{
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        p->SetDone();
    }
}

void CItem::UpwardSetUndone()
{
    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (p->IsType(IT_DRIVE) && p->IsDone())
        {
            if (CItem* unknown = p->FindUnknownItem(); unknown != nullptr)
            {
                p->UpwardSubtractSize(unknown->GetSize());
                unknown->SetSize(0);
            }
        }

        p->SetType(ITF_DONE, false);
    }
}

CItem* CItem::FindRecyclerItem() const
{
    // There are no cross-platform way to consistently identify the recycle bin so attempt
    // to find an item with the most probable to least probable values
    for (const std::wstring& possible : { L"$RECYCLE.BIN", L"RECYCLER", L"RECYCLED" })
    {
        for (const auto& child : GetChildren())
        {
            if (child->IsType(IT_DIRECTORY) && child->GetName().CompareNoCase(possible.c_str()) == 0)
            {
                return child;
            }
        }
    }

    return nullptr;
}

void CItem::CreateFreeSpaceItem()
{
    ASSERT(IsType(IT_DRIVE));

    UpwardSetUndone();

    auto [total, free] = CDirStatApp::getDiskFreeSpace(GetPath());

    const auto freespace = new CItem(IT_FREESPACE, Localization::Lookup(IDS_FREESPACE_ITEM));
    freespace->SetSize(free);
    freespace->SetDone();

    AddChild(freespace);
}

CItem* CItem::FindFreeSpaceItem() const
{
    for (const auto& child : GetChildren())
    {
        if (child->IsType(IT_FREESPACE))
        {
            return child;
        }
    }
    return nullptr;
}

void CItem::UpdateFreeSpaceItem() const
{
    ASSERT(IsType(IT_DRIVE));

    CItem* freeSpaceItem = FindFreeSpaceItem();
    if (freeSpaceItem == nullptr)
    {
        return;
    }

    // Rebaseline as if freespace was not shown
    freeSpaceItem->UpwardSubtractSize(freeSpaceItem->GetSize());

    auto [total, free] = CDirStatApp::getDiskFreeSpace(GetPath());
    freeSpaceItem->UpwardAddSize(free);
}

void CItem::UpdateUnknownItem() const
{
    ASSERT(IsType(IT_DRIVE));

    CItem* unknown = FindUnknownItem();
    if (unknown == nullptr)
    {
        return;
    }

    // Rebaseline as if unknown size was not shown
    unknown->UpwardSubtractSize(unknown->GetSize());

    // Get the tallied size, account for whether the freespace item is part of it
    const CItem* freeSpaceItem = FindFreeSpaceItem();
    const ULONGLONG tallied = GetSize() - (freeSpaceItem ? freeSpaceItem->GetSize() : 0);

    auto [total, free] = CDirStatApp::getDiskFreeSpace(GetPath());
    unknown->UpwardAddSize((tallied > total - free) ? 0 : total - free - tallied);
}

void CItem::RemoveFreeSpaceItem()
{
    ASSERT(IsType(IT_DRIVE));

    if (const auto freespace = FindFreeSpaceItem(); freespace != nullptr)
    {
        UpwardSetUndone();
        UpwardSubtractSize(freespace->GetSize());
        RemoveChild(freespace);
    }
}

void CItem::CreateUnknownItem()
{
    ASSERT(IsType(IT_DRIVE));

    UpwardSetUndone();

    const auto unknown = new CItem(IT_UNKNOWN, Localization::Lookup(IDS_UNKNOWN_ITEM));
    unknown->SetDone();

    AddChild(unknown);
}

CItem* CItem::FindUnknownItem() const
{
    for (const auto& child : GetChildren())
    {
        if (child->IsType(IT_UNKNOWN))
        {
            return child;
        }
    }
    return nullptr;
}

void CItem::RemoveUnknownItem()
{
    ASSERT(IsType(IT_DRIVE));

    if (const auto unknown = FindUnknownItem(); unknown != nullptr)
    {
        UpwardSetUndone();
        UpwardSubtractSize(unknown->GetSize());
        RemoveChild(unknown);
    }
} 

void CItem::CollectExtensionData(CExtensionData* ed) const
{
    std::stack<const CItem*> queue({this});
    while (!queue.empty())
    {
        const auto& qitem = queue.top();
        queue.pop();
        if (qitem->IsType(IT_FILE))
        {
            const CStringW& ext = qitem->GetExtension();
            SExtensionRecord r;
            if (ed->Lookup(ext, r))
            {
                r.bytes += qitem->GetSize();
                r.files++;
            }
            else
            {
                r.bytes = qitem->GetSize();
                r.files = 1;
            }
            ed->SetAt(ext, r);
        }
        else for (const auto& child : qitem->m_ci->m_children)
        {
            queue.push(child);
        }
    }
}

ULONGLONG CItem::GetProgressRangeMyComputer() const
{
    ASSERT(IsType(IT_MYCOMPUTER));

    ULONGLONG range = 0;
    for (const auto& child : GetChildren())
    {
        range += child->GetProgressRangeDrive();
    }
    return range;
}

ULONGLONG CItem::GetProgressRangeDrive() const
{
    auto [total, free] = CDirStatApp::getDiskFreeSpace(GetPath());

    total -= free;

    ASSERT(total >= 0);
    return total;
}

COLORREF CItem::GetGraphColor() const
{
    if (IsType(IT_UNKNOWN))
    {
        return RGB(255, 255, 0) | CTreemap::COLORFLAG_LIGHTER;
    }

    if (IsType(IT_FREESPACE))
    {
        return RGB(100, 100, 100) | CTreemap::COLORFLAG_DARKER;
    }

    if (IsType(IT_FILE))
    {
        return GetDocument()->GetCushionColor(GetExtension());
    }

    return RGB(0, 0, 0);
 }

bool CItem::MustShowReadJobs() const
{
    if (GetParent() != nullptr)
    {
        return !GetParent()->IsDone();
    }

    return !IsDone();
}

COLORREF CItem::GetPercentageColor() const
{
    const int i = GetIndent() % COptions::TreeListColorCount;
    return std::vector<COLORREF>
    {
        COptions::TreeListColor0,
        COptions::TreeListColor1,
        COptions::TreeListColor2,
        COptions::TreeListColor3,
        COptions::TreeListColor4,
        COptions::TreeListColor5,
        COptions::TreeListColor6,
        COptions::TreeListColor7
    } [i] ;
}

CStringW CItem::UpwardGetPathWithoutBackslash() const
{
    // allow persistent storage to prevent constant reallocation
    thread_local CStringW path;
    path = L"\\";

    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (p->IsType(IT_DIRECTORY))
        {
            path = p->m_name + L"\\" + path;
        }
        else if (p->IsType(IT_FILE))
        {
            path = p->m_name;
        }
        else if (p->IsType(IT_DRIVE))
        {
            path = PathFromVolumeName(p->m_name) + L"\\" + path;
        }
    }

    return path.TrimRight(L'\\');
}

CItem* CItem::AddDirectory(const FileFindEnhanced& finder)
{
    const bool follow = !finder.IsProtectedReparsePoint() &&
        CDirStatApp::Get()->IsFollowingAllowed(finder.GetFilePath(), finder.GetAttributes());

    const auto & child = new CItem(IT_DIRECTORY, finder.GetFileName());
    child->SetLastChange(finder.GetLastWriteTime());
    child->SetAttributes(finder.GetAttributes());
    AddChild(child);
    child->UpwardAddReadJobs(follow ? 1 : 0);
    return child;
}

CItem* CItem::AddFile(const FileFindEnhanced& finder)
{
    const auto & child = new CItem(IT_FILE, finder.GetFileName());
    child->SetSize(finder.GetFileSize());
    child->SetLastChange(finder.GetLastWriteTime());
    child->SetAttributes(finder.GetAttributes());
    AddChild(child);
    child->SetDone();
    return child;
}

void CItem::UpwardDrivePacman()
{
    if (!COptions::PacmanAnimation)
    {
        return;
    }

    for (auto p = this; p != nullptr; p = p->GetParent())
    {
        if (p->IsType(IT_FILE) || !p->IsVisible()) continue;
        if (p->GetReadJobs() == 0) p->StopPacman();
        else p->DrivePacman();
    }
}

std::wstring CItem::GetFileHash(bool const partial)
{
    // Initialize hash for this thread
    constexpr auto BufferSizePartial = 1024ul * 1024ul;
    constexpr auto BufferSizeFull = 4ul * 1024ul * 1024ul;
    thread_local std::array<BYTE, BufferSizeFull> FileBuffer;
    thread_local std::vector<BYTE> Hash;
    thread_local SmartPointer<BCRYPT_HASH_HANDLE> HashHandle(BCryptDestroyHash);
    thread_local DWORD HashLength = 0;
    const auto BufferSize = partial ? BufferSizePartial : BufferSizeFull;
    if (HashLength == 0)
    {
        BCRYPT_ALG_HANDLE AlgHandle = nullptr;
        DWORD ResultLength = 0;
        if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER, BCRYPT_HASH_REUSABLE_FLAG) != 0 ||
            BCryptGetProperty(AlgHandle, BCRYPT_HASH_LENGTH, reinterpret_cast<PBYTE>(&HashLength), sizeof(HashLength), &ResultLength, 0) != 0 ||
            BCryptCreateHash(AlgHandle, &HashHandle, nullptr, 0, nullptr, 0, BCRYPT_HASH_REUSABLE_FLAG) != 0)
        {
            return {};
        }
    }

    // Open file for reading
    SmartPointer<HANDLE> hFile(CloseHandle, CreateFile(GetPath(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    // Hash data one read at a time
    DWORD iReadResult = 0;
    DWORD iHashResult = 0;
    DWORD iReadBytes = 0;
    Hash.resize(HashLength);
    while ((iReadResult = ReadFile(hFile, FileBuffer.data(), BufferSize, &iReadBytes, nullptr)) != 0 && iReadBytes > 0)
    {
        UpwardDrivePacman();
        iHashResult = BCryptHashData(HashHandle, FileBuffer.data(), iReadBytes, 0);
        if (iHashResult != 0 || partial) break;
    }

    // Complete hash data
    if (iHashResult != 0 || iReadResult == 0 ||
        BCryptFinishHash(HashHandle, Hash.data(), HashLength, 0) != 0)
    {
        return {};
    }

    // Convert to hex string
    std::wstring sHash;
    sHash.resize(2ull * HashLength);
    DWORD iHashStringLength = static_cast<DWORD>(sHash.size() + 1ull);
    CryptBinaryToStringW(Hash.data(), HashLength, CRYPT_STRING_HEXRAW | CRYPT_STRING_NOCRLF,
        sHash.data(), &iHashStringLength);
    return sHash;
}
