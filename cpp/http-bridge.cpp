#include "http-bridge.h"
#include "http-bridge_generated.h"
#include <string>
#include <string.h>
#include <stdio.h>

#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
#include <Ws2tcpip.h>
#else
#include <socket.h>
#endif

namespace httpbridge
{
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	bool Startup()
	{
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
		WSADATA wsaData;
		int wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (wsa_startup != 0)
		{
			printf("WSAStartup failed: %d\n", wsa_startup);
			return false;
		}
#endif
		return true;
	}

	void Shutdown()
	{
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
		WSACleanup();
#endif
	}

	const char* VersionString(HttpVersion version)
	{
		switch (version)
		{
		case HttpVersion10: return "HTTP/1.0";
		case HttpVersion11: return "HTTP/1.1";
		case HttpVersion2: return "HTTP/2";
		default: return "HTTP/?.?";
		}
	}

	size_t Hash16B(uint64_t pair[2])
	{
		// This is the final mixing step of xxhash64
		uint64_t PRIME64_2 = 14029467366897019727ULL;
		uint64_t PRIME64_3 = 1609587929392839161ULL;
		uint64_t a = pair[0];
		a ^= a >> 33;
		a *= PRIME64_2;
		a ^= a >> 29;
		a *= PRIME64_3;
		a ^= a >> 32;
		uint64_t b = pair[1];
		b ^= b >> 33;
		b *= PRIME64_2;
		b ^= b >> 29;
		b *= PRIME64_3;
		b ^= b >> 32;
		return (size_t) (a ^ b);
	}

	template <size_t buf_size>
	void utoa(uint32_t v, char (&buf)[buf_size])
	{
		int i = 0;
		for (;;)
		{
			if (i >= (int) buf_size - 1)
				break;
			uint32_t new_v = v / 10;
			uint32_t mod_v = v % 10;
			buf[i++] = mod_v + '0';
			if (new_v == 0)
				break;
			v = new_v;
		}
		// fixup for all cases where v != 0. ie chop off the last '0', eg remove the '0' from "0123", which here will be "3210".
		//if (i != 0)
		//	i--;
		buf[i] = 0;
		i--;
		for (int j = 0; j < i; j++, i--)
		{
			char t = buf[j];
			buf[j] = buf[i];
			buf[i] = t;
		}
	}

	// TODO: Remove SleepNano if it's not used by 1.0
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
	void SleepNano(int64_t nanoseconds)
	{
		YieldProcessor();
		Sleep((DWORD) (nanoseconds / 1000000));
	}
#else
	void SleepNano(int64_t nanoseconds)
	{
		timespec t;
		t.tv_nsec = nanoseconds % 1000000000; 
		t.tv_sec = (nanoseconds - t.tv_nsec) / 1000000000;
		nanosleep(&t, nullptr);
	}
#endif

	// Read 32-bit little endian
	uint32_t Read32LE(const void* buf)
	{
		const uint8_t* b = (const uint8_t*) buf;
		return ((uint32_t) b[0]) | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
	}

	// Write 32-bit little endian
	void Write32LE(void* buf, uint32_t v)
	{
		uint8_t* b = (uint8_t*) buf;
		b[0] = v;
		b[1] = v >> 8;
		b[2] = v >> 16;
		b[3] = v >> 24;
	}

	void* Alloc(size_t size, Logger* logger, bool panicOnFail = true)
	{
		void* b = malloc(size);
		if (b == nullptr)
		{
			if (logger != nullptr)
				logger->Log("httpbridge: Out of memory, about to panic");
			if (panicOnFail)
				HTTPBRIDGE_PANIC();
		}
		return b;
	}

	void* Realloc(void* buf, size_t size, Logger* logger, bool panicOnFail = true)
	{
		void* newBuf = realloc(buf, size);
		if (newBuf == nullptr)
		{
			if (logger != nullptr)
				logger->Log("httpbridge: Out of memory, about to panic");
			if (panicOnFail)
				HTTPBRIDGE_PANIC();
			return buf;
		}
		return newBuf;
	}

	void Free(void* buf)
	{
		free(buf);
	}

	httpbridge::HttpVersion TranslateVersion(httpbridge::TxHttpVersion v)
	{
		switch (v)
		{
		case httpbridge::TxHttpVersion_Http10: return HttpVersion10;
		case httpbridge::TxHttpVersion_Http11: return HttpVersion11;
		case httpbridge::TxHttpVersion_Http2: return HttpVersion2;
		}
		HTTPBRIDGE_PANIC();
		return HttpVersion2;
	}

	httpbridge::TxHttpVersion TranslateVersion(httpbridge::HttpVersion v)
	{
		switch (v)
		{
		case HttpVersion10: return httpbridge::TxHttpVersion_Http10;
		case HttpVersion11: return httpbridge::TxHttpVersion_Http11;
		case HttpVersion2: return httpbridge::TxHttpVersion_Http2;
		}
		HTTPBRIDGE_PANIC();
		return httpbridge::TxHttpVersion_Http2;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void Logger::Log(const char* msg)
	{
		fputs(msg, stdout);
		auto len = strlen(msg);
		if (len != 0 && msg[len - 1] != '\n')
			fputs("\n", stdout);
	}

	void Logger::Logf(HTTPBRIDGE_PRINTF_FORMAT_Z const char* msg, ...)
	{
		char buf[4096];
		va_list va;
		va_start(va, msg);
#ifdef _MSC_VER
		vsnprintf_s(buf, sizeof(buf), msg, va);
#else
		vsnprintf(buf, sizeof(buf), msg, va);
#endif
		va_end(va);
		buf[sizeof(buf) - 1] = 0;
		Log(buf);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ITransport::~ITransport()
	{
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TransportTCP : public ITransport
	{
	public:
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
		typedef SOCKET socket_t;
		static const uint32_t	Infinite = INFINITE;
		static const socket_t	InvalidSocket = INVALID_SOCKET;
		static const int		ErrWOULDBLOCK = WSAEWOULDBLOCK;
		static const int		ErrTIMEOUT = WSAETIMEDOUT;
		static const int		ErrSEND_BUFFER_FULL = WSAENOBUFS;	// untested
		static const int		ErrSOCKET_ERROR = SOCKET_ERROR;
#else
		typedef int socket_t;
		static const uint32_t	Infinite			= 0xFFFFFFFF;
		static const socket_t	InvalidSocket		= (socket_t)(~0);
		static const int		ErrWOULDBLOCK		= EWOULDBLOCK;
		static const int		ErrTIMEOUT			= ETIMEDOUT;
		static const int		ErrSEND_BUFFER_FULL = EMSGSIZE;		// untested
		static const int		ErrSOCKET_ERROR		= -1;
#endif
		static const int		ChunkSize = 1048576;
		static const uint32_t	ReadTimeoutMilliseconds = 500;
		socket_t				Socket = InvalidSocket;

		virtual				~TransportTCP() override;
		virtual bool		Connect(const char* addr) override;
		virtual SendResult	Send(size_t size, const void* data, size_t& sent) override;
		virtual RecvResult	Recv(size_t maxSize, size_t& read, void* data) override;

	private:
		void			Close();
		bool			SetNonBlocking();
		bool			SetReadTimeout(uint32_t timeoutMilliseconds);
		static int		LastError();
	};

	TransportTCP::~TransportTCP()
	{
		Close();
	}

	void TransportTCP::Close()
	{
		if (Socket != InvalidSocket)
		{
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
			closesocket(Socket);
#else
			::close(Socket);
#endif
			Socket = InvalidSocket;
		}
	}

	bool TransportTCP::Connect(const char* addr)
	{
		std::string port;
		const char* colon = strrchr(addr, ':');
		if (colon != nullptr)
			port = colon + 1;
		else
			return false;

		std::string host(addr, colon - addr);

		addrinfo hints;
		addrinfo* res = nullptr;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		//hints.ai_flags = AI_V4MAPPED;
		getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
		for (addrinfo* it = res; it; it = it->ai_next)
		{
			Socket = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
			if (Socket == InvalidSocket)
				continue;

			if (it->ai_family == AF_INET6)
			{
				// allow IPv4 on this IPv6 socket
				DWORD only6 = 0;
				if (setsockopt(Socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*) &only6, sizeof(only6)))
				{
					Close();
					continue;
				}
			}

			if (connect(Socket, it->ai_addr, (int) it->ai_addrlen) == 0)
			{
				//if (!SetNonBlocking())
				//	Log->Log("Unable to set socket to non-blocking");
				if (!SetReadTimeout(ReadTimeoutMilliseconds))
				{
					Log->Log("Unable to set socket read timeout");
					Close();
					continue;
				}
				break;
			}
			else
			{
				const char* proto = it->ai_family == PF_INET6 ? "IPv6" : "IP4";
				Log->Logf("Unable to connect (%s): %d", proto, (int) LastError());
				Close();
			}
		}
		if (res)
			freeaddrinfo(res);

		return Socket != InvalidSocket;
	}

	SendResult TransportTCP::Send(size_t size, const void* data, size_t& sent)
	{
		const char* datab = (const char*) data;
		sent = 0;
		while (sent != size)
		{
			int try_send = (int) (size - sent < ChunkSize ? size - sent : ChunkSize);
			int sent_now = send(Socket, datab + sent, try_send, 0);
			if (sent_now == ErrSOCKET_ERROR)
			{
				int e = LastError();
				if (e == ErrWOULDBLOCK || e == ErrSEND_BUFFER_FULL)
				{
					// TODO: This concept here, of having the send buffer full, is untested
					return SendResult_BufferFull;
				}
				else
				{
					// TODO: test possible results here that are not in fact the socket being closed
					return SendResult_Closed;
				}
			}
			else
			{
				sent += sent_now;
			}
		}
		return SendResult_All;
	}

	RecvResult TransportTCP::Recv(size_t maxSize, size_t& bytesRead, void* data)
	{
		char* bdata = (char*) data;
		int bytesReadLocal = 0;
		while (bytesRead < maxSize)
		{
			int try_read = (int) (maxSize - bytesRead < ChunkSize ? maxSize - bytesRead : ChunkSize);
			int read_now = recv(Socket, bdata + bytesRead, try_read, 0);
			if (read_now == ErrSOCKET_ERROR)
			{
				int e = LastError();
				if (e == ErrWOULDBLOCK || e == ErrTIMEOUT)
					return RecvResult_NoData;
				// TODO: test possible error codes here - there might be some that are recoverable
				return RecvResult_Closed;
			}
			else
			{
				bytesRead += read_now;
				bytesReadLocal += read_now;
				if (read_now > 0)
					break;
			}
		}
		return bytesReadLocal ? RecvResult_Data : RecvResult_NoData;
	}

	int TransportTCP::LastError()
	{
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
		return WSAGetLastError();
#else
		return errno;
#endif
	}

	// TODO: No longer used - get rid of this if that's still true at 1.0
	bool TransportTCP::SetNonBlocking()
	{
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
		u_long mode = 1;
		return ioctlsocket(Socket, FIONBIO, &mode) == 0;
#else
		int flags = fcntl(Socket, F_GETFL, 0);
		if (flags < 0)
			return false;
		flags |= O_NONBLOCK;
		return fcntl(Socket, F_SETFL, flags) == 0;
#endif
	}

	bool TransportTCP::SetReadTimeout(uint32_t timeoutMilliseconds)
	{
#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
		uint32_t tv = timeoutMilliseconds;
		return setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char*) &tv, sizeof(tv)) == 0;
#else
		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = timeoutMilliseconds * 1000;
		return setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const void*) &tv, sizeof(tv)) == 0;
#endif
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// Cache of header pairs received. The sizes here do not include a null terminator.
	class HeaderCacheRecv
	{
	public:
		void Insert(uint16_t id, int32_t keyLen, const void* key, int32_t valLen, const void* val);
		void Get(uint16_t id, int32_t& keyLen, const void*& key, int32_t& valLen, const void*& val);		// A non-existent item will yield 0,0,0,0

	private:
		struct Item
		{
			Vector<uint8_t> Key;
			Vector<uint8_t> Value;
		};
		Vector<Item> Items;
	};

	void HeaderCacheRecv::Insert(uint16_t id, int32_t keyLen, const void* key, int32_t valLen, const void* val)
	{
		while (Items.Size() <= (int32_t) id)
			Items.Push(Item());
		Items[id].Key.Resize(keyLen);
		Items[id].Value.Resize(valLen);
		memcpy(&Items[id].Key[0], key, keyLen);
		memcpy(&Items[id].Value[0], val, valLen);
	}

	void HeaderCacheRecv::Get(uint16_t id, int32_t& keyLen, const void*& key, int32_t& valLen, const void*& val)
	{
		if (id >= Items.Size())
		{
			keyLen = 0;
			valLen = 0;
			key = nullptr;
			val = nullptr;
		}
		else
		{
			keyLen = Items[id].Key.Size();
			valLen = Items[id].Value.Size();
			key = &Items[id].Key[0];
			val = &Items[id].Value[0];
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Backend::Backend()
	{
	}

	Backend::~Backend()
	{
		Close();
		Free(RecvBuf);
	}

	Logger* Backend::AnyLog()
	{
		return Log ? Log : &NullLog;
	}

	bool Backend::Connect(const char* network, const char* addr)
	{
		ITransport* tx = nullptr;
		if (strcmp(network, "tcp") == 0)
			tx = new TransportTCP();
		else
			return false;

		if (!Connect(tx, addr))
		{
			delete tx;
			return false;
		}
		return true;
	}

	bool Backend::IsConnected()
	{
		return Transport != nullptr;
	}

	void Backend::Close()
	{
		std::vector<StreamKey> waiting;
		for (auto& item : WaitingRequests)
			waiting.push_back(item.first);

		for (auto item : waiting)
			CloseWaiting(item);

		if (WaitingBufferTotal != 0)
			HTTPBRIDGE_PANIC();

		delete Transport;
		Transport = nullptr;
		delete HeaderCacheRecv;
		HeaderCacheRecv = nullptr;
		RecvSize = 0;
	}

	bool Backend::Connect(ITransport* transport, const char* addr)
	{
		Close();
		transport->Log = Log != nullptr ? Log : &NullLog;
		if (transport->Connect(addr))
		{
			ThreadId = std::this_thread::get_id();
			Transport = transport;
			HeaderCacheRecv = new httpbridge::HeaderCacheRecv();
			return true;
		}
		return false;
	}

	SendResult Backend::Send(Response& response)
	{
		size_t offset = 0;
		size_t len = 0;
		void* buf = nullptr;
		response.Finish(len, buf);
		while (offset != len)
		{
			size_t sent = 0;
			auto res = Transport->Send(len - offset, (uint8_t*) buf + offset, sent);
			offset += sent;
			if (res == SendResult_Closed)
				return res;
		}
		return SendResult_All;
	}

	RecvResult Backend::Recv(Request& request)
	{
		RecvResult res = RecvInternal(request);
		if (res == RecvResult_Data)
		{
			StreamKey key = { request.Channel(), request.Stream() };
			auto m = WaitingRequests.find(key);
			if (m != WaitingRequests.end())
			{
				Request* canonical = m->second;
				if (request.FrameBodyOffset() + request.FrameBodyLength() > canonical->BodyLength())
				{
					AnyLog()->Log("Request sent too many body bytes");
					SendResponse(*canonical, Status400_BadRequest);
					CloseWaiting(key);
				}
				else
				{
					canonical->WriteBodyData(request.FrameBodyOffset(), request.FrameBodyLength(), request.FrameBody());
					if (request.FrameBodyOffset() + request.FrameBodyLength() == canonical->BodyLength())
					{
						std::swap(request, *canonical);
						CloseWaiting(key);
						return RecvResult_Data;
					}
				}
				return RecvResult_NoData;
			}
		}
		return res;
	}

	void Backend::ResendWhenBodyIsDone(Request& request)
	{
		if (request.IsEntireBodyInsideHeader() || !request.IsHeader())
			LogAndPanic("ResendWhenBodyIsDone called on a request in an illegal state");

		// You can't make this decision from a different thread. By the time you have
		// called ResendWhenBodyIsDone(), the backend thread will already have processed subsequent frames
		// for this request. You need to make this decision from the same thread that is calling Recv, and
		// you need to make it before calling Recv again.
		if (std::this_thread::get_id() != ThreadId)
			LogAndPanic("ResendWhenBodyIsDone called from a thread other than the Backend thread");

		StreamKey key = { request.Channel(), request.Stream() };
		if (WaitingRequests.find(key) != WaitingRequests.end())
			LogAndPanic("ResendWhenBodyIsDone called twice on the same request");

		// If we run out of memory, or blow our memory budget, then we don't inform the caller.
		// He can do nothing about it anyway.

		if (request.BodyLength() + WaitingBufferTotal > MaxWaitingBufferTotal)
		{
			AnyLog()->Log("MaxWaitingBufferTotal exceeded");
			SendResponse(request, Status503_ServiceUnavailable);
			return;
		}

		if (!request.ReallocForEntireBody())
		{
			AnyLog()->Log("ReallocForEntireBody failed");
			SendResponse(request, Status503_ServiceUnavailable);
			return;
		}

		WaitingBufferTotal += request.BodyLength();

		Request* copy = new Request();
		std::swap(*copy, request);
		WaitingRequests[key] = copy;
	}

	RecvResult Backend::RecvInternal(Request& request)
	{
		if (Transport == nullptr)
			return RecvResult_Closed;

		// Grow the receive buffer
		const size_t initialBufSize = 65536;
		const size_t maxBufSize = 1024 * 1024;
		if (RecvBufCapacity - RecvSize == 0)
		{
			if (RecvBufCapacity >= maxBufSize)
			{
				AnyLog()->Logf("Server is trying to send us a frame larger than 1MB. Closing connection.");
				Close();
				return RecvResult_Closed;
			}
			RecvBufCapacity = RecvBufCapacity == 0 ? initialBufSize : RecvBufCapacity * 2;
			RecvBuf = (uint8_t*) Realloc(RecvBuf, RecvBufCapacity, Log);
		}

		// Read
		size_t read = 0;
		auto result = Transport->Recv(RecvBufCapacity - RecvSize, read, RecvBuf + RecvSize);
		if (result == RecvResult_Closed)
		{
			AnyLog()->Logf("Server closed connection");
			Close();
			return result;
		}

		// Process frame
		RecvSize += read;
		if (RecvSize >= 4)
		{
			uint32_t frameSize = Read32LE(RecvBuf);
			if (RecvSize >= frameSize)
			{
				// We have a frame to process.
				const TxFrame* frame = GetTxFrame((uint8_t*) RecvBuf + 4);
				if (frame->frametype() == TxFrameType_Header)
				{
					UnpackHeader(frame, request);
				}
				else if (frame->frametype() == TxFrameType_Body)
				{
					UnpackBody(frame, request);
				}
				else
				{
					AnyLog()->Logf("Unrecognized frame type %d. Closing connection.", (int) frame->frametype());
					Close();
					return RecvResult_Closed;
				}
				// TODO: get rid of this expensive move by using a circular buffer. AHEM.. one can't use a circular buffer with FlatBuffers.
				// But maybe we use something like flip/flopping buffers. ie two buffers that we alternate between.
				memmove(RecvBuf, RecvBuf + 4 + frameSize, RecvSize - frameSize - 4);
				RecvSize -= frameSize + 4;
				return RecvResult_Data;
			}
		}

		return RecvResult_NoData;
	}

	void Backend::UnpackHeader(const TxFrame* frame, Request& request)
	{
		auto headers = frame->headers();
		size_t headerBlockSize = TotalHeaderBlockSize(frame);
		uint8_t* hblock = (uint8_t*) Alloc(headerBlockSize, Log);

		auto lines = (Request::HeaderLine*) hblock;								// header lines
		int32_t hpos = (headers->size() + 1) * sizeof(Request::HeaderLine);		// offset into hblock

		for (uint32_t i = 0; i < headers->size(); i++)
		{
			const TxHeaderLine* line = headers->Get(i);
			int32_t keyLen, valueLen;
			const void *key, *value;
			if (line->id() != 0)
			{
				if (line->key()->size() != 0)
					HeaderCacheRecv->Insert(line->id(), line->key()->size(), line->key()->Data(), line->value()->size(), line->value()->Data());
				HeaderCacheRecv->Get(line->id(), keyLen, key, valueLen, value);
			}
			else
			{
				keyLen = line->key()->size();
				key = line->key()->Data();
				valueLen = line->value()->size();
				value = line->value()->Data();
			}

			memcpy(hblock + hpos, key, keyLen);
			hblock[hpos + keyLen] = 0;
			lines[i].KeyStart = hpos;
			lines[i].KeyLen = keyLen;
			hpos += keyLen + 1;
			memcpy(hblock + hpos, value, valueLen);
			hblock[hpos + valueLen] = 0;
			hpos += valueLen + 1;
		}
		// add terminal HeaderLine
		lines[headers->size()].KeyStart = hpos;
		lines[headers->size()].KeyLen = 0;

		request.InitHeader(this, TranslateVersion(frame->version()), frame->channel(), frame->stream(), frame->body_total_length(), headers->size(), hblock);
	}

	void Backend::UnpackBody(const TxFrame* frame, Request& request)
	{
		void* body = Alloc(frame->body()->size(), Log);
		memcpy(body, frame->body()->Data(), frame->body()->size());
		request.InitBody(this, TranslateVersion(frame->version()), frame->channel(), frame->stream(), frame->body_total_length(), frame->body_offset(), frame->body()->size(), body);
	}

	size_t Backend::TotalHeaderBlockSize(const TxFrame* frame)
	{
		// the +1 is for the terminal HeaderLine
		size_t total = sizeof(Request::HeaderLine) * (frame->headers()->size() + 1);

		for (uint32_t i = 0; i < frame->headers()->size(); i++)
		{
			const TxHeaderLine* line = frame->headers()->Get(i);
			if (line->key()->size() != 0)
			{
				total += line->key()->size() + line->value()->size();
			}
			else
			{
				int keyLen = 0;
				int valLen = 0;
				const void *key = nullptr, *val = nullptr;
				HeaderCacheRecv->Get(line->id(), keyLen, key, valLen, val);
				total += keyLen + valLen;
			}
			// +2 for the null terminators
			total += 2;
		}
		return total;
	}

	void Backend::LogAndPanic(const char* msg)
	{
		AnyLog()->Log(msg);
		HTTPBRIDGE_PANIC();
	}

	void Backend::SendResponse(const Request& request, StatusCode status)
	{
		Response response;
		response.Init(request);
		response.Status = status;
		Send(response);
	}

	void Backend::CloseWaiting(StreamKey key)
	{
		Request* request = WaitingRequests[key];
		
		if (WaitingBufferTotal < request->BodyLength())
			LogAndPanic("WaitingBufferTotal underflow");
		WaitingBufferTotal -= request->BodyLength();

		WaitingRequests.erase(key);
		delete request;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Request::Request()
	{
	}

	Request::~Request()
	{
		Free((void*) HeaderBlock);
		Free((void*) _FrameBody);
	}

	void Request::InitHeader(httpbridge::Backend* backend, HttpVersion version, uint64_t channel, uint64_t stream, uint64_t bodyTotalLength, int32_t headerCount, const void* headerBlock)
	{
		_IsHeader = true;
		_Version = version;
		_Channel = channel;
		_Stream = stream;
		_BodyLength = bodyTotalLength;
		_HeaderCount = headerCount;
		HeaderBlock = (const uint8_t*) headerBlock;
	}

	void Request::InitBody(httpbridge::Backend* backend, HttpVersion version, uint64_t channel, uint64_t stream, uint64_t bodyTotalLength, uint64_t bodyOffset, uint64_t bodyBytes, const void* body)
	{
		_IsHeader = false;
		_Version = version;
		_Channel = channel;
		_Stream = stream;
		_BodyLength = bodyTotalLength;
		_FrameBodyOffset = bodyOffset;
		_FrameBodyLength = bodyBytes;
		_FrameBody = body;
	}

	HttpVersion Request::Version() const
	{
		return _Version;
	}

	const char* Request::Method() const
	{
		if (!IsHeader())
			return nullptr;
		// actually the 'key' of header[0]
		auto lines = (const HeaderLine*) HeaderBlock;
		return (const char*) (HeaderBlock + lines[0].KeyStart);
	}

	const char* Request::URI() const
	{
		if (!IsHeader())
			return nullptr;
		// actually the 'value' of header[0]
		auto lines = (const HeaderLine*) HeaderBlock;
		return (const char*) (HeaderBlock + lines[0].KeyStart + lines[0].KeyLen + 1);
	}

	bool Request::HeaderByName(const char* name, size_t& len, const void*& buf) const
	{
		auto nameLen = strlen(name);
		auto count = HeaderCount() + NumPseudoHeaderLines;
		auto lines = (const HeaderLine*) HeaderBlock;
		for (int32_t i = NumPseudoHeaderLines; i < count; i++)
		{
			const uint8_t* bKey = HeaderBlock + lines[i].KeyStart;
			if (memcmp(bKey, name, nameLen) == 0)
			{
				buf = HeaderBlock + (lines[i].KeyStart + lines[i].KeyLen);
				len = lines[i + 1].KeyStart - (lines[i].KeyStart + lines[i].KeyLen);
				return true;
			}
		}
		return false;
	}

	const char* Request::HeaderByName(const char* name) const
	{
		size_t len;
		const void* buf;
		if (HeaderByName(name, len, buf))
			return (const char*) buf;
		else
			return nullptr;
	}

	void Request::HeaderAt(int32_t index, int32_t& keyLen, const void*& key, int32_t& valLen, const void*& val) const
	{
		if ((uint32_t) index >= (uint32_t) HeaderCount())
		{
			keyLen = 0;
			valLen = 0;
			key = nullptr;
			val = nullptr;
		}
		else
		{
			index += NumPseudoHeaderLines;
			auto lines = (const HeaderLine*) HeaderBlock;
			keyLen = lines[index].KeyLen;
			key = HeaderBlock + lines[index].KeyStart;
			valLen = lines[index + 1].KeyStart - (lines[index].KeyStart + lines[index].KeyLen);
			val = HeaderBlock + lines[index].KeyStart + lines[index].KeyLen + 1;
		}
	}

	void Request::HeaderAt(int32_t index, const char*& key, const char*& val) const
	{
		int32_t keyLen, valLen;
		HeaderAt(index, keyLen, (const void*&) key, valLen, (const void*&) val);
	}

	void Request::ResendWhenBodyIsDone()
	{
		_Backend->ResendWhenBodyIsDone(*this);
	}

	bool Request::IsEntireBodyInsideHeader() const
	{
		return IsHeader() && BodyLength() == FrameBodyLength();
	}

	void Request::WriteBodyData(size_t offset, size_t len, const void* data)
	{
		if ((uint64_t) offset + (uint64_t) len > _BodyLength)
			HTTPBRIDGE_PANIC();

		memcpy((uint8_t*) _FrameBody + offset, data, len);
	}

	bool Request::ReallocForEntireBody()
	{
		void* newBody = Realloc((void*) _FrameBody, BodyLength(), _Backend->Log, false);
		if (newBody == nullptr)
			return false;
		_FrameBody = newBody;
		return true;
	}

	/*
	BodyReader* Request::BodyReader()
	{
		if (_BodyReader == nullptr)
			_BodyReader = new httpbridge::BodyReader(*this);
		return _BodyReader;
	}
	*/

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef HTTPBRIDGE_A_BAD_IDEA
	BodyReader::BodyReader(const Request& request)
	{

	}

	void BodyReader::Send(Request& bodyFrame)
	{

	}
#endif

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static_assert(sizeof(Response::ByteVectorOffset) == sizeof(flatbuffers::Offset<flatbuffers::Vector<uint8_t>>), "Assumed flatbuffers offset size is wrong");

	Response::Response()
	{
	}

	Response::~Response()
	{
		if (FBB != nullptr)
			delete FBB;
		//if (Frame != nullptr)
		//{
		//	delete &Frame->fbb_;
		//	delete Frame;
		//}
	}

	void Response::Init(const Request& request)
	{
		Version = request.Version();
		Stream = request.Stream();
		Channel = request.Channel();
	}
	
	void Response::WriteHeader(const char* key, const char* value)
	{
		WriteHeader(strlen(key), key, strlen(value), value);
	}

	void Response::WriteHeader(size_t keyLen, const char* key, size_t valLen, const char* value)
	{
		// Ensure that our uint32_t casts down below are safe
		if (keyLen > 1024*1024 || valLen > 1024*1024)
			HTTPBRIDGE_PANIC();

		HeaderIndex.Push(HeaderBuf.Size());
		memcpy(HeaderBuf.AddSpace((uint32_t) keyLen), key, keyLen);
		
		HeaderIndex.Push(HeaderBuf.Size());
		memcpy(HeaderBuf.AddSpace((uint32_t) valLen), value, valLen);
	}

	void Response::SetBody(size_t len, const void* body)
	{
		// Ensure sanity, as well as safety because BodyLength is uint32
		if (len > 1024*1024*1024)
			HTTPBRIDGE_PANIC();

		CreateBuilder();
		BodyOffset = (ByteVectorOffset) FBB->CreateVector((const uint8_t*) body, len).o;
		BodyLength = (uint32_t) len;
		//Frame->add_body(Frame->fbb_.CreateVector((const uint8_t*) body, len));
		//Frame->add_body_offset(0);
		//Frame->add_body_total_length(len);
	}

	void Response::Finish(size_t& len, void*& buf)
	{
		CreateBuilder();

		std::vector<flatbuffers::Offset<TxHeaderLine>> lines;
		{
			char statusStr[4];
			utoa((uint32_t) Status, statusStr);
			auto key = FBB->CreateVector((const uint8_t*) statusStr, 3);
			auto line = CreateTxHeaderLine(*FBB, key, 0, 0);
			lines.push_back(line);
		}

		HeaderIndex.Push(HeaderBuf.Size()); // add a terminator

		for (int32_t i = 0; i < HeaderIndex.Size() - 2; i += 2)
		{
			uint32_t keyLen = HeaderIndex[i + 1] - HeaderIndex[i];
			uint32_t valLen = HeaderIndex[i + 2] - HeaderIndex[i + 1];
			auto key = FBB->CreateVector((const uint8_t*) &HeaderBuf[HeaderIndex[i]], keyLen);
			auto val = FBB->CreateVector((const uint8_t*) &HeaderBuf[HeaderIndex[i + 1]], valLen);
			auto line = CreateTxHeaderLine(*FBB, key, val, 0);
			lines.push_back(line);
		}
		auto linesVector = FBB->CreateVector(lines);

		TxFrameBuilder frame(*FBB);
		frame.add_body(BodyOffset);
		frame.add_body_offset(0);
		frame.add_body_total_length(BodyLength);
		frame.add_headers(linesVector);
		frame.add_channel(Channel);
		frame.add_stream(Stream);
		frame.add_frametype(TxFrameType_Header);
		frame.add_version(TranslateVersion(Version));
		auto root = frame.Finish();
		FinishTxFrameBuffer(*FBB, root);
		len = FBB->GetSize();
		buf = FBB->GetBufferPointer();
		
		// Hack the FBB to write our frame size at the start of the buffer
		// Right here 'len' contains the size of the flatbuffer, which is our "frame size"
		byte frame_size[4];
		Write32LE(frame_size, (uint32_t) len);
		FBB->PushBytes(frame_size, 4);
		len = FBB->GetSize();
		buf = FBB->GetBufferPointer();
		// Now 'len' contains the size of the flatbuffer + 4
	}

	void Response::CreateBuilder()
	{
		//if (Frame == nullptr)
		//{
		//	auto builder = new flatbuffers::FlatBufferBuilder();
		//	Frame = new TxFrameBuilder(*builder);
		//}
		if (FBB == nullptr)
		{
			FBB = new flatbuffers::FlatBufferBuilder();
		}
	}

}
