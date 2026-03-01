#include "Session/CustomTcpSession.h"
#include "Helper/CRC32Helper.h"

const char CustomProtocolTryToken[] = "alskdjfhg";      // 客户端发起连接发送的请求Token
const char CustomProtocolConfirmToken[] = "qpwoeiruty"; // 服务端接收到请求后返回的确认Token

const uint32_t CustomProtocolMagic = 0x1A2B3C4D; // 魔数，用以包校验

const char HeartBuffer[] = "23388990";
bool IsHeartBeat(const Buffer& Buffer)
{
	static int HeartSize = sizeof(HeartBuffer) - 1;
	return (Buffer.Length() == HeartSize && 0 == strncmp(HeartBuffer, Buffer.Byte(), HeartSize));
}

struct CustomTcpMsgHeader
{
	uint32_t magic = CustomProtocolMagic;
	int seq = 0;
	int ack = -1;
	uint8_t msgType = 0; // 0:null, 1:请求, 2:响应
	int length = 0;
	uint32_t checksum = 0xFFFFFFFF;

	CustomTcpMsgHeader() {}
	CustomTcpMsgHeader(int seq, int ack, int length)
		: seq(seq), ack(ack), length(length)
	{
		if (ack != -1)
			msgType = 2;
	}
};

using Base = BaseNetWorkSession;

// 校验包
bool CheckPakHeader(CustomTcpMsgHeader header, const uint8_t* data, size_t len)
{
	uint32_t save = header.checksum;

	header.checksum = 0; // 置零

	uint32_t crc = CRC32Helper::calculate((uint8_t*)(&header), sizeof(CustomTcpMsgHeader)); // 计算Header的CRC
	if (data)
		crc = CRC32Helper::update(crc, data, len); // 增量计算PayLoad的CRC

	return crc == save;
}

// 处理流内容，在流的头部添加seq和ack字段
void AddPakHeader(Buffer* buf, CustomTcpMsgHeader header)
{
	if (!buf && header.length > 0)
		return;

	header.checksum = 0; // 置零

	uint32_t crc = CRC32Helper::calculate((uint8_t*)(&header), sizeof(CustomTcpMsgHeader)); // 计算Header的CRC
	if (buf)
		crc = CRC32Helper::update(crc, (uint8_t*)(buf->Data()), buf->Length()); // 增量计算PayLoad的CRC

	header.checksum = crc;

	buf->Unshift(&header, sizeof(CustomTcpMsgHeader));
}

enum class AnalysisResult
{
	InputError = -3,    // 输入错误
	MagicError = -2,    // 魔数错误
	ChecksumError = -1, // 数据包校验和错误
	BufferAGAIN = 0,    // Buffer未取到足够长度
	Success = 1,
};
AnalysisResult AnalysisDataPackage(Buffer* buf, CustomPackage* outPak)
{
	if (!buf || !outPak)
	{
		std::cout << "CustomTcpSession::AnalysisDataPackage null buf or null outPak!\n";
		return AnalysisResult::InputError;
	}

	if (buf->Remain() < sizeof(CustomTcpMsgHeader))
	{
		return AnalysisResult::BufferAGAIN;
	}

	int oriPos = buf->Position();

	CustomTcpMsgHeader header;
	buf->Read(&header, sizeof(CustomTcpMsgHeader));

	if (header.magic != CustomProtocolMagic)
		return AnalysisResult::MagicError;

	if (buf->Remain() < header.length)
	{
		buf->Seek(oriPos);
		return AnalysisResult::BufferAGAIN;
	}

	if (!CheckPakHeader(header, (uint8_t*)(buf->Byte() + buf->Position()), header.length))
	{
		buf->Shift(buf->Position() + header.length);
		return AnalysisResult::ChecksumError;
	}

	outPak->seq = header.seq;
	outPak->ack = header.ack;
	outPak->msgType = header.msgType;
	outPak->buffer.Append(*buf, header.length);

	return AnalysisResult::Success;
}

CustomTcpSession::CustomTcpSession(PureTCPClient* client)
{
	cachePak = new CustomPackage();
	if (client)
		BaseClient = client;
	else
		BaseClient = new PureTCPClient();
}

CustomTcpSession::~CustomTcpSession()
{
	Release();
	SAFE_DELETE(BaseClient);
	SAFE_DELETE(cachePak);
}

Task<bool> CustomTcpSession::Connect(std::string IP, uint16_t Port)
{
	co_return co_await Base::Connect(IP, Port);
}

bool CustomTcpSession::Release()
{
	_callbackRecvRequest = nullptr;

	bool result = Base::Release();

	CustomPackage* pak = nullptr;
	while (_RecvPaks.dequeue(pak))
		SAFE_DELETE(pak);
	while (_SendPaks.dequeue(pak))
		SAFE_DELETE(pak);

	_AwaitMap.Clear();

	cacheBuffer.Release();
	if (cachePak)
	{
		cachePak->seq = 0;
		cachePak->ack = -1;
		cachePak->buffer.Release();
	}

	return true;
}

bool CustomTcpSession::AsyncSend(const Buffer& buffer)
{
	return Send(buffer, -1);
}

Task<std::shared_ptr<AwaitResult>> CustomTcpSession::AwaitSend(Buffer buffer, std::chrono::milliseconds timeout)
{
	auto res = std::make_shared<AwaitResult>();

	try
	{
		if (!buffer.Data() || buffer.Length() <= 0)
		{
			res->code = AwaitErrorCode::InvalidBuffer;
			co_return res;
		}

		auto task = std::make_shared<AwaitTask>();
		task->seq = seq++;
		task->res = res;

		if (!_AwaitMap.Insert(task->seq, task))
		{
			res->code = AwaitErrorCode::InnerError;
			co_return res;
		}

		Buffer buf(buffer);
		CustomTcpMsgHeader header(task->seq, -1, buffer.Length());
		header.msgType = 1;
		AddPakHeader(&buf, header);

		res->code = AwaitErrorCode::TimeOut;
		task->timer = std::make_shared<CoTimer>(timeout);
		if (!BaseClient->Send(buf)) // 发送
		{
			_AwaitMap.Erase(task->seq);
			res->code = AwaitErrorCode::NetWorkError;
			co_return res;
		}
		co_await(*task->timer);
		_AwaitMap.Erase(task->seq);
		co_return task->res;
	}
	catch (const std::exception& e)
	{
		std::cout << "CustomTcpSession::AwaitSend Exception:" << e.what() << '\n';
		res->code = AwaitErrorCode::InnerError;
		co_return res;
	}
}

bool CustomTcpSession::OnSessionClose()
{
	auto callback = _callbackSessionClose;
	Release();
	if (callback)
		callback(this);
	return true;
}

bool CustomTcpSession::OnRecvData(Buffer* buffer)
{
	if (!isHandshakeComplete)
	{
		if (CheckHandshakeConfirmMsg(*buffer) != CheckHandshakeStatus::Success)
			return false;
	}

	if (buffer->Remain() > 0)
		cacheBuffer.Append(*buffer);

	while (cacheBuffer.Remain() > 0)
	{
		// 解析数据包
		AnalysisResult result = AnalysisDataPackage(&cacheBuffer, cachePak);
		if (result == AnalysisResult::Success)
		{
			// 数据包解析成功，获得完整Package
			cacheBuffer.Shift(cacheBuffer.Position());

			cachePak->buffer.Seek(0);
			CustomPackage* newPak = cachePak;
			cachePak = new CustomPackage();

			std::lock_guard<SpinLock> lock(_ProcessLock);
			ProcessPakage(newPak);
		}
		else if (result == AnalysisResult::BufferAGAIN)
		{
			break;
		}
		else if (result == AnalysisResult::MagicError)
		{
			cacheBuffer.Shift(sizeof(CustomProtocolMagic));
		}
		else if (result == AnalysisResult::ChecksumError)
		{
		}
	}
	return true;
}

bool CustomTcpSession::Send(const Buffer& buffer, int ack)
{
	try
	{
		if (!buffer.Data() || buffer.Length() < 0)
			return true;

		int seq = this->seq++;
		Buffer buf(buffer);
		AddPakHeader(&buf, CustomTcpMsgHeader(seq, ack, buffer.Length()));
		return BaseClient->Send(buf);
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}
}

void CustomTcpSession::ProcessPakage(CustomPackage* newPak)
{

	if (newPak)
	{
		if (_RecvPaks.size() > 300)
		{
			CustomPackage* pak = nullptr;
			if (_RecvPaks.dequeue(pak))
				SAFE_DELETE(pak);
		}
		_RecvPaks.enqueue(newPak);
	}

	int count = 10;
	CustomPackage* pak = nullptr;
	while (_RecvPaks.front(pak) && count > 0)
	{
		if (!pak)
		{
			_RecvPaks.dequeue(pak);
			SAFE_DELETE(pak);
			continue;
		}

		switch (pak->msgType)
		{
		case 0:
		{
			if (_callbackRecvData)
			{
				try
				{
					_callbackRecvData(this, &pak->buffer);
				}
				catch (...)
				{
				}
				_RecvPaks.dequeue(pak);
				SAFE_DELETE(pak);
			}
			break;
		}
		case 1:
		{
			if (_callbackRecvRequest)
			{
				Buffer resposne;
				try
				{
					_callbackRecvRequest(this, &pak->buffer, &resposne);
				}
				catch (...)
				{
				}
				if (resposne.Length() > 0)
					Send(resposne, pak->seq);
				resposne.Release();
				_RecvPaks.dequeue(pak);
				SAFE_DELETE(pak);
			}

			break;
		}
		case 2:
		{
			std::shared_ptr<AwaitTask> task;
			if (_AwaitMap.Find(pak->ack, task) && task)
			{
				if (task->res)
				{
					task->res->response.QuoteFromBuf(pak->buffer);
					task->res->code = AwaitErrorCode::Success;
				}
				if (task->timer)
					task->timer->wake();
			}
			_RecvPaks.dequeue(pak);
			SAFE_DELETE(pak);

			break;
		}
		default:
			break;
		}

		count--;
	}
}

void CustomTcpSession::OnBindRecvDataCallBack()
{
	if (_ProcessLock.trylock())
	{
		try
		{
			ProcessPakage();
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << '\n';
		}
		_ProcessLock.unlock();
	}
}

void CustomTcpSession::OnBindSessionCloseCallBack()
{
}

Task<bool> CustomTcpSession::TryHandshake()
{
	Buffer token(CustomProtocolTryToken, sizeof(CustomProtocolTryToken) - 1);

	_handshaketimer = std::make_shared<CoTimer>(std::chrono::milliseconds(_handshaketimeOutMs));
	if (!BaseClient->Send(token))
		co_return false;

	co_await(*_handshaketimer);
	_handshaketimer.reset();
	co_return isHandshakeComplete;
}

CheckHandshakeStatus CustomTcpSession::CheckHandshakeTryMsg(Buffer& buffer)
{
	if (isHandshakeComplete)
	{
		return CheckHandshakeStatus::None;
	}

	cacheBuffer.WriteFromOtherBufferPos(buffer);

	int tokenLength = sizeof(CustomProtocolTryToken) - 1;
	if (cacheBuffer.Length() < tokenLength)
		return CheckHandshakeStatus::BufferAgain;

	if (strncmp(cacheBuffer.Byte(), CustomProtocolTryToken, tokenLength) != 0)
		return CheckHandshakeStatus::Fail;

	Buffer rsp(CustomProtocolConfirmToken, sizeof(CustomProtocolConfirmToken) - 1);
	if (!BaseClient->Send(rsp))
		return CheckHandshakeStatus::Fail;

	cacheBuffer.Shift(tokenLength);
	cacheBuffer.Seek(0);

	isHandshakeComplete = true;
	return CheckHandshakeStatus::Success;
}

CheckHandshakeStatus CustomTcpSession::CheckHandshakeConfirmMsg(Buffer& buffer)
{
	if (isHandshakeComplete)
	{
		return CheckHandshakeStatus::None;
	}

	cacheBuffer.WriteFromOtherBufferPos(buffer);

	int tokenLength = sizeof(CustomProtocolConfirmToken) - 1;
	if (cacheBuffer.Length() < tokenLength)
		return CheckHandshakeStatus::BufferAgain;

	if (strncmp(cacheBuffer.Byte(), CustomProtocolConfirmToken, tokenLength) != 0)
		return CheckHandshakeStatus::Fail;

	cacheBuffer.Shift(tokenLength);
	cacheBuffer.Seek(0);

	isHandshakeComplete = true;

	auto timer = _handshaketimer;
	if (timer)
		timer->wake();

	return CheckHandshakeStatus::Success;
}

PureTCPClient* CustomTcpSession::GetBaseClient()
{
	return (PureTCPClient*)BaseClient;
}

void CustomTcpSession::BindRecvRequestCallBack(std::function<void(BaseNetWorkSession*, Buffer* recv, Buffer* resp)> callback)
{
	_callbackRecvRequest = callback;
	OnBindRecvRequestCallBack();
}

void CustomTcpSession::OnBindRecvRequestCallBack()
{
	if (_ProcessLock.trylock())
	{
		try
		{
			ProcessPakage();
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << '\n';
		}
		_ProcessLock.unlock();
	}
}