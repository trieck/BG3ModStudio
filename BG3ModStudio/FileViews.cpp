#include "stdafx.h"
#include "FileDialogEx.h"
#include "FileViewFactory.h"
#include "FileViews.h"
#include "resources/resource.h"

LRESULT FilesView::OnCreate(LPCREATESTRUCT pcs)
{
    if (pcs == nullptr) {
        return -1;
    }

    if (TabViewImpl::OnCreate(pcs) == -1) {
        return -1;
    }

    LOGFONT lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfHeight = 16;
    lf.lfWeight = FW_SEMIBOLD;
    Checked::tcsncpy_s(lf.lfFaceName, _countof(lf.lfFaceName), _T("Tahoma"), _TRUNCATE);

    m_tabFont.CreateFontIndirect(&lf);
    lf.lfWeight = FW_NORMAL;
    m_disabledFont.CreateFontIndirect(&lf);

    m_tabViewCtrl.m_tabCtrl.SetFont(m_tabFont);

    m_tabViewCtrl.SetTopMargin(1);
    m_tabViewCtrl.SetViewBorder(2);

    return 0;
}

BOOL FilesView::NewFile()
{
    auto fileView = FileViewFactory::CreateFileView(*this, rcDefault);
    if (fileView == nullptr) {
        AtlMessageBox(*this, L"Unable to create a file view", nullptr, MB_ICONERROR);
        return FALSE;
    }

    CString title;
    title.Format(L"Untitled %d", ++m_nextId);

    AddPage(fileView, title, nullptr);

    return TRUE;
}

BOOL FilesView::ActivateFile(const CString& path, LPVOID data)
{
    auto it = m_data.find(data);
    if (it != m_data.end()) {
        SetActivePage(it->second);
        OnPageActivated(it->second);
        UpdateLayout();
        return TRUE;
    }

    // The file view may be open and not associated with the data yet
    auto fileView = GetFileView(path);
    if (fileView != nullptr) {
        return ActivateView(fileView, data);
    }

    fileView = FileViewFactory::CreateFileView(path, *this, rcDefault);
    if (fileView == nullptr) {
        CString message;
        message.Format(L"Unable to create a file view for \"%s\".\nThis file type may be unsupported.",
                       path.GetString());
        AtlMessageBox(*this, static_cast<LPCWSTR>(message), nullptr, MB_ICONERROR);
        return FALSE;
    }

    if (!fileView->LoadFile(path)) {
        fileView->Destroy();

        CString message;
        message.Format(L"Unable to load file \"%s\".", path.GetString());
        AtlMessageBox(*this, static_cast<LPCWSTR>(message), nullptr, MB_ICONERROR);
        return FALSE;
    }

    CString title = PathFindFileName(path);

    AddPage(fileView, title, path, data);

    return TRUE;
}

BOOL FilesView::CloseFileByData(LPVOID data)
{
    auto it = m_data.find(data);
    if (it == m_data.end()) {
        return FALSE;
    }

    CloseFile(it->second);

    return TRUE;
}

IFileView::Ptr FilesView::ActiveView() const
{
    int activePage = GetActivePage();

    if (activePage < 0) {
        return nullptr;
    }

    return m_views[activePage];
}

int FilesView::ActivePage() const
{
    return GetActivePage();
}

const std::vector<IFileView::Ptr>& FilesView::Files() const
{
    return m_views;
}

PVOID FilesView::GetData(int index) const
{
    if (index < 0 || index >= GetPageCount()) {
        return nullptr;
    }

    auto hItem = GetPageData(index);

    return hItem;
}

PVOID FilesView::CloseFile(int index)
{
    if (index < 0 || index >= GetPageCount()) {
        return nullptr;
    }

    const auto& vit = m_views.begin() + index;
    auto& fileView = *vit;

    if (fileView->IsDirty()) {
        CString message;
        message.Format(L"Save changes to \"%s\"?", fileView->GetPath());
        auto result = AtlMessageBox(*this, static_cast<LPCWSTR>(message), IDR_MAINFRAME,
                                    MB_YESNOCANCEL | MB_ICONQUESTION | MB_ICONWARNING);
        if (result == IDCANCEL) {
            return nullptr;
        }

        if (result == IDYES) {
            if (!SaveFile(fileView)) {
                return nullptr;
            }
        }
    }

    auto hItem = GetData(index);

    auto it = m_data.find(hItem);
    if (it != m_data.end()) {
        m_data.erase(it);

        // adjust indexes
        for (auto& idx : m_data | std::views::values) {
            if (idx > index) {
                --idx;
            }
        }
    }

    fileView->Destroy();

    m_views.erase(vit);

    RemovePage(index);

    // Update tooltips
    TOOLINFO ti{};
    ti.cbSize = sizeof(TOOLINFO);
    ti.hwnd = m_tabViewCtrl.m_tabCtrl;
    ti.uId = index;
    ti.hinst = _Module.GetResourceInstance();

    auto toolTips = m_tabViewCtrl.m_tabCtrl.GetToolTips();

    auto toolCount = toolTips.GetToolCount();
    for (auto i = 0; i < toolCount; ++i) {
        ti.uId = i;
        toolTips.DelTool(&ti);
    }

    for (auto i = 0; i < GetPageCount(); ++i) {
        ti.uId = i;
        ti.lpszText = const_cast<LPWSTR>(m_views[i]->GetPath().GetString());
        toolTips.AddTool(&ti);
    }

    UpdateLayout();

    return hItem;
}

PVOID FilesView::CloseOtherFiles(int index)
{
    auto hItem = GetData(index);

    for (auto i = GetPageCount() - 1; i >= 0; --i) {
        if (i == index) {
            continue;
        }
        CloseFile(i);
    }

    return hItem;
}

BOOL FilesView::CloseAllFiles()
{
    for (auto i = GetPageCount() - 1; i >= 0; --i) {
        auto before = GetPageCount();
        CloseFile(i);
        if (GetPageCount() == before) {
            return FALSE;
        }
    }
    return TRUE;
}

PVOID FilesView::CloseActiveFile()
{
    return CloseFile(GetActivePage());
}

FileEncoding FilesView::FileEncoding(int index) const
{
    if (index < 0 || index >= GetPageCount()) {
        return UNKNOWN;
    }

    ATLASSERT(index < static_cast<int>(m_views.size()));

    return m_views[index]->GetEncoding();
}

BOOL FilesView::IsDirty(int index) const
{
    if (index < 0 || index >= GetPageCount()) {
        return FALSE;
    }

    return m_views[index]->IsDirty();
}

BOOL FilesView::IsDirty() const
{
    for (const auto& view : m_views) {
        if (view->IsDirty()) {
            return TRUE;
        }
    }

    return FALSE;
}

BOOL FilesView::SaveFile(const IFileView::Ptr& fileView)
{
    if (fileView == nullptr) {
        return FALSE;
    }

    if (!fileView->IsDirty()) {
        return TRUE;
    }

    if (fileView->GetPath().IsEmpty()) {
        // Prompt for a file name
        auto path = GetTitle(fileView);
        if (path.IsEmpty()) {
            path = L"Untitled";
        }

        auto filter = L"All Files (*.*)\0*.*\0"
            L"XML Files (*.xml, *.lsx)\0*.xml;*.lsx\0"
            L"JSON Files (*.json, *.lsj)\0*.json;*.lsj\0"
            L"Text Files(*.txt)\0 * .txt\0\0";

        FileDialogEx dlg(FileDialogEx::Save, *this, nullptr, path, 0, filter);
        auto hr = dlg.Construct();
        if (FAILED(hr)) {
            return FALSE;
        }

        if (dlg.DoModal() != IDOK) {
            return FALSE;
        }

        auto filename = dlg.paths().front();
        fileView->SetPath(filename);

        SetTitle(fileView, PathFindFileName(filename));
    }

    if (!fileView->SaveFile()) {
        CString message;
        message.Format(L"Unable to save file \"%s\".", fileView->GetPath());
        AtlMessageBox(*this, static_cast<LPCWSTR>(message), nullptr, MB_ICONERROR);
        return FALSE;
    }

    return TRUE;
}

BOOL FilesView::SaveAll()
{
    BOOL success = TRUE;

    for (const auto& view : m_views) {
        if (!SaveFile(view)) {
            success = FALSE;
        }
    }

    return success;
}

CString FilesView::GetTitle(const IFileView::Ptr& fileView) const
{
    if (fileView == nullptr) {
        return L"";
    }

    CString title;

    for (auto i = 0; i < GetPageCount(); ++i) {
        if (m_views[i] == fileView) {
            title = GetPageTitle(i);
            break;
        }
    }

    return title;
}

void FilesView::SetTitle(const IFileView::Ptr& fileView, LPCTSTR lpstrTitle)
{
    if (fileView == nullptr) {
        return;
    }

    for (auto i = 0; i < GetPageCount(); ++i) {
        if (m_views[i] == fileView) {
            SetPageTitle(i, lpstrTitle);
            break;
        }
    }
}

BOOL FilesView::ActiveViewIsEditable() const
{
    auto view = ActiveView();
    if (view == nullptr) {
        return FALSE;
    }

    return view->IsText() && view->IsEditable();
}

IFileView::Ptr FilesView::GetFileView(int index) const
{
    if (index < 0 || index >= GetPageCount()) {
        return nullptr;
    }
    return m_views[index];
}

IFileView::Ptr FilesView::GetFileView(LPVOID data) const
{
    auto it = m_data.find(data);
    if (it == m_data.end()) {
        return nullptr;
    }

    return m_views[it->second];
}

IFileView::Ptr FilesView::GetFileView(const CString& path) const
{
    for (const auto& view : m_views) {
        if (view->GetPath().CompareNoCase(path) == 0) {
            return view;
        }
    }

    return nullptr;
}

inline BOOL FilesView::ActivateView(const IFileView::Ptr& fileView, LPVOID data)
{
    auto index = GetViewIndex(fileView);
    if (index == -1) {
        return FALSE;
    }

    m_data[data] = index; // rewrite or insert

    SetActivePage(index);
    OnPageActivated(index);
    UpdateLayout();

    return TRUE;
}

int FilesView::GetViewIndex(const IFileView::Ptr& fileView) const
{
    for (auto i = 0; i < GetPageCount(); ++i) {
        if (m_views[i] == fileView) {
            return i;
        }
    }

    return -1;
}

BOOL FilesView::SetData(const CString& path, LPVOID data)
{
    auto fileView = GetFileView(path);
    if (fileView == nullptr) {
        return FALSE;
    }

    auto index = GetViewIndex(fileView);
    if (index == -1) {
        return FALSE;
    }

    m_data[data] = index;

    SetPageData(index, data);

    return TRUE;
}

BOOL FilesView::RenameFile(const CString& oldname, const CString& newname)
{
    auto fileView = GetFileView(oldname);
    if (fileView == nullptr) {
        return FALSE;
    }

    fileView->SetPath(newname);

    if (fileView->IsDirty()) {
        CString message;
        message.Format(L"The file \"%s\" has been renamed to \"%s\", but your changes have not been saved.\n"
                       L"Do you want to save the changes under the new name?", oldname.GetString(),
                       newname.GetString());
        auto result = AtlMessageBox(*this, static_cast<LPCWSTR>(message), IDR_MAINFRAME,
                                    MB_YESNO | MB_ICONQUESTION | MB_ICONWARNING);
        if (result == IDYES) {
            SaveFile(fileView);
        }
    }

    auto index = GetViewIndex(fileView);
    if (index == -1) {
        return FALSE;
    }

    SetTitle(fileView, PathFindFileName(newname));

    auto toolTips = m_tabViewCtrl.m_tabCtrl.GetToolTips();

    TOOLINFO ti{};
    ti.cbSize = sizeof(TOOLINFO);
    ti.hwnd = m_tabViewCtrl.m_tabCtrl;
    ti.uId = index;
    ti.hinst = _Module.GetResourceInstance();

    toolTips.DelTool(&ti);

    ti.lpszText = const_cast<LPWSTR>(newname.GetString());
    toolTips.AddTool(&ti);

    UpdateLayout();

    return TRUE;
}

void FilesView::OnPageActivated(int nPage)
{
    NMHDR nmhdr;
    nmhdr.hwndFrom = this->m_hWnd;
    nmhdr.idFrom = nPage;
    nmhdr.code = TBVN_PAGEACTIVATED;
    GetTopLevelParent().SendMessage(WM_NOTIFY, this->GetDlgCtrlID(), reinterpret_cast<LPARAM>(&nmhdr));
}

void FilesView::OnPageContextMenu(int nPage, const CPoint& pt)
{
    TVWCONTEXTMENUINFO cmInfo;
    cmInfo.hdr.hwndFrom = this->m_hWnd;
    cmInfo.hdr.idFrom = nPage;
    cmInfo.hdr.code = TBVN_CONTEXTMENU;
    cmInfo.pt = pt;
    GetTopLevelParent().SendMessage(WM_NOTIFY, this->GetDlgCtrlID(), reinterpret_cast<LPARAM>(&cmInfo));
}

void FilesView::OnDrawTabItem(LPDRAWITEMSTRUCT dis)
{
    CDCHandle dc(dis->hDC);
    auto rc = dis->rcItem;
    auto sel = (dis->itemState & ODS_SELECTED) != 0;
    auto hTab = dis->hwndItem;
    auto index = dis->itemID;

    auto clrFace = GetSysColor(sel ? COLOR_WINDOW : COLOR_BTNFACE);
    auto clrLight = GetSysColor(COLOR_BTNHIGHLIGHT);
    auto clrDkShadow = GetSysColor(COLOR_3DDKSHADOW);
    auto clrText = sel ? RGB(96, 0, 0) : GetSysColor(COLOR_GRAYTEXT);

    dc.FillSolidRect(&rc, clrFace);

    if (sel) {
        dc.Draw3dRect(&rc, clrLight, clrDkShadow);
    }

    WCHAR text[MAX_PATH]{};

    TCITEM tci{};
    tci.mask = TCIF_TEXT;
    tci.pszText = text;
    tci.cchTextMax = _countof(text);
    TabCtrl_GetItem(hTab, index, &tci);

    auto hOldFont = dc.SelectFont(sel ? m_tabFont : m_disabledFont);

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(clrText);

    InflateRect(&rc, -1, -1);

    dc.DrawText(text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    dc.SelectFont(hOldFont);
}

DWORD FilesView::GetTabCtrlStyle() const
{
    return CControlWinTraits::GetWndStyle(0) | TCS_TOOLTIPS | TCS_OWNERDRAWFIXED;
}

BOOL FilesView::AddPage(const IFileView::Ptr& fileView, LPCTSTR lpstrTitle, LPCTSTR lpstrPath, LPVOID pData)
{
    ATLASSERT(fileView != nullptr);
    ATLASSERT(lpstrTitle != nullptr);

    auto nPages = GetPageCount();

    if (pData != nullptr) {
        m_data[pData] = nPages;
    }

    m_views.emplace_back(fileView);

    if (!Base::AddPage(*fileView, lpstrTitle, -1, pData)) {
        ATLTRACE("Unable to add page.\n");
        return FALSE;
    }

    ATLASSERT(m_tabViewCtrl.m_tabCtrl.IsWindow());

    CString path;
    if (lpstrPath != nullptr) {
        path = lpstrPath;
    }

    TOOLINFO ti{};
    ti.cbSize = sizeof(TOOLINFO);
    ti.hwnd = m_tabViewCtrl.m_tabCtrl;
    ti.uId = nPages;
    ti.hinst = _Module.GetResourceInstance();
    ti.lpszText = const_cast<LPTSTR>(path.GetString());

    m_tabViewCtrl.m_tabCtrl.GetToolTips().UpdateTipText(&ti);

    SetActivePage(nPages);

    return TRUE;
}
