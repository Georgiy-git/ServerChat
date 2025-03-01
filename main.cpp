#include "Server.hpp"

int main() {
	setlocale(LC_ALL, "RU");
	B::io_context io_context;
	Server server(io_context, 53888);
	server.async_accept();
	io_context.run();
}