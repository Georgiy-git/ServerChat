#pragma once

#include <vector>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <iostream>
#include <unordered_set>
#include <streambuf>
#include "sqlite3.h"
#include <set>

class Session : public std::enable_shared_from_this<Session> {

protected:
	boost::asio::ip::tcp::socket socket; //Сокет
	boost::system::error_code ec; //Переменная для ошибок
	boost::asio::streambuf streambuf; //Буфер
	std::unordered_set<std::shared_ptr<Session>> &clients; //Список клиентов
	
	//Для БД
	sqlite3* db;
	std::string sql;
	char* error_mess;
	int ok{}; 
	static int callback_author(void* x, int column, char** data, char** column_name);
	static int callback_chats(void* x, int column, char** data, char** column_name);
	static int callback_flag(void* x, int column, char** data, char** column_name);

	bool flag_sql {false}; //Флаг, найден ли пользователь в БД

	//Чтение из потока в буфер
	void read_from_buffer(const boost::system::error_code& error, std::size_t bytes);

	//Отправка сообщения всем поользователям
	void write_everyone(std::string& line);

	//Отправка сообщения в локальный чат
	void write_local_chat(std::string& line);

	//Авторизация
	void autorization();

	//Проверка результата запроса в БД
	void pruf(int ok, char* error);

	//Преобразование теста в серверный
	std::string sm(std::string& str);

	void _help();
	void _users();
	void _enter(std::string);
	void _create_chat(std::string);
	void _add_user_to_chat(std::string);
	void _open_lockal_chat(std::string);
	void _open_global_chat();
	void _users_local();
	void _delete_chat(std::string);

public:
	Session(boost::asio::ip::tcp::socket&& socket, std::unordered_set<std::shared_ptr<Session>>& clients);

	std::string name; //Имя пользователя
	std::string role;
	std::string password;
	std::vector<std::string> chats;
	std::string this_local_chat;

	//Отправляет текущему пользователю сообщение
	void async_write(std::string message);

	//Чтение потока
	void async_read();

	//
	void recive_files();
};