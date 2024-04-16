// ownerdrawnlistcontrol.h - Declaration of COwnerDrawnListControl and COwnerDrawnListItem
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

#include <vector>

#include "SortingListControl.h"

class COwnerDrawnListControl;

//
// COwnerDrawnListItem. An item in a COwnerDrawnListControl.
// Some columns (subitems) may be owner drawn (DrawSubitem() returns true),
// COwnerDrawnListControl draws the texts (GetText()) of all others.
// DrawLabel() draws a standard label (width image, text, selection and focus rect)
//
class COwnerDrawnListItem : public CSortingListItem
{
public:
    COwnerDrawnListItem()          = default;
    virtual ~COwnerDrawnListItem() = default;

    // This text is drawn, if DrawSubitem returns false
    CStringW GetText(int subitem) const override = 0;
    // This color is used for the  current item
    virtual COLORREF GetItemTextColor() const
    {
        return ::GetSysColor(COLOR_WINDOWTEXT);
    }

    // Return value is true, if the item draws itself.
    // width != NULL -> only determine width, do not draw.
    // If focus rectangle shall not begin leftmost, set *focusLeft
    // to the left edge of the desired focus rectangle.
    virtual bool DrawSubitem(int subitem, CDC* pdc, CRect rc, UINT state, int* width, int* focusLeft) const = 0;

    virtual void DrawAdditionalState(CDC* /*pdc*/, const CRect & /*rcLabel*/) const {}

    void DrawSelection(const COwnerDrawnListControl* list, CDC* pdc, CRect rc, UINT state) const;

protected:
    void DrawLabel(const COwnerDrawnListControl* list, CImageList* il, CDC* pdc, CRect& rc, UINT state, int* width, int* focusLeft, bool indent = true) const;
    void DrawPercentage(CDC* pdc, CRect rc, double fraction, COLORREF color) const;
};

//
// COwnerDrawnListControl. Must be report view. Deals with COwnerDrawnListItems.
// Can have a grid or not (own implementation, don't set LVS_EX_GRIDLINES). Flicker-free.
//
class COwnerDrawnListControl : public CSortingListControl
{
    DECLARE_DYNAMIC(COwnerDrawnListControl)

public:
    COwnerDrawnListControl(int rowHeight, std::vector<int>* column_order, std::vector<int>* column_widths);
    ~COwnerDrawnListControl() override = default;
    void OnColumnsInserted();
    virtual void SysColorChanged();

    int GetRowHeight() const;
    void ShowGrid(bool show);
    void ShowStripes(bool show);
    void ShowFullRowSelection(bool show);
    bool IsFullRowSelection() const;

    COLORREF GetWindowColor() const;
    COLORREF GetStripeColor() const;
    COLORREF GetNonFocusHighlightColor() const;
    COLORREF GetNonFocusHighlightTextColor() const;
    COLORREF GetHighlightColor() const;
    COLORREF GetHighlightTextColor() const;

    bool IsItemStripeColor(int i) const;
    bool IsItemStripeColor(const COwnerDrawnListItem* item) const;
    COLORREF GetItemBackgroundColor(int i) const;
    COLORREF GetItemBackgroundColor(const COwnerDrawnListItem* item) const;
    COLORREF GetItemSelectionBackgroundColor(int i) const;
    COLORREF GetItemSelectionBackgroundColor(const COwnerDrawnListItem* item) const;
    COLORREF GetItemSelectionTextColor(int i) const;

    COwnerDrawnListItem* GetItem(int i) const;
    int FindListItem(const COwnerDrawnListItem* item) const;
    int GetTextXMargin() const;
    int GetGeneralLeftIndent() const;
    void AdjustColumnWidth(int col);
    CRect GetWholeSubitemRect(int item, int subitem) const;

    bool HasFocus() const;
    bool IsShowSelectionAlways() const;

protected:
    void InitializeColors();
    void DrawItem(LPDRAWITEMSTRUCT pdis) override;
    int GetSubItemWidth(const COwnerDrawnListItem* item, int subitem);
    bool IsColumnRightAligned(int col) const;

    COLORREF m_windowColor = CLR_NONE; // The default background color if !m_showStripes
    COLORREF m_stripeColor = CLR_NONE; // The stripe color, used for every other item if m_showStripes
    int m_rowHeight;                   // Height of an item
    bool m_showGrid = false;           // Whether to draw a grid
    bool m_showStripes = false;        // Whether to show stripes
    bool m_showFullRowSelect = false;  // Whether to draw full row selection

    DECLARE_MESSAGE_MAP()
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnHdnDividerdblclick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnHdnItemchanging(NMHDR* pNMHDR, LRESULT* pResult);
};
