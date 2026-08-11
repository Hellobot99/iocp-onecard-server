#include <iostream>
#include "Server.h"

Server *g_Server = nullptr;

int main()
{
	Server server(2);
	server.Start(9000);
}