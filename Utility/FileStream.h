#pragma once

#include "Stream.h"
#include "StreamBase.h"

class FileStream : public StreamBase
{
public:
    using StreamBase::read;
    using StreamBase::write;

    FileStream();
    ~FileStream() override;

    FileStream(FileStream&& rhs) noexcept;
    FileStream& operator=(FileStream&& rhs) noexcept;

    FileStream(const FileStream&) = delete;
    FileStream& operator=(const FileStream&) = delete;

    void open(const char* path, const char* mode);
    void close();

    // StreamBase
    size_t read(char* buf, size_t size) override;
    size_t write(const char* buf, size_t size) override;
    void seek(int64_t offset, SeekMode mode) override;
    size_t tell() const override;
    size_t size() const override;

    ByteBuffer read();
    Stream read(size_t bytes);
    void write(StreamBase& stream);

    bool isOpen() const;
    bool flush();

private:
    void closeNoThrow() noexcept;
    bool readBlock();
    bool writeBlock();
    uint64_t logicalPos() const;
    void alloc();
    
    HANDLE m_file; // file handle
    std::unique_ptr<uint8_t[]> m_buf; // internal buffer for read/write operations
    uint8_t* m_pbuf; // buffer pointer
    uint32_t m_nRemaining; // number of bytes remaining in buffer
    uint64_t m_fileBase; // current file position where buffer starts
    DWORD m_blockSize; // block size for buffered I/O
    uint64_t m_fileSize; // cached file size
    DWORD m_access; // file access mode
    DWORD m_shareMode; // file share mode
    DWORD m_creation; // file creation disposition
};
