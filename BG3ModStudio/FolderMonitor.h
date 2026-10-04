#pragma once

#include <atlbase.h>
#include <atlsync.h>

struct FileChangeNotification
{
    DWORD action;
    CString path;
};

class FolderMonitor
{
public:
    FolderMonitor(HWND targetWindow, const CString& directory);
    ~FolderMonitor();

    FolderMonitor(const FolderMonitor&) = delete;
    FolderMonitor& operator=(const FolderMonitor&) = delete;

    bool Start();
    void Stop(DWORD timeout = INFINITE);

private:
    static DWORD WINAPI MonitorProc(LPVOID parameter);
    void Monitor();
    bool PostChange(DWORD action, const CString& path) const;

    HWND m_targetWindow;
    CString m_directory;
    CHandle m_thread;
    CEvent m_stopEvent;
};
