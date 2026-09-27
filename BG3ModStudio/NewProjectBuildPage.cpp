#include "stdafx.h"
#include "NewProjectBuildPage.h"
#include "FileStream.h"
#include "ResourceHelper.h"
#include "StringHelper.h"
#include "Util.h"
#include "Win32ErrorMessage.h"

NewProjectBuildPage::NewProjectBuildPage(NewProjectWizard* pWiz, _U_STRINGorID title) : BasePage(title), m_pWiz(pWiz)
{
    SetHeaderTitle(_T("New Project"));
    SetHeaderSubTitle(_T("Building the project folder..."));
}

BOOL NewProjectBuildPage::OnInitDialog(HWND hWnd, LPARAM lParam)
{
    m_progress = GetDlgItem(IDC_PROGRESS_NEW_PROJECT);
    ATLASSERT(m_progress.IsWindow());

    m_openNewProject = GetDlgItem(IDC_CHK_OPEN_NEW_PROJECT);
    ATLASSERT(m_openNewProject.IsWindow());

    m_openNewProject.SetCheck(BST_CHECKED); // default to checked

    return TRUE; // let the system set the focus
}

LRESULT NewProjectBuildPage::OnProjRange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    m_progress.SetRange32(0, static_cast<int>(lParam));
    m_progress.SetPos(0);
    m_progress.SetState(PBST_NORMAL);

    return 0;
}

LRESULT NewProjectBuildPage::OnProjProgress(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    m_progress.SetPos(static_cast<int>(wParam));
    return 0;
}

LRESULT NewProjectBuildPage::OnProjComplete(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    if (wParam == 0) {
        AtlMessageBox(*this, L"Project created successfully.", L"Success", MB_ICONINFORMATION);
        SetWizardButtons(PSWIZB_FINISH);
    } else {
        auto lower = 0, upper = 0;
        m_progress.GetRange(lower, upper);
        m_progress.SetPos(upper);
        m_progress.SetState(PBST_ERROR);

        CString msg;
        msg.Format(L"An error occurred while creating the project:\n%s", static_cast<LPCTSTR>(m_lastError));
        AtlMessageBox(*this, msg.GetString(), nullptr, MB_ICONERROR);
        SetWizardButtons(PSWIZB_BACK | PSWIZB_FINISH);
    }

    return 0;
}

BOOL NewProjectBuildPage::CreateFolder(const CString& folder)
{
    auto result = SHCreateDirectoryEx(nullptr, folder, nullptr);

    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS;
}

BOOL NewProjectBuildPage::CreateFileEx(const CString& path)
{
    CString dirPath(path);
    PathRemoveFileSpecW(dirPath.GetBuffer(MAX_PATH));
    dirPath.ReleaseBuffer();

    if (!CreateFolder(dirPath)) {
        return FALSE;
    }

    auto hFile = CreateFile(path, GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    CloseHandle(hFile);

    return TRUE;
}

BOOL NewProjectBuildPage::WriteFileEx(const CString& path, const CStringA& contents)
{
    if (!CreateFileEx(path)) { // Ensure all parent folders exist
        return FALSE;
    }

    auto utf8Path = StringHelper::toUTF8(path);

    FileStream fs;

    try {
        fs.open(utf8Path.GetString(), "wb");
        fs.write(contents.GetString(), contents.GetLength());
    } catch (const std::exception& /*ex*/) {
        return FALSE;
    }

    return TRUE;
}

BOOL NewProjectBuildPage::WriteTemplate(UINT nResID, const ResourceHelper::ResourceMap& map, const CString& path)
{
    auto contents = ResourceHelper::ExpandTemplate(nResID, map);
    if (!WriteFileEx(path, contents)) {
        return FALSE;
    }

    return TRUE;
}

DWORD NewProjectBuildPage::BuildProc(LPVOID pv)
{
    auto* pThis = static_cast<NewProjectBuildPage*>(pv);
    ATLASSERT(pThis);

    pThis->PostMessage(WM_PROJ_RANGE, 0, 100); // reasonable default range

    const auto& root = pThis->m_pWiz->GetProjectFolder();
    const auto& modName = pThis->m_pWiz->GetModName();
    const auto& author = pThis->m_pWiz->GetAuthor();

    ResourceHelper::ResourceMap templateMap;
    templateMap[L"MOD_NAME"] = modName.GetString();
    templateMap[L"FOLDER"] = modName.GetString();
    templateMap[L"VERSION"] = L"36028797018963968";
    templateMap[L"PUBLISH_VERSION"] = L"36028797018963968";
    templateMap[L"AUTHOR"] = author.GetString();
    templateMap[L"UUID"] = Util::MakeUUID();

    // Create meta.lsx
    CString metaPath;
    metaPath.Format(L"%s\\Mods\\%s\\meta.lsx", root, modName);

    if (!pThis->WriteTemplate(IDR_META_TEMPLATE, templateMap, metaPath)) {
        pThis->m_lastError = Win32ErrorMessage::GetErrorMsg();
        pThis->PostMessage(WM_PROJ_COMPLETE, -1, 0);
        return 0;
    }

    pThis->PostMessage(WM_PROJ_PROGRESS, 10);

    // Create localization file
    CString locaPath;
    locaPath.Format(L"%s\\Localization\\English\\%s.xml", root, modName);

    if (!pThis->WriteTemplate(IDR_LOCA_TEMPLATE, templateMap, locaPath)) {
        pThis->m_lastError = Win32ErrorMessage::GetErrorMsg();
        pThis->PostMessage(WM_PROJ_COMPLETE, -1, 0);
        return 0;
    }

    pThis->PostMessage(WM_PROJ_PROGRESS, 20);

    // Create merged.lsx
    CString rootTemplatesPath;
    rootTemplatesPath.Format(L"%s\\Public\\%s\\RootTemplates\\merged.lsx", root, modName);

    if (!pThis->WriteTemplate(IDR_ROOT_TEMPLATES_TEMPLATE, templateMap, rootTemplatesPath)) {
        pThis->m_lastError = Win32ErrorMessage::GetErrorMsg();
        pThis->PostMessage(WM_PROJ_COMPLETE, -1, 0);
        return 0;
    }

    pThis->PostMessage(WM_PROJ_PROGRESS, 30);

    // Create TreasureTable.txt
    CString treasureTablePath;
    treasureTablePath.Format(L"%s\\Public\\%s\\Stats\\Generated\\TreasureTable.txt", root, modName);

    if (!pThis->WriteTemplate(IDR_TREASURE_TEMPLATE, templateMap, treasureTablePath)) {
        pThis->m_lastError = Win32ErrorMessage::GetErrorMsg();
        pThis->PostMessage(WM_PROJ_COMPLETE, -1, 0);
        return 0;
    }

    pThis->PostMessage(WM_PROJ_PROGRESS, 100);
    pThis->PostMessage(WM_PROJ_COMPLETE, 0, 0);

    return 0;
}

int NewProjectBuildPage::OnSetActive()
{
    m_progress.SetPos(0);
    m_progress.SetStep(1);
    m_progress.SetState(PBST_NORMAL);

    SetWizardButtons(0);

    auto hThread = CreateThread(nullptr, 0, BuildProc, this, 0, nullptr);
    if (hThread == nullptr) {
        ATLTRACE(_T("Unable to create worker thread.\n"));
        return 0;
    }

    CloseHandle(hThread);

    return 0;
}

int NewProjectBuildPage::OnWizardBack()
{
    return 0;
}

int NewProjectBuildPage::OnWizardNext()
{
    return 0;
}

int NewProjectBuildPage::OnWizardFinish()
{
    m_pWiz->SetOpenNewProject(m_openNewProject.GetCheck() == BST_CHECKED);

    return 0;
}

BOOL NewProjectBuildPage::OnQueryCancel()
{
    return FALSE;
}
