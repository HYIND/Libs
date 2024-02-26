#include "ProtocolHelper/WebSocketHeleper.h"
#include "Helper/base64.h"
#include "Helper/sha1.h"
#include <random>

using namespace std;

#define SP " "
#define EOL "\r\n"
#define DEFAULT_HTTP_VERSION "HTTP/1.1"

// max handshake frame = 10k
#define WS_MAX_HANDSHAKE_FRAME_SIZE 1024 * 100

// max dataframepayload
#define WS_MAX_DATAFRAME_PAYLOAD_SIZE 200000

const std::string magic_key = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

enum class ParseHandshakeResult
{
    None = -1,
    IncompleteBuffer = 0,
    ParseOK = 1
};
// 检查握手请求包
ParseHandshakeResult GetHandshakeRequestElement(const std::string &msg, std::map<std::string, std::string> &params, int &handShakeDataLength)
{

    // two EOLs mean a completed http1.1 request line
    // 查找双"\r\n"，若未找到，则认为请求不完整
    std::string::size_type endpos = msg.find(std::string(EOL) + EOL);
    if (endpos == std::string::npos)
    {
        handShakeDataLength = -1;
        return ParseHandshakeResult::IncompleteBuffer;
    }

    // can't find end of request line in current, and we continue receiving data
    std::vector<std::string> lines;
    if (strHelper::splitStr<std::vector<std::string>>(lines, msg.substr(0, endpos), EOL) < 2)
    {
        handShakeDataLength = -1;
        return ParseHandshakeResult::IncompleteBuffer;
    }

    std::vector<std::string>::iterator it = lines.begin();

    while ((it != lines.end()) && strHelper::trim(*it).empty())
    {
        ++it;
    };

    std::vector<std::string> reqLineParams;
    if (strHelper::splitStr<std::vector<std::string>>(reqLineParams, *it, SP) < 3)
    {
        handShakeDataLength = -1;
        return ParseHandshakeResult::IncompleteBuffer;
    }

    std::string method_ = strHelper::trim(reqLineParams[0]);
    std::string uri_ = strHelper::trim(reqLineParams[1]);
    std::string version_ = strHelper::trim(reqLineParams[2]);

    params["method"] = method_;
    params["uri"] = uri_;
    params["version"] = version_;

    for (++it; it != lines.end(); ++it)
    {
        // header fields format:
        // field name: values
        std::string::size_type pos = it->find_first_of(":");
        if (pos == std::string::npos)
            continue; // invalid line

        std::string k = it->substr(0, pos);

        std::string v = it->substr(pos + 1);
        if (strHelper::trim(k).empty())
        {
            continue;
        }
        if (strHelper::trim(v).empty())
        {
            continue;
        }

        params[k] = v;
        std::cout << "handshake element k:" << k.c_str() << " v:" << v.c_str() << std::endl;
    }

    handShakeDataLength = endpos + 4;
    return ParseHandshakeResult::ParseOK;
}

// 检查握手回复包
ParseHandshakeResult GetHandshakeResponseElement(const std::string &msg, std::map<std::string, std::string> &params, int &handShakeDataLength)
{

    // two EOLs mean a completed http1.1 request line
    // 查找双"\r\n"，若未找到，则认为请求不完整
    std::string::size_type endpos = msg.find(std::string(EOL) + EOL);
    if (endpos == std::string::npos)
    {
        handShakeDataLength = -1;
        return ParseHandshakeResult::IncompleteBuffer;
    }

    // can't find end of request line in current, and we continue receiving data
    std::vector<std::string> lines;
    if (strHelper::splitStr<std::vector<std::string>>(lines, msg.substr(0, endpos), EOL) < 2)
    {
        handShakeDataLength = -1;
        return ParseHandshakeResult::IncompleteBuffer;
    }

    std::vector<std::string>::iterator it = lines.begin();

    while ((it != lines.end()) && strHelper::trim(*it).empty())
    {
        ++it;
    };

    std::vector<std::string> responseLineParams;
    if (strHelper::splitStr<std::vector<std::string>>(responseLineParams, *it, SP) < 4)
    {
        handShakeDataLength = -1;
        return ParseHandshakeResult::IncompleteBuffer;
    }

    std::string version_ = strHelper::trim(responseLineParams[0]);
    std::string status_ = strHelper::trim(responseLineParams[1]);
    std::string switchingProtocol1 = strHelper::trim(responseLineParams[2]);
    std::string switchingProtocol2 = strHelper::trim(responseLineParams[3]);

    params["version"] = version_;
    params["status"] = status_;
    params["swProto1"] = switchingProtocol1;
    params["swProto2"] = switchingProtocol2;

    for (++it; it != lines.end(); ++it)
    {
        // header fields format:
        // field name: values
        std::string::size_type pos = it->find_first_of(":");
        if (pos == std::string::npos)
            continue; // invalid line

        std::string k = it->substr(0, pos);

        std::string v = it->substr(pos + 1);
        if (strHelper::trim(k).empty())
        {
            continue;
        }
        if (strHelper::trim(v).empty())
        {
            continue;
        }

        params[k] = v;
        std::cout << "handshake element k:" << k.c_str() << " v:" << v.c_str() << std::endl;
    }

    handShakeDataLength = endpos + 4;
    return ParseHandshakeResult::ParseOK;
}

const std::string get_param(std::map<std::string, std::string> &params, const std::string &name)
{
    auto it = params.find(name);
    if (it != params.end())
    {
        return it->second;
    }
    return std::string();
}

bool WebSocketAnalysisHelp::GenerateHandshakeRequest(
    std::string &hs_req, std::string &SecWSKey,
    const std::string &uri, const std::string &host, const std::string &origin, const std::vector<std::string> &protocol)
{

    srand(time(0));
    int randomNum[4];
    for (int i = 0; i < 4; i++)
    {
        randomNum[i] = rand();
        if (rand() % 2 > 0)
            randomNum[i] = -randomNum[i];
    }
    char randomChars[16];
    memcpy(randomChars, &randomNum[1], 4);
    memcpy(randomChars + 4, &randomNum[2], 4);
    memcpy(randomChars + 8, &randomNum[2], 4);
    memcpy(randomChars + 12, &randomNum[2], 4);

    char SecWebSocketKey[128] = {0};
    Base64encode(SecWebSocketKey, randomChars, sizeof(randomChars));
    SecWSKey = SecWebSocketKey;

    std::ostringstream sstream;

    {
        sstream << "GET  /" << uri << DEFAULT_HTTP_VERSION << EOL;
        if (!host.empty())
            sstream << "Host: " << host << EOL;
        sstream << "Connection: Upgrade" << EOL;
        sstream << "Upgrade: websocket" << EOL;
        sstream << "Sec-WebSocket-Key: " << SecWebSocketKey << EOL;
        sstream << "Sec-WebSocket-Version: " << 13 << EOL;
        if (!origin.empty())
            sstream << "Origin: " << origin << EOL;
    }

    {

        sstream << "Sec-WebSocket-Protocol: chat";
        for (auto &str : protocol)
        {
            if (!str.empty() && str.compare(string("chat")) != 0)
                sstream << " " << str;
        }
        sstream << EOL;
    }

    sstream << EOL;
    hs_req = sstream.str();

    return true;
}

int WebSocketAnalysisHelp::AnalysisHandshakeRequest(Buffer &input, std::string &SecWSKey, std::map<std::string, std::string> &params, int &handShakeBufferLength)
{

    if (input.Length() > WS_MAX_HANDSHAKE_FRAME_SIZE)
    {
        return WS_MAX_HANDSHAKE_FRAME_SIZE;
    }

    std::string inputstr(input.Byte(), input.Length());
    ParseHandshakeResult result = GetHandshakeRequestElement(inputstr, params, handShakeBufferLength);
    if (result != ParseHandshakeResult::ParseOK)
    {
        return -1;
    }

    // 检查websocket握手请求的相应字段
    if (get_param(params, "method") != "GET" ||
        get_param(params, "Upgrade") != "websocket" ||
        get_param(params, "Connection") != "Upgrade" ||
        get_param(params, "Sec-WebSocket-Version") != "13" ||
        get_param(params, "Sec-WebSocket-Key") == "")
    {
        return WS_ERROR_INVALID_HANDSHAKE_PARAMS;
    }

    SecWSKey = get_param(params, "Sec-WebSocket-Key");

    return 1;
}

bool WebSocketAnalysisHelp::GenerateHandshakeResponse(std::map<std::string, std::string> &params, const std::string &SecWSKey, std::string &hs_rsp)
{
    if (params.empty())
        return false;

    std::string raw_key = SecWSKey + magic_key;
    std::string sha1_key = SHA1::SHA1HashString(raw_key);
    char accept_key[128] = {0};
    Base64encode(accept_key, sha1_key.c_str(), sha1_key.length());

    std::ostringstream sstream;
    sstream << "HTTP/1.1 101 Switching Protocols" << EOL;
    sstream << "Connection: Upgrade" << EOL;
    sstream << "Upgrade: websocket" << EOL;
    sstream << "Sec-WebSocket-Accept: " << accept_key << EOL;
    sstream << "Sec-WebSocket-Version: " << 13 << EOL;
    if (get_param(params, "Sec-WebSocket-Protocol") != "")
    {
        sstream << "Sec-WebSocket-Protocol: chat" << EOL;
    }
    sstream << EOL;
    hs_rsp = sstream.str();

    return true;
}

int WebSocketAnalysisHelp::AnalysisHandshakeResponse(Buffer &input, const std::string &SecWSKey, std::map<std::string, std::string> &params, int &handShakeBufferLength)
{
    if (input.Length() > WS_MAX_HANDSHAKE_FRAME_SIZE)
    {
        return WS_MAX_HANDSHAKE_FRAME_SIZE;
    }

    std::string inputstr(input.Byte(), input.Length());
    ParseHandshakeResult result = GetHandshakeResponseElement(inputstr, params, handShakeBufferLength);
    if (result != ParseHandshakeResult::ParseOK)
    {
        return -1;
    }

    // 检查websocket握手请求的相应字段
    if (get_param(params, "version") != DEFAULT_HTTP_VERSION ||
        get_param(params, "status") != "101" ||
        get_param(params, "swProto1") != "Switching" ||
        get_param(params, "swProto2") != "Protocols" ||
        get_param(params, "Upgrade") != "websocket" ||
        get_param(params, "Connection") != "Upgrade" ||
        get_param(params, "Sec-WebSocket-Accept") == "" ||
        get_param(params, "Sec-WebSocket-Version") != "13")
    {
        return WS_ERROR_INVALID_HANDSHAKE_PARAMS;
    }

    // 验证Accept
    std::string raw_key = SecWSKey + magic_key;
    std::string sha1_key = SHA1::SHA1HashString(raw_key);
    char GenerateAccept_key[128] = {0};
    Base64encode(GenerateAccept_key, sha1_key.c_str(), sha1_key.length());

    const std::string &AcceptStr = get_param(params, "Sec-WebSocket-Accept");
    if (strncmp(GenerateAccept_key, AcceptStr.c_str(), AcceptStr.length()) != 0)
    {
        return 0;
    }

    return 1;
}

enum class ParseDataFrameResult
{
    Error = -1,
    IncompleteBuffer = 0,
    ParseOK = 1
};
ParseDataFrameResult GetDataFrameInfo(Buffer &input, WebSocketDataframe &info)
{
    if (input.Remaind() < 2)
        return ParseDataFrameResult::IncompleteBuffer;

    // FIN, opcode
    uint8_t onebyte = 0;
    input.Read((char *)&onebyte, 1);
    info.fin = onebyte >> 7;
    info.opcode = onebyte & 0x7F;

    // payload length
    input.Read((char *)&onebyte, 1);
    info.mask = onebyte >> 7 & 0x01;
    info.length_type = onebyte & 0x7F;

    if (info.length_type < 126)
    {
        info.payload_length = info.length_type;
    }
    else if (info.length_type == 126)
    {
        /*
        uint16_t len = 0;
        input.read_bytes_x((char *)&len, 2);
        len = (len << 8) | (len >>8 & 0xFF);
        payload_length = len;
        */

        if (input.Remaind() < 2)
            return ParseDataFrameResult::IncompleteBuffer;
        uint16_t len = 0;
        uint8_t array[2] = {0};
        input.Read((char *)array, 2);
        len = uint16_t(array[0] << 8) | uint16_t(array[1]);
        info.payload_length = len;
    }
    else if (info.length_type == 127)
    {
        if (input.Remaind() < 8)
            return ParseDataFrameResult::IncompleteBuffer;
        // if you don't have ntohll
        uint64_t len = 0;
        uint8_t array[8] = {0};
        input.Read((char *)array, 8);
        len = (array[0] << 56) | array[1] << 48 | array[2] << 40 | array[3] << 32 | array[4] << 24 | array[5] << 16 | array[6] << 8 | array[7];

        if (info.payload_length > 0xFFFFFFFF)
        {
            return ParseDataFrameResult::Error;
        }
    }
    else
    {
        return ParseDataFrameResult::Error;
    }

    // masking key
    if (info.mask == 1)
    {
        if (input.Remaind() < 4)
            return ParseDataFrameResult::IncompleteBuffer;
        input.Read((char *)info.masking_key, 4);
    }

    return ParseDataFrameResult::ParseOK;
}

void GetDataFramePayload(Buffer &input, WebSocketDataframe &info)
{
    info.payload.ReSize(info.payload_length);
    if (info.mask == 1)
    {
        char real = 0;
        for (uint64_t i = 0; i < info.payload_length; i++)
        {
            input.Read(&real, 1);
            real = real ^ info.masking_key[i % 4];
            info.payload.Write(&real, 1);
        }
    }
    else
    {
        info.payload.WriteFromOtherBufferPos(input, info.payload_length);
    }
    info.payload.Seek(0);
}

int WebSocketAnalysisHelp::AnalysisDataframe(Buffer &input, WebSocketDataframe &info)
{
    try
    {
        int oripos = input.Postion();

        ParseDataFrameResult result = GetDataFrameInfo(input, info);
        if (result != ParseDataFrameResult::ParseOK)
        {
            input.Seek(oripos);
            if (result == ParseDataFrameResult::IncompleteBuffer)
                return -1;
            else /* if (result == ParseDataFrameResult::Error) */
                return 0;
        }
        /*
                std::cout << "WebSocketPacket: header size: " << input.Postion() - oripos
                          << " info.payload_length: " << info.payload_length << " input.remaindSize: " << input.Remaind() << std::endl;
         */
        if (input.Remaind() < info.payload_length)
        {
            // buffer size is not enough, so we continue recving data
            std::cout << "WebSocketPacket: AnalysisDataframe: IncompleteBuffer, continue recving data." << std::endl;
            input.Seek(oripos);
            return -1;
        }

        GetDataFramePayload(input, info);

        // std::cout << "WebSocketPacket: received data with payload size:" << info.payload_length << std::endl;

        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 0;
    }
}

const uint8_t Generate_length_type(uint64_t payload_length)
{
    if (payload_length < 126)
    {
        return payload_length;
    }
    else if (payload_length >= 126 && payload_length <= 0xFFFF)
    {
        return 126;
    }
    else
    {
        return 127;
    }
}

void AppendSignalDataFrameBufferFromPos(
    Buffer &input,
    uint8_t fin, uint8_t opcode, uint8_t mask,
    MaskKey masking_key, uint8_t rsv1, uint8_t rsv2, uint8_t rsv3, Buffer &output)
{

    uint8_t onebyte = 0;
    onebyte |= (fin << 7);
    onebyte |= (rsv1 << 6);
    onebyte |= (rsv2 << 5);
    onebyte |= (rsv3 << 4);
    onebyte |= (opcode & 0x0F);
    output.Write((char *)&onebyte, 1);

    onebyte = 0;
    // set mask flag
    onebyte = onebyte | (mask << 7);

    uint64_t payload_length = std::min(input.Remaind(), WS_MAX_DATAFRAME_PAYLOAD_SIZE);
    uint8_t length_type = Generate_length_type(payload_length);
    if (length_type < 126)
    {
        onebyte |= payload_length;
        output.Write((char *)&onebyte, 1);
    }
    else if (length_type == 126)
    {
        onebyte |= length_type;
        output.Write((char *)&onebyte, 1);

        // also can use htons
        onebyte = (payload_length >> 8) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = payload_length & 0xFF;
        output.Write((char *)&onebyte, 1);
    }
    else if (length_type == 127)
    {
        onebyte |= length_type;
        output.Write((char *)&onebyte, 1);

        // also can use htonll if you have it
        onebyte = (payload_length >> 56) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = (payload_length >> 48) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = (payload_length >> 40) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = (payload_length >> 32) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = (payload_length >> 24) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = (payload_length >> 16) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = (payload_length >> 8) & 0xFF;
        output.Write((char *)&onebyte, 1);
        onebyte = payload_length & 0XFF;
        output.Write((char *)&onebyte, 1);
    }
    else
    {
        return;
    }

    /*     std::cout
            << "WebSocketPacket: send data with header size: " << output.Length()
            << " payload size:" << payload_length << std::endl
            << std::endl; */
    if (mask == 1)
    {
        char value = 0;
        // masking key
        output.Write((char *)masking_key.uint8keys, 4);
        for (uint64_t i = 0; i < payload_length; i++)
        {
            input.Read(&value, 1);
            value = value ^ masking_key.uint8keys[i % 4];
            output.Write(&value, 1);
        }
    }
    else
    {
        output.WriteFromOtherBufferPos(input, payload_length);
    }
}

bool WebSocketAnalysisHelp::GenerateDataFrameBuffer(
    Buffer &input, uint8_t opcode, uint8_t mask,
    Buffer &output,
    MaskKey masking_key, uint8_t rsv1, uint8_t rsv2, uint8_t rsv3)
{
    if (opcode == 0x0 || opcode == 0x9 || opcode == 0xA || opcode == 0x8)
        return false;

    int oriPos = input.Postion();
    input.Seek(0);
    output.Seek(0);

    // 分片数
    int count = input.Length() % WS_MAX_DATAFRAME_PAYLOAD_SIZE == 0
                    ? input.Length() / WS_MAX_DATAFRAME_PAYLOAD_SIZE
                    : input.Length() / WS_MAX_DATAFRAME_PAYLOAD_SIZE + 1;
    if (count < 0)
        return false;

    for (int i = 1; i <= count; i++)
    {
        if (i == count) // 尾片
        {
            AppendSignalDataFrameBufferFromPos(input, 1, opcode, mask, masking_key, rsv1, rsv2, rsv3, output);
        }
        else if (i == 1) // 首片
        {
            AppendSignalDataFrameBufferFromPos(input, 0, opcode, mask, masking_key, rsv1, rsv2, rsv3, output);
        }
        else // 中间片
        {
            AppendSignalDataFrameBufferFromPos(input, 0, 0x0, mask, masking_key, rsv1, rsv2, rsv3, output);
        }
    }

    input.Seek(oriPos);
    if (output.Postion() != output.Length())
        output.ReSize(output.Postion());
    else
        output.Seek(0);

    return true;
}

bool WebSocketAnalysisHelp::GenerateSpecialDataFrameBuffer(
    uint8_t opcode,
    Buffer &output,
    Buffer *input = nullptr, uint8_t mask, MaskKey masking_key, uint8_t rsv1, uint8_t rsv2, uint8_t rsv3)
{
    if (opcode != 0x9 && opcode != 0xA && opcode != 0x8)
        return false;

    output.Seek(0);

    static Buffer ZeroBuf(0);
    Buffer &buf = ZeroBuf;

    if (input && input->Length() > 0)
        buf = *input;

    AppendSignalDataFrameBufferFromPos(buf, 0, opcode, mask, masking_key, rsv1, rsv2, rsv3, output);

    if (output.Postion() != output.Length())
        output.ReSize(output.Postion());
    else
        output.Seek(0);

    return true;
}
