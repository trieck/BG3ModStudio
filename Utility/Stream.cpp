#include "pch.h"
#include "Exception.h"
#include "Stream.h"

static auto constexpr DEFAULT_BUFFER_SIZE = 4096;

Stream::Stream() : m_pos(0), m_size(0), m_capacity(0)
{
    alloc(DEFAULT_BUFFER_SIZE);
}

Stream::Stream(StreamBase& stream)
{
    auto size = stream.size();
    alloc(std::max<size_t>(size, DEFAULT_BUFFER_SIZE));

    stream.read(m_bytes.get(), size);

    m_size = size;
}

Stream::Stream(size_t capacity) : m_pos(0), m_size(0), m_capacity(0)
{
    alloc(capacity);
}

Stream::Stream(const char* buf, size_t size)
{
    alloc(std::max<size_t>(size, DEFAULT_BUFFER_SIZE));

    memcpy(m_bytes.get(), buf, size);

    m_size = size;
}

Stream::Stream(ByteBuffer buf)
    : m_pos(0), m_size(buf.second), m_capacity(buf.second), m_bytes(std::move(buf.first))
{
}

Stream::Stream(const std::string& str)
{
    auto* pstr = str.c_str();
    auto size = str.size();

    alloc(std::max<size_t>(size, DEFAULT_BUFFER_SIZE));

    memcpy(m_bytes.get(), pstr, size);

    m_size = size;
}

Stream::~Stream() = default;

size_t Stream::read(char* buf, size_t size)
{
    auto cbRead = std::min<size_t>(size, m_size - m_pos);

    memcpy(buf, m_bytes.get() + m_pos, cbRead);

    m_pos += cbRead;

    return cbRead;
}

size_t Stream::write(const char* buf, size_t size)
{
    auto newSize = m_pos + size;
    if (newSize > m_capacity) {
        realloc(std::max({newSize, static_cast<size_t>(DEFAULT_BUFFER_SIZE), m_capacity * 2}));
    }

    memcpy(m_bytes.get() + m_pos, buf, size);

    m_size = std::max(m_size, newSize);
    m_pos += size;

    return size;
}

void Stream::seek(int64_t offset, SeekMode mode)
{
    int64_t newPos;

    switch (mode) {
    case SeekMode::Begin:
        if (offset >= 0 && static_cast<size_t>(offset) <= m_size) {
            m_pos = offset;
        } else {
            throw Exception("Invalid seek (Begin)");
        }
        break;
    case SeekMode::Current:
        newPos = static_cast<int64_t>(m_pos) + offset;
        if (newPos < 0 || static_cast<size_t>(newPos) > m_size) {
            throw Exception("Invalid seek (Current)");
        }
        m_pos = newPos;
        break;
    case SeekMode::End:
        if (offset > 0) {
            m_pos = m_size; // clamp
        } else if (static_cast<size_t>(-offset) > m_size) {
            throw Exception("Invalid seek (End)");
        } else {
            m_pos = m_size + offset;
        }
        break;
    }
}

size_t Stream::tell() const
{
    return m_pos;
}

void Stream::alloc(size_t size)
{
    if (size > 0) {
        m_bytes = std::make_unique<uint8_t[]>(size);
    }

    m_capacity = size;
    m_pos = 0;
}

void Stream::realloc(size_t size)
{
    auto newBytes = std::make_unique<uint8_t[]>(size);
    memcpy(newBytes.get(), m_bytes.get(), m_size);
    m_bytes = std::move(newBytes);
    m_capacity = size;
}

size_t Stream::size() const
{
    return m_size;
}

Stream Stream::makeStream(UInt8Ptr&& ptr, size_t size)
{
    Stream stream;
    stream.attach({std::move(ptr), size});
    return stream;
}

Stream Stream::makeStream(const char* buf, size_t size)
{
    return {buf, size};
}

Stream Stream::makeStream(ByteBuffer&& buf)
{
    Stream stream(std::move(buf));
    return stream;
}

Stream Stream::makeStream(const ByteBuffer& buf)
{
    Stream stream;
    stream.attach({std::make_unique<uint8_t[]>(buf.second), buf.second});
    memcpy(stream.m_bytes.get(), buf.first.get(), buf.second);
    return stream;
}

Stream Stream::makeStream(const std::string& str)
{
    Stream stream(str);
    return stream;
}

Stream Stream::makeStream(StreamBase& stream)
{
    Stream newStream;

    char buf[DEFAULT_BUFFER_SIZE];
    size_t bytesRead;

    while ((bytesRead = stream.read(buf, sizeof(buf))) > 0) {
        newStream.write(buf, bytesRead);
    }

    newStream.seek(0, SeekMode::Begin);

    return newStream;
}

Stream Stream::read(size_t bytes)
{
    if (m_pos > m_size || bytes > m_size - m_pos) {
        throw Exception("Unexpected end of stream.");
    }

    auto buf = std::make_unique<char[]>(bytes);

    read(buf.get(), bytes);

    return {buf.get(), bytes};
}

std::string Stream::str() const
{
    return std::string(reinterpret_cast<char*>(m_bytes.get()), m_size);
}

ByteBuffer Stream::bytes() const
{
    Stream stream(reinterpret_cast<char*>(m_bytes.get()), m_size);

    return {std::move(stream.m_bytes), stream.m_size};
}

size_t Stream::capacity() const
{
    return m_capacity;
}

void Stream::attach(ByteBuffer buffer)
{
    m_bytes = std::move(buffer.first);
    m_size = buffer.second;
    m_capacity = buffer.second;
    m_pos = 0;
}

ByteBuffer Stream::detach()
{
    ByteBuffer buffer{std::move(m_bytes), m_size};

    m_pos = 0;
    m_size = 0;
    m_capacity = 0;
    m_bytes.reset();

    return buffer;
}

uint8_t* Stream::data() const
{
    return m_bytes.get();
}

Stream::Stream(Stream&& rhs) noexcept
{
    *this = std::move(rhs);
}

Stream& Stream::operator=(Stream&& rhs) noexcept
{
    if (this != &rhs) {
        m_pos = rhs.m_pos;
        m_size = rhs.m_size;
        m_capacity = rhs.m_capacity;
        m_bytes = std::move(rhs.m_bytes);

        rhs.m_pos = 0;
        rhs.m_size = 0;
        rhs.m_capacity = 0;
    }

    return *this;
}
