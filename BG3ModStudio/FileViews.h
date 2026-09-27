#pragma once

#include "TextFileView.h"
#include "TabViewImpl.h"

class FilesView : public TabViewImpl<FilesView>
{
public:
    using Base = TabViewImpl;

    BEGIN_MSG_MAP(FilesView)
        MSG_WM_CREATE(OnCreate)
        CHAIN_MSG_MAP(TabViewImpl)
        ALT_MSG_MAP(1) // tab view control
        NOTIFY_CODE_HANDLER(TCN_SELCHANGE, OnTabSelChange)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
    END_MSG_MAP()

    DECLARE_WND_CLASS_EX(_T("FilesView"), 0, COLOR_APPWORKSPACE)

    void OnPageActivated(int nPage);
    void OnPageContextMenu(int nPage, const CPoint& pt);
    void OnDrawTabItem(LPDRAWITEMSTRUCT dis);
    DWORD GetTabCtrlStyle() const;

    BOOL ActivateFile(const CString& path, LPVOID data);
    BOOL ActivateView(const IFileView::Ptr& fileView, LPVOID data);
    BOOL ActiveViewIsEditable() const;
    BOOL CloseFileByData(LPVOID data);
    BOOL IsDirty() const;
    BOOL IsDirty(int index) const;
    BOOL NewFile();
    BOOL RenameFile(const CString& oldname, const CString& newname);
    BOOL SaveAll();
    BOOL SaveFile(const IFileView::Ptr& fileView);
    BOOL SetData(const CString& path, LPVOID data);
    const std::vector<IFileView::Ptr>& Files() const;
    CString GetTitle(const IFileView::Ptr& fileView) const;
    FileEncoding FileEncoding(int index) const;
    IFileView::Ptr ActiveView() const;
    IFileView::Ptr GetFileView(const CString& path) const;
    IFileView::Ptr GetFileView(int index) const;
    IFileView::Ptr GetFileView(LPVOID data) const;
    int ActivePage() const;
    int GetViewIndex(const IFileView::Ptr& fileView) const;
    PVOID CloseActiveFile();
    PVOID CloseFile(int index);
    PVOID CloseOtherFiles(int index);
    PVOID GetData(int index) const;
    BOOL CloseAllFiles();
    void SetTitle(const IFileView::Ptr& fileView, LPCTSTR lpstrTitle);

private:
    LRESULT OnCreate(LPCREATESTRUCT pcs);

    BOOL AddPage(const IFileView::Ptr& fileView, LPCTSTR lpstrTitle, LPCTSTR lpstrPath, LPVOID pData = nullptr);

    CFont m_tabFont, m_disabledFont;
    std::unordered_map<LPVOID, INT> m_data; // map file data to page indexes
    std::vector<IFileView::Ptr> m_views; // file views
    uint32_t m_nextId{0};
};
