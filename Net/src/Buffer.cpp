#include "Helper/Buffer.h"

Buffer::Buffer()
{
    _buf = nullptr;
    _pos = 0;
    _length = 0;
}

Buffer::Buffer(const Buffer &other)
{
    CopyFromBuf(other);
}

Buffer::Buffer(Buffer &&other)
{
    QuoteFromBuf(other);
}

Buffer::Buffer(const uint64_t length)
{
    _buf = new char[length + 1];
    memset(_buf, '\0', length);
    _length = length;
}

Buffer::Buffer(const char *buf, uint64_t length)
{
    CopyFromBuf(buf, length);
}
Buffer::Buffer(const std::string &str)
{
    CopyFromBuf(str.c_str(), str.length());
}

Buffer::~Buffer()
{
    Release();
}

Buffer &Buffer::operator=(const Buffer &other)
{
    if (this == &other)
        return *this;
    CopyFromBuf(other);
    return *this;
}

Buffer &Buffer::operator=(Buffer &&other)
{
    if (this == &other)
        return *this;
    QuoteFromBuf(other);
    return *this;
}

void Buffer::Release()
{
    SAFE_DELETE_ARRAY(_buf);
    _pos = 0;
    _length = 0;
}

void *Buffer::Data()
{
    return _buf;
}

const void *Buffer::Data() const
{
    return _buf;
}

char *Buffer::Byte()
{
    return (char *)_buf;
}

const char *Buffer::Byte() const
{
    return (const char *)_buf;
}

uint64_t Buffer::Length() const
{
    return _length;
}

uint64_t Buffer::Position() const
{
    return _pos;
}

uint64_t Buffer::Remain() const
{
    return std::max((uint64_t)0, _length - _pos);
}

void Buffer::CopyFromBuf(const Buffer &other)
{
    SAFE_DELETE_ARRAY(_buf);

    _buf = new char[other._length + 1];
    memcpy(_buf, other._buf, other._length);
    _length = other._length;
    _pos = 0;
}

void Buffer::CopyFromBuf(const char *buf, uint64_t length)
{
    SAFE_DELETE_ARRAY(_buf);

    length = std::max((uint64_t)0, length);

    _buf = new char[length + 1];
    memcpy(_buf, buf, length);
    _length = length;
    _pos = 0;
}
void Buffer::QuoteFromBuf(Buffer &other)
{
    SAFE_DELETE_ARRAY(_buf);

    this->_buf = other._buf;
    this->_length = other._length;
    this->_pos = 0;
    other._buf = nullptr;
    other._length = 0;
    other._pos = 0;
}

void Buffer::QuoteFromBuf(Buffer &&other)
{
    SAFE_DELETE_ARRAY(_buf);

    this->_buf = other._buf;
    this->_length = other._length;
    this->_pos = 0;
    other._buf = nullptr;
    other._length = 0;
    other._pos = 0;
}

uint64_t Buffer::Write(const Buffer &other)
{
    return Write(other.Data(), other.Length());
}

uint64_t Buffer::Write(const std::string &str)
{
    return Write(str.c_str(), str.length());
}

uint64_t Buffer::Write(const void *buf, const uint64_t length)
{
    if (length <= 0)
        return 0;
    if (_pos + length > this->_length)
    {
        char *newBuf = new char[_pos + length + 1];
        memcpy(newBuf, _buf, this->_length);
        SAFE_DELETE_ARRAY(this->_buf);
        this->_buf = newBuf;
        this->_length = _pos + length;
    }
    memcpy(_buf + _pos, buf, length);
    _pos += length;
    return length;
}

uint64_t Buffer::Append(Buffer &other)
{
    return Append(other, other.Length() - other.Position());
}

uint64_t Buffer::Append(Buffer &other, uint64_t length)
{
    uint64_t truthAppend = std::min(other.Length() - other.Position(), length);
    if (truthAppend <= 0)
        return 0;

    char *newBuf = new char[_length + truthAppend + 1];

    if (_buf)
        memcpy(newBuf, _buf, _length);
    memcpy(newBuf + _length, other.Byte() + other.Position(), truthAppend);

    SAFE_DELETE_ARRAY(_buf);
    _buf = newBuf;
    _length = _length + truthAppend;

    other.Seek(other.Position() + truthAppend);
    return truthAppend;
}

uint64_t Buffer::WriteFromOtherBufferPos(Buffer &other)
{
    return WriteFromOtherBufferPos(other, other.Length() - other.Position());
}

uint64_t Buffer::WriteFromOtherBufferPos(Buffer &other, uint64_t length)
{
    uint64_t truthRead = std::min(other.Length() - other.Position(), length);
    if (truthRead <= 0)
        return 0;
    Write((char *)other.Data() + other.Position(), truthRead);
    other.Seek(other.Position() + truthRead);
    return truthRead;
}

uint64_t Buffer::Read(void *buf, const uint64_t length)
{
    if (length <= 0)
        return 0;
    uint64_t truthRead = std::min(length, this->_length - _pos);
    if (truthRead > 0)
        memcpy(buf, _buf + _pos, truthRead);
    _pos += truthRead;
    return truthRead;
}
uint64_t Buffer::Seek(const uint64_t index)
{
    if (index < 0)
        _pos = 0;
    _pos = std::min(index, _length);
    return _pos;
}

void Buffer::ReSize(const uint64_t length)
{
    uint64_t truthlength = std::max(length, (uint64_t)0);
    if (length == _length)
        return;

    char *oribuf = _buf;
    uint64_t oriLength = _length;

    _buf = new char[length + 1];
    if (oribuf)
        memcpy(_buf, oribuf, std::min(oriLength, length));
    _length = length;
    _pos = 0;

    SAFE_DELETE_ARRAY(oribuf);
}

void Buffer::Shift(const uint64_t length)
{
    uint64_t truthShift = std::min(_length, length);
    if (truthShift <= 0)
        return;

    char *oribuf = _buf;
    uint64_t oriLength = _length;

    uint64_t newlength = oriLength - truthShift;
    _buf = new char[newlength + 1];
    if (oribuf)
        memcpy(_buf, oribuf + truthShift, newlength);

    _length = newlength;
    _pos = 0;

    SAFE_DELETE_ARRAY(oribuf);
}

void Buffer::Unshift(const void *buf, const uint64_t length)
{
    if (length <= 0)
        return;

    char *oribuf = _buf;

    uint64_t oriLength = _length;
    uint64_t newLength = length + oriLength;

    _buf = new char[newLength + 1];
    memcpy(_buf, buf, length);
    if (oribuf)
        memcpy(_buf + length, oribuf, oriLength);

    _length = newLength;
    _pos = 0;

    SAFE_DELETE_ARRAY(oribuf);
}
