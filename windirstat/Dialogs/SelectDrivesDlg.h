// SelectDrivesDlg.h - Declaration of CDriveItem, CDrivesList and CSelectDrivesDlg
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

#include "OwnerDrawnListControl.h"
#include "Layout.h"
#include "resource.h"

#include <shared_mutex>
#include <unordered_set>

//
// The dialog has these three radio buttons.
//
enum RADIO
{
    RADIO_ALLLOCALDRIVES,
    RADIO_SOMEDRIVES,
    RADIO_AFOLDER
};

class CDrivesList;

//
// CDriveItem. An item in the CDrivesList Control.
// All methods are called by the gui thread.
//
class CDriveItem final : public COwnerDrawnListItem
{
public:
    CDriveItem(CDrivesList* list, LPCWSTR pszPath);
    void StartQuery(HWND dialog, UINT serial);

    void SetDriveInformation(bool success, LPCWSTR name, ULONGLONG total, ULONGLONG free);

    int Compare(const CSortingListItem* baseOther, int subitem) const override;

    CStringW GetPath() const;
    CStringW GetDrive() const;
    bool IsRemote() const;
    bool IsSUBSTed() const;
    bool DrawSubitem(int subitem, CDC* pdc, CRect rc, UINT state, int* width, int* focusLeft) const override;
    CStringW GetText(int subitem) const override;
    int GetImage() const override;

private:
    CDrivesList* m_list; // Backpointer
    CStringW m_path;     // e.g. "C:\"
    bool m_isRemote;     // Whether the drive type is DRIVE_REMOTE (network drive)

    bool m_querying = true; // Information thread is running.
    bool m_success = false; // Drive is accessible. false while m_querying is true.

    CStringW m_name;            // e.g. "BOOT (C:)"
    ULONGLONG m_totalBytes = 0; // Capacity
    ULONGLONG m_freeBytes = 0;  // Free space

    double m_used = 0.0; // used space / total space
};

//
// CDriveInformationThread. Does the GetVolumeInformation() call, which
// may hang for ca. 30 sec, it a network drive is not accessible.
//
class CDriveInformationThread final : public CWinThread
{
    // Set of all running CDriveInformationThreads.
    // Used by InvalidateDialogHandle().
    static std::unordered_set<CDriveInformationThread*> _runningThreads;
    static std::shared_mutex _mutexRunningThreads;

    // The objects register and unregister themselves in _runningThreads
    void AddRunningThread();
    void RemoveRunningThread();

public:
    static void InvalidateDialogHandle();

    CDriveInformationThread(LPCWSTR path, LPARAM driveItem, HWND dialog, UINT serial);
    BOOL InitInstance() override;

    LPARAM GetDriveInformation(bool& success, CStringW& name, ULONGLONG& total, ULONGLONG& free) const;

private:
    const CStringW m_path;    // Path like "C:\"
    const LPARAM m_driveItem; // The list item, we belong to

    std::shared_mutex m_mutex; // for m_dialog
    HWND m_dialog;             // synchronized by m_cs
    const UINT m_serial;       // serial number of m_dialog

    // "[out]"-parameters
    CStringW m_name;            // Result: name like "BOOT (C:)", valid if m_success
    ULONGLONG m_totalBytes = 0; // Result: capacity of the drive, valid if m_success
    ULONGLONG m_freeBytes = 0;  // Result: free space on the drive, valid if m_success
    bool m_success = false;     // Result: false, iff drive is unaccessible.
};

//
// CDrivesList.
//
class CDrivesList final : public COwnerDrawnListControl
{
    DECLARE_DYNAMIC(CDrivesList)

    CDrivesList();
    CDriveItem* GetItem(int i) const;
    void SelectItem(const CDriveItem* item);
    bool IsItemSelected(int i) const;

    bool HasImages() override;

    DECLARE_MESSAGE_MAP()
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLvnDeleteitem(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void MeasureItem(LPMEASUREITEMSTRUCT mis);
    afx_msg void OnNMDblclk(NMHDR* pNMHDR, LRESULT* pResult);
};

//
// CSelectDrivesDlg. The initial dialog, where the user can select
// one or more drives or a folder for scanning.
//
class CSelectDrivesDlg final : public CDialogEx
{
    DECLARE_DYNAMIC(CSelectDrivesDlg)

    enum { IDD = IDD_SELECTDRIVES };

    static CStringW getFullPathName_(LPCWSTR relativePath);

public:
    CSelectDrivesDlg(CWnd* pParent = nullptr);
    ~CSelectDrivesDlg() override = default;

    // Dialog Data
    int m_radio = 0;       // out.
    CStringW m_folderName; // out. Valid if m_radio = RADIO_AFOLDER
    CStringArray m_drives; // out. Valid if m_radio != RADIO_AFOLDER

    void DoDataExchange(CDataExchange* pDX) override;
    BOOL OnInitDialog() override;
    void OnOK() override;

    void UpdateButtons();

    static UINT _serial; // Each Instance of this dialog gets a serial number
    CDrivesList m_list;
    CMFCEditBrowseCtrl m_browse;
    CButton m_okButton;
    std::vector<std::wstring> m_selectedDrives;
    CLayout m_layout;
    // Callback function for the dialog shown by SHBrowseForFolder()
    // MUST be static!
    static int CALLBACK BrowseCallbackProc(HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM lpData);

    DECLARE_MESSAGE_MAP()
    afx_msg void OnBnClickedAllLocalDrives();
    afx_msg void OnBnClickedFolder();
    afx_msg void OnBnClickedSomeDrives();
    afx_msg void OnEnChangeFolderName();
    afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT mis);
    afx_msg void OnLvnItemchangedDrives(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnDestroy();
    afx_msg LRESULT OnWmuOk(WPARAM, LPARAM);
    afx_msg LRESULT OnWmuThreadFinished(WPARAM, LPARAM lparam);
    afx_msg void OnSysColorChange();
};
