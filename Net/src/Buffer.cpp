#include "Helper/Buffer.h"

using namespace std;

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

Buffer::Buffer(const int length)
{
    _buf = new char[length];
    memset(_buf, '\0', length);
    _length = length;
}

Buffer::Buffer(const char *buf, int length)
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

void Buffer::Release()
{
    SAFE_DELETE_ARRAY(_buf);
    _pos = 0;
    _length = 0;
}

void *Buffer::Data() const
{
    return _buf;
}

char *Buffer::Byte() const
{
    return (char *)_buf;
}
int Buffer::Length() const
{
    return _length;
}

int Buffer::Postion() const
{
    return _pos;
}

int Buffer::Remaind() const
{
    return std::max(0, _length - _pos);
}
void Buffer::CopyFromBuf(const Buffer &other)
{
    SAFE_DELETE_ARRAY(_buf);

    _buf = new char[other._length];
    memcpy(_buf, other._buf, other._length);
    _length = other._length;
    _pos = 0;
}

void Buffer::CopyFromBuf(const char *buf, int length)
{
    SAFE_DELETE_ARRAY(_buf);

    length = std::max(0, length);

    _buf = new char[length];
    memcpy(_buf, buf, length);
    _length = length;
    _pos = 0;
}

void Buffer::QuoteFromBuf(char *buf, int length)
{
    SAFE_DELETE_ARRAY(_buf);

    _buf = buf;
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

int Buffer::Write(const Buffer &other)
{
    return Write(other.Data(), other.Length());
}

int Buffer::Write(const string &str)
{
    return Write(str.c_str(), str.length());
}

int Buffer::Write(const void *buf, const int length)
{
    if (length <= 0)
        return 0;
    if (_pos + length > this->_length)
    {
        char *newBuf = new char[_pos + length];
        memcpy(newBuf, _buf, this->_length);
        SAFE_DELETE_ARRAY(this->_buf);
        this->_buf = newBuf;
        this->_length = _pos + length;
    }
    memcpy(_buf + _pos, buf, length);
    _pos += length;
    return length;
}

int Buffer::Append(Buffer &other)
{
    return Append(other, other.Length() - other.Postion());
}

int Buffer::Append(Buffer &other, int length)
{
    int truthAppend = std::min(other.Length() - other.Postion(), length);
    if (truthAppend <= 0)
        return 0;

    char *newBuf = new char[_length + truthAppend];
    memcpy(newBuf, _buf, _length);
    memcpy(newBuf + _length, other.Byte() + other.Postion(), truthAppend);

    SAFE_DELETE_ARRAY(_buf);
    _buf = newBuf;
    _length = _length + truthAppend;

    other.Seek(other.Postion() + truthAppend);
    return truthAppend;
}

int Buffer::WriteFromOtherBufferPos(Buffer &other)
{
    return WriteFromOtherBufferPos(other, other.Length() - other.Postion());
}

int Buffer::WriteFromOtherBufferPos(Buffer &other, int length)
{
    int truthRead = std::min(other.Length() - other.Postion(), length);
    if (truthRead <= 0)
        return 0;
    Write((char *)other.Data() + other.Postion(), truthRead);
    other.Seek(other.Postion() + truthRead);
    return truthRead;
}

int Buffer::Read(void *buf, const int length)
{
    if (length <= 0)
        return 0;
    int truthRead = min(length, this->_length - _pos);
    if (truthRead > 0)
        memcpy(buf, _buf + _pos, truthRead);
    _pos += truthRead;
    return truthRead;
}
int Buffer::Seek(const int index)
{
    if (index < 0)
        _pos = 0;
    _pos = min(index, _length);
    return _pos;
}

void Buffer::ReSize(const int length)
{
    // int minSize = min(length, 0);
    if (length == _length)
        return;

    char *oribuf = _buf;
    int oriLength = _length;

    _buf = new char[length];
    if (oribuf)
        memcpy(_buf, oribuf, min(oriLength, length));
    _length = length;
    _pos = 0;

    SAFE_DELETE_ARRAY(oribuf);
}

void Buffer::Shift(const int length)
{
    int truthShift = std::min(_length, length);
    if (truthShift <= 0)
        return;

    memcpy(Byte(), Byte() + truthShift, _length - truthShift);

    ReSize(_length - truthShift);
}

void Buffer::Unshift(const void *buf, const int length)
{
    if (length <= 0)
        return;

    char *oribuf = _buf;

    int oriLength = _length;
    int newLength = length + oriLength;

    _buf = new char[newLength];
    memcpy(_buf, buf, length);
    if (oribuf)
        memcpy(_buf + length, oribuf, oriLength);

    _length = newLength;
    _pos = 0;

    SAFE_DELETE_ARRAY(oribuf);
}
