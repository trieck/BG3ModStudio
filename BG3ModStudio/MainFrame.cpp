#include "stdafx.h"
#include "AboutDlg.h"
#include "COMError.h"
#include "FileDialogEx.h"
#include "FileOperation.h"
#include "FindReplaceDlg.h"
#include "GameObjectDlg.h"
#include "IconExplorerDlg.h"
#include "IndexDlg.h"
#include "Localization.h"
#include "MainFrame.h"
#include "NewProjectWizard.h"
#include "PakExplorerDlg.h"
#include "PAKWizard.h"
#include "ResourceUtils.h"
#include "SearchDlg.h"
#include "SettingsDlg.h"
#include "StringHelper.h"
#include "Util.h"
#include "UUIDDlg.h"

#include <filesystem>

#include "DatabaseDlg.h"
namespace fs = std::filesystem;

static constexpr auto REGISTRY_KEY = L"Software\\Rieck Enterprises\\BG3ModStudio";

BOOL MainFrame::DefCreate()
{
    RECT rect = {0, 0, 1024, 600};
    auto hWnd = CreateEx(nullptr, rect);
    if (hWnd == nullptr) {
        ATLTRACE(_T("Unable to create main frame.\n"));
        return FALSE;
    }

    return TRUE;
}

BOOL MainFrame::PreTranslateMessage(MSG* pMsg)
{
    if (IsDialogMessage(pMsg)) {
        return TRUE;
    }

    return CFrameWindowImpl::PreTranslateMessage(pMsg);
}

LRESULT MainFrame::OnCreate(LPCREATESTRUCT pcs)
{
    m_cmdBar.Create(m_hWnd, rcDefault, nullptr, ATL_SIMPLE_CMDBAR_PANE_STYLE);
    m_cmdBar.LoadImages(IDR_MAINFRAME);
    m_cmdBar.AttachMenu(GetMenu());
    this->SetMenu(nullptr); // remove old menu

    auto fileMenu = m_cmdBar.GetMenu().GetSubMenu(0);
    ATLASSERT(!fileMenu.IsNull());

    m_mru.SetMaxEntries(4);
    m_mru.SetMenuHandle(fileMenu);
    m_mru.ReadFromRegistry(REGISTRY_KEY);
    m_mru.UpdateMenu();

    if (RunTimeHelper::IsRibbonUIAvailable()) {
        UIAddMenu(m_cmdBar.GetMenu(), true);
        UIRemoveUpdateElement(ID_FILE_MRU_FIRST);
    }

    if (!CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE)) {
        ATLTRACE("Unable to create rebar.\n");
        return -1;
    }

    if (!AddSimpleReBarBand(m_cmdBar)) {
        ATLTRACE("Unable to add rebar band.\n");
        return -1;
    }

    if (!CreateSimpleStatusBar()) {
        ATLTRACE("Unable to create status bar.\n");
        return -1;
    }

    ATLASSERT(::IsWindow(m_hWndStatusBar));
    m_statusBar.Attach(m_hWndStatusBar);

    int parts[] = {200, -1};
    m_statusBar.SetParts(2, parts);
    m_statusBar.SetSimple(FALSE);

    m_bom.LoadIcon(IDI_BOM, 12, 12);
    m_nobom.LoadIcon(IDI_NOBOM, 12, 12);

    m_hWndClient = m_splitter.Create(*this, rcDefault, nullptr,
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
    if (m_hWndClient == nullptr) {
        ATLTRACE("Unable to create splitter window.\n");
        return -1;
    }

    if (!m_folderView.Create(m_splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                             WS_EX_CLIENTEDGE)) {
        ATLTRACE("Unable to create folder view window.\n");
        return -1;
    }

    if (!m_filesView.Create(m_splitter, rcDefault, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE)) {
        ATLTRACE("Unable to create files view window.\n");
        return -1;
    }

    m_splitter.SetSplitterPane(0, m_folderView);
    m_splitter.SetSplitterPane(1, m_filesView);
    m_splitter.SetSplitterPosPct(30);

    UISetCheck(ID_VIEW_STATUS_BAR, 1);

    ShowRibbonUI(TRUE);

    UpdateLayout();
    CenterWindow();

    // register object for message filtering and idle updates
    auto pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != nullptr);
    pLoop->AddMessageFilter(this);
    pLoop->AddIdleHandler(this);

    return 1;
}

void MainFrame::OnFolderOpen()
{
    FileDialogEx dlg(FileDialogEx::Folder, *this);
    auto hr = dlg.Construct();
    if (FAILED(hr)) {
        return;
    }

    if (dlg.DoModal() != IDOK) {
        return;
    }

    OpenFolder(dlg.paths()[0]);
}

void MainFrame::OnFolderClose()
{
    if (!m_filesView.CloseAllFiles()) {
        return;
    }
    m_folderView.DeleteAllItems();
    m_folderView.RedrawWindow();

    UpdateTitle();
}

void MainFrame::OnFolderPack()
{
    auto rootPath = m_folderView.GetRootPath();
    if (rootPath.IsEmpty()) {
        AtlMessageBox(*this, L"Please open a folder first.", nullptr, MB_ICONEXCLAMATION);
        return;
    }

    PAKWizard wizard(rootPath);
    wizard.DoModal();
}

void MainFrame::OnGameObject()
{
    GameObjectDlg::RunOnce(*this);
}

void MainFrame::OnIconExplorer()
{
    IconExplorerDlg::RunOnce(*this);
}

void MainFrame::OnIndex()
{
    IndexDlg dlg;
    dlg.RunModal(*this);
}

void MainFrame::OnLaunchGame()
{
    Settings settings;
    auto gamePath = settings.GetString(_T("Settings"), _T("GamePath"), _T(""));

    CString directory;
    directory.Format(_T("%s\\bin"), static_cast<LPCTSTR>(gamePath));

    CString exePath;
    exePath.Format(_T("%s\\bg3.exe"), static_cast<LPCTSTR>(directory));

    if (!PathFileExists(exePath)) {
        CString msg;
        msg.Format(L"Game executable not found:\n\n%s\n\nPlease verify your game path.", exePath);
        AtlMessageBox(*this, msg.GetString(), nullptr, MB_ICONEXCLAMATION);
        return;
    }

    SHELLEXECUTEINFO sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = m_hWnd;
    sei.lpVerb = _T("open");
    sei.lpFile = exePath;
    sei.lpParameters = _T("--skip-launcher");
    sei.lpDirectory = directory;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteEx(&sei)) {
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        CoMessageBox(*this, hr, nullptr, L"Failed to launch game executable.", MB_ICONERROR);
        return;
    }

    if (sei.hProcess != nullptr) {
        CloseHandle(sei.hProcess);
    }
}

void MainFrame::OnSearch()
{
    SearchDlg::RunOnce(*this);
}

void MainFrame::OnSettings()
{
    SettingsDlg dlg;
    dlg.DoModal(*this);
}

void MainFrame::OnUUID()
{
    UUIDDlg dlg;
    dlg.DoModal(*this);
}

void MainFrame::OnFileSave()
{
    auto view = m_filesView.ActiveView();
    ATLASSERT(view != nullptr);

    m_filesView.SaveFile(view);

    AddFile(view->GetPath());
}

void MainFrame::OnFileSaveAll()
{
    m_filesView.SaveAll();
}

void MainFrame::OnFindReplace()
{
    FindReplaceDlg::RunOnce(*this);
}

void MainFrame::OnFileExit()
{
    PostMessage(WM_CLOSE);
}

void MainFrame::OnDeleteFile()
{
    auto item = m_folderView.GetSelectedItem();
    if (item.IsNull()) {
        return;
    }

    auto root = m_folderView.GetRootItem();
    if (item == root) {
        AtlMessageBox(*this, L"Cannot delete the root folder.", nullptr, MB_ICONEXCLAMATION);
        return;
    }

    auto lParam = item.GetData();
    if (lParam == TIT_UNKNOWN || lParam == TIT_FILE || lParam == TIT_FOLDER) {
        return; // not fully constructed
    }

    auto data = std::bit_cast<TreeItemData*>(lParam);
    CString filename = data ? data->path : CString();
    if (filename.IsEmpty()) {
        m_folderView.DeleteItem(item);
        m_folderView.RedrawWindow();
        return;
    }

    auto isFolder = data && data->type == TIT_FOLDER;

    CString message;
    if (isFolder) {
        message.Format(L"Are you sure you want to delete the folder \"%s\"?", filename);
    } else {
        message.Format(L"Are you sure you want to delete the file \"%s\"?", filename);
    }

    auto result = AtlMessageBox(*this, static_cast<LPCTSTR>(message), nullptr,
                                MB_ICONWARNING | MB_ICONQUESTION | MB_YESNO);
    if (result != IDYES) {
        return;
    }

    FileOperation op;
    auto hr = op.Create();
    if (FAILED(hr)) {
        CoMessageBox(*this, hr, nullptr, MB_ICONERROR);
        return;
    }

    CWaitCursor wait;
    hr = op.DeleteItem(filename);
    if (FAILED(hr)) {
        CoMessageBox(*this, hr, nullptr, MB_ICONERROR);
        return;
    }

    auto hItem = item.m_hTreeItem;
    auto hParent = m_folderView.GetParentItem(hItem);

    m_folderView.DeleteItem(hItem);
    m_folderView.SelectItem(hParent);
    m_folderView.EnsureVisible(hParent);
    m_folderView.RedrawWindow();
}

void MainFrame::OnFileAbout()
{
    AboutDlg dlg;
    dlg.DoModal(*this);
}

void MainFrame::OnNewFile()
{
    ATLASSERT(m_filesView.IsWindow());

    m_filesView.NewFile();
}

void MainFrame::OnNewFolder()
{
    NewProjectWizard wizard;
    if (wizard.DoModal(*this) == IDOK && wizard.GetOpenNewProject()) {
        const auto& projectFolder = wizard.GetProjectFolder();
        if (!projectFolder.IsEmpty()) {
            OpenFolder(projectFolder);
        }
    }
}

void MainFrame::OnNewFileHere()
{
    auto parent = m_folderView.GetSelectedItem();
    if (parent.IsNull()) {
        return;
    }

    ATLASSERT(m_folderView.GetItemType(parent) == TIT_FOLDER);

    // Ensure the parent is known to have children
    TVITEM tvi{};
    tvi.mask = TVIF_CHILDREN;
    tvi.hItem = parent;
    tvi.cChildren = 1;
    TreeView_SetItem(m_folderView, &tvi);

    auto newItem = m_folderView.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM/*mask*/,
                                           L"New File",
                                           1, /* image */
                                           1, /* selected image */
                                           0 /*state*/,
                                           0 /*state mask */,
                                           TIT_FILE /*lparam*/,
                                           parent, TVI_LAST);
    ATLASSERT(newItem != nullptr);

    m_folderView.EnsureVisible(newItem);
    m_folderView.EditLabel(newItem);
}

void MainFrame::OnNewFolderHere()
{
    auto parent = m_folderView.GetSelectedItem();
    if (parent.IsNull()) {
        return;
    }

    ATLASSERT(m_folderView.GetItemType(parent) == TIT_FOLDER);

    // Ensure the parent is known to have children
    TVITEM tvi{};
    tvi.mask = TVIF_CHILDREN;
    tvi.hItem = parent;
    tvi.cChildren = 1;
    TreeView_SetItem(m_folderView, &tvi);

    auto newItem = m_folderView.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM /*mask*/,
                                           L"New Folder",
                                           0, /* image */
                                           0, /* selected image */
                                           0 /*state*/,
                                           0 /*state mask */,
                                           TIT_FOLDER /*lparam*/,
                                           parent, TVI_LAST);
    ATLASSERT(newItem != nullptr);

    m_folderView.EnsureVisible(newItem);
    m_folderView.EditLabel(newItem);
}

void MainFrame::OnPakOpen()
{
    FileDialogEx dlg(FileDialogEx::Open, m_hWnd, _T("pak"), nullptr, OFN_HIDEREADONLY,
                     _T("Pak Files (*.pak)\0*.pak\0All Files (*.*)\0*.*\0"));
    auto hr = dlg.Construct();
    if (FAILED(hr)) {
        return;
    }

    if (dlg.DoModal() != IDOK) {
        return;
    }

    const auto& paths = dlg.paths();
    if (paths.empty()) {
        return;
    }

    CWaitCursor wait;
    PAKReader reader;
    if (!reader.read(StringHelper::toUTF8(paths[0]).GetString())) {
        AtlMessageBox(*this, L"Failed to open PAK file.", nullptr, MB_ICONERROR);
        return;
    }

    auto* pakDlg = new PakExplorerDlg();
    pakDlg->SetPAKReader(std::move(reader));
    pakDlg->Run(*this);
}

void MainFrame::OnRenameFile()
{
    auto item = m_folderView.GetSelectedItem();
    if (item.IsNull()) {
        return;
    }

    auto type = m_folderView.GetItemType(item);
    if (type != TIT_FILE && type != TIT_FOLDER) {
        return;
    }

    m_folderView.EnsureVisible(item);
    m_folderView.EditLabel(item);
}

void MainFrame::OnConvertLoca()
{
    auto item = m_folderView.GetSelectedItem();
    if (item.IsNull()) {
        return;
    }

    auto path = m_folderView.GetItemPath(item);
    if (path.IsEmpty()) {
        return;
    }

    TCHAR szPath[MAX_PATH];
    Checked::tcscpy_s(szPath, _countof(szPath), path.GetString());

    if (PathFindExtension(szPath) && _tcslen(PathFindExtension(szPath)) > 1) {
        PathRemoveExtension(szPath);
    }

    PathRemoveExtension(szPath);
    _tcscat_s(szPath, _T(".loca"));

    TCHAR szFolder[MAX_PATH];
    Checked::tcscpy_s(szFolder, _countof(szFolder), path.GetString());
    PathRemoveFileSpec(szFolder);

    auto filter = L"LOCA Files (*.loca)\0*.loca\0"
        L"All Files(*.*)\0*.*\0\0";

    FileDialogEx dlg(FileDialogEx::Save, *this, nullptr, szPath, 0, filter);
    auto hr = dlg.Construct();
    if (FAILED(hr)) {
        return;
    }

    hr = dlg.SetFolder(szFolder);
    if (FAILED(hr)) {
        return;
    }

    if (dlg.DoModal() != IDOK) {
        return;
    }

    auto utf8Path = StringHelper::toUTF8(path);
    auto utf8LocaPath = StringHelper::toUTF8(dlg.paths().front());

    LocaResource resource;

    try {
        resource = LocaXmlReader::Read(utf8Path.GetString());
    } catch (const std::exception& e) {
        CString error = StringHelper::fromUTF8(e.what());
        CString message;
        message.Format(L"An error occurred while reading the file: \"%s\": \"%s\".",
                       path, error);
        AtlMessageBox(*this, message.GetString(), nullptr, MB_ICONERROR);
        return;
    }

    try {
        LocaWriter::Write(utf8LocaPath.GetString(), resource);
    } catch (const std::exception& e) {
        CString error = StringHelper::fromUTF8(e.what());
        CString message;
        message.Format(L"An error occurred while writing the file: \"%s\": \"%s\".",
                       dlg.paths().front(), error);
        AtlMessageBox(*this, message.GetString(), nullptr, MB_ICONERROR);
        return;
    }

    AddFile(dlg.paths().front());

    AtlMessageBox(*this, L"File converted successfully.", nullptr, MB_ICONINFORMATION);
}

void MainFrame::OnConvertLSF()
{
    auto item = m_folderView.GetSelectedItem();
    if (item.IsNull()) {
        return;
    }

    auto path = m_folderView.GetItemPath(item);
    if (path.IsEmpty()) {
        return;
    }

    TCHAR szPath[MAX_PATH];
    Checked::tcscpy_s(szPath, _countof(szPath), path.GetString());

    if (PathFindExtension(szPath) && _tcslen(PathFindExtension(szPath)) > 1) {
        PathRemoveExtension(szPath);
    }

    PathRemoveExtension(szPath);
    _tcscat_s(szPath, _T(".lsf"));

    TCHAR szFolder[MAX_PATH];
    Checked::tcscpy_s(szFolder, _countof(szFolder), path.GetString());
    PathRemoveFileSpec(szFolder);

    auto filter = L"LSF Files (*.lsf)\0*.lsf\0"
        L"All Files(*.*)\0*.*\0\0";

    FileDialogEx dlg(FileDialogEx::Save, *this, nullptr, szPath, 0, filter);
    auto hr = dlg.Construct();
    if (FAILED(hr)) {
        return;
    }

    hr = dlg.SetFolder(szFolder);
    if (FAILED(hr)) {
        return;
    }

    if (dlg.DoModal() != IDOK) {
        return;
    }

    auto lsfFile = dlg.paths().front();
    auto utf8Path = StringHelper::toUTF8(path);
    auto utf8LSFPath = StringHelper::toUTF8(lsfFile);

    auto resource = ResourceUtils::loadResource(utf8Path, LSX);

    try {
        ResourceUtils::saveResource(utf8LSFPath, resource, LSF);
        AddFile(lsfFile);
    } catch (const std::exception& e) {
        CString error = StringHelper::fromUTF8(e.what());
        CString message;
        message.Format(L"An error occurred while writing the file: \"%s\": \"%s\".",
                       lsfFile, error);
        AtlMessageBox(*this, message.GetString(), nullptr, MB_ICONERROR);
    }
}

void MainFrame::OnDatabase()
{
    DatabaseDlg dlg;
    dlg.RunModal(*this);
}

void MainFrame::OnClose()
{
    m_mru.WriteToRegistry(REGISTRY_KEY);

    if (m_filesView.IsWindow() && !m_filesView.CloseAllFiles()) {
        return;
    }

    if (m_folderView.IsWindow()) {
        m_folderView.DeleteAllItems();
    }

    DestroyWindow();
}

LRESULT MainFrame::OnTVDelete(LPNMHDR pnmhdr)
{
    const auto item = MAKE_OLDTREEITEM(pnmhdr, &m_folderView);

    if (m_filesView.IsWindow()) {
        m_filesView.CloseFileByData(item.m_hTreeItem);
    }

    BOOL bHandled;
    ReflectNotifications(WM_NOTIFY, 0, reinterpret_cast<LPARAM>(pnmhdr), bHandled);

    return 0;
}

LRESULT MainFrame::OnTVBeginLabelEdit(LPNMHDR pnmhdr)
{
    NMTVDISPINFO* pDispInfo = reinterpret_cast<NMTVDISPINFO*>(pnmhdr);

    auto editCtrl = m_folderView.GetEditControl();
    if (editCtrl.IsWindow()) {
        editCtrl.SetLimitText(MAX_PATH);
        editCtrl.SetWindowText(pDispInfo->item.pszText);
    } else {
        return TRUE; // prevent label edit
    }

    return FALSE;
}

LRESULT MainFrame::OnTVEndLabelEdit(LPNMHDR pnmhdr)
{
    NMTVDISPINFO* pDispInfo = reinterpret_cast<NMTVDISPINFO*>(pnmhdr);

    if (pDispInfo->item.pszText == nullptr || wcslen(pDispInfo->item.pszText) == 0) {
        return FALSE;
    }

    auto lParam = pDispInfo->item.lParam;
    switch (lParam) {
    case TIT_UNKNOWN:
        return FALSE;
    case TIT_FILE:
    case TIT_FOLDER:
        return NewFile(pDispInfo);
    default:
        // File rename
        return RenameFile(pDispInfo);
    }
}

LRESULT MainFrame::OnTVSelChanged(LPNMHDR /*pnmhdr*/)
{
    auto item = m_folderView.GetSelectedItem();
    if (item.IsNull()) {
        return 0;
    }

    CWaitCursor wait;

    auto lParam = item.GetData();
    if (lParam == TIT_UNKNOWN || lParam == TIT_FILE || lParam == TIT_FOLDER) {
        return 0; // not fully constructed
    }

    auto data = std::bit_cast<TreeItemData*>(lParam);
    if (data && data->type == TIT_FILE) {
        if (!m_filesView.ActivateFile(data->path, item.m_hTreeItem)) {
            // Select the item's parent if unable to be activated (guaranteed).
            m_folderView.SelectItem(m_folderView.GetParentItem(item.m_hTreeItem));
        }
    }

    return 0;
}

LRESULT MainFrame::OnTabActivated(LPNMHDR pnmhdr)
{
    auto page = static_cast<int>(pnmhdr->idFrom);
    if (page < 0) {
        UpdateEncodingStatus(UNKNOWN);
        return 0;
    }

    auto encoding = m_filesView.FileEncoding(page);
    UpdateEncodingStatus(encoding);

    auto hTreeItem = static_cast<HTREEITEM>(m_filesView.GetData(page));
    if (hTreeItem == nullptr) {
        return 0;
    }

    if (m_folderView.GetSelectedItem() == hTreeItem) {
        return 0;
    }

    m_folderView.Expand(hTreeItem);
    m_folderView.SelectItem(hTreeItem);
    m_folderView.EnsureVisible(hTreeItem);

    return 0;
}

LRESULT MainFrame::OnTabContextMenu(LPNMHDR pnmh)
{
    auto lpcmi = reinterpret_cast<LPTBVCONTEXTMENUINFO>(pnmh);

    auto tab = static_cast<int>(pnmh->idFrom);

    CMenu menu;
    menu.LoadMenu(IDR_TAB_CONTEXT);

    CMenuHandle popup = menu.GetSubMenu(0);

    HTREEITEM hTreeItem = nullptr;

    switch (popup.TrackPopupMenu(
        TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL | TPM_RETURNCMD | TPM_NONOTIFY, lpcmi->pt.x, lpcmi->pt.y,
        *this)) {
    case ID_TAB_CLOSE:
        hTreeItem = static_cast<HTREEITEM>(m_filesView.CloseFile(tab));
        break;
    case ID_TAB_CLOSE_ALL:
        m_filesView.CloseAllFiles();
        hTreeItem = m_folderView.GetSelectedItem();
        break;
    case ID_TAB_CLOSE_OTHERS:
        m_filesView.CloseOtherFiles(tab);
        break;
    default:
        break;
    }

    if (hTreeItem) {
        if (m_folderView.GetSelectedItem() == hTreeItem) {
            m_folderView.SelectItem(m_folderView.GetRootItem());
        }
    }

    return 0;
}

LRESULT MainFrame::OnRClick(LPNMHDR pnmh)
{
    if (pnmh->hwndFrom != m_folderView) {
        return 0;
    }

    auto item = m_folderView.GetSelectedItem();
    if (item.IsNull()) {
        return 0;
    }

    CMenu menu;
    menu.LoadMenu(IDR_TREE_CONTEXT);

    CMenuHandle popup = menu.GetSubMenu(0);
    Util::SetMenuItemIcon(popup, ID_TREE_NEWFOLDERHERE, IDI_FOLDER);
    Util::SetMenuItemIcon(popup, ID_TREE_NEWFILEHERE, IDI_FILE);

    CPoint pt;
    GetCursorPos(&pt);

    popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_VERTICAL, pt.x, pt.y, *this);

    return 0;
}

void MainFrame::OnFileChanged(WPARAM wParam, LPARAM lParam)
{
    PIDLIST_ABSOLUTE* pidls;
    LONG event;
    auto hLock = SHChangeNotification_Lock(
        reinterpret_cast<HANDLE>(wParam), static_cast<DWORD>(lParam),
        &pidls, &event);

    if (hLock) {
        ProcessFileChange(event, pidls);
        SHChangeNotification_Unlock(hLock);
    }
}

BOOL MainFrame::HasEditableView() const
{
    return m_filesView.ActiveViewIsEditable();
}

BOOL MainFrame::IsDialogMessage(PMSG pMsg)
{
    if (!pMsg) {
        return FALSE;
    }

    auto hWnd = pMsg->hwnd;
    if (!::IsWindow(hWnd)) {
        return FALSE;
    }

    // Find the root ancestor (top-level dialog or frame)
    auto hRoot = ::GetAncestor(hWnd, GA_ROOT);
    if (!::IsWindow(hRoot)) {
        return FALSE;
    }

    // Verify the root is a dialog window (WC_DIALOG == #32770)
    if (::GetClassLongPtr(hRoot, GCW_ATOM) != (LONG_PTR)WC_DIALOG) {
        return FALSE;
    }

    // Let Windows handle dialog navigation (TAB, ESC, ENTER, etc.)
    return ::IsDialogMessage(hRoot, pMsg);
}

void MainFrame::ProcessFileChange(LONG event, PIDLIST_ABSOLUTE* pidls)
{
    if (!m_folderView.IsWindow()) {
        return;
    }

    auto PidlToString = [](PCIDLIST_ABSOLUTE pidl) -> CString {
        if (!pidl) {
            return {};
        }

        PWSTR p = nullptr;
        auto hr = SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &p);
        if (FAILED(hr)) {
            return {};
        }

        CString s(p);

        CoTaskMemFree(p);

        return s;
    };

    CString oldPath, newPath;
    oldPath = PidlToString(pidls[0]);

    switch (event) {
    case SHCNE_CREATE:
    case SHCNE_MKDIR:
        AddFile(oldPath);
        break;
    case SHCNE_DELETE:
    case SHCNE_RMDIR:
        RemoveFile(oldPath);
        break;
    case SHCNE_RENAMEITEM:
    case SHCNE_RENAMEFOLDER:
        newPath = PidlToString(pidls[1]);
        RenameFile(oldPath, newPath);
        break;
    default:
        break;
    }
}

void MainFrame::AddFile(const CString& filename)
{
    HTREEITEM hItem = m_folderView.AddFile(filename);
    if (hItem == nullptr) {
        return;
    }

    m_filesView.SetData(filename, hItem);
}

void MainFrame::RemoveFile(const CString& filename)
{
    auto hItem = m_folderView.FindFile(filename);
    if (hItem == nullptr) {
        return;
    }

    m_filesView.CloseFileByData(hItem); // should we do this?
    m_folderView.DeleteItem(hItem);
}

void MainFrame::RenameFile(const CString& oldname, const CString& newname)
{
    m_folderView.RenameFile(oldname, newname);
    m_filesView.RenameFile(oldname, newname);
}

BOOL MainFrame::IsFolderOpen() const
{
    if (!m_folderView.IsWindow()) {
        return FALSE;
    }

    auto root = m_folderView.GetRootItem();
    return !root.IsNull();
}

BOOL MainFrame::IsFolderSelected() const
{
    if (!m_folderView.IsWindow()) {
        return FALSE;
    }

    auto hItem = m_folderView.GetSelectedItem();
    if (hItem.IsNull()) {
        return FALSE;
    }

    auto type = m_folderView.GetItemType(hItem);

    return type == TIT_FOLDER;
}

BOOL MainFrame::IsFileSelected() const
{
    if (!m_folderView.IsWindow()) {
        return FALSE;
    }

    auto hItem = m_folderView.GetSelectedItem();
    if (hItem.IsNull()) {
        return FALSE;
    }

    auto type = m_folderView.GetItemType(hItem);

    return type == TIT_FILE;
}

BOOL MainFrame::IsXmlSelected() const
{
    if (!m_folderView.IsWindow()) {
        return FALSE;
    }

    auto hItem = m_folderView.GetSelectedItem();
    if (hItem.IsNull()) {
        return FALSE;
    }

    auto path = m_folderView.GetItemPath(hItem);

    CString ext = PathFindExtension(path);
    return ext.CompareNoCase(L".xml") == 0;
}

BOOL MainFrame::IsLSXSelected() const
{
    if (!m_folderView.IsWindow()) {
        return FALSE;
    }

    auto hItem = m_folderView.GetSelectedItem();
    if (hItem.IsNull()) {
        return FALSE;
    }

    auto path = m_folderView.GetItemPath(hItem);

    CString ext = PathFindExtension(path);
    return ext.CompareNoCase(L".lsx") == 0;
}

void MainFrame::UpdateTitle()
{
    CString strTitle;

    auto* title = AtlGetStringPtr(IDR_MAINFRAME);
    ATLASSERT(title != nullptr);

    if (IsFolderOpen()) {
        auto root = m_folderView.GetRootItem();
        ATLASSERT(root.m_hTreeItem != nullptr);

        auto lParam = root.GetData();
        if (lParam == TIT_UNKNOWN || lParam == TIT_FILE || lParam == TIT_FOLDER) {
            return; // not fully constructed
        }

        auto data = std::bit_cast<TreeItemData*>(lParam);
        if (data != nullptr && data->path.GetLength()) {
            strTitle.Format(_T("%s : %s"), title, data->path);
        }
    } else {
        strTitle = title;
    }

    SetWindowText(strTitle);
}

BOOL MainFrame::NewFile(LPNMTVDISPINFO pDispInfo)
{
    auto type = static_cast<TreeItemType>(pDispInfo->item.lParam);
    if (type != TIT_FILE && type != TIT_FOLDER) {
        return FALSE; // not a new file
    }

    CString newName = pDispInfo->item.pszText;

    static constexpr WCHAR INVALID_CHARS[] = L"\\/:*?\"<>|";
    if (newName.FindOneOf(INVALID_CHARS) != -1) {
        MessageBox(L"Invalid filename. Do not use: \\ / : * ? \" < > |", L"Error", MB_ICONERROR);
        m_folderView.PostMessage(TVM_EDITLABEL, 0, reinterpret_cast<LPARAM>(pDispInfo->item.hItem));
        return FALSE;
    }

    auto hItem = pDispInfo->item.hItem;
    auto hParent = m_folderView.GetParentItem(hItem);
    if (hParent.IsNull()) {
        return FALSE; // no parent
    }

    if (m_folderView.GetItemType(hParent) != TIT_FOLDER) {
        return FALSE; // parent not a folder
    }

    auto path = m_folderView.GetItemPath(hParent);
    if (path.IsEmpty()) {
        return FALSE; // no path
    }

    WCHAR fullPath[MAX_PATH]{};
    PathCombine(fullPath, path, newName);

    if (PathFileExists(fullPath)) {
        MessageBox(L"A file with this name already exists.", L"Error", MB_ICONERROR);
        m_folderView.PostMessage(TVM_EDITLABEL, 0, reinterpret_cast<LPARAM>(pDispInfo->item.hItem));
        return FALSE; // Reject rename
    }

    FileOperation op;
    auto hr = op.Create();
    if (FAILED(hr)) {
        return FALSE;
    }

    if (type == TIT_FILE) {
        hr = op.NewFile(fullPath);
        if (FAILED(hr)) {
            CoMessageBox(*this, hr, nullptr, L"File creation error", MB_ICONERROR);
            return FALSE;
        }
    } else { // Folder
        hr = op.NewFolder(fullPath);
        if (FAILED(hr)) {
            CoMessageBox(*this, hr, nullptr, L"Directory creation error", MB_ICONERROR);
            return FALSE;
        }
    }

    auto* data = new TreeItemData{.type = type, .path = fullPath};

    m_folderView.SetItemData(hItem, reinterpret_cast<DWORD_PTR>(data));

    pDispInfo->item.lParam = reinterpret_cast<LPARAM>(data);

    return TRUE;
}

BOOL MainFrame::RenameFile(LPNMTVDISPINFO pDispInfo)
{
    auto* pData = reinterpret_cast<TREEITEMDATA*>(pDispInfo->item.lParam);
    if (!pData) {
        return FALSE; // not a valid tree item
    }

    ATLASSERT(pData->type == TIT_FILE || pData->type == TIT_FOLDER);

    const auto& oldName = pData->path;
    CString newName = pDispInfo->item.pszText;

    WCHAR dir[MAX_PATH];
    wcscpy_s(dir, pData->path);
    PathRemoveFileSpec(dir);

    WCHAR fullPath[MAX_PATH];
    PathCombine(fullPath, dir, newName);

    if (wcscmp(oldName, fullPath) == 0) {
        return FALSE; // no rename
    }

    static constexpr WCHAR INVALID_CHARS[] = L"\\/:*?\"<>|";
    if (newName.FindOneOf(INVALID_CHARS) != -1) {
        MessageBox(L"Invalid filename. Do not use: \\ / : * ? \" < > |", L"Error", MB_ICONERROR);
        m_folderView.PostMessage(TVM_EDITLABEL, 0, reinterpret_cast<LPARAM>(pDispInfo->item.hItem));
        return FALSE;
    }

    FileOperation op;
    auto hr = op.Create();
    if (FAILED(hr)) {
        CoMessageBox(*this, hr, nullptr, MB_ICONERROR);
        return FALSE;
    }

    hr = op.RenameItems(oldName, newName);
    if (FAILED(hr)) {
        CoMessageBox(*this, hr);
        return FALSE;
    }

    return TRUE;
}

void MainFrame::UpdateEncodingStatus(FileEncoding encoding)
{
    CString status;
    HICON hIcon = nullptr;

    switch (encoding) {
    case UTF8:
        status = L"UTF-8";
        hIcon = m_nobom;
        break;
    case UTF8BOM:
        status = L"UTF-8 with BOM";
        hIcon = m_bom;
        break;
    default:
        status = L""; // Unknown
        break;
    }

    m_statusBar.SetIcon(1, hIcon);
    m_statusBar.SetText(1, status);
}

void MainFrame::IterateFiles(HTREEITEM hItem, const FileCallback& callback)
{
    if (!hItem) {
        return;
    }

    do {
        auto data = std::bit_cast<LPTREEITEMDATA>(m_folderView.GetItemData(hItem));
        if (data && data->type == TIT_FILE) {
            callback(data->path);
        } else if (data && data->type == TIT_FOLDER) {
            auto hChild = m_folderView.GetChildItem(hItem);
            if (hChild != nullptr) {
                IterateFiles(hChild, callback);
            }
        } else {
            ATLASSERT(0);
        }

        hItem = m_folderView.GetNextSiblingItem(hItem);
    } while (hItem);
}

MainFrame::OpenFolderResult MainFrame::OpenFolder(const CString& folder)
{
    PIDL destination;
    auto hr = SHParseDisplayName(folder, nullptr, destination.put(), 0, nullptr);
    if (FAILED(hr)) {
        CoMessageBox(*this, hr, nullptr, _T("Error"), MB_ICONERROR);
        return OpenFolderResult::Failed;
    }

    if (!m_filesView.CloseAllFiles()) {
        return OpenFolderResult::Canceled;
    }

    m_rootPIDL = std::move(destination);
    m_folderView.SetFolder(folder);

    UpdateTitle();

    SHChangeNotifyEntry entry{m_rootPIDL.get(), TRUE};
    m_notify.reset(SHChangeNotifyRegister(
        m_hWnd,
        SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
        SHCNE_DISKEVENTS,
        WM_FILE_CHANGED,
        1, &entry));

    m_mru.AddToList(folder);

    return OpenFolderResult::Opened;
}

void MainFrame::OnViewStatusBar()
{
    BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
    ::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
    UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
    UpdateLayout();
}

void MainFrame::OnMRUMenuItem(UINT /*uCode*/, int nID, HWND /*hwndCtrl*/)
{
    CString folder;

    if (!m_mru.GetFromList(nID, folder)) {
        return;
    }

    if (OpenFolder(folder) == OpenFolderResult::Failed) {
        m_mru.RemoveFromList(nID);
    }
}

LRESULT MainFrame::OnCopyData(HWND hWnd, PCOPYDATASTRUCT pcds)
{
    if (!pcds) {
        return FALSE;
    }

    auto event = pcds->dwData;
    if (event == CD_RENAME_EVENT) {
        const auto* info = static_cast<const RenameInfo*>(pcds->lpData);
        RenameFile(info->oldPath, info->newPath);
        return TRUE;
    }

    return FALSE;
}

LRESULT MainFrame::OnHasEditableView()
{
    return HasEditableView();
}

void MainFrame::OnFindReplace(WPARAM, LPARAM lParam)
{
    auto* params = reinterpret_cast<LPFINDREPLACE_PARAMS>(lParam);
    if (params == nullptr) {
        return; // invalid params
    }

    auto pView = m_filesView.ActiveView();
    if (pView == nullptr) {
        return; // no active view
    }

    auto* pfindable = dynamic_cast<ITextFindable*>(pView.get());
    if (pfindable == nullptr) {
        return; // not findable
    }

    pfindable->FindReplace(params);
}

BOOL MainFrame::OnIdle()
{
    UIEnable(ID_FILE_CLOSE, IsFolderOpen());
    UIEnable(ID_FILE_NEW, IsFolderOpen());
    UIEnable(ID_TOOL_FIND_REPLACE, HasEditableView());
    UIEnable(ID_TOOL_LOCA, IsXmlSelected());
    UIEnable(ID_TOOL_LSF, IsLSXSelected());
    UIEnable(ID_TOOL_PACKAGE, IsFolderOpen());
    UIEnable(ID_TREE_DELETE_FILE, IsFileSelected());
    UIEnable(ID_TREE_DELETE_FOLDER, IsFolderSelected());
    UIEnable(ID_TREE_MAKELSFHERE, IsLSXSelected());
    UIEnable(ID_TREE_NEWFILEHERE, IsFolderSelected());
    UIEnable(ID_TREE_NEWFOLDERHERE, IsFolderSelected());
    UIEnable(ID_TREE_RENAME_FILE, IsFolderSelected() || IsFileSelected());

    if (m_filesView.IsWindow()) {
        UIEnable(ID_FILE_SAVE, m_filesView.IsDirty(m_filesView.ActivePage()));
        UIEnable(ID_FILE_SAVE_ALL, m_filesView.IsDirty());
    }

    UIUpdateMenuBar();

    return FALSE;
}
