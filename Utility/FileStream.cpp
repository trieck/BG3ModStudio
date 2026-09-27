#include "pch.h"
#include "Exception.h"
#include "FileStream.h"

FileStream::FileStream() :
    m_file(INVALID_HANDLE_VALUE),
    m_pbuf(nullptr), m_nRemaining(0),
    m_fileBase(0), m_blockSize(0),
    m_access(0), m_shareMode(0), m_creation(0)
{
    alloc();
}

FileStream::~FileStream()
{
    closeNoThrow();
}

FileStream::FileStream(FileStream&& rhs) noexcept : m_file(INVALID_HANDLE_VALUE)
{
    *this = std::move(rhs);
}

FileStream& FileStream::operator=(FileStream&& rhs) noexcept
{
    if (this != &rhs) {
        closeNoThrow();
        m_file = rhs.m_file;
        m_buf = std::move(rhs.m_buf);
        m_pbuf = rhs.m_pbuf;
        m_nRemaining = rhs.m_nRemaining;
        m_fileBase = rhs.m_fileBase;
        m_blockSize = rhs.m_blockSize;
        m_fileSize = rhs.m_fileSize;
        m_access = rhs.m_access;
        m_shareMode = rhs.m_shareMode;
        m_creation = rhs.m_creation;
        rhs.m_file = INVALID_HANDLE_VALUE;
        rhs.m_pbuf = nullptr;
        rhs.m_nRemaining = 0;
        rhs.m_fileBase = 0;
        rhs.m_blockSize = 0;
        rhs.m_fileSize = 0;
        rhs.m_access = 0;
        rhs.m_shareMode = 0;
        rhs.m_creation = 0;
    }

    return *this;
}

void FileStream::open(const char* path, const char* mode)
{
    close();

    if (strcmp(mode, "rb") == 0) {
        m_access = GENERIC_READ;
        m_creation = OPEN_EXISTING;
    } else if (strcmp(mode, "wb") == 0) {
        m_access = GENERIC_WRITE;
        m_creation = CREATE_ALWAYS;
    } else if (strcmp(mode, "ab") == 0) {
        m_access = GENERIC_WRITE;
        m_creation = OPEN_ALWAYS;
    } else {
        throw Exception("Invalid mode specified for file stream.");
    }

    m_shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

    m_file = CreateFileA(path, m_access, m_shareMode, nullptr, m_creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_file == INVALID_HANDLE_VALUE) {
        throw Exception(GetLastError());
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(m_file, &size)) {
        throw Exception(GetLastError());
    }

    m_fileSize = size.QuadPart;

    if (strcmp(mode, "ab") == 0) {
        seek(0, SeekMode::End);
    }
}

void FileStream::close()
{
    DWORD error = ERROR_SUCCESS;
    if (m_file != INVALID_HANDLE_VALUE) {
        if (!flush()) {
            error = GetLastError();
            if (error == ERROR_SUCCESS) error = ERROR_WRITE_FAULT;
        } else if ((m_access & GENERIC_WRITE) && !FlushFileBuffers(m_file)) {
            error = GetLastError();
        }
        CloseHandle(m_file);
        m_file = INVALID_HANDLE_VALUE;
    }

    // Reset logical state
    m_fileBase = 0;
    m_nRemaining = 0;
    m_pbuf = m_buf.get();
    m_fileSize = 0;
    m_access = 0;
    m_shareMode = 0;
    m_creation = 0;

    if (error != ERROR_SUCCESS) {
        throw Exception(error);
    }
}

void FileStream::closeNoThrow() noexcept
{
    try {
        close();
    } catch (...) {
        // Explicit close reports save failures; destruction must not throw.
    }
}

void FileStream::write(StreamBase& stream)
{
    static constexpr auto BUFFER_SIZE = 4096u;

    auto buffer = std::make_unique<char[]>(BUFFER_SIZE);

    size_t remaining = stream.size();
    stream.seek(0, SeekMode::Begin);

    while (remaining > 0) {
        auto toRead = std::min<size_t>(remaining, BUFFER_SIZE);
        auto read = stream.read(buffer.get(), toRead);
        if (read == 0) {
            break; // EOF
        }

        write(buffer.get(), read);

        remaining -= read;
    }
}

size_t FileStream::read(char* buf, size_t size)
{
    if (!(m_access & GENERIC_READ)) {
        throw Exception("Stream not opened for reading.");
    }

    size_t totalRead = 0;
    while (size > 0) {
        auto chunk = static_cast<DWORD>(std::min<size_t>(size, MAXDWORD));
        if (m_nRemaining == 0) { // buffer empty
            if (!readBlock()) {
                throw Exception("Failed to read from file.");
            }
        }
        auto read = std::min<DWORD>(chunk, m_nRemaining);
        memcpy(buf, m_pbuf, read);

        if (read == 0) {
            return totalRead; // EOF
        }

        buf += read;
        size -= read;
        m_pbuf += read;
        m_nRemaining -= read;
        totalRead += read;
    }

    return totalRead;
}

size_t FileStream::write(const char* buf, size_t size)
{
    if (!(m_access & GENERIC_WRITE)) {
        throw Exception("Stream not opened for writing.");
    }

    size_t totalWritten = 0;

    while (size > 0) {
        // Space left in buffer
        size_t space = m_blockSize - m_nRemaining;

        // If buffer full, flush it
        if (space == 0) {
            if (!writeBlock()) {
                throw Exception("Failed to flush write buffer.");
            }
            space = m_blockSize;
        }

        // Copy as much as fits
        size_t toCopy = std::min(size, space);
        memcpy(m_pbuf, buf, toCopy);

        // Advance buffer state
        m_pbuf += toCopy;
        m_nRemaining += static_cast<uint32_t>(toCopy);

        // Advance input
        buf += toCopy;
        size -= toCopy;
        totalWritten += toCopy;
    }

    return totalWritten;
}

void FileStream::seek(int64_t offset, SeekMode mode)
{
    // Write buffers contain pending bytes, not unread bytes. Flush before seeking.
    if ((m_access & GENERIC_WRITE) && !flush()) {
        throw Exception("Failed to flush write buffer before seeking.");
    }
    auto curOffset = m_pbuf - m_buf.get();
    auto curPos = m_fileBase + curOffset;
    auto bufEnd = m_fileBase + curOffset + m_nRemaining;

    // Try to optimize seek within buffer

    if (mode == SeekMode::Begin &&
        std::cmp_greater_equal(offset, m_fileBase) &&
        std::cmp_less(offset, bufEnd)) {
        auto bufOffset = offset - m_fileBase;
        m_pbuf = m_buf.get() + bufOffset;
        m_nRemaining = static_cast<uint32_t>(bufEnd - offset);
        return;
    }

    auto target = curPos + offset;
    if (mode == SeekMode::Current &&
        std::cmp_greater_equal(target, m_fileBase) &&
        std::cmp_less(target, bufEnd)) {
        auto bufOffset = target - m_fileBase;
        m_pbuf = m_buf.get() + bufOffset;
        m_nRemaining = static_cast<uint32_t>(bufEnd - target);
        return;
    }

    target = size() + offset;
    if (mode == SeekMode::End &&
        std::cmp_greater_equal(target, m_fileBase) &&
        std::cmp_less(target, bufEnd)) {
        auto bufOffset = target - m_fileBase;
        m_pbuf = m_buf.get() + bufOffset;
        m_nRemaining = static_cast<uint32_t>(bufEnd - target);
        return;
    }

    LARGE_INTEGER li{}, pos{};
    // The OS read position is ahead of tell() due to read-ahead buffering.
    li.QuadPart = mode == SeekMode::Current ? static_cast<int64_t>(curPos) + offset : offset;
    auto origin = mode == SeekMode::Current ? FILE_BEGIN : static_cast<DWORD>(mode);

    // Perform actual seek
    auto result = SetFilePointerEx(m_file, li, &pos, origin);
    if (!result) {
        throw Exception(GetLastError());
    }

    // Invalidate buffer
    m_nRemaining = 0;
    m_fileBase = pos.QuadPart;
    m_pbuf = m_buf.get();
}

size_t FileStream::tell() const
{
    auto offset = m_pbuf - m_buf.get();
    return m_fileBase + offset;
}

size_t FileStream::size() const
{
    return m_fileSize;
}

ByteBuffer FileStream::read()
{
    auto sz = size();
    auto buf = std::make_unique<uint8_t[]>(sz);

    seek(0, SeekMode::Begin);
    read(buf.get(), sz);

    ByteBuffer buffer{std::move(buf), sz};

    return buffer;
}

Stream FileStream::read(size_t bytes)
{
    auto buf = std::make_unique<char[]>(bytes);

    read(buf.get(), bytes);

    return {buf.get(), bytes};
}

bool FileStream::isOpen() const
{
    return m_file != INVALID_HANDLE_VALUE;
}

bool FileStream::flush()
{
    if (!(m_access & GENERIC_WRITE)) {
        return true;
    }

    if (m_nRemaining == 0) {
        return true;
    }

    if (!writeBlock()) {
        return false;
    }

    return true;
}

bool FileStream::readBlock()
{
    LARGE_INTEGER pos{};
    if (!SetFilePointerEx(m_file, {}, &pos, FILE_CURRENT)) {
        throw Exception(GetLastError());
    }

    ULONG read;
    if (!ReadFile(m_file, m_buf.get(), m_blockSize, &read, nullptr)) {
        return FALSE;
    }

    if (read == 0) {
        return true; // EOF
    }

    m_pbuf = m_buf.get();
    m_nRemaining = read;
    m_fileBase = pos.QuadPart;

    return TRUE;
}

bool FileStream::writeBlock()
{
    size_t totalWritten = 0;

    while (totalWritten < m_nRemaining) {
        DWORD written = 0;
        if (!WriteFile(
            m_file,
            m_buf.get() + totalWritten,
            static_cast<DWORD>(m_nRemaining - totalWritten),
            &written,
            nullptr)) {
            return false;
        }

        if (written == 0) {
            SetLastError(ERROR_WRITE_FAULT);
            return false;
        }

        totalWritten += written;
    }

    m_fileBase += totalWritten;
    m_fileSize = std::max(m_fileSize, m_fileBase);

    m_nRemaining = 0;
    m_pbuf = m_buf.get();

    return true;
}

void FileStream::alloc()
{
    DWORD sectorSize;
    if (!GetDiskFreeSpace(nullptr, nullptr, &sectorSize, nullptr, nullptr)) {
        throw Exception(GetLastError());
    }

    m_blockSize = sectorSize * 128; // is this a good block size?
    m_buf = std::make_unique<uint8_t[]>(m_blockSize);
    m_pbuf = m_buf.get();
    memset(m_pbuf, 0, m_blockSize);
    m_nRemaining = 0;
}

uint64_t FileStream::logicalPos() const
{
    auto offset = m_pbuf - m_buf.get();
    return m_fileBase + offset;
}
