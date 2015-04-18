#include "server.h"
#ifndef _WIN32
#include <signal.h>
#endif

static hb::Server* SingleServer;

#ifdef _WIN32
BOOL ctrl_handler(DWORD ev)
{ 
	if (ev == CTRL_C_EVENT && SingleServer != nullptr)
	{
		SingleServer->Stop();
		return TRUE;
	}
	return FALSE;
}
void setup_ctrl_c_handler()
{
	SetConsoleCtrlHandler((PHANDLER_ROUTINE) ctrl_handler, TRUE);
}
#else
void signal_handler(int sig)
{
	if (SingleServer != nullptr)
		SingleServer->Stop();
}
void setup_ctrl_c_handler()
{
	struct sigaction sig;
	sig.sa_handler = signal_handler;
	sigemptyset(&sig.sa_mask);
	sig.sa_flags = 0;
	sigaction(SIGINT, &sig, nullptr);
}
#endif

class MyHandler : public hb::IServerHandler
{
public:
	virtual void HandleRequest(hb::Request& req, hb::Response& resp)
	{
		printf("%s\n", req.URI());
		resp.SetStatus(hb::Status200_OK);
		resp.WriteHeader("Content-Type", "text/plain");
		resp.SetBody(10, "hello good");
	}
};

int main(int argc, char** argv)
{
	setup_ctrl_c_handler();

	hb::Startup();

	MyHandler handler;

	hb::Server server;
	server.Handler = &handler;
	SingleServer = &server;
	server.ListenAndRun(8080);
	
	hb::Shutdown();

	SingleServer = nullptr;
	return 0;
}