#include "stdafx.h"
#include "FolderMonitor.h"

namespace
{
constexpr DWORD MONITOR_BUFFER_SIZE = 64 * 1024;
}

FolderMonitor::FolderMonitor(HWND targetWindow, const CString& directory)
    : m_targetWindow(targetWindow), m_directory(directory), m_stopEvent(TRUE, FALSE)
{
}

FolderMonitor::~FolderMonitor()
{
    Stop();
}

bool FolderMonitor::Start()
{
    if (!m_stopEvent || m_thread) {
        return false;
    }

    m_stopEvent.Reset();
    m_thread.Attach(CreateThread(nullptr, 0, MonitorProc, this, 0, nullptr));
    if (!m_thread) {
        return false;
    }

    return true;
}

void FolderMonitor::Stop(DWORD timeout)
{
    if (!m_thread) {
        return;
    }

    m_stopEvent.Set();
    auto result = WaitForSingleObject(m_thread, timeout);
    if (result == WAIT_OBJECT_0) {
        m_thread.Close();
    }
}

DWORD WINAPI FolderMonitor::MonitorProc(LPVOID parameter)
{
    auto* monitor = static_cast<FolderMonitor*>(parameter);
    monitor->Monitor();
    return 0;
}

bool FolderMonitor::PostChange(DWORD action, const CString& path) const
{
    if (!IsWindow(m_targetWindow)) {
        return false;
    }

    auto* notification = new (std::nothrow) FileChangeNotification{action, path};
    if (notification == nullptr) {
        return false;
    }

    if (!PostMessage(m_targetWindow, WM_FILE_CHANGED, 0, reinterpret_cast<LPARAM>(notification))) {
        delete notification;
        return false;
    }

    return true;
}

void FolderMonitor::Monitor()
{
    CHandle directory;
    directory.Attach(CreateFile(
        m_directory,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr));
    if (!directory) {
        return;
    }

    CHandle changeEvent;
    changeEvent.Attach(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    if (!changeEvent) {
        return;
    }

    std::vector<uint8_t> buffer(MONITOR_BUFFER_SIZE);
    std::wstring pendingRename;
    OVERLAPPED overlapped{};
    overlapped.hEvent = changeEvent;
    HANDLE events[] = {changeEvent, m_stopEvent.m_h};

    while (WaitForSingleObject(m_stopEvent.m_h, 0) != WAIT_OBJECT_0) {
        ResetEvent(changeEvent);
        DWORD bytesReturned = 0;
        if (!ReadDirectoryChangesW(
                directory,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_CREATION,
                &bytesReturned,
                &overlapped,
                nullptr)) {
            break;
        }

        auto waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0 + 1) {
            CancelIoEx(directory, &overlapped);
            break;
        }
        if (waitResult != WAIT_OBJECT_0) {
            break;
        }

        if (!GetOverlappedResult(directory, &overlapped, &bytesReturned, FALSE)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) {
                break;
            }
            continue;
        }

        if (bytesReturned == 0) {
            continue;
        }

        auto* current = buffer.data();
        auto* end = current + bytesReturned;
        while (current < end) {
            auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(current);
            if (reinterpret_cast<const uint8_t*>(info) + sizeof(FILE_NOTIFY_INFORMATION) > end ||
                info->FileNameLength % sizeof(wchar_t) != 0 ||
                reinterpret_cast<const uint8_t*>(info) + offsetof(FILE_NOTIFY_INFORMATION, FileName) +
                        info->FileNameLength > end) {
                break;
            }

            CString path(m_directory);
            path.Append(L"\\");
            path.Append(info->FileName, static_cast<int>(info->FileNameLength / sizeof(wchar_t)));

            if (info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                pendingRename = path.GetString();
            } else if (info->Action == FILE_ACTION_RENAMED_NEW_NAME && !pendingRename.empty()) {
                PostChange(FILE_ACTION_RENAMED_OLD_NAME, CString(pendingRename.c_str()));
                PostChange(FILE_ACTION_RENAMED_NEW_NAME, path);
                pendingRename.clear();
            } else {
                PostChange(info->Action, path);
            }

            if (info->NextEntryOffset == 0) {
                break;
            }
            if (info->NextEntryOffset < sizeof(FILE_NOTIFY_INFORMATION) ||
                current + info->NextEntryOffset >= end) {
                break;
            }
            current += info->NextEntryOffset;
        }
    }

    CancelIoEx(directory, &overlapped);
}
