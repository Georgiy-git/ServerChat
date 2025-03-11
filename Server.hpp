#pragma once

#include <optional>
#include "Session.hpp"

namespace B = boost::asio;
using tcp = B::ip::tcp;
using error_code = boost::system::error_code;

class Server {

private:
	B::io_context& io_context;
	std::optional<tcp::socket> socket;
	tcp::acceptor acceptor;
	std::unordered_set<std::shared_ptr<Session>> clients;

public:
	Server(B::io_context& io_context, int port) : 
		io_context{ io_context }, acceptor(io_context, tcp::endpoint(tcp::v4(), port)) {
		std::cout << "Сервер запущен на порту " << port << std::endl;
		std::cout << "Сервер текущей версии в логировании не обрабатывает кириллицу " << std::endl;
	}

	void async_accept() {
		socket.emplace(io_context);

		acceptor.async_accept(*socket, [&](error_code error) {

			tcp::endpoint remote_endpoint = socket->remote_endpoint();

			auto client = std::make_shared<Session>(std::move(*socket), clients);
			std::cout << "Новое подключение:  IP " << remote_endpoint.address().to_string() <<
				"   Порт " << remote_endpoint.port() << std::endl;
			client->async_read();

			async_accept();
			});
	}
};