// DirStatDoc.cpp - Implementation of CDirStatDoc
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
#include "CsvLoader.h"
#include "deletewarningdlg.h"
#include "DirStatDoc.h"
#include "FileTreeView.h"
#include "GlobalHelpers.h"
#include "TreeMapView.h"
#include "Item.h"
#include "Localization.h"
#include "MainFrame.h"
#include "ModalShellApi.h"
#include "WinDirStat.h"
#include <common/CommonHelpers.h>
#include <common/MdExceptions.h>
#include <common/SmartPointer.h>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stack>

CDirStatDoc* _theDocument;

CDirStatDoc* GetDocument()
{
    return _theDocument;
}

IMPLEMENT_DYNCREATE(CDirStatDoc, CDocument)

CDirStatDoc::CDirStatDoc() :
        m_showFreeSpace(COptions::ShowFreeSpace)
      , m_showUnknown(COptions::ShowUnknown)
{
    ASSERT(nullptr == _theDocument);
    _theDocument = this;

    VTRACE(L"sizeof(CItem) = %zd", sizeof(CItem));
}

CDirStatDoc::~CDirStatDoc()
{
    delete m_rootItem;
    _theDocument = nullptr;
}

// Encodes a selection from the CSelectDrivesDlg into a string which can be routed as a pseudo
// document "path" through MFC and finally arrives in OnOpenDocument().
//
CStringW CDirStatDoc::EncodeSelection(RADIO radio, const CStringW& folder, const CStringArray& drives)
{
    CStringW ret;
    switch (radio)
    {
    case RADIO_ALLLOCALDRIVES:
    case RADIO_SOMEDRIVES:
        {
            for (int i = 0; i < drives.GetSize(); i++)
            {
                if (i > 0)
                {
                    ret += CStringW(GetEncodingSeparator());
                }
                ret += drives[i];
            }
        }
        break;

    case RADIO_AFOLDER:
        {
            ret = folder;
        }
        break;
    }
    return ret;
}

// The inverse of EncodeSelection
//
void CDirStatDoc::DecodeSelection(const CStringW& s, CStringW& folder, CStringArray& drives)
{
    folder.Empty();
    drives.RemoveAll();

    // s is either something like "C:\programme"
    // or something like "C:|D:|E:".

    CStringArray sa;
    int i = 0;

    while (i < s.GetLength())
    {
        CStringW token;
        while (i < s.GetLength() && s[i] != GetEncodingSeparator())
        {
            token += s[i++];
        }

        token.TrimLeft();
        token.TrimRight();
        ASSERT(!token.IsEmpty());
        sa.Add(token);

        if (i < s.GetLength())
        {
            i++;
        }
    }

    ASSERT(sa.GetSize() > 0);

    if (sa.GetSize() > 1)
    {
        for (int j = 0; j < sa.GetSize(); j++)
        {
            CStringW d = sa[j];
            ASSERT(2 == d.GetLength());
            ASSERT(wds::chrColon == d[1]);

            drives.Add(d + L"\\");
        }
    }
    else
    {
        CStringW f = sa[0];
        if (2 == f.GetLength() && wds::chrColon == f[1])
        {
            drives.Add(f + L"\\");
        }
        else
        {
            // Remove trailing backslash, if any and not drive-root.
            if (f.GetLength() > 0 && wds::strBackslash == f.Right(1) && (f.GetLength() != 3 || f[1] != wds::chrColon))
            {
                f = f.Left(f.GetLength() - 1);
            }

            folder = f;
        }
    }
}

WCHAR CDirStatDoc::GetEncodingSeparator()
{
    return wds::chrPipe; // This character must be one, which is not allowed in file names.
}

void CDirStatDoc::DeleteContents()
{
    CWaitCursor wc;

    // Wait for system to fully shutdown
    ShutdownCoordinator();

    // Cleanup structures
    delete m_rootItemDupe;
    delete m_rootItem;
    m_rootItem = nullptr;
    m_zoomItem = nullptr;
    CDirStatApp::Get()->ReReadMountPoints();
}

BOOL CDirStatDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument())
    {
        return FALSE;
    }

    UpdateAllViews(nullptr, HINT_NEWROOT);
    return TRUE;
}

BOOL CDirStatDoc::OnOpenDocument(LPCWSTR lpszPathName)
{
    // Temporary minimize extra reviews
    CMainFrame::Get()->MinimizeTreeMapView();
    CMainFrame::Get()->MinimizeExtensionView();

    // Prepare for new root and delete any existing data
    CDocument::OnNewDocument();

    // Decode list of folders to scan
    const CStringW spec = lpszPathName;
    CStringW folder;
    CStringArray drives;
    DecodeSelection(spec, folder, drives);

    // Determine if we should add multiple drives under a single node
    CStringArray rootFolders;
    if (drives.GetSize() > 0)
    {
        m_showMyComputer = drives.GetSize() > 1;
        for (int i = 0; i < drives.GetSize(); i++)
        {
            rootFolders.Add(drives[i]);
        }
    }
    else
    {
        ASSERT(!folder.IsEmpty());
        m_showMyComputer = false;
        rootFolders.Add(folder);
    }

    CArray<CItem*, CItem*> driveItems;

    if (m_showMyComputer)
    {
        m_rootItem = new CItem(IT_MYCOMPUTER | ITF_ROOTITEM, Localization::Lookup(IDS_MYCOMPUTER));
        for (int i = 0; i < rootFolders.GetSize(); i++)
        {
            const auto drive = new CItem(IT_DRIVE, rootFolders[i]);
            driveItems.Add(drive);
            m_rootItem->AddChild(drive);
        }
    }
    else
    {
        const ITEMTYPE type = IsDrive(rootFolders[0]) ? IT_DRIVE : IT_DIRECTORY;
        m_rootItem = new CItem(type | ITF_ROOTITEM, rootFolders[0]);
        if (m_rootItem->IsType(IT_DRIVE))
        {
            driveItems.Add(m_rootItem);
        }
        m_rootItem->UpdateStatsFromDisk();
    }
    m_zoomItem = m_rootItem;

    // Set new node for duplicate view
    m_rootItemDupe = new CItemDupe();

    // Update new root for display
    UpdateAllViews(nullptr, HINT_NEWROOT);
    StartupCoordinator(std::vector({ GetDocument()->GetRootItem() }));
    return true;
}

BOOL CDirStatDoc::OnOpenDocument(CItem * newroot)
{
    CMainFrame::Get()->MinimizeTreeMapView();
    CMainFrame::Get()->MinimizeExtensionView();

    CDocument::OnNewDocument(); // --> DeleteContents()

    m_rootItemDupe = new CItemDupe();
    m_rootItem = newroot;
    m_zoomItem = m_rootItem;

    UpdateAllViews(nullptr, HINT_NEWROOT);
    StartupCoordinator({});
    return true;
}

// We don't want MFCs AfxFullPath()-Logic, because lpszPathName
// is not a path. So we have overridden this.
//
void CDirStatDoc::SetPathName(const LPCWSTR lpszPathName, BOOL /*bAddToMRU*/)
{
    // MRU would be fine but is not implemented yet.

    m_strPathName = lpszPathName;
    ASSERT(!m_strPathName.IsEmpty()); // must be set to something
    m_bEmbedded = FALSE;
    SetTitle(lpszPathName);

    ASSERT_VALID(this);
}

// Prefix the window title (with percentage or "Scanning")
//
void CDirStatDoc::SetTitlePrefix(const CStringW& prefix) const
{
    static CStringW suffix = IsAdmin() ? L" (Administrator)" : L"";
    CStringW docName = prefix + L" " + GetTitle() + L" " + suffix;
    CMainFrame::Get()->UpdateFrameTitleForDocument(docName.Trim());
}

COLORREF CDirStatDoc::GetCushionColor(LPCWSTR ext)
{
    SExtensionRecord r;
    VERIFY(GetExtensionData()->Lookup(ext, r));
    return r.color;
}

COLORREF CDirStatDoc::GetZoomColor()
{
    return RGB(0, 0, 255);
}

const CExtensionData* CDirStatDoc::GetExtensionData()
{
    if (!m_extensionDataValid)
    {
        RebuildExtensionData();
    }
    return &m_extensionData;
}

ULONGLONG CDirStatDoc::GetRootSize() const
{
    ASSERT(m_rootItem != nullptr);
    ASSERT(IsRootDone());
    return m_rootItem->GetSizePhysical();
}

bool CDirStatDoc::IsDrive(const CStringW& spec)
{
    return 3 == spec.GetLength() && wds::chrColon == spec[1] && wds::chrBackslash == spec[2];
}

// Starts a refresh of all mount points in our tree.
// Called when the user changes the follow mount points option.
//
void CDirStatDoc::RefreshReparsePointItems()
{
    CWaitCursor wc;
    
    if (CItem* root = GetRootItem(); nullptr != root)
    {
        RecurseRefreshReparsePoints(root);
    }
}

bool CDirStatDoc::HasRootItem() const
{
    return m_rootItem != nullptr;
}

bool CDirStatDoc::IsRootDone() const
{
    return HasRootItem() && m_rootItem->IsDone();
}

CItem* CDirStatDoc::GetRootItem() const
{
    return m_rootItem;
}

CItem* CDirStatDoc::GetZoomItem() const
{
    return m_zoomItem;
}

CItemDupe* CDirStatDoc::GetRootItemDupe() const
{
    return m_rootItemDupe;
}

bool CDirStatDoc::IsZoomed() const
{
    return GetZoomItem() != GetRootItem();
}

void CDirStatDoc::SetHighlightExtension(const LPCWSTR ext)
{
    m_highlightExtension = ext;
    CMainFrame::Get()->SetSelectionMessageText();
}

CStringW CDirStatDoc::GetHighlightExtension()
{
    return m_highlightExtension;
}

// The very root has been deleted.
//
void CDirStatDoc::UnlinkRoot()
{
    CMainFrame::Get()->InvokeInMessageThread([this]
    {
        DeleteContents();
        UpdateAllViews(nullptr, HINT_NEWROOT);
    });
}

// Determines, whether an UDC works for a given item.
//
bool CDirStatDoc::UserDefinedCleanupWorksForItem(USERDEFINEDCLEANUP* udc, const CItem* item)
{
    return item != nullptr &&
        (item->IsType(IT_DRIVE) && udc->worksForDrives) ||
        (item->IsType(IT_DIRECTORY) && udc->worksForDirectories) ||
        (item->IsType(IT_FILE) && udc->worksForFiles) ||
        (item->HasUncPath() && udc->worksForUncPaths);
}

void CDirStatDoc::OpenItem(const CItem* item, LPCWSTR verb)
{
    ASSERT(item != nullptr);

    // determine path to feed into shell function
    SmartPointer<LPITEMIDLIST> pidl(CoTaskMemFree, nullptr);
    if (item->IsType(IT_MYCOMPUTER))
    {
        (void) SHGetSpecialFolderLocation(nullptr, CSIDL_DRIVES, &pidl);
    }
    else
    {
        pidl = ILCreateFromPath(item->GetPath());
    }

    // launch properties dialog
    SHELLEXECUTEINFO sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.hwnd = *AfxGetMainWnd();
    sei.lpVerb = verb;
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_IDLIST;
    sei.lpIDList = pidl;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteEx(&sei);
}

void CDirStatDoc::RecurseRefreshReparsePoints(CItem* item)
{
    std::vector<CItem*> to_refresh;

    if (item == nullptr) return;
    std::stack<CItem*> reparse_stack({item});
    while (!reparse_stack.empty())
    {
        const auto& qitem = reparse_stack.top();
        reparse_stack.pop();

        if (!item->IsType(IT_DIRECTORY | IT_DRIVE)) continue;
        for (const auto& child : qitem->GetChildren())
        {
            if (!child->IsType(IT_DIRECTORY | IT_DRIVE | ITF_ROOTITEM))
            {
                continue;
            }

            if (CDirStatApp::Get()->GetReparseInfo()->IsReparsePoint(child->GetAttributes()) &&
                CDirStatApp::Get()->IsFollowingAllowed(child->GetPathLong(), child->GetAttributes()))
            {
                to_refresh.push_back(child);
            }
            else
            {
                reparse_stack.push(child);
            }
        }
    }

    if (!to_refresh.empty())
    {
        RefreshItem(to_refresh);
    }
}

// Gets all items of type IT_DRIVE.
//
std::vector<CItem*> CDirStatDoc::GetDriveItems() const
{
    std::vector<CItem*> drives;
    CItem* root = GetRootItem();

    if (nullptr == root)
    {
        return drives;
    }


    if (root->IsType(IT_MYCOMPUTER))
    {
        for (const auto& child : root->GetChildren())
        {
            ASSERT(child->IsType(IT_DRIVE));
            drives.push_back(child);
        }
    }
    else if (root->IsType(IT_DRIVE))
    {
        drives.push_back(root);
    }

    return drives;
}

void CDirStatDoc::RefreshRecyclers() const
{
    std::vector<CItem*> to_refresh;
    for (const auto & drive : GetDriveItems())
    {
        if (CItem* recycler = drive->FindRecyclerItem(); recycler != nullptr)
        {
            to_refresh.push_back(recycler);
        }
    }

    if (!to_refresh.empty()) GetDocument()->StartupCoordinator(to_refresh);
}

void CDirStatDoc::RebuildExtensionData()
{
    CWaitCursor wc;

    m_extensionData.RemoveAll();
    if (IsRootDone())
    {
        m_rootItem->CollectExtensionData(&m_extensionData);
    }
    
    CStringArray sortedExtensions;
    SortExtensionData(sortedExtensions);
    SetExtensionColors(sortedExtensions);

    m_extensionDataValid = true;
}

void CDirStatDoc::SortExtensionData(CStringArray& sortedExtensions)
{
    sortedExtensions.SetSize(m_extensionData.GetCount());

    POSITION pos = m_extensionData.GetStartPosition();
    for (int i = 0; pos != nullptr; i++)
    {
        CStringW ext;
        SExtensionRecord r;
        m_extensionData.GetNextAssoc(pos, ext, r);

        sortedExtensions[i] = ext;
    }

    _pqsortExtensionData = &m_extensionData;
    qsort(sortedExtensions.GetData(), sortedExtensions.GetSize(), sizeof(CStringW), [](LPCVOID item1, LPCVOID item2)
    {
        const CStringW* ext1 = static_cast<const CStringW*>(item1);
        const CStringW* ext2 = static_cast<const CStringW*>(item2);
        SExtensionRecord r1;
        SExtensionRecord r2;
        VERIFY(_pqsortExtensionData->Lookup(*ext1, r1));
        VERIFY(_pqsortExtensionData->Lookup(*ext2, r2));
        return usignum(r2.bytes, r1.bytes);
    });
    _pqsortExtensionData = nullptr;
}

void CDirStatDoc::SetExtensionColors(const CStringArray& sortedExtensions)
{
    static CArray<COLORREF, COLORREF&> colors;

    if (colors.IsEmpty())
    {
        CTreemap::GetDefaultPalette(colors);
    }

    for (int i = 0; i < sortedExtensions.GetSize(); i++)
    {
        COLORREF c = colors[colors.GetSize() - 1];
        if (i < colors.GetSize())
        {
            c = colors[i];
        }
        m_extensionData[sortedExtensions[i]].color = c;
    }
}

CExtensionData* CDirStatDoc::_pqsortExtensionData;

// Deletes a file or directory via SHFileOperation.
// Return: false, if canceled
//
bool CDirStatDoc::DeletePhysicalItems(const std::vector<CItem*>& items, bool toTrashBin)
{
    if (COptions::ShowDeleteWarning)
    {
        CDeleteWarningDlg warning(items);
        if (IDYES != warning.DoModal())
        {
            return false;
        }
        COptions::ShowDeleteWarning = !warning.m_dontShowAgain;
    }

    // Fetch the parent item of the current focus / selected item so we can reselect
    const auto reselect = CFileTreeControl::Get()->GetItem(
        CFileTreeControl::Get()->GetSelectionMark())->GetParent();

    CModalShellApi msa;
    for (const auto& item : items)
    {
        msa.DeleteFile(item->GetPath(), toTrashBin);
    }

    RefreshItem(items);

    // Attempt to reselect the item
    CFileTreeControl::Get()->SelectItem(reselect, true, true);

    return true;
}

void CDirStatDoc::SetZoomItem(CItem* item)
{
    m_zoomItem = item;
    UpdateAllViews(nullptr, HINT_ZOOMCHANGED);
}

// Starts a refresh of an item.
// If the physical item has been deleted,
// updates selection, zoom and working item accordingly.
//
void CDirStatDoc::RefreshItem(const std::vector<CItem*>& item)
{
    GetDocument()->StartupCoordinator(item);
}

// UDC confirmation Dialog.
//
void CDirStatDoc::AskForConfirmation(USERDEFINEDCLEANUP* udc, const CItem* item)
{
    if (!udc->askForConfirmation)
    {
        return;
    }

    CStringW msg;
    msg.FormatMessage(udc->recurseIntoSubdirectories ? Localization::Lookup(IDS_RUDC_CONFIRMATIONss) :
        Localization::Lookup(IDS_UDC_CONFIRMATIONss), udc->title.Obj().c_str(), item->GetPath().GetString());

    if (IDYES != AfxMessageBox(msg, MB_YESNO))
    {
        AfxThrowUserException();
    }
}

void CDirStatDoc::PerformUserDefinedCleanup(USERDEFINEDCLEANUP* udc, const CItem* item)
{
    CWaitCursor wc;

    const CStringW path = item->GetPath();

    // Verify that path still exists
    if (item->IsType(IT_DIRECTORY | IT_DRIVE))
    {
        if (!FolderExists(path) && !DriveExists(path))
        {
            MdThrowStringExceptionF(Localization::Lookup(IDS_THEDIRECTORYsDOESNOTEXIST), path.GetString());
        }
    }
    else
    {
        ASSERT(item->IsType(IT_FILE));

        if (!::PathFileExists(path))
        {
            MdThrowStringExceptionF(Localization::Lookup(IDS_THEFILEsDOESNOTEXIST), path.GetString());
        }
    }

    if (udc->recurseIntoSubdirectories)
    {
        ASSERT(item->IsType(IT_DRIVE | IT_DIRECTORY));

        RecursiveUserDefinedCleanup(udc, path, path);
    }
    else
    {
        CallUserDefinedCleanup(item->IsType(IT_DIRECTORY | IT_DRIVE), udc->commandLine.Obj().c_str(), path, path, udc->showConsoleWindow, udc->waitForCompletion);
    }
}

void CDirStatDoc::RefreshAfterUserDefinedCleanup(const USERDEFINEDCLEANUP* udc, CItem* item)
{
    switch (static_cast<REFRESHPOLICY>(udc->refreshPolicy.Obj()))
    {
    case RP_NO_REFRESH:
        break;

    case RP_REFRESH_THIS_ENTRY:
        {
            RefreshItem(item);
        }
        break;

    case RP_REFRESH_THIS_ENTRYS_PARENT:
        {
            RefreshItem(nullptr == item->GetParent() ? item : item->GetParent());
        }
        break;

    // case RP_ASSUME_ENTRY_HAS_BEEN_DELETED:
    // Feature not implemented.
    // break;

    default:
        ASSERT(FALSE);
    }
}

void CDirStatDoc::RecursiveUserDefinedCleanup(USERDEFINEDCLEANUP* udc, const CStringW& rootPath, const CStringW& currentPath)
{
    // (Depth first.)

    FileFindEnhanced finder;
    for (BOOL b = finder.FindFile(currentPath); b; b = finder.FindNextFile())
    {
        if (finder.IsDots() || !finder.IsDirectory())
        {
            continue;
        }
        if (!CDirStatApp::Get()->IsFollowingAllowed(finder.GetFilePath(), finder.GetAttributes()))
        {
            continue;
        }

        RecursiveUserDefinedCleanup(udc, rootPath, finder.GetFilePath());
    }

    CallUserDefinedCleanup(true, udc->commandLine.Obj().c_str(), rootPath, currentPath, udc->showConsoleWindow, true);
}

void CDirStatDoc::CallUserDefinedCleanup(bool isDirectory, const CStringW& format, const CStringW& rootPath, const CStringW& currentPath, bool showConsoleWindow, bool wait)
{
    const CStringW userCommandLine = BuildUserDefinedCleanupCommandLine(format, rootPath, currentPath);

    const CStringW app = GetCOMSPEC();
    CStringW cmdline;
    cmdline.Format(L"%s /C %s", GetBaseNameFromPath(app).GetString(), userCommandLine.GetString());
    const CStringW directory = isDirectory ? currentPath : GetFolderNameFromPath(currentPath);

    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = showConsoleWindow ? SW_SHOWNORMAL : SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcess(app, cmdline.GetBuffer(), nullptr,
        nullptr, false, 0, nullptr,
        directory, &si, &pi))
    {
        MdThrowStringExceptionF(Localization::Lookup(IDS_COULDNOTCREATEPROCESSssss), app.GetString(),
            cmdline.GetString(), directory.GetString(), MdGetWinErrorText(::GetLastError()).GetString());
        return;
    }

    CloseHandle(pi.hThread);

    if (wait)
    {
        WaitForHandleWithRepainting(pi.hProcess);
    }

    CloseHandle(pi.hProcess);
}

CStringW CDirStatDoc::BuildUserDefinedCleanupCommandLine(LPCWSTR format, LPCWSTR rootPath, LPCWSTR currentPath)
{
    const CStringW rootName    = GetBaseNameFromPath(rootPath);
    const CStringW currentName = GetBaseNameFromPath(currentPath);

    CStringW s = format;

    // Because file names can contain "%", we first replace our placeholders with
    // strings which contain a forbidden character.
    s.Replace(L"%p", L">p");
    s.Replace(L"%n", L">n");
    s.Replace(L"%sp", L">sp");
    s.Replace(L"%sn", L">sn");

    // Now substitute
    s.Replace(L">p", rootPath);
    s.Replace(L">n", rootName);
    s.Replace(L">sp", currentPath);
    s.Replace(L">sn", currentName);

    return s;
}

void CDirStatDoc::PushReselectChild(CItem* item)
{
    m_reselectChildStack.AddHead(item);
}

CItem* CDirStatDoc::PopReselectChild()
{
    return m_reselectChildStack.RemoveHead();
}

void CDirStatDoc::ClearReselectChildStack()
{
    m_reselectChildStack.RemoveAll();
}

bool CDirStatDoc::IsReselectChildAvailable() const
{
    return !m_reselectChildStack.IsEmpty();
}

bool CDirStatDoc::DirectoryListHasFocus()
{
    return LF_DIRECTORYLIST == CMainFrame::Get()->GetLogicalFocus();
}

bool CDirStatDoc::DuplicateListHasFocus()
{
    return LF_DUPLICATELIST == CMainFrame::Get()->GetLogicalFocus();
}

std::vector<CItem*> CDirStatDoc::GetAllSelected()
{
    return DuplicateListHasFocus() ? CFileDupeControl::Get()->GetAllSelected<CItem>() :
        CFileTreeControl::Get()->GetAllSelected<CItem>();
}

void CDirStatDoc::OnUpdateCentralHandler(CCmdUI* pCmdUI)
{
    struct command_filter
    {
        bool allow_none = false;       // allow display when nothing is selected
        bool allow_many = false;       // allow display when multiple items are selected
        bool allow_early = false;      // allow display before processing is finished
        bool tree_focus = false;       // only display in tree view
        ITEMTYPE types_allow = IT_ANY; // only display if these types are allowed
        bool (*extra)(CItem*) = [](CItem*) { return true; }; // extra checks
    };

    // special conditions
    static auto doc = this;
    static bool (*can_zoom_out)(CItem*) = [](CItem*) { return doc->GetZoomItem() != doc->GetRootItem(); };
    static bool (*parent_not_null)(CItem*) = [](CItem* item) { return item != nullptr && item->GetParent() != nullptr; };
    static bool (*reslect_avail)(CItem*) = [](CItem*) { return doc->IsReselectChildAvailable(); };
    static bool (*not_root)(CItem*) = [](CItem* item) { return item != nullptr && !item->IsRootItem(); };
    static bool (*is_suspended)(CItem*) = [](CItem*) { return CMainFrame::Get()->IsScanSuspended(); };
    static bool (*is_not_suspended)(CItem*) = [](CItem*) { return doc->HasRootItem() && !doc->IsRootDone() && !CMainFrame::Get()->IsScanSuspended(); };

    static std::unordered_map<UINT, const command_filter> filters
    {
        // ID                           none   many   early  focus  types
        { ID_REFRESH_ALL,             { true,  true,  false, false, IT_ANY} },
        { ID_REFRESH_SELECTED,        { false, true,  false, false, IT_MYCOMPUTER | IT_DRIVE | IT_DIRECTORY | IT_FILE } },
        { ID_SAVE_RESULTS,            { true,  true,  false, false, IT_ANY} },
        { ID_EDIT_COPY_CLIPBOARD,     { false, true,  true,  false, IT_DRIVE | IT_DIRECTORY | IT_FILE } },
        { ID_CLEANUP_EMPTY_BIN,       { true,  true,  false, false, IT_ANY} },
        { ID_TREEMAP_RESELECT_CHILD,  { true,  true,  true,  false, IT_ANY, reslect_avail } },
        { ID_TREEMAP_SELECT_PARENT,   { false, false, true,  false, IT_ANY, parent_not_null } },
        { ID_TREEMAP_ZOOMIN,          { false, false, false, false, IT_DRIVE | IT_DIRECTORY} },
        { ID_TREEMAP_ZOOMOUT,         { false, false, false, false, IT_DIRECTORY, can_zoom_out } },
        { ID_CLEANUP_EXPLORER_SELECT, { false, true,  true,  false, IT_DIRECTORY | IT_FILE } },
        { ID_CLEANUP_OPEN_IN_CONSOLE, { false, true,  true,  false, IT_DRIVE | IT_DIRECTORY | IT_FILE } },
        { ID_SCAN_RESUME,             { true,  true,  true,  false, IT_ANY, is_suspended } },
        { ID_SCAN_SUSPEND,            { true,  true,  true,  false, IT_ANY, is_not_suspended } },
        { ID_CLEANUP_DELETE_BIN,      { false, true,  false,  true, IT_DIRECTORY | IT_FILE, not_root } },
        { ID_CLEANUP_DELETE,          { false, true,  false,  true, IT_DIRECTORY | IT_FILE, not_root } },
        { ID_CLEANUP_OPEN_SELECTED,   { false, true,  true,  false, IT_MYCOMPUTER | IT_DRIVE | IT_DIRECTORY | IT_FILE } },
        { ID_CLEANUP_PROPERTIES,      { false, true,  true,  false, IT_MYCOMPUTER | IT_DRIVE | IT_DIRECTORY | IT_FILE } }
    };

    if (!filters.contains(pCmdUI->m_nID))
    {
        ASSERT(FALSE);
        return;
    }

    const auto& filter = filters[pCmdUI->m_nID];
    const auto& items = GetAllSelected();

    bool allow = true;
    allow &= !filter.tree_focus || DirectoryListHasFocus() || DuplicateListHasFocus();
    allow &= filter.allow_none || !items.empty();
    allow &= filter.allow_many || items.size() <= 1;
    allow &= filter.allow_early || IsRootDone();
    if (items.empty()) allow &= filter.extra(nullptr);
    for (const auto& item : items)
    {
        allow &= filter.extra(item);
        allow &= item->IsType(filter.types_allow);
    }

    pCmdUI->Enable(allow); 
}

#define ON_COMMAMD_UPDATE_WRAPPER(x,y) ON_COMMAND(x, y) ON_UPDATE_COMMAND_UI(x, OnUpdateCentralHandler)
BEGIN_MESSAGE_MAP(CDirStatDoc, CDocument) 
    ON_COMMAMD_UPDATE_WRAPPER(ID_REFRESH_SELECTED, OnRefreshSelected)
    ON_COMMAMD_UPDATE_WRAPPER(ID_REFRESH_ALL, OnRefreshAll)
    ON_COMMAND(ID_LOAD_RESULTS, OnLoadResults)
    ON_COMMAMD_UPDATE_WRAPPER(ID_SAVE_RESULTS, OnSaveResults)
    ON_COMMAMD_UPDATE_WRAPPER(ID_EDIT_COPY_CLIPBOARD, OnEditCopy)
    ON_COMMAMD_UPDATE_WRAPPER(ID_CLEANUP_EMPTY_BIN, OnCleanupEmptyRecycleBin)
    ON_UPDATE_COMMAND_UI(ID_VIEW_SHOWFREESPACE, OnUpdateViewShowFreeSpace)
    ON_COMMAND(ID_VIEW_SHOWFREESPACE, OnViewShowFreeSpace)
    ON_UPDATE_COMMAND_UI(ID_VIEW_SHOWUNKNOWN, OnUpdateViewShowUnknown)
    ON_COMMAND(ID_VIEW_SHOWUNKNOWN, OnViewShowUnknown)
    ON_COMMAMD_UPDATE_WRAPPER(ID_TREEMAP_ZOOMIN, OnTreemapZoomIn)
    ON_COMMAMD_UPDATE_WRAPPER(ID_TREEMAP_ZOOMOUT, OnTreemapZoomOut)
    ON_COMMAMD_UPDATE_WRAPPER(ID_CLEANUP_EXPLORER_SELECT, OnExplorerSelect)
    ON_COMMAMD_UPDATE_WRAPPER(ID_CLEANUP_OPEN_IN_CONSOLE, OnCommandPromptHere)
    ON_COMMAMD_UPDATE_WRAPPER(ID_CLEANUP_DELETE_BIN, OnCleanupDeleteToBin)
    ON_COMMAMD_UPDATE_WRAPPER(ID_CLEANUP_DELETE, OnCleanupDelete)
    ON_UPDATE_COMMAND_UI_RANGE(ID_USERDEFINEDCLEANUP0, ID_USERDEFINEDCLEANUP9, OnUpdateUserDefinedCleanup)
    ON_COMMAND_RANGE(ID_USERDEFINEDCLEANUP0, ID_USERDEFINEDCLEANUP9, OnUserDefinedCleanup)
    ON_COMMAMD_UPDATE_WRAPPER(ID_TREEMAP_SELECT_PARENT, OnTreemapSelectParent)
    ON_COMMAMD_UPDATE_WRAPPER(ID_TREEMAP_RESELECT_CHILD, OnTreemapReselectChild)
    ON_COMMAMD_UPDATE_WRAPPER(ID_CLEANUP_OPEN_SELECTED, OnCleanupOpenTarget)
    ON_COMMAMD_UPDATE_WRAPPER(ID_CLEANUP_PROPERTIES, OnCleanupProperties)
    ON_COMMAMD_UPDATE_WRAPPER(ID_SCAN_RESUME, OnScanResume)
    ON_COMMAMD_UPDATE_WRAPPER(ID_SCAN_SUSPEND, OnScanSuspend)
END_MESSAGE_MAP()

void CDirStatDoc::OnRefreshSelected()
{
    RefreshItem(GetAllSelected());
}

void CDirStatDoc::OnRefreshAll()
{
    RefreshItem(GetRootItem());
}

void CDirStatDoc::OnSaveResults()
{
    // Request the file path from the user
    CStringW file_select_string;
    file_select_string.Format(L"%s (*.csv)|*.csv|%s (*.*)|*.*||",
        Localization::Lookup(IDS_CSV_FILES).GetString(), Localization::Lookup(IDS_ALL_FILES).GetString());
    CFileDialog dlg(FALSE, L"csv", nullptr, OFN_EXPLORER | OFN_DONTADDTORECENT, file_select_string.GetString());
    if (dlg.DoModal() != IDOK) return;

    CWaitCursor wc;
    SaveResults(dlg.GetPathName().GetString(), GetRootItem());
}

void CDirStatDoc::OnLoadResults()
{
    // Request the file path from the user
    CStringW file_select_string;
    file_select_string.Format(L"%s (*.csv)|*.csv|%s (*.*)|*.*||",
        Localization::Lookup(IDS_CSV_FILES).GetString(), Localization::Lookup(IDS_ALL_FILES).GetString());
    CFileDialog dlg(TRUE, L"csv", nullptr, OFN_EXPLORER | OFN_DONTADDTORECENT | OFN_PATHMUSTEXIST, file_select_string.GetString());
    if (dlg.DoModal() != IDOK) return;

    CWaitCursor wc;
    CItem* newroot = LoadResults(dlg.GetPathName().GetString());
    GetDocument()->OnOpenDocument(newroot);
}

void CDirStatDoc::OnEditCopy()
{
    // create concatenated paths
    CStringW paths;
    const auto & items = GetAllSelected();
    for (const auto & item : items)
    {
        if (paths.GetLength() > 0) paths += L"\r\n";
        paths += item->GetPath();
    }

    CMainFrame::Get()->CopyToClipboard(paths.GetBuffer());
}

void CDirStatDoc::OnCleanupEmptyRecycleBin()
{
    CModalShellApi msa;

    SHEmptyRecycleBin(*AfxGetMainWnd(), nullptr, 0);

    RefreshRecyclers();
    UpdateAllViews(nullptr);
}

void CDirStatDoc::OnUpdateViewShowFreeSpace(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(m_showFreeSpace);
}

void CDirStatDoc::OnViewShowFreeSpace()
{
    for (const auto& drive : GetDriveItems())
    {
        if (m_showFreeSpace)
        {
            const CItem* free = drive->FindFreeSpaceItem();
            ASSERT(free != nullptr);

            if (GetZoomItem() == free)
            {
                m_zoomItem = free->GetParent();
            }

            drive->RemoveFreeSpaceItem();
        }
        else
        {
            drive->CreateFreeSpaceItem();
        }
    }

    // Toogle value
    m_showFreeSpace = !m_showFreeSpace;
    COptions::ShowFreeSpace = m_showFreeSpace;

    // Force recalculation and graph refresh
    StartupCoordinator({});
}

void CDirStatDoc::OnUpdateViewShowUnknown(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(m_showUnknown);
}

void CDirStatDoc::OnViewShowUnknown()
{
    for (const auto& drive : GetDriveItems())
    {
        if (m_showUnknown)
        {
            const CItem* unknown = drive->FindUnknownItem();
            ASSERT(unknown != nullptr);

            if (GetZoomItem() == unknown)
            {
                m_zoomItem = unknown->GetParent();
            }

            drive->RemoveUnknownItem();
        }
        else
        {
            drive->CreateUnknownItem();
        }
    }

    // Toogle value
    m_showUnknown = !m_showUnknown;
    COptions::ShowUnknown = m_showUnknown;

    // Force recalculation and graph refresh
    StartupCoordinator({});
}

void CDirStatDoc::OnTreemapZoomIn()
{
    const auto & item = CFileTreeControl::Get()->GetFirstSelectedItem<CItem>();
    if (item != nullptr)
    {
        SetZoomItem(item);
    }
}

void CDirStatDoc::OnTreemapZoomOut()
{
    if (GetZoomItem() != nullptr)
    {
        SetZoomItem(GetZoomItem()->GetParent());
    }
}

void CDirStatDoc::OnExplorerSelect()
{
    // accumulate a unique set of paths
    const auto& items = GetAllSelected();
    std::unordered_set<std::wstring>paths;
    for (const auto& item : items)
    {
        // use function to determine parent to address non-drive rooted paths
        std::filesystem::path target(item->GetPath().GetString());
        paths.insert(target.parent_path());
    }
    
    for (const auto& path : paths)
    {
        // create path pidl
        SmartPointer<LPITEMIDLIST> parent(CoTaskMemFree);
        parent = ILCreateFromPath(path.c_str());

        // structures to hold and track pidls for children
        std::vector<SmartPointer<LPITEMIDLIST>> pidl_cleanup;
        std::vector<LPITEMIDLIST> pidl;

        // create list of children from paths
        for (auto & item : items)
        {
            // not processing this path yet
            std::filesystem::path target(item->GetPath().GetString());
            if (target.parent_path() == path)
            {
                pidl.push_back(ILCreateFromPath(item->GetPath()));
                pidl_cleanup.emplace_back(CoTaskMemFree, pidl.back());
            }
        }

        // attempt to open the items in the shell
        SHOpenFolderAndSelectItems(parent, static_cast<UINT>(pidl.size()), const_cast<LPCITEMIDLIST*>(pidl.data()), 0);
    }
}

void CDirStatDoc::OnCommandPromptHere()
{
    try
    {
        // accumulate a unique set of paths
        const auto& items = GetAllSelected();
        std::unordered_set<std::wstring>paths;
        for (const auto& item : items)
        {
            paths.insert(item->GetFolderPath().GetString());
        }

        // launch a command prompt for each path
        const CStringW cmd = GetCOMSPEC();
        for (const auto& path : paths)
        {
            ShellExecuteThrow(*AfxGetMainWnd(), L"open", cmd, nullptr, path.c_str(), SW_SHOWNORMAL);
        }
    }
    catch (CException* pe)
    {
        pe->ReportError();
        pe->Delete();
    }
}

void CDirStatDoc::OnCleanupDeleteToBin()
{
    const auto & items = GetAllSelected();
    if (DeletePhysicalItems(items, true))
    {
        RefreshRecyclers();
        UpdateAllViews(nullptr);
    }
}

void CDirStatDoc::OnCleanupDelete()
{
    const auto & items = GetAllSelected();
    if (DeletePhysicalItems(items, false))
    {
        UpdateAllViews(nullptr);
    }
}

void CDirStatDoc::OnUpdateUserDefinedCleanup(CCmdUI* pCmdUI)
{
    const int i = pCmdUI->m_nID - ID_USERDEFINEDCLEANUP0;
    const auto & items = GetAllSelected();
    bool allow_control = DirectoryListHasFocus() && COptions::UserDefinedCleanups.at(i).enabled && !items.empty();
    if (allow_control) for (const auto & item : items)
    {
        allow_control &= UserDefinedCleanupWorksForItem(&COptions::UserDefinedCleanups[i], item);
    }

    pCmdUI->Enable(allow_control);
}

void CDirStatDoc::OnUserDefinedCleanup(const UINT id)
{
    USERDEFINEDCLEANUP* udc = &COptions::UserDefinedCleanups[id - ID_USERDEFINEDCLEANUP0];
    const auto & items = GetAllSelected();
    for (const auto & item : items)
    {
        ASSERT(UserDefinedCleanupWorksForItem(udc, item));
        if (!UserDefinedCleanupWorksForItem(udc, item))
        {
            return;
        }

        try
        {
            AskForConfirmation(udc, item);
            PerformUserDefinedCleanup(udc, item);
            RefreshAfterUserDefinedCleanup(udc, item);
        }
        catch (CUserException* pe)
        {
            pe->Delete();
        }
        catch (CException* pe)
        {
            pe->ReportError();
            pe->Delete();
        }
    }
}

void CDirStatDoc::OnTreemapSelectParent()
{
    const auto & item = CFileTreeControl::Get()->GetFirstSelectedItem<CItem>();
    PushReselectChild(item);
    CFileTreeControl::Get()->SelectItem(item->GetParent(), true, true);
    UpdateAllViews(nullptr, HINT_SELECTIONREFRESH);
}

void CDirStatDoc::OnTreemapReselectChild()
{
    const CItem* item = PopReselectChild();
    CFileTreeControl::Get()->SelectItem(item, true, true);
    UpdateAllViews(nullptr, HINT_SELECTIONREFRESH);
}

void CDirStatDoc::OnCleanupOpenTarget()
{
    const auto & items = GetAllSelected();
    for (const auto & item : items)
    {
        OpenItem(item);
    }
}

void CDirStatDoc::OnCleanupProperties()
{
    const auto & items = GetAllSelected();
    for (const auto & item : items)
    {
        OpenItem(item, L"properties");
    }
}

void CDirStatDoc::OnScanSuspend()
{
    CWaitCursor wc;

    // Wait for system to fully shutdown
    std::thread([this]() mutable
    {
        queue.suspend(true);
        PostMessage(CMainFrame::Get()->GetSafeHwnd(), WM_USER + 1, 0, 0);
    }).detach();

    // Read all messages in this next loop, removing each message as we read it.
    for (MSG msg; ::GetMessage(&msg, nullptr, 0, 0); )
    {
        if (msg.message == WM_USER + 1) break;
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    // Mark as suspended
    if (CMainFrame::Get() != nullptr)
        CMainFrame::Get()->SuspendState(true);
}

void CDirStatDoc::OnScanResume()
{
    queue.resume();

    if (CMainFrame::Get() != nullptr)
        CMainFrame::Get()->SuspendState(false);
}

void CDirStatDoc::ShutdownCoordinator(const bool wait)
{
    if (wait) OnScanSuspend();

    if (queue.drain(nullptr) && wait)
    {
        for (auto& thread : threads)
        {
            thread.join();
        }
    }
}

void CDirStatDoc::StartupCoordinator(std::vector<CItem*> items)
{
    // Stop any previous executions
    ShutdownCoordinator();
    OnScanResume();

    // Address currently zoomed / selected item conflicts
    const auto zoom_item = GetZoomItem();
    for (const auto& item : std::vector(items))
    {
        // Abort if bad entry detected
        if (item == nullptr)
        {
            return;
        }

        // Bring the zoom out if it would be invalidated
        if (item->IsAncestorOf(zoom_item))
        {
            SetZoomItem(item);
        }
    }

    // Clear any reselection options since they may be invalidated
    ClearReselectChildStack();

    // Do not attempt to update graph while scanning
    CMainFrame::Get()->GetTreeMapView()->SuspendRecalculationDrawing(true);

    // Start a thread so we do not hang the message loop
    // Lambda captures assume document exists for duration of thread
    std::thread([this,items] () mutable
    {
        // Wait for other threads to finish if this was scheduled in parallel
        static std::shared_mutex mutex;
        std::lock_guard lock(mutex);

        const auto selected_items = GetAllSelected();
        using visual_info = struct { bool wasExpanded; bool isSelected; int oldScrollPosition; };
        std::unordered_map<CItem *,visual_info> visualInfo;
        for (auto item : std::vector(items))
        {
            // Record current visual arrangement to reapply afterward
            if (item->IsVisible())
            {
                visualInfo[item].isSelected = std::ranges::find(selected_items, item) != selected_items.end();
                visualInfo[item].wasExpanded = item->IsExpanded();
            }

            // Skip pruning if it is a new element
            if (!item->IsDone()) continue;

            item->UncacheImage();
            item->UpwardRecalcLastChange(true);
            item->UpwardSubtractSizePhysical(item->GetSizePhysical());
            item->UpwardSubtractSizeLogical(item->GetSizeLogical());
            item->UpwardSubtractFiles(item->GetFilesCount());
            item->UpwardSubtractFolders(item->GetFoldersCount());
            item->RemoveAllChildren();
            item->UpwardSetUndone();

            // children removal will collapse item so re-expand it
            if (visualInfo.contains(item) && item->IsVisible())
                item->SetExpanded(visualInfo[item].wasExpanded);
  
            // Handle if item to be refreshed has been removed
            if (item->IsType(IT_FILE | IT_DIRECTORY | IT_DRIVE) &&
                !FileFindEnhanced::DoesFileExist(item->GetFolderPath(),
                    item->IsType(IT_FILE) ? item->GetName() : CStringW(L"")))
            {
                // Remove item from list so we do not rescan it
                std::erase(items, item);

                if (item->IsRootItem())
                {
                    // Handle deleted root item
                    CMainFrame::Get()->InvokeInMessageThread([]
                    {
                        GetDocument()->UnlinkRoot();
                        CMainFrame::Get()->MinimizeTreeMapView();
                        CMainFrame::Get()->MinimizeExtensionView();
                    });
                    return;
                }

                // Handle non-root item by removing from parent
                item->UpwardSubtractFiles(item->IsType(IT_FILE) ? 1 : 0);
                item->UpwardSubtractFolders(item->IsType(IT_FILE) ? 0 : 1);
                item->GetParent()->RemoveChild(item);
            }
        }

        // Reset queue from last iteration
        const int max_threads = COptions::ScanningThreads;
        queue.reset(max_threads);

        // Add items to processing queue
        for (const auto item : std::vector(items))
        {
            // Skip any items we should not follow
            if (!item->IsType(ITF_ROOTITEM) && !CDirStatApp::Get()->IsFollowingAllowed(item->GetPath(), item->GetAttributes()))
            {
                continue;
            }

            item->UpwardAddReadJobs(1);
            item->UpwardSetUndone();
            queue.push(item);
        }

        // Create subordinate threads if there is work to do
        if (queue.has_items())
        {
            threads.clear();
            for (int i = 0; i < max_threads; i++)
            {
                threads.emplace_back([this]
                {
                    CItem::ScanItems(&queue);
                });
            }

            // Wait for all threads to run out of work
            if (queue.wait_for_all())
            {
                // Exit here and stop progress if drained by an outside actor
                CMainFrame::Get()->InvokeInMessageThread([]
                {
                    CMainFrame::Get()->SetProgressComplete();
                    CMainFrame::Get()->MinimizeTreeMapView();
                    CMainFrame::Get()->MinimizeExtensionView();
                });
                return;
            }

            // Flag workers to exit and wait for threads
            queue.drain(nullptr);
            for (auto& thread : threads)
            {
                thread.join();
            }
            threads.clear();
        }

        // Restore unknown and freespace items
        for (const auto& item : items)
        {
            if (!item->IsType(IT_DRIVE)) continue;
            
            if (COptions::ShowFreeSpace)
            {
                item->CreateFreeSpaceItem();
            }
            if (COptions::ShowUnknown)
            {
                item->CreateUnknownItem();
            }
        }

        // Sorting and other finalization tasks
        CItem::ScanItemsFinalize(GetRootItem());

        // Invoke a UI thread to do updates
        CMainFrame::Get()->InvokeInMessageThread([&items,&visualInfo]
        {
            for (const auto& item : items)
            {
                // restore scroll position if previously set
                if (visualInfo.contains(item) && item->IsVisible())
                    item->SetScrollPosition(visualInfo[item].oldScrollPosition);
            }

            CMainFrame::Get()->LockWindowUpdate();
            GetDocument()->RebuildExtensionData();
            GetDocument()->UpdateAllViews(nullptr);
            CMainFrame::Get()->SetProgressComplete();
            CMainFrame::Get()->RestoreExtensionView();
            CMainFrame::Get()->RestoreTreeMapView();
            CMainFrame::Get()->GetTreeMapView()->SuspendRecalculationDrawing(false);
            CMainFrame::Get()-> UnlockWindowUpdate();
        });
    }).detach();
}
