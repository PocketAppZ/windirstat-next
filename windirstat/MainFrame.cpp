// MainFrame.cpp - Implementation of CMySplitterWnd, CPacmanControl and CMainFrame
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
#include "WinDirStat.h"
#include "TreeMapView.h"
#include "FileTabbedView.h"
#include "FileTreeView.h"
#include "ExtensionView.h"
#include "DirStatDoc.h"
#include "GlobalHelpers.h"
#include "Item.h"
#include "Localization.h"
#include "Property.h"
#include "PageAdvanced.h"
#include "PageCleanups.h"
#include "PageTreeList.h"
#include "PageTreeMap.h"
#include "PageGeneral.h"
#include "MainFrame.h"

#include <common/MdExceptions.h>

#include <functional>
#include <unordered_map>

namespace
{
    // Clipboard-Opener
    class COpenClipboard final
    {
    public:
        COpenClipboard(CWnd* owner, bool empty = true)
        {
            m_open = owner->OpenClipboard();
            if (!m_open)
            {
                MdThrowStringException(Localization::Lookup(IDS_CANNOTOPENCLIPBOARD));
            }
            if (empty)
            {
                if (!EmptyClipboard())
                {
                    MdThrowStringException(Localization::Lookup(IDS_CANNOTEMTPYCLIPBOARD));
                }
            }
        }

        ~COpenClipboard()
        {
            if (m_open)
            {
                CloseClipboard();
            }
        }

    private:
        BOOL m_open;
    };
}

/////////////////////////////////////////////////////////////////////////////

IMPLEMENT_DYNAMIC(COptionsPropertySheet, CPropertySheet)

COptionsPropertySheet::COptionsPropertySheet()
    : CPropertySheet(Localization::Lookup(IDS_WINDIRSTAT_SETTINGS))
{
}

void COptionsPropertySheet::SetLanguageChanged(bool changed)
{
    m_languageChanged = changed;
}

BOOL COptionsPropertySheet::OnInitDialog()
{
    const BOOL bResult = CPropertySheet::OnInitDialog();
    Localization::UpdateDialogs(*this);
    Localization::UpdateTabControl(*GetTabControl());

    SetActivePage(min(COptions::ConfigPage, GetPageCount() - 1));
    return bResult;
}

BOOL COptionsPropertySheet::OnCommand(WPARAM wParam, LPARAM lParam)
{
    COptions::ConfigPage = GetActiveIndex();

    const int cmd = LOWORD(wParam);
    if (IDOK == cmd || ID_APPLY_NOW == cmd)
    {
        if (m_languageChanged && (IDOK == cmd || !m_alreadyAsked))
        {
            const int r = AfxMessageBox(Localization::Lookup(IDS_LANGUAGERESTARTNOW), MB_YESNOCANCEL);
            if (IDCANCEL == r)
            {
                return true; // "Message handled". Don't proceed.
            }
            else if (IDNO == r)
            {
                m_alreadyAsked = true; // Don't ask twice.
            }
            else
            {
                ASSERT(IDYES == r);
                m_restartApplication = true;

                if (ID_APPLY_NOW == cmd)
                {
                    // This _posts_ a message...
                    EndDialog(IDOK);
                    // ... so after returning from this function, the OnOK()-handlers
                    // of the pages will be called, before the sheet is closed.
                }
            }
        }
    }

    return CPropertySheet::OnCommand(wParam, lParam);
}

/////////////////////////////////////////////////////////////////////////////

CMySplitterWnd::CMySplitterWnd(double * splitterPos) : 
    m_userSplitterPos(splitterPos)
{
    m_wasTrackedByUser = (*splitterPos > 0 && *splitterPos < 1);
}

BEGIN_MESSAGE_MAP(CMySplitterWnd, CSplitterWnd)
    ON_WM_SIZE()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

void CMySplitterWnd::StopTracking(BOOL bAccept)
{
    CSplitterWnd::StopTracking(bAccept);

    if (bAccept)
    {
        CRect rcClient;
        GetClientRect(rcClient);

        if (GetColumnCount() > 1)
        {
            int dummy;
            int cxLeft;
            GetColumnInfo(0, cxLeft, dummy);

            if (rcClient.Width() > 0)
            {
                m_splitterPos = static_cast<double>(cxLeft) / rcClient.Width();
            }
        }
        else
        {
            int dummy;
            int cyUpper;
            GetRowInfo(0, cyUpper, dummy);

            if (rcClient.Height() > 0)
            {
                m_splitterPos = static_cast<double>(cyUpper) / rcClient.Height();
            }
        }
        m_wasTrackedByUser = true;
        *m_userSplitterPos = m_splitterPos;
    }
}

void CMySplitterWnd::SetSplitterPos(double pos)
{
    m_splitterPos = pos;

    CRect rcClient;
    GetClientRect(rcClient);

    if (GetColumnCount() > 1)
    {
        if (m_pColInfo != nullptr)
        {
            const int cxLeft = static_cast<int>(pos * rcClient.Width());
            if (cxLeft >= 0)
            {
                SetColumnInfo(0, cxLeft, 0);
                RecalcLayout();
            }
        }
    }
    else
    {
        if (m_pRowInfo != nullptr)
        {
            const int cyUpper = static_cast<int>(pos * rcClient.Height());
            if (cyUpper >= 0)
            {
                SetRowInfo(0, cyUpper, 0);
                RecalcLayout();
            }
        }
    }
}

void CMySplitterWnd::RestoreSplitterPos(double posIfVirgin)
{
    if (m_wasTrackedByUser)
    {
        SetSplitterPos(*m_userSplitterPos);
    }
    else
    {
        SetSplitterPos(posIfVirgin);
    }
}

void CMySplitterWnd::OnSize(UINT nType, int cx, int cy)
{
    if (GetColumnCount() > 1)
    {
        const int cxLeft = static_cast<int>(cx * m_splitterPos);
        if (cxLeft > 0)
        {
            SetColumnInfo(0, cxLeft, 0);
        }
    }
    else
    {
        const int cyUpper = static_cast<int>(cy * m_splitterPos);
        if (cyUpper > 0)
        {
            SetRowInfo(0, cyUpper, 0);
        }
    }
    CSplitterWnd::OnSize(nType, cx, cy);
}

void CMySplitterWnd::OnDestroy()
{
    CSplitterWnd::OnDestroy();
}

/////////////////////////////////////////////////////////////////////////////

CPacmanControl::CPacmanControl()
{
    m_pacman.SetBackgroundColor(::GetSysColor(COLOR_BTNFACE));
    m_pacman.SetSpeed(0.00005f);
}

void CPacmanControl::Drive()
{
    if (::IsWindow(m_hWnd))
    {
        m_pacman.UpdatePosition();
        RedrawWindow();
    }
}

void CPacmanControl::Start()
{
    m_pacman.Start();
}

void CPacmanControl::Stop()
{
    m_pacman.Stop();
}

BEGIN_MESSAGE_MAP(CPacmanControl, CStatic)
    ON_WM_PAINT()
    ON_WM_CREATE()
END_MESSAGE_MAP()

int CPacmanControl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CStatic::OnCreate(lpCreateStruct) == -1)
    {
        return -1;
    }

    m_pacman.Reset();
    m_pacman.Start();
    return 0;
}

void CPacmanControl::OnPaint()
{
    const CPaintDC dc(this);
    CRect rc;
    GetClientRect(rc);
    m_pacman.Draw(&dc, rc);
}

/////////////////////////////////////////////////////////////////////////////

void CDeadFocusWnd::Create(CWnd* parent)
{
    const CRect rc(0, 0, 0, 0);
    VERIFY(CWnd::Create(AfxRegisterWndClass(0, nullptr, nullptr, nullptr), L"_deadfocus", WS_CHILD, rc, parent, 0));
}

CDeadFocusWnd::~CDeadFocusWnd()
{
    CWnd::DestroyWindow();
}

BEGIN_MESSAGE_MAP(CDeadFocusWnd, CWnd)
    ON_WM_KEYDOWN()
END_MESSAGE_MAP()

void CDeadFocusWnd::OnKeyDown(UINT nChar, UINT /*nRepCnt*/, UINT /*nFlags*/ )
{
    if (nChar == VK_TAB)
    {
        CMainFrame::Get()->MoveFocus(LF_DIRECTORYLIST);
    }
}

/////////////////////////////////////////////////////////////////////////////
UINT CMainFrame::s_taskBarMessage = ::RegisterWindowMessage(L"TaskbarButtonCreated");

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWndEx)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWndEx)
    ON_COMMAND(ID_CONFIGURE, OnConfigure)
    ON_COMMAND(ID_VIEW_SHOWFILETYPES, OnViewShowFileTypes)
    ON_COMMAND(ID_VIEW_SHOWTREEMAP, OnViewShowtreemap)
    ON_MESSAGE(WM_ENTERSIZEMOVE, OnEnterSizeMove)
    ON_MESSAGE(WM_EXITSIZEMOVE, OnExitSizeMove)
    ON_MESSAGE(WM_CALLBACKUI, OnCallbackRequest)
    ON_REGISTERED_MESSAGE(s_taskBarMessage, OnTaskButtonCreated)
    ON_UPDATE_COMMAND_UI(ID_VIEW_SHOWFILETYPES, OnUpdateViewShowFileTypes)
    ON_UPDATE_COMMAND_UI(ID_VIEW_SHOWTREEMAP, OnUpdateViewShowtreemap)
    ON_UPDATE_COMMAND_UI(IDS_RAMUSAGEs, OnUpdateEnableControl)
    ON_UPDATE_COMMAND_UI(IDS_IDLEMESSAGE, OnUpdateEnableControl)
    ON_WM_CLOSE()
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_INITMENUPOPUP()
    ON_WM_SIZE()
    ON_WM_SYSCOLORCHANGE()
    ON_WM_TIMER()
END_MESSAGE_MAP()

constexpr auto ID_INDICATOR_IDLEMESSAGE_INDEX = 0;
constexpr auto ID_INDICATOR_MEMORYUSAGE_INDEX = 1;
constexpr auto ID_INDICATOR_CAPS_INDEX = 2;
constexpr auto ID_INDICATOR_NUM_INDEX = 3;
constexpr auto ID_INDICATOR_SCRL_INDEX = 4;

constexpr UINT indicators[]
{
    IDS_IDLEMESSAGE,
    IDS_RAMUSAGEs,
    ID_INDICATOR_CAPS,
    ID_INDICATOR_NUM,
    ID_INDICATOR_SCRL,
};

CMainFrame* CMainFrame::_singleton;

CMainFrame::CMainFrame() :
        m_SubSplitter(COptions::SubSplitterPos.Ptr())
      , m_Splitter(COptions::MainSplitterPos.Ptr())
{
    _singleton = this;
}

CMainFrame::~CMainFrame()
{
    _singleton = nullptr;
}

LRESULT CMainFrame::OnTaskButtonCreated(WPARAM, LPARAM)
{
    if (!m_TaskbarList)
    {
        const HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_ITaskbarList3, reinterpret_cast<LPVOID*>(&m_TaskbarList));
        if (FAILED(hr))
        {
            VTRACE(L"CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL) failed %08X", hr);
        }
    }
    return 0;
}

void CMainFrame::CreateProgress(ULONGLONG range)
{
    // A range of 0 means that we have no range.
    // In this case we display pacman.

    if (COptions::FollowMountPoints || COptions::FollowJunctions || COptions::ShowUncompressedFileSizes)
    {
        range = 0;
    }
    m_progressRange = range;
    m_progressPos = 0;
    m_progressVisible = true;
    if (range > 0)
    {
        CreateStatusProgress();
    }
    else
    {
        CreatePacmanProgress();
    }
}

void CMainFrame::SetProgressPos(ULONGLONG pos)
{
    if (m_progressRange > 0 && pos > m_progressRange)
    {
        pos = m_progressRange;
    }

    m_progressPos = pos;
    UpdateProgress();
}

void CMainFrame::SetProgressComplete() // called by CDirStatDoc
{
    if (m_TaskbarList)
    {
        m_TaskbarList->SetProgressState(*this, m_TaskbarButtonState = TBPF_NOPROGRESS);
    }

    DestroyProgress();
    GetDocument()->SetTitlePrefix(wds::strEmpty);
    SetMessageText(Localization::Lookup(IDS_IDLEMESSAGE));
}

bool CMainFrame::IsScanSuspended() const
{
    return m_scanSuspend;
}

void CMainFrame::SuspendState(const bool suspend)
{
    m_scanSuspend = suspend;
    if (m_TaskbarList)
    {
        if (m_TaskbarButtonState == TBPF_PAUSED)
        {
            m_TaskbarList->SetProgressState(*this, m_TaskbarButtonState = m_TaskbarButtonPreviousState);
        }
        else
        {
            m_TaskbarButtonPreviousState = m_TaskbarButtonState;
            m_TaskbarList->SetProgressState(*this, m_TaskbarButtonState |= TBPF_PAUSED);
        }
    }
    CPacman::SetGlobalSuspendState(suspend);
    UpdateProgress();
}

void CMainFrame::UpdateProgress()
{
    // Update working item tracker if changed
    CItem* workingItem = GetDocument()->GetRootItem();
    if (workingItem != m_workingItem)
    {
        m_workingItem = workingItem;

        // Create progress display elements if now actively working item
        if (m_workingItem != nullptr && !m_workingItem->IsDone())
        {
            CreateProgress(m_workingItem->GetProgressRange());
        }
    }

    // Exit early if we not ready for visual updates
    if (!m_progressVisible || m_workingItem == nullptr) return;

    // Update pacman graphic (does nothing if hidden)
    m_progressPos = m_workingItem->GetProgressPos();
    m_pacman.Drive();

    CStringW title_prefix;
    CStringW suspended;

    // Display the suspend text in the bar if suspended
    if (IsScanSuspended())
    {
        static const CStringW suspend_string = Localization::Lookup(IDS_SUSPENDED);
        suspended = suspend_string;
    }

    if (m_progressRange > 0 && m_progress.m_hWnd != nullptr)
    {
        const int pos = static_cast<int>((m_progressPos * 100ull) / m_progressRange);
        m_progress.SetPos(pos);

        title_prefix.Format(L"%d%% %s", pos, suspended.GetString());
        if (m_TaskbarList && m_TaskbarButtonState != TBPF_PAUSED)
        {
            if (pos == 100)
            {
                m_TaskbarList->SetProgressState(*this, m_TaskbarButtonState = TBPF_INDETERMINATE); // often happens before we're finished
            }
            else
            {
                m_TaskbarList->SetProgressState(*this, m_TaskbarButtonState = TBPF_NORMAL); // often happens before we're finished
                m_TaskbarList->SetProgressValue(*this, m_progressPos, m_progressRange);
            }
        }
    }
    else
    {
        static const CStringW scanning_string = Localization::Lookup(IDS_SCANNING);
        title_prefix = scanning_string + L" " + suspended;
    }

    GetDocument()->SetTitlePrefix(title_prefix.Trim());
}

void CMainFrame::CreateStatusProgress()
{
    if (m_progress.m_hWnd == nullptr)
    {
        CRect rc;
        m_wndStatusBar.GetItemRect(0, rc);
        m_progress.Create(WS_CHILD | WS_VISIBLE, rc, &m_wndStatusBar, ID_WDS_CONTROL);
        m_progress.ModifyStyle(WS_BORDER, 0);
    }
    if (m_TaskbarList)
    {
        m_TaskbarList->SetProgressState(*this, m_TaskbarButtonState = TBPF_INDETERMINATE);
    }
}

void CMainFrame::CreatePacmanProgress()
{
    if (m_pacman.m_hWnd == nullptr)
    {
        CRect rc;
        m_wndStatusBar.GetItemRect(0, rc);
        m_pacman.Create(wds::strEmpty, WS_CHILD | WS_VISIBLE, rc, &m_wndStatusBar, ID_WDS_CONTROL);
        m_pacman.ModifyStyleEx(0, WS_EX_COMPOSITED, 0);
        m_pacman.Start();
    }
}

void CMainFrame::DestroyProgress()
{
    if (::IsWindow(m_progress.m_hWnd))
    {
        m_progress.DestroyWindow();
        m_progress.m_hWnd = nullptr;
    }
    else if (::IsWindow(m_pacman.m_hWnd))
    {
        m_pacman.Stop();
        m_pacman.DestroyWindow();
        m_pacman.m_hWnd = nullptr;
    }

    m_workingItem = nullptr;
    m_progressVisible = false;
}

void CMainFrame::SetStatusPaneText(int pos, const CStringW & text)
{
    // do not process the update if text is the same
    static CString last;
    if (last == text) return;
    last = text;

    // attempt to update width if dc is accessible
    if (const CDC* dc = GetDC(); dc != nullptr)
    {
        const int cx = dc->GetTextExtent(text, text.GetLength()).cx;
        m_wndStatusBar.SetPaneWidth(pos, cx);
    }

    m_wndStatusBar.SetPaneText(pos, text);
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWndEx::OnCreate(lpCreateStruct) == -1)
    {
        return -1;
    }

    VERIFY(m_wndToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC));
    VERIFY(m_wndToolBar.LoadToolBar(IDR_MAINFRAME));

    VERIFY(m_wndStatusBar.Create(this));
    m_wndStatusBar.SetIndicators(indicators, _countof(indicators));
    m_wndStatusBar.SetPaneStyle(ID_INDICATOR_IDLEMESSAGE_INDEX, SBPS_STRETCH);
    SetStatusPaneText(ID_INDICATOR_CAPS_INDEX, Localization::Lookup(IDS_INDICATOR_CAPS));
    SetStatusPaneText(ID_INDICATOR_NUM_INDEX, Localization::Lookup(IDS_INDICATOR_NUM));
    SetStatusPaneText(ID_INDICATOR_SCRL_INDEX, Localization::Lookup(IDS_INDICATOR_SCRL));

    m_wndDeadFocus.Create(this);

    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockPane(&m_wndToolBar);

    // map from toolbar resources to specific icons
    const std::unordered_map<UINT, std::pair<UINT, UINT>> toolbar_map =
    {
        { ID_FILE_SELECT, {IDB_FILE_SELECT, IDS_FILE_SELECT}},
        { ID_CLEANUP_OPEN_SELECTED, {IDB_CLEANUP_OPEN_SELECTED, IDS_CLEANUP_OPEN_SELECTED}},
        { ID_EDIT_COPY_CLIPBOARD, {IDB_EDIT_COPY_CLIPBOARD, IDS_EDIT_COPY_CLIPBOARD}},
        { ID_CLEANUP_EXPLORER_SELECT, {IDB_CLEANUP_EXPLORER_SELECT, IDS_CLEANUP_EXPLORER_SELECT}},
        { ID_CLEANUP_OPEN_IN_CONSOLE, {IDB_CLEANUP_OPEN_IN_CONSOLE, IDS_CLEANUP_OPEN_IN_CONSOLE}},
        { ID_REFRESH_SELECTED, {IDB_REFRESH_SELECTED, IDS_REFRESH_SELECTED}},
        { ID_REFRESH_ALL, {IDB_REFRESH_ALL, IDS_REFRESH_ALL}},
        { ID_SCAN_RESUME, {IDB_SCAN_RESUME, IDS_GENERIC_BLANK}},
        { ID_SCAN_SUSPEND, {IDB_SCAN_SUSPEND, IDS_SUSPEND}},
        { ID_CLEANUP_DELETE_BIN, {IDB_CLEANUP_DELETE_BIN, IDS_CLEANUP_DELETE_BIN}},
        { ID_CLEANUP_DELETE, {IDB_CLEANUP_DELETE, IDS_CLEANUP_DELETE}},
        { ID_CLEANUP_PROPERTIES, {IDB_CLEANUP_PROPERTIES, IDS_CLEANUP_PROPERTIES}},
        { ID_TREEMAP_ZOOMIN, {IDB_TREEMAP_ZOOMIN, IDS_TREEMAP_ZOOMIN}},
        { ID_TREEMAP_ZOOMOUT, {IDB_TREEMAP_ZOOMOUT, IDS_TREEMAP_ZOOMOUT}},
        { ID_HELP_MANUAL, {IDB_HELP_MANUAL, IDS_HELP_MANUAL}}};

    // update toolbar images with high resolution versions
    CMFCToolBarImages* images = new CMFCToolBarImages();
    images->SetImageSize({ 16,16 }, TRUE);
    for (int i = 0; i < m_wndToolBar.GetCount();i++)
    {
        // lookup the button in the editor toolbox
        const auto button = m_wndToolBar.GetButton(i);
        if (button->m_nID == 0) continue;
        ASSERT(toolbar_map.contains(button->m_nID));

        // load high quality bitmap from resource
        CBitmap bitmap;
        bitmap.LoadBitmapW(toolbar_map.at(button->m_nID).first);
        const int image = images->AddImage(bitmap, TRUE);
        CMFCToolBar::SetUserImages(images);

        // copy button into new toolbar control
        CMFCToolBarButton new_button(button->m_nID, image, nullptr, TRUE, TRUE);
        new_button.m_nStyle = button->m_nStyle | TBBS_DISABLED;
        new_button.m_strText = Localization::Lookup(toolbar_map.at(button->m_nID).second);
        m_wndToolBar.ReplaceButton(button->m_nID, new_button);
    }

    // setup look at feel
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows7));
    CDockingManager::SetDockingMode(DT_SMART);

    return 0;
}

void CMainFrame::InitialShowWindow()
{
    const WINDOWPLACEMENT wpsetting = COptions::MainWindowPlacement;
    if (wpsetting.length != 0)
    {
        SetWindowPlacement(&wpsetting);
    }

    SetTimer(ID_WDS_CONTROL, 25, nullptr);
}

void CMainFrame::InvokeInMessageThread(std::function<void()> callback)
{
    if (CDirStatApp::Get()->m_nThreadID == GetCurrentThreadId()) callback();
    else CMainFrame::Get()->SendMessage(WM_CALLBACKUI, 0, reinterpret_cast<LPARAM>(&callback));
}

void CMainFrame::OnClose()
{
    CWaitCursor wc;

    // Suspend the scan and wait for scan to complete
    GetDocument()->ShutdownCoordinator();

    // Stop the timer so we are not updating elements during shutdown
    KillTimer(ID_WDS_CONTROL);

    // It's too late, to do this in OnDestroy(). Because the toolbar, if undocked,
    // is already destroyed in OnDestroy(). So we must save the toolbar state here
    // in OnClose().
    COptions::ShowToolbar = (m_wndToolBar.GetStyle() & WS_VISIBLE) != 0;
    COptions::ShowStatusbar = (m_wndStatusBar.GetStyle() & WS_VISIBLE) != 0;

    CFrameWndEx::OnClose();
}

void CMainFrame::OnDestroy()
{
    // Save our window position
    WINDOWPLACEMENT wp = { .length = sizeof(wp) };
    GetWindowPlacement(&wp);
    COptions::MainWindowPlacement = wp;

    COptions::ShowFileTypes = GetExtensionView()->IsShowTypes();
    COptions::ShowTreemap = GetTreeMapView()->IsShowTreemap();

    // Close all artifacts and our child windows
    CFrameWndEx::OnDestroy();

    // Persist values at very end after all children have closed
    PersistedSetting::WritePersistedProperties();
}

BOOL CMainFrame::OnCreateClient(LPCREATESTRUCT /*lpcs*/, CCreateContext* pContext)
{
    m_Splitter.CreateStatic(this, 2, 1);
    m_Splitter.CreateView(1, 0, RUNTIME_CLASS(CTreeMapView), CSize(100, 100), pContext);
    m_SubSplitter.CreateStatic(&m_Splitter, 1, 2,WS_CHILD  | WS_VISIBLE | WS_BORDER, m_Splitter.IdFromRowCol(0, 0));
    m_SubSplitter.CreateView(0, 0, RUNTIME_CLASS(CFileTabbedView), CSize(700, 500), pContext);
    m_SubSplitter.CreateView(0, 1, RUNTIME_CLASS(CExtensionView), CSize(100, 500), pContext);

    m_TreeMapView = DYNAMIC_DOWNCAST(CTreeMapView, m_Splitter.GetPane(1, 0));
    m_FileTabbedView = DYNAMIC_DOWNCAST(CFileTabbedView, m_SubSplitter.GetPane(0, 0));
    m_ExtensionView = DYNAMIC_DOWNCAST(CExtensionView, m_SubSplitter.GetPane(0, 1));

    MinimizeTreeMapView();
    MinimizeExtensionView();

    GetExtensionView()->ShowTypes(COptions::ShowFileTypes);
    GetTreeMapView()->ShowTreemap(COptions::ShowTreemap);

    return TRUE;
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    // seed initial title bar text
    static CStringW title = Localization::Lookup(IDS_APP_TITLE) + (IsAdmin() ? L" (Administrator)" : L"");
    cs.style &= ~FWS_ADDTOTITLE;
    cs.lpszName = title.GetString();

    if (!CFrameWndEx::PreCreateWindow(cs))
    {
        return FALSE;
    }

    return TRUE;
}

void CMainFrame::MinimizeExtensionView()
{
    m_SubSplitter.SetSplitterPos(1.0);
}

void CMainFrame::RestoreExtensionView()
{
    if (GetExtensionView()->IsShowTypes())
    {
        m_SubSplitter.RestoreSplitterPos(0.72);
        GetExtensionView()->RedrawWindow();
    }
}

void CMainFrame::MinimizeTreeMapView()
{
    m_Splitter.SetSplitterPos(1.0);
}

void CMainFrame::RestoreTreeMapView()
{
    if (GetTreeMapView()->IsShowTreemap())
    {
        m_Splitter.RestoreSplitterPos(0.4);
        GetTreeMapView()->DrawEmptyView();
        GetTreeMapView()->RedrawWindow();
    }
}

LRESULT CMainFrame::OnEnterSizeMove(WPARAM, LPARAM)
{
    GetTreeMapView()->SuspendRecalculationDrawing(true);
    return 0;
}

LRESULT CMainFrame::OnExitSizeMove(WPARAM, LPARAM)
{
    GetTreeMapView()->SuspendRecalculationDrawing(false);
    return 0;
}

void CMainFrame::OnTimer(UINT_PTR nIDEvent)
{
    if (static bool first_run = true; first_run)
    {
        SetStatusPaneText(ID_INDICATOR_IDLEMESSAGE_INDEX, Localization::Lookup(IDS_IDLEMESSAGE));
        first_run = false;
    }

    // Determine whether we should be doing a fast UI update or not
    static unsigned int update_counter = 0;
    const bool do_slow_update = update_counter % 10 == 0;
    const bool do_fast_update = GetDocument()->HasRootItem() && (do_slow_update ||
        !GetDocument()->IsRootDone() && !IsScanSuspended());
    update_counter++;

    // UI updates that do not need to processed frequently
    if (do_slow_update)
    {
        // Update memory usage
        SetStatusPaneText(ID_INDICATOR_MEMORYUSAGE_INDEX, CDirStatApp::GetCurrentProcessMemoryInfo());

        // Force toolbar updates since they do not appear to always receive onidle commands
        m_wndToolBar.OnUpdateCmdUI(this, FALSE);
    }

    // UI updates that do need to processed frequently
    if (do_fast_update)
    {
        // Update the visual progress on the bottom of the screen
        UpdateProgress();

        // By sorting items, items will be redrawn which will
        // also force pacman to update with recent position
        CFileTreeControl::Get()->SortItems();
    }

    CFrameWndEx::OnTimer(nIDEvent);
}

LRESULT CMainFrame::OnCallbackRequest(WPARAM, LPARAM lParam)
{
    const auto & callback = *static_cast<std::function<void()>*>(reinterpret_cast<LPVOID>(lParam));
    callback();
    return 0;
}

void CMainFrame::CopyToClipboard(const LPCWSTR psz)
{
    try
    {
        COpenClipboard clipboard(this);
        const SIZE_T cchBufLen = _tcslen(psz) + 1;

        const HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, cchBufLen * sizeof(WCHAR));
        if (h == nullptr)
        {
            MdThrowStringException(L"GlobalAlloc failed.");
        }

        const LPVOID lp = ::GlobalLock(h);
        ASSERT(lp != nullptr);

        if (!lp)
        {
            MdThrowStringException(L"GlobalLock failed.");
        }

        wcscpy_s(static_cast<LPWSTR>(lp), cchBufLen, psz);

        ::GlobalUnlock(h);

        if (nullptr == ::SetClipboardData(CF_UNICODETEXT, h))
        {
            MdThrowStringException(Localization::Lookup(IDS_CANNOTSETCLIPBAORDDATA));
        }
    }
    catch (CException& pe)
    {
        pe.ReportError();
    }
}

void CMainFrame::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
    CFrameWndEx::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
    if (bSysMenu) return;
    
    CString menu_text;
    GetMenu()->GetMenuStringW(nIndex, menu_text, MF_BYPOSITION);
    if (menu_text.CompareNoCase(Localization::Lookup(IDS_MENU_CLEANUP)) == 0)
    {
        UpdateCleanupMenu(pPopupMenu);
    }
}

void CMainFrame::UpdateCleanupMenu(CMenu* menu) const
{
    ULONGLONG items;
    ULONGLONG bytes;
    QueryRecycleBin(items, bytes);

    CStringW info;
    if (items == 1)
    {
        info.FormatMessage(Localization::Lookup(IDS_ONEITEMss), FormatBytes(bytes).GetString(), COptions::HumanFormat && bytes != 0 ? wds::strEmpty : (wds::strBlankSpace + GetSpec_Bytes()).GetString());
    }
    else
    {
        info.FormatMessage(Localization::Lookup(IDS_sITEMSss), FormatCount(items).GetString(), FormatBytes(bytes).GetString(), COptions::HumanFormat && bytes != 0 ? wds::strEmpty : (wds::strBlankSpace + GetSpec_Bytes()).GetString());
    }

    CStringW s = Localization::Lookup(IDS_EMPTYRECYCLEBIN);
    s += info;
    const UINT state = menu->GetMenuState(ID_CLEANUP_EMPTY_BIN, MF_BYCOMMAND);
    VERIFY(menu->ModifyMenu(ID_CLEANUP_EMPTY_BIN, MF_BYCOMMAND | MF_STRING, ID_CLEANUP_EMPTY_BIN, s));
    menu->EnableMenuItem(ID_CLEANUP_EMPTY_BIN, state);

    // remove everything after the last separator
    for (int i = menu->GetMenuItemCount() - 1; i >= 0; i--)
    {
        if ((menu->GetMenuState(i, MF_BYPOSITION) & MF_SEPARATOR) != 0) break;
        menu->RemoveMenu(i, MF_BYPOSITION);
    }

    AppendUserDefinedCleanups(menu);
}

void CMainFrame::QueryRecycleBin(ULONGLONG& items, ULONGLONG& bytes)
{
    items = 0;
    bytes = 0;

    const DWORD drives = ::GetLogicalDrives();
    DWORD mask         = 0x00000001;
    for (int i = 0; i < wds::iNumDriveLetters; i++, mask <<= 1)
    {
        if ((drives & mask) == 0)
        {
            continue;
        }

        CStringW s;
        s.Format(L"%c:\\", i + wds::chrCapA);

        const UINT type = ::GetDriveType(s);
        if (type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR)
        {
            continue;
        }

        if (type == DRIVE_REMOTE)
        {
            continue;
        }

        SHQUERYRBINFO qbi;
        ZeroMemory(&qbi, sizeof(qbi));
        qbi.cbSize = sizeof(qbi);

        if (FAILED(::SHQueryRecycleBin(s, &qbi)))
        {
            continue;
        }

        items += qbi.i64NumItems;
        bytes += qbi.i64Size;
    }
}

void CMainFrame::AppendUserDefinedCleanups(CMenu* menu) const
{
    bool bHasItem = false;
    for (size_t iCurrent = 0; iCurrent < COptions::UserDefinedCleanups.size(); iCurrent++)
    {
        auto& udc = COptions::UserDefinedCleanups[iCurrent];
        if (!udc.enabled) continue;

        CStringW string;
        string.FormatMessage(Localization::Lookup(IDS_UDCsCTRLd), udc.title.Obj().c_str(), iCurrent);

        const auto& items = CFileTreeControl::Get()->GetAllSelected<CItem>();
        bool udc_valid = GetLogicalFocus() == LF_DIRECTORYLIST && !items.empty();
        if (udc_valid) for (const auto& item : items)
        {
            udc_valid &= GetDocument()->UserDefinedCleanupWorksForItem(&udc, item);
        }

        bHasItem = true;
        const UINT flags = udc_valid ? MF_ENABLED : (MF_DISABLED | MF_GRAYED);
        menu->AppendMenu(flags | MF_STRING, ID_USERDEFINEDCLEANUP0 + iCurrent, string);
    }

    if (!bHasItem)
    {
        // This is just to show new users, that they can configure user defined cleanups.
        menu->AppendMenu(MF_GRAYED, 0, Localization::Lookup(IDS_USERDEFINEDCLEANUP0));
    }
}

void CMainFrame::SetLogicalFocus(const LOGICAL_FOCUS lf)
{
    if (lf != m_logicalFocus)
    {
        m_logicalFocus = lf;
        SetSelectionMessageText();

        GetDocument()->UpdateAllViews(nullptr, HINT_SELECTIONSTYLECHANGED);
    }
}

LOGICAL_FOCUS CMainFrame::GetLogicalFocus() const
{
    return m_logicalFocus;
}

void CMainFrame::MoveFocus(const LOGICAL_FOCUS lf)
{
    switch (lf)
    {
    case LF_NONE:
        {
            SetLogicalFocus(LF_NONE);
            m_wndDeadFocus.SetFocus();
        }
        break;
    case LF_DIRECTORYLIST:
        {
            GetFileTreeView()->SetFocus();
        }
        break;
    case LF_EXTENSIONLIST:
        {
            GetExtensionView()->SetFocus();
        }
        break;
    }
}

void CMainFrame::SetSelectionMessageText()
{
    switch (GetLogicalFocus())
    {
    case LF_NONE:
        {
            SetMessageText(Localization::Lookup(IDS_IDLEMESSAGE));
        }
        break;
    case LF_DIRECTORYLIST:
        {
            // display file name in bottom left corner if only one item is selected
            const auto item = CFileTreeControl::Get()->GetFirstSelectedItem<CItem>();
            if (item != nullptr)
            {
                // decide when to do this
                SetMessageText(item->GetPath());
            }
            else
            {
                SetMessageText(Localization::Lookup(IDS_IDLEMESSAGE));
            }
        }
        break;
    case LF_EXTENSIONLIST:
        {
            SetMessageText(wds::strStar + GetDocument()->GetHighlightExtension());
        }
        break;
    }
}

void CMainFrame::OnUpdateEnableControl(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(true);
}

void CMainFrame::OnSize(UINT nType, int cx, int cy)
{
    CFrameWndEx::OnSize(nType, cx, cy);

    if (!::IsWindow(m_wndStatusBar.m_hWnd))
    {
        return;
    }

    CRect rc;
    m_wndStatusBar.GetItemRect(0, rc);

    if (m_progress.m_hWnd != nullptr)
    {
        m_progress.MoveWindow(rc);
    }
    else if (m_pacman.m_hWnd != nullptr)
    {
        m_pacman.MoveWindow(rc);
    }
}

void CMainFrame::OnUpdateViewShowtreemap(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(GetTreeMapView()->IsShowTreemap());
}

void CMainFrame::OnViewShowtreemap()
{
    GetTreeMapView()->ShowTreemap(!GetTreeMapView()->IsShowTreemap());
    if (GetTreeMapView()->IsShowTreemap())
    {
        RestoreTreeMapView();
    }
    else
    {
        MinimizeTreeMapView();
    }
}

void CMainFrame::OnUpdateViewShowFileTypes(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(GetExtensionView()->IsShowTypes());
}

void CMainFrame::OnViewShowFileTypes()
{
    GetExtensionView()->ShowTypes(!GetExtensionView()->IsShowTypes());
    if (GetExtensionView()->IsShowTypes())
    {
        RestoreExtensionView();
    }
    else
    {
        MinimizeExtensionView();
    }
}

void CMainFrame::OnConfigure()
{
    COptionsPropertySheet sheet;

    CPageGeneral general;
    CPageTreelist treelist;
    CPageTreemap treemap;
    CPageCleanups cleanups;
    CPageAdvanced advanced;

    sheet.AddPage(&general);
    sheet.AddPage(&treelist);
    sheet.AddPage(&treemap);
    sheet.AddPage(&cleanups);
    sheet.AddPage(&advanced);

    sheet.DoModal();

    if (sheet.m_restartApplication)
    {
        CDirStatApp::Get()->RestartApplication();
    }
}

void CMainFrame::OnSysColorChange()
{
    CFrameWndEx::OnSysColorChange();
    GetFileTreeView()->SysColorChanged();
    GetExtensionView()->SysColorChanged();
}

BOOL CMainFrame::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd, CCreateContext* pContext)
{
    if (!CFrameWndEx::LoadFrame(nIDResource, dwDefaultStyle, pParentWnd, pContext))
    {
        return FALSE;
    }

    Localization::UpdateMenu(*GetMenu());
    Localization::UpdateDialogs(*this);
    SetTitle(Localization::Lookup(IDS_APP_TITLE));

    return TRUE;
}
