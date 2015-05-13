#include "http-bridge.h"
#include "http-bridge_generated.h"
#include <string>
#include <string.h>
#include <stdio.h>

#ifdef HTTPBRIDGE_PLATFORM_WINDOWS
#include <Ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h> // #TODO Get rid of this at 1.0 if we no longer set sockets to non-blocking
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace hb
{
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// These are arbitrary maximums, chosen for sanity, and to fit within a signed 32-bit int
	static const int MaxHeaderKeyLen = 1024;
	static const int MaxHeaderValueLen = 1024 * 1024;

	void PanicMsg(const char* file, int line, const char* msg)
	{
		fprintf(stderr, "httpbridge panic %s:%d: %s\n", file, line, msg);
	}

	HTTPBRIDGE_NORETURN_PREFIX void BuiltinTrap()
	{
#ifdef _MSC_VER
		__debugbreak();
#else
		__builtin_trap();
#endif
	}

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

	const char*	StatusString(StatusCode status)
	{
		switch (status)
		{
		case Status100_Continue:                        return "Continue";
		case Status101_Switching_Protocols:             return "Switching Protocols";
		case Status102_Processing:                      return "Processing";
		case Status200_OK:                              return "OK";
		case Status201_Created:                         return "Created";
		case Status202_Accepted:                        return "Accepted";
		case Status203_Non_Authoritative_Information:   return "Non-Authoritative Information";
		case Status204_No_Content:                      return "No Content";
		case Status205_Reset_Content:                   return "Reset Content";
		case Status206_Partial_Content:                 return "Partial Content";
		case Status207_Multi_Status:                    return "Multi-Status";
		case Status208_Already_Reported:                return "Already Reported";
		case Status226_IM_Used:                         return "IM Used";
		case Status300_Multiple_Choices:                return "Multiple Choices";
		case Status301_Moved_Permanently:               return "Moved Permanently";
		case Status302_Found:                           return "Found";
		case Status303_See_Other:                       return "See Other";
		case Status304_Not_Modified:                    return "Not Modified";
		case Status305_Use_Proxy:                       return "Use Proxy";
		case Status307_Temporary_Redirect:              return "Temporary Redirect";
		case Status308_Permanent_Redirect:              return "Permanent Redirect";
		case Status400_Bad_Request:                     return "Bad Request";
		case Status401_Unauthorized:                    return "Unauthorized";
		case Status402_Payment_Required:                return "Payment Required";
		case Status403_Forbidden:                       return "Forbidden";
		case Status404_Not_Found:                       return "Not Found";
		case Status405_Method_Not_Allowed:              return "Method Not Allowed";
		case Status406_Not_Acceptable:                  return "Not Acceptable";
		case Status407_Proxy_Authentication_Required:   return "Proxy Authentication Required";
		case Status408_Request_Timeout:                 return "Request Timeout";
		case Status409_Conflict:                        return "Conflict";
		case Status410_Gone:                            return "Gone";
		case Status411_Length_Required:                 return "Length Required";
		case Status412_Precondition_Failed:             return "Precondition Failed";
		case Status413_Payload_Too_Large:               return "Payload Too Large";
		case Status414_URI_Too_Long:                    return "URI Too Long";
		case Status415_Unsupported_Media_Type:          return "Unsupported Media Type";
		case Status416_Range_Not_Satisfiable:           return "Range Not Satisfiable";
		case Status417_Expectation_Failed:              return "Expectation Failed";
		case Status421_Misdirected_Request:             return "Misdirected Request";
		case Status422_Unprocessable_Entity:            return "Unprocessable Entity";
		case Status423_Locked:                          return "Locked";
		case Status424_Failed_Dependency:               return "Failed Dependency";
		case Status425_Unassigned:                      return "Unassigned";
		case Status426_Upgrade_Required:                return "Upgrade Required";
		case Status427_Unassigned:                      return "Unassigned";
		case Status428_Precondition_Required:           return "Precondition Required";
		case Status429_Too_Many_Requests:               return "Too Many Requests";
		case Status430_Unassigned:                      return "Unassigned";
		case Status431_Request_Header_Fields_Too_Large: return "Request Header Fields Too Large";
		case Status500_Internal_Server_Error:           return "Internal Server Error";
		case Status501_Not_Implemented:                 return "Not Implemented";
		case Status502_Bad_Gateway:                     return "Bad Gateway";
		case Status503_Service_Unavailable:             return "Service Unavailable";
		case Status504_Gateway_Timeout:                 return "Gateway Timeout";
		case Status505_HTTP_Version_Not_Supported:      return "HTTP Version Not Supported";
		case Status506_Variant_Also_Negotiates:         return "Variant Also Negotiates";
		case Status507_Insufficient_Storage:            return "Insufficient Storage";
		case Status508_Loop_Detected:                   return "Loop Detected";
		case Status509_Unassigned:                      return "Unassigned";
		case Status510_Not_Extended:                    return "Not Extended";
		case Status511_Network_Authentication_Required: return "Network Authentication Required";
		}
		return "Unrecognized HTTP Status Code";
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

	// Returns the length of the string, excluding the null terminator
	int U32toa(uint32_t v, char* buf, size_t buf_size)
	{
		int i = 0;
		for (;;)
		{
			if (i >= (int) buf_size - 1)
				break;
			uint32_t new_v = v / 10;
			char mod_v = (char) (v % 10);
			buf[i++] = mod_v + '0';
			if (new_v == 0)
				break;
			v = new_v;
		}
		buf[i] = 0;
		i--;
		for (int j = 0; j < i; j++, i--)
		{
			char t = buf[j];
			buf[j] = buf[i];
			buf[i] = t;
		}
		return i + 1;
	}

	// Returns the length of the string, excluding the null terminator
	int U64toa(uint64_t v, char* buf, size_t buf_size)
	{
		if (v <= 4294967295u)
			return U32toa((uint32_t) v, buf, buf_size);
		int i = 0;
		for (;;)
		{
			if (i >= (int) buf_size - 1)
				break;
			uint64_t new_v = v / 10;
			char mod_v = (char) (v % 10);
			buf[i++] = mod_v + '0';
			if (new_v == 0)
				break;
			v = new_v;
		}
		buf[i] = 0;
		i--;
		for (int j = 0; j < i; j++, i--)
		{
			char t = buf[j];
			buf[j] = buf[i];
			buf[i] = t;
		}
		return i + 1;
	}

	template <size_t buf_size>
	int u32toa(uint32_t v, char(&buf)[buf_size])
	{
		return U32toa(v, buf, buf_size);
	}

	template <size_t buf_size>
	int u64toa(uint64_t v, char(&buf)[buf_size])
	{
		return U64toa(v, buf, buf_size);
	}

	uint64_t uatoi64(const char* s, size_t len)
	{
		uint64_t v = 0;
		for (int i = 0; i < len; i++)
			v = v * 10 + (s[i] - '0');
		return v;
	}

	uint64_t uatoi64(const char* s)
	{
		uint64_t v = 0;
		for (int i = 0; s[i]; i++)
			v = v * 10 + (s[i] - '0');
		return v;
	}

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

	void* Alloc(size_t size, Logger* logger, bool panicOnFail)
	{
		void* b = malloc(size);
		if (b == nullptr)
		{
			if (logger != nullptr)
				logger->Logf("Out of memory allocating %llu bytes", (uint64_t) size);
			if (panicOnFail)
				HTTPBRIDGE_PANIC("Out of memory (alloc)");
		}
		return b;
	}

	void* Realloc(void* buf, size_t size, Logger* logger, bool panicOnFail)
	{
		void* newBuf = realloc(buf, size);
		if (newBuf == nullptr)
		{
			if (logger != nullptr)
				logger->Logf("Out of memory allocating %llu bytes", (uint64_t) size);
			if (panicOnFail)
				HTTPBRIDGE_PANIC("Out of memory (realloc)");
			return buf;
		}
		return newBuf;
	}

	void Free(void* buf)
	{
		free(buf);
	}

	template<typename T> T min(T a, T b) { return a < b ? a : b; }
	template<typename T> T max(T a, T b) { return a < b ? b : a; }

	static void BufWrite(uint8_t*& buf, const void* src, size_t len)
	{
		memcpy(buf, src, len);
		buf += len;
	}

	hb::HttpVersion TranslateVersion(httpbridge::TxHttpVersion v)
	{
		switch (v)
		{
		case httpbridge::TxHttpVersion_Http10: return HttpVersion10;
		case httpbridge::TxHttpVersion_Http11: return HttpVersion11;
		case httpbridge::TxHttpVersion_Http2: return HttpVersion2;
		}
		HTTPBRIDGE_PANIC("Unknown TxHttpVersion");
		return HttpVersion2;
	}

	httpbridge::TxHttpVersion TranslateVersion(hb::HttpVersion v)
	{
		switch (v)
		{
		case HttpVersion10: return httpbridge::TxHttpVersion_Http10;
		case HttpVersion11: return httpbridge::TxHttpVersion_Http11;
		case HttpVersion2: return httpbridge::TxHttpVersion_Http2;
		}
		HTTPBRIDGE_PANIC("Unknown HttpVersion");
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
		typedef DWORD setsockopt_t;
		static const uint32_t	Infinite = INFINITE;
		static const socket_t	InvalidSocket = INVALID_SOCKET;
		static const int		ErrWOULDBLOCK = WSAEWOULDBLOCK;
		static const int		ErrTIMEOUT = WSAETIMEDOUT;
		static const int		ErrSEND_BUFFER_FULL = WSAENOBUFS;	// untested
		static const int		ErrSOCKET_ERROR = SOCKET_ERROR;
#else
		typedef int socket_t;
		typedef int setsockopt_t;
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
				setsockopt_t only6 = 0;
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
					// #TODO: This concept here, of having the send buffer full, is untested
					return SendResult_BufferFull;
				}
				else
				{
					// #TODO: test possible results here that are not in fact the socket being closed
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
				// #TODO: test possible error codes here - there might be some that are recoverable
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

	// #TODO: No longer used - get rid of this if that's still true at 1.0
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

	Buffer::Buffer()
	{
	}

	Buffer::~Buffer()
	{
		Free(Data);
	}
	
	uint8_t* Buffer::Preallocate(size_t n)
	{
		while (Count + n > Capacity)
			GrowCapacityOrPanic();
		return Data + Count;
	}

	void Buffer::EraseFromStart(size_t n)
	{
		memmove(Data, Data + n, Count - n);
	}

	void Buffer::Write(const void* buf, size_t n)
	{
		while (Count + n > Capacity)
			GrowCapacityOrPanic();
		memcpy(Data + Count, buf, n);
		Count += n;
	}
	
	bool Buffer::TryWrite(const void* buf, size_t n)
	{
		while (Count + n > Capacity)
		{
			if (!TryGrowCapacity())
				return false;
		}
		memcpy(Data + Count, buf, n);
		Count += n;
		return true;
	}

	void Buffer::WriteStr(const char* s)
	{
		Write(s, strlen(s));
	}

	void Buffer::WriteUInt64(uint64_t v)
	{
		// 18446744073709551615 (max uint64) is 20 characters, and U64toa insists on adding a null terminator
		while (Count + 21 > Capacity)
			GrowCapacityOrPanic();
		Count += U64toa(v, (char*) (Data + Count), 21);
	}

	void Buffer::GrowCapacityOrPanic()
	{
		Capacity = Capacity == 0 ? 64 : Capacity * 2;
		Data = (uint8_t*) Realloc(Data, Capacity, nullptr);
	}

	bool Buffer::TryGrowCapacity()
	{
		auto newCapacity = Capacity == 0 ? 64 : Capacity * 2;
		auto newData = (uint8_t*) Realloc(Data, newCapacity, nullptr, false);
		if (newData == nullptr)
		{
			return false;
		}
		else
		{
			Capacity = newCapacity;
			Data = newData;
			return true;
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	InFrame::InFrame()
	{
		IsHeader = false;
		IsLast = false;
		IsAborted = false;
		Request = nullptr;
		Reset();
	}

	InFrame::~InFrame()
	{
		Reset();
	}

	void InFrame::Reset()
	{
		if (Request != nullptr && !Request->BodyBuffer.IsPointerInside(BodyBytes))
			Free(BodyBytes);
		
		if (IsAborted)
		{
			Request->DecrementLiveness();
			Request->DecrementLiveness();
		}
		else if (IsLast)
		{
			Request->DecrementLiveness();
		}
		
		// Note that Request can have destroyed itself by this point, so nullptr is the only legal value for it now

		Request = nullptr;
		IsHeader = false;
		IsLast = false;
		IsAborted = false;

		BodyBytes = nullptr;
		BodyBytesLen = 0;
	}

	bool InFrame::ResendWhenBodyIsDone()
	{
		return Request->Backend->ResendWhenBodyIsDone(*this);
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
		// Pull items out of CurrentRequests hash map, because we can't iterate over them while deleting them
		std::vector<Request*> waiting;
		for (auto& item : CurrentRequests)
			waiting.push_back(item.second);

		for (auto item : waiting)
		{
			while (!item->DecrementLiveness()) {}
		}

		HTTPBRIDGE_ASSERT(BufferedRequestsTotalBytes == 0);

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
			HeaderCacheRecv = new hb::HeaderCacheRecv();
			return true;
		}
		return false;
	}

	SendResult Backend::Send(Response& response)
	{
		// TODO: Only if this is the last response frame, THEN decrement liveness (ie once we support streaming responses out)
		Request* req = GetRequestOrDie(response.Channel, response.Stream);
		req->DecrementLiveness();

		size_t offset = 0;
		size_t len = 0;
		void* buf = nullptr;
		response.FinishFlatbuffer(len, buf);
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

	bool Backend::Recv(InFrame& frame)
	{
		frame.Reset();

		RecvResult res = RecvInternal(frame);
		if (res != RecvResult_Data)
			return false;

		StreamKey streamKey = MakeStreamKey(*frame.Request);

		if (frame.IsHeader)
		{
			CurrentRequests[streamKey] = frame.Request;

			if (frame.Request->BodyLength == 0)
			{
				HTTPBRIDGE_ASSERT(frame.IsLast);
				//frame.Request->Status = RequestStatus_Received;
				frame.Request->IsBuffered = true;
				frame.Request->BodyBuffer.Data = frame.BodyBytes;
				frame.Request->BodyBuffer.Capacity = frame.BodyBytesLen;
				frame.Request->BodyBuffer.Count = frame.BodyBytesLen;
				BufferedRequestsTotalBytes += frame.BodyBytesLen;
				return true;
			}

			if (frame.Request->BodyLength <= MaxAutoBufferSize)
			{
				// Automatically place requests into the 'ResendWhenBodyIsDone' queue
				if (ResendWhenBodyIsDone(frame))
					return true;
				frame.Reset();
				return false;
			}

			return true;
		}

		return true;
	}

	bool Backend::ResendWhenBodyIsDone(InFrame& frame)
	{
		Request* request = frame.Request;

		if (!frame.IsHeader || frame.IsLast || request->BodyLength == 0)
			LogAndPanic("ResendWhenBodyIsDone may only be called on the first frame of a request (the header frame), with a non-zero body length");

		// You can't make this decision from a different thread. By the time you have
		// called ResendWhenBodyIsDone(), the backend thread will already have processed subsequent frames
		// for this request. You need to make this decision from the same thread that is calling Recv, and
		// you need to make it before calling Recv again.
		// #TODO: Why is this here? Surely ALL calls to Backend need to be made from the same thread???
		// Reply: Probably not. It seems like a very desirable property of Backend's API that we can dispatch
		// requests off to multiple threads, and that those threads can send their replies whenever they're
		// done, instead of pushing the synchronization burden onto the user of the API.
		if (std::this_thread::get_id() != ThreadId)
			LogAndPanic("ResendWhenBodyIsDone called from a thread other than the Backend thread");

		StreamKey key = MakeStreamKey(*request);
		if (CurrentRequests.find(key) == CurrentRequests.end())
			LogAndPanic("ResendWhenBodyIsDone called on an unknown request");

		size_t initialSize = min(InitialBufferSize, (size_t) request->BodyLength);
		initialSize = max(initialSize, frame.BodyBytesLen);
		initialSize = max(initialSize, (size_t) 16);

		if (initialSize + (uint64_t) BufferedRequestsTotalBytes > (uint64_t) MaxWaitingBufferTotal)
		{
			AnyLog()->Log("MaxWaitingBufferTotal exceeded");
			SendResponse(*request, Status503_Service_Unavailable);
			return false;
		}

		request->BodyBuffer.Data = (uint8_t*) Alloc(initialSize, AnyLog(), false);
		if (request->BodyBuffer.Data == nullptr)
		{
			AnyLog()->Log("Alloc for request buffer failed");
			SendResponse(*request, Status503_Service_Unavailable);
			return false;
		}

		memcpy(request->BodyBuffer.Data, frame.BodyBytes, frame.BodyBytesLen);
		request->BodyBuffer.Count = frame.BodyBytesLen;
		request->BodyBuffer.Capacity = initialSize;
		Free(frame.BodyBytes);
		frame.BodyBytes = request->BodyBuffer.Data;

		BufferedRequestsTotalBytes += (size_t) request->BodyBuffer.Capacity;

		request->IsBuffered = true;
		return true;
	}

	void Backend::RequestDestroyed(const StreamKey& key)
	{
		auto cr = CurrentRequests.find(key);
		HTTPBRIDGE_ASSERT(cr != CurrentRequests.end());
		Request* request = cr->second;

		if (request->IsBuffered)
		{
			if (BufferedRequestsTotalBytes < request->BodyBuffer.Capacity)
				LogAndPanic("BufferedRequestsTotalBytes underflow");
			BufferedRequestsTotalBytes -= (size_t) request->BodyBuffer.Capacity;
		}

		CurrentRequests.erase(key);
	}

	RecvResult Backend::RecvInternal(InFrame& inframe)
	{
		if (Transport == nullptr)
			return RecvResult_Closed;

		// Grow the receive buffer. We don't expect a frame to be more than 1MB.
		const size_t initialBufSize = 65536;
		const size_t maxBufSize = 1024 * 1024;	// maxBufSize must be a power of 2, otherwise the overflow condition below will frequently be triggered
		if (RecvBufCapacity - RecvSize == 0)
		{
			if (RecvBufCapacity >= maxBufSize)
			{
				AnyLog()->Logf("Server is trying to send us a frame larger than %d bytes. Closing connection.", (int) maxBufSize);
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
				const httpbridge::TxFrame* txframe = httpbridge::GetTxFrame((uint8_t*) RecvBuf + 4);
				bool goodFrame = true;
				if (txframe->frametype() == httpbridge::TxFrameType_Header)
				{
					UnpackHeader(txframe, inframe);
					inframe.IsHeader = true;
					inframe.IsLast = !!(txframe->flags() & httpbridge::TxFrameFlags_Final);
					goodFrame = goodFrame && UnpackBody(txframe, inframe);
				}
				else if (txframe->frametype() == httpbridge::TxFrameType_Body)
				{
					goodFrame = goodFrame && UnpackBody(txframe, inframe);
					inframe.IsLast = !!(txframe->flags() & httpbridge::TxFrameFlags_Final);
				}
				else if (txframe->frametype() == httpbridge::TxFrameType_Abort)
				{
					inframe.IsAborted = true;
				}
				else
				{
					AnyLog()->Logf("Unrecognized frame type %d. Closing connection.", (int) txframe->frametype());
					Close();
					return RecvResult_Closed;
				}
				// #TODO: get rid of this expensive move by using a circular buffer. AHEM.. one can't use a circular buffer with FlatBuffers.
				// But maybe we use something like flip/flopping buffers. ie two buffers that we alternate between.
				memmove(RecvBuf, RecvBuf + 4 + frameSize, RecvSize - frameSize - 4);
				RecvSize -= frameSize + 4;
				if (!goodFrame)
				{
					inframe.Reset();
					return RecvResult_NoData;
				}
				return RecvResult_Data;
			}
		}

		return RecvResult_NoData;
	}

	void Backend::UnpackHeader(const httpbridge::TxFrame* txframe, InFrame& inframe)
	{
		auto headers = txframe->headers();
		size_t headerBlockSize = TotalHeaderBlockSize(txframe);
		uint8_t* hblock = (uint8_t*) Alloc(headerBlockSize, Log);

		auto lines = (Request::HeaderLine*) hblock;								// header lines
		int32_t hpos = (headers->size() + 1) * sizeof(Request::HeaderLine);		// offset into hblock

		for (uint32_t i = 0; i < headers->size(); i++)
		{
			const httpbridge::TxHeaderLine* line = headers->Get(i);
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

		inframe.Request = new hb::Request();
		inframe.Request->Initialize(this, TranslateVersion(txframe->version()), txframe->channel(), txframe->stream(), headers->size(), hblock);
	}

	bool Backend::UnpackBody(const httpbridge::TxFrame* txframe, InFrame& inframe)
	{
		if (inframe.Request == nullptr)
		{
			auto cr = CurrentRequests.find(MakeStreamKey(txframe->channel(), txframe->stream()));
			if (cr == CurrentRequests.end())
			{
				AnyLog()->Logf("Received body bytes for unknown stream [%lld:%lld]", txframe->channel(), txframe->stream());
				return false;
			}
			inframe.Request = cr->second;
		}

		// Empty body frames are a waste, but not an error. Likely to be the final frame.
		if (txframe->body() == nullptr)
			return true;

		if (inframe.Request->IsBuffered)
		{
			Buffer& buf = inframe.Request->BodyBuffer;
			if (buf.Count + inframe.BodyBytesLen > inframe.Request->BodyLength)
			{
				AnyLog()->Log("Request sent too many body bytes. Ignoring frame");
				return false;
			}
			// Assume that a buffer realloc is going to grow by buf.Capacity (ie 2x growth).
			if (buf.Count + txframe->body()->size() > buf.Capacity && BufferedRequestsTotalBytes + buf.Capacity > MaxWaitingBufferTotal)
			{
				// TODO: same as below - terminate the stream
			}
			auto oldBufCapacity = buf.Capacity;
			if (!buf.TryWrite(txframe->body()->Data(), txframe->body()->size()))
			{
				// TODO: figure out a way to terminate the stream right here, with a 503 response
				AnyLog()->Log("Failed to allocate memory for body frame");
				return false;
			}
			BufferedRequestsTotalBytes += buf.Capacity - oldBufCapacity;
			inframe.BodyBytesLen = txframe->body()->size();
			inframe.BodyBytes = buf.Data + buf.Count - txframe->body()->size();
			return true;
		}
		else
		{
			// non-buffered
			inframe.BodyBytesLen = txframe->body()->size();
			if (inframe.BodyBytesLen != 0)
			{
				inframe.BodyBytes = (uint8_t*) Alloc(inframe.BodyBytesLen, Log);
				memcpy(inframe.BodyBytes, txframe->body()->Data(), inframe.BodyBytesLen);
			}
			return true;
		}
	}

	size_t Backend::TotalHeaderBlockSize(const httpbridge::TxFrame* frame)
	{
		// the +1 is for the terminal HeaderLine
		size_t total = sizeof(Request::HeaderLine) * (frame->headers()->size() + 1);

		for (uint32_t i = 0; i < frame->headers()->size(); i++)
		{
			const httpbridge::TxHeaderLine* line = frame->headers()->Get(i);
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
		HTTPBRIDGE_PANIC(msg);
	}

	void Backend::SendResponse(Request& request, StatusCode status)
	{
		Response response(&request);
		response.Status = status;
		Send(response);
	}

	Request* Backend::GetRequestOrDie(uint64_t channel, uint64_t stream)
	{
		auto iter = CurrentRequests.find(MakeStreamKey(channel, stream));
		HTTPBRIDGE_ASSERT(iter != CurrentRequests.end());
		return iter->second;
	}

	StreamKey Backend::MakeStreamKey(uint64_t channel, uint64_t stream)
	{
		return StreamKey{ channel, stream };
	}

	StreamKey Backend::MakeStreamKey(const Request& request)
	{
		return StreamKey{ request.Channel, request.Stream };
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Request::Request()
	{
	}

	Request::~Request()
	{
		Free();
	}

	void Request::Initialize(hb::Backend* backend, HttpVersion version, uint64_t channel, uint64_t stream, int32_t headerCount, const void* headerBlock)
	{
		Backend = backend;
		Version = version;
		Channel = channel;
		Stream = stream;
		_HeaderCount = headerCount;
		_HeaderBlock = (const uint8_t*) headerBlock;
		const char* contentLength = HeaderByName("Content-Length");
		if (contentLength != nullptr)
			BodyLength = (uint64_t) uatoi64(contentLength);
	}

	bool Request::DecrementLiveness()
	{
		_Liveness--;
		if (_Liveness == 0)
		{
			Backend->RequestDestroyed(StreamKey{ Channel, Stream });
			delete this;
			return true;
		}
		return false;
	}

	void Request::Reset()
	{
		Free();
		*this = Request();
	}

	const char* Request::Method() const
	{
		// actually the 'key' of header[0]
		auto lines = (const HeaderLine*) _HeaderBlock;
		return (const char*) (_HeaderBlock + lines[0].KeyStart);
	}

	const char* Request::URI() const
	{
		// actually the 'value' of header[0]
		auto lines = (const HeaderLine*) _HeaderBlock;
		return (const char*) (_HeaderBlock + lines[0].KeyStart + lines[0].KeyLen + 1);
	}

	const std::string& Request::Path()
	{
		if (_CachedPath == "")
		{
			_CachedPath = URI();
		}
		return _CachedPath;
	}

	bool Request::HeaderByName(const char* name, size_t& len, const void*& buf, int nth) const
	{
		auto nameLen = strlen(name);
		auto count = HeaderCount() + NumPseudoHeaderLines;
		auto lines = (const HeaderLine*) _HeaderBlock;
		for (int32_t i = NumPseudoHeaderLines; i < count; i++)
		{
			const uint8_t* bKey = _HeaderBlock + lines[i].KeyStart;
			if (memcmp(bKey, name, nameLen) == 0)
				nth--;

			if (nth == -1)
			{
				buf = _HeaderBlock + (lines[i].KeyStart + lines[i].KeyLen + 1);
				len = lines[i + 1].KeyStart - 1 - (lines[i].KeyStart + lines[i].KeyLen + 1);
				return true;
			}
		}
		return false;
	}

	const char* Request::HeaderByName(const char* name, int nth) const
	{
		size_t len;
		const void* buf;
		if (HeaderByName(name, len, buf, nth))
			return (const char*) buf;
		else
			return nullptr;
	}

	void Request::HeaderAt(int32_t index, int32_t& keyLen, const char*& key, int32_t& valLen, const char*& val) const
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
			auto lines = (const HeaderLine*) _HeaderBlock;
			keyLen = lines[index].KeyLen;
			key = (const char*) (_HeaderBlock + lines[index].KeyStart);
			valLen = lines[index + 1].KeyStart - (lines[index].KeyStart + lines[index].KeyLen);
			val = (const char*) (_HeaderBlock + lines[index].KeyStart + lines[index].KeyLen + 1);
		}
	}

	void Request::HeaderAt(int32_t index, const char*& key, const char*& val) const
	{
		int32_t keyLen, valLen;
		HeaderAt(index, keyLen, key, valLen, val);
	}

	void Request::Free()
	{
		hb::Free((void*) _HeaderBlock);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static_assert(sizeof(Response::ByteVectorOffset) == sizeof(flatbuffers::Offset<flatbuffers::Vector<uint8_t>>), "Assumed flatbuffers offset size is wrong");

	Response::Response()
	{
	}

	Response::Response(const Request* request, StatusCode status)
	{
		Status = status;
		Backend = request->Backend;
		Version = request->Version;
		Stream = request->Stream;
		Channel = request->Channel;
	}

	Response::~Response()
	{
		if (FBB != nullptr)
			delete FBB;
	}

	void Response::WriteHeader(const char* key, const char* value)
	{
		size_t keyLen = strlen(key);
		size_t valLen = strlen(value);
		HTTPBRIDGE_ASSERT(keyLen <= MaxHeaderKeyLen);
		HTTPBRIDGE_ASSERT(valLen <= MaxHeaderValueLen);
		WriteHeader((int) keyLen, key, (int) valLen, value);
	}

	void Response::WriteHeader(int32_t keyLen, const char* key, int32_t valLen, const char* value)
	{
		// Ensure that our uint32_t casts down below are safe, and also header sanity
		HTTPBRIDGE_ASSERT(keyLen <= MaxHeaderKeyLen && valLen <= MaxHeaderValueLen);
		
		// Keys may not be empty, but values are allowed to be empty
		HTTPBRIDGE_ASSERT(keyLen > 0 && valLen >= 0);

		HeaderIndex.Push(HeaderBuf.Size());
		memcpy(HeaderBuf.AddSpace((uint32_t) keyLen), key, keyLen);
		HeaderBuf.Push(0);

		HeaderIndex.Push(HeaderBuf.Size());
		memcpy(HeaderBuf.AddSpace((uint32_t) valLen), value, valLen);
		HeaderBuf.Push(0);
	}

	void Response::SetBody(size_t len, const void* body)
	{
		// Ensure sanity, as well as safety because BodyLength is uint32
		HTTPBRIDGE_ASSERT(len <= 1024 * 1024 * 1024);

		CreateBuilder();
		BodyOffset = (ByteVectorOffset) FBB->CreateVector((const uint8_t*) body, len).o;
		BodyLength = (uint32_t) len;
	}

	SendResult Response::Send()
	{
		return Backend->Send(*this);
	}

	const char* Response::HeaderByName(const char* name, int nth) const
	{
		for (int32_t index = 0; index < HeaderCount(); index++)
		{
			int i = index << 1;
			const char* key = &HeaderBuf[HeaderIndex[i]];
			const char* val = &HeaderBuf[HeaderIndex[i + 1]];
			if (strcmp(key, name) == 0)
				nth--;
			if (nth == -1)
				return val;
		}
		return nullptr;
	}

	void Response::HeaderAt(int32_t index, int32_t& keyLen, const char*& key, int32_t& valLen, const char*& val) const
	{
		if (index > HeaderCount())
		{
			keyLen = 0;
			valLen = 0;
			key = nullptr;
			val = nullptr;
			return;
		}
		keyLen = HeaderKeyLen(index);
		valLen = HeaderValueLen(index);
		int i = index << 1;
		key = &HeaderBuf[HeaderIndex[i]];
		val = &HeaderBuf[HeaderIndex[i + 1]];
	}

	void Response::HeaderAt(int32_t index, const char*& key, const char*& val) const
	{
		int32_t keyLen, valLen;
		HeaderAt(index, keyLen, key, valLen, val);
	}

	void Response::FinishFlatbuffer(size_t& len, void*& buf)
	{
		HTTPBRIDGE_ASSERT(!IsFlatBufferBuilt);

		CreateBuilder();

		std::vector<flatbuffers::Offset<httpbridge::TxHeaderLine>> lines;
		{
			char statusStr[4];
			u32toa((uint32_t) Status, statusStr);
			auto key = FBB->CreateVector((const uint8_t*) statusStr, 3);
			auto line = httpbridge::CreateTxHeaderLine(*FBB, key, 0, 0);
			lines.push_back(line);
		}

		HeaderIndex.Push(HeaderBuf.Size()); // add a terminator

		for (int32_t i = 0; i < HeaderIndex.Size() - 2; i += 2)
		{
			// The extra 1's here are for removing the null terminator that we add to keys and values
			uint32_t keyLen = HeaderIndex[i + 1] - HeaderIndex[i] - 1;
			uint32_t valLen = HeaderIndex[i + 2] - HeaderIndex[i + 1] - 1;
			auto key = FBB->CreateVector((const uint8_t*) &HeaderBuf[HeaderIndex[i]], keyLen);
			auto val = FBB->CreateVector((const uint8_t*) &HeaderBuf[HeaderIndex[i + 1]], valLen);
			auto line = httpbridge::CreateTxHeaderLine(*FBB, key, val, 0);
			lines.push_back(line);
		}
		auto linesVector = FBB->CreateVector(lines);

		httpbridge::TxFrameBuilder frame(*FBB);
		frame.add_body(BodyOffset);
		frame.add_headers(linesVector);
		frame.add_channel(Channel);
		frame.add_stream(Stream);
		frame.add_frametype(httpbridge::TxFrameType_Header);
		frame.add_version(TranslateVersion(Version));
		auto root = frame.Finish();
		httpbridge::FinishTxFrameBuffer(*FBB, root);
		len = FBB->GetSize();
		buf = FBB->GetBufferPointer();
		
		// Hack the FBB to write our frame size at the start of the buffer.
		// This is not a 'hack' in the sense that it's bad or needs to be removed at some point.
		// It's simply not the intended use of FBB.
		// Right here 'len' contains the size of the flatbuffer, which is our "frame size"
		uint8_t frame_size[4];
		Write32LE(frame_size, (uint32_t) len);
		FBB->PushBytes(frame_size, 4);
		len = FBB->GetSize();
		buf = FBB->GetBufferPointer();
		// Now 'len' contains the size of the flatbuffer + 4

		IsFlatBufferBuilt = true;
	}

	void Response::SerializeToHttp(size_t& len, void*& buf)
	{
		// This doesn't make any sense. Additionally, we need the guarantee that our
		// Body is always FBB->GetBufferPointer() + 4, because we're reaching into the
		// FBB internals here.
		HTTPBRIDGE_ASSERT(!IsFlatBufferBuilt);

		if (HeaderByName("Content-Length") == nullptr)
		{
			char s[11]; // 10 characters needed for 4294967295
			u32toa(BodyLength, s);
			WriteHeader("Content-Length", s);
		}

		// Compute response size
		const size_t sCRLF = 2;
		const int32_t headerCount = HeaderCount();
		
		// HTTP/1.1 200		12 bytes
		len = 12 + 1 + strlen(StatusString(Status)) + sCRLF;
		
		// Headers
		// 2		The ": " in between the key and value of each header
		// CRLF		The newline after each header
		// -2		The null terminators that we add to each key and each value (and counted up inside HeaderBuf.size)
		len += headerCount * (2 + sCRLF - 2) + HeaderBuf.Size();
		len += sCRLF;
		len += BodyLength;

		// Write response
		buf = malloc(len);
		HTTPBRIDGE_ASSERT(buf != nullptr);
		uint8_t* out = (uint8_t*) buf;
		const char* CRLF = "\r\n";

		BufWrite(out, "HTTP/1.1 ", 9);
		char statusNStr[4];
		u32toa((uint32_t) Status, statusNStr);
		BufWrite(out, statusNStr, 3);
		BufWrite(out, " ", 1);
		BufWrite(out, StatusString(Status), strlen(StatusString(Status)));
		BufWrite(out, CRLF, 2);

		for (int32_t i = 0; i < headerCount * 2; i += 2)
		{
			// The extra 1's here are for removing the null terminator that we add to keys and values
			uint32_t valTop = i + 2 == HeaderIndex.Size() ? HeaderBuf.Size() : HeaderIndex[i + 2];
			uint32_t keyLen = HeaderIndex[i + 1] - HeaderIndex[i] - 1;
			uint32_t valLen = valTop - HeaderIndex[i + 1] - 1;
			BufWrite(out, &HeaderBuf[HeaderIndex[i]], keyLen);
			BufWrite(out, ": ", 2);
			BufWrite(out, &HeaderBuf[HeaderIndex[i + 1]], valLen);
			BufWrite(out, CRLF, 2);
		}
		BufWrite(out, CRLF, 2);

		// Write body
		uint8_t* flatbuf = FBB->GetBufferPointer();
		BufWrite(out, flatbuf + 4, BodyLength);
	}

	void Response::CreateBuilder()
	{
		if (FBB == nullptr)
			FBB = new flatbuffers::FlatBufferBuilder();
	}

	int32_t Response::HeaderKeyLen(int32_t _i) const
	{
		// key_0_start, val_0_start, key_1_start, val_1_start, ...
		// (with null terminators after each key and each value)
		uint32_t i = _i;
		i <<= 1;
		if (i + 1 >= (uint32_t) HeaderIndex.Size())
			return 0;
		return HeaderIndex[i + 1] - HeaderIndex[i] - 1; // extra -1 is to remove null terminator
	}

	int32_t Response::HeaderValueLen(int32_t _i) const
	{
		uint32_t i = _i;
		i <<= 1;
		uint32_t top;
		if (i + 3 >= (uint32_t) HeaderIndex.Size())
			return 0;

		if (i + 2 == HeaderIndex.Size())
			top = HeaderBuf.Size();
		else
			top = HeaderIndex[i + 2];

		return top - HeaderIndex[i + 1] - 1; // extra -1 is to remove null terminator
	}

}
