#pragma once

#include "FileViews.h"
#include "FolderMonitor.h"
#include "FolderView.h"
#include "resources/resource.h"
#include "resources/ribbon.h"

class MainFrame : public CRibbonFrameWindowImpl<MainFrame>,
    public CMessageFilter,
    public CIdleHandler
{
public:
    DECLARE_FRAME_WND_CLASS(nullptr, IDR_MAINFRAME)

    BOOL PreTranslateMessage(MSG* pMsg) override;
    BOOL DefCreate();

    LRESULT OnCreate(LPCREATESTRUCT pcs);
    BOOL OnIdle() override;

    BEGIN_UPDATE_UI_MAP(MainFrame)
        UPDATE_ELEMENT(ID_FILE_CLOSE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_FILE_NEW, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_FILE_SAVE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_FILE_SAVE_ALL, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TOOL_BG3, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TOOL_FIND_REPLACE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TOOL_LOCA, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TOOL_LSF, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TOOL_PACKAGE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TREE_DELETE_FILE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TREE_DELETE_FOLDER, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TREE_MAKELSFHERE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TREE_NEWFILEHERE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TREE_NEWFOLDERHERE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_TREE_RENAME_FILE, UPDUI_MENUPOPUP)
        UPDATE_ELEMENT(ID_VIEW_STATUS_BAR, UPDUI_MENUPOPUP)
    END_UPDATE_UI_MAP()

    BEGIN_MSG_MAP(MainFrame)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_CLOSE(OnClose)
        MSG_WM_COPYDATA(OnCopyData)
        MESSAGE_HANDLER2(WM_FILE_CHANGED, OnFileChanged)
        MESSAGE_HANDLER4(WM_HAS_EDITABLE_VIEW, OnHasEditableView)
        MESSAGE_HANDLER2(WM_FIND_REPLACE_COMMAND, OnFindReplace)
        COMMAND_ID_HANDLER3(ID_APP_ABOUT, OnFileAbout)
        COMMAND_ID_HANDLER3(ID_APP_EXIT, OnFileExit)
        COMMAND_ID_HANDLER3(ID_FILE_CLOSE, OnFolderClose)
        COMMAND_ID_HANDLER3(ID_FILE_NEW, OnNewFile)
        COMMAND_ID_HANDLER3(ID_FILE_NEW_FOLDER, OnNewFolder)
        COMMAND_ID_HANDLER3(ID_FILE_OPEN, OnFolderOpen)
        COMMAND_ID_HANDLER3(ID_FILE_PAK_OPEN, OnPakOpen)
        COMMAND_ID_HANDLER3(ID_FILE_SAVE, OnFileSave)
        COMMAND_ID_HANDLER3(ID_FILE_SAVE_ALL, OnFileSaveAll)
        COMMAND_ID_HANDLER3(ID_TOOL_BG3, OnLaunchGame)
        COMMAND_ID_HANDLER3(ID_TOOL_DB, OnDatabase)
        COMMAND_ID_HANDLER3(ID_TOOL_FIND_REPLACE, OnFindReplace)
        COMMAND_ID_HANDLER3(ID_TOOL_GAMEOBJECT, OnGameObject)
        COMMAND_ID_HANDLER3(ID_TOOL_ICON_EXPLORER, OnIconExplorer)
        COMMAND_ID_HANDLER3(ID_TOOL_INDEX, OnIndex)
        COMMAND_ID_HANDLER3(ID_TOOL_LOCA, OnConvertLoca)
        COMMAND_ID_HANDLER3(ID_TOOL_LSF, OnConvertLSF)
        COMMAND_ID_HANDLER3(ID_TOOL_PACKAGE, OnFolderPack)
        COMMAND_ID_HANDLER3(ID_TOOL_SEARCH, OnSearch)
        COMMAND_ID_HANDLER3(ID_TOOL_SETTINGS, OnSettings)
        COMMAND_ID_HANDLER3(ID_TOOL_UUID, OnUUID)
        COMMAND_ID_HANDLER3(ID_TREE_DELETE_FILE, OnDeleteFile)
        COMMAND_ID_HANDLER3(ID_TREE_DELETE_FOLDER, OnDeleteFile)
        COMMAND_ID_HANDLER3(ID_TREE_NEWFILEHERE, OnNewFileHere)
        COMMAND_ID_HANDLER3(ID_TREE_NEWFOLDERHERE, OnNewFolderHere)
        COMMAND_ID_HANDLER3(ID_TREE_RENAME_FILE, OnRenameFile)
        COMMAND_ID_HANDLER3(ID_VIEW_STATUS_BAR, OnViewStatusBar)
        COMMAND_RANGE_HANDLER_EX(ID_FILE_MRU_FIRST, ID_FILE_MRU_LAST, OnMRUMenuItem)

        REFLECT_NOTIFY_CODE(TVN_ITEMEXPANDING)
        NOTIFY_CODE_HANDLER_EX(TVN_DELETEITEM, OnTVDelete)
        NOTIFY_CODE_HANDLER_EX(TVN_SELCHANGED, OnTVSelChanged)
        NOTIFY_CODE_HANDLER_EX(TVN_BEGINLABELEDIT, OnTVBeginLabelEdit)
        NOTIFY_CODE_HANDLER_EX(TVN_ENDLABELEDIT, OnTVEndLabelEdit)
        NOTIFY_CODE_HANDLER_EX(TBVN_PAGEACTIVATED, OnTabActivated)
        NOTIFY_CODE_HANDLER_EX(TBVN_CONTEXTMENU, OnTabContextMenu)
        NOTIFY_CODE_HANDLER_EX(NM_RCLICK, OnRClick)

        CHAIN_MSG_MAP(CRibbonFrameWindowImpl)
    END_MSG_MAP()

    BEGIN_RIBBON_CONTROL_MAP(MainFrame)
        RIBBON_CONTROL(m_mru)
    END_RIBBON_CONTROL_MAP()

private:
    LRESULT OnCopyData(HWND hWnd, PCOPYDATASTRUCT pcds);
    LRESULT OnHasEditableView();
    void OnClose();
    void OnConvertLoca();
    void OnConvertLSF();
    void OnDatabase();
    void OnDeleteFile();
    void OnFileAbout();
    void OnFileExit();
    void OnFileSave();
    void OnFileSaveAll();
    void OnFindReplace();
    void OnFindReplace(WPARAM, LPARAM);
    void OnFolderClose();
    void OnFolderOpen();
    void OnFolderPack();
    void OnGameObject();
    void OnIconExplorer();
    void OnIndex();
    void OnLaunchGame();
    void OnMRUMenuItem(UINT uCode, int nID, HWND hwndCtrl);
    void OnNewFile();
    void OnNewFileHere();
    void OnNewFolder();
    void OnNewFolderHere();
    void OnPakOpen();
    void OnRenameFile();
    void OnSearch();
    void OnSettings();
    void OnUUID();
    void OnViewStatusBar();

    LRESULT OnRClick(LPNMHDR pnmh);
    LRESULT OnTabActivated(LPNMHDR pnmhdr);
    LRESULT OnTabContextMenu(LPNMHDR pnmh);
    LRESULT OnTVBeginLabelEdit(LPNMHDR pnmhdr);
    LRESULT OnTVDelete(LPNMHDR pnmhdr);
    LRESULT OnTVEndLabelEdit(LPNMHDR pnmhdr);
    LRESULT OnTVSelChanged(LPNMHDR pnmhdr);
    void OnFileChanged(WPARAM wParam, LPARAM lParam);

    using FileCallback = std::function<void(const CStringW& filePath)>;

    BOOL HasEditableView() const;
    BOOL IsDialogMessage(PMSG pMsg);
    BOOL IsFileSelected() const;
    BOOL IsFolderOpen() const;
    BOOL IsFolderSelected() const;
    BOOL IsLSXSelected() const;
    BOOL IsXmlSelected() const;
    BOOL NewFile(LPNMTVDISPINFO pDispInfo);
    enum class OpenFolderResult { Opened, Canceled, Failed };
    OpenFolderResult OpenFolder(const CString& folder);
    BOOL RenameFile(LPNMTVDISPINFO pDispInfo);
    void AddFile(const CString& filename);
    void IterateFiles(HTREEITEM hItem, const FileCallback& callback);
    void ProcessFileChange(DWORD action, const CString& filename);
    void RemoveFile(const CString& filename);
    void RenameFile(const CString& oldname, const CString& newname);
    void UpdateEncodingStatus(FileEncoding encoding);
    void UpdateTitle();

    CCommandBarCtrl m_cmdBar;
    CIcon m_bom, m_nobom;
    CRibbonRecentItemsCtrl<ID_FILE_RECENT_FILES> m_mru;
    CSplitterWindow m_splitter;
    CStatusBarCtrl m_statusBar;
    FilesView m_filesView{};
    FolderView m_folderView{};
    std::unique_ptr<FolderMonitor> m_folderMonitor;
    CString m_pendingRename;
};
