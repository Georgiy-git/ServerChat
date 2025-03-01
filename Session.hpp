#pragma once

#include <vector>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <iostream>
#include <unordered_set>
#include <streambuf>
#include "sqlite3.h"

class Session : public std::enable_shared_from_this<Session> {

protected:
	boost::asio::ip::tcp::socket socket; //�����
	boost::system::error_code ec; //���������� ��� ������
	boost::asio::streambuf streambuf; //�����
	std::unordered_set<std::shared_ptr<Session>> &clients; //������ ��������
	
	//��� ��
	sqlite3* db;
	std::string sql;
	char* error_mess;
	int ok{}; 
	static int callback_author(void* x, int column, char** data, char** column_name);
	int found{}; //����, ������ �� ������������ � ��

	//������ �� ������ � �����
	void read_from_buffer(const boost::system::error_code& error, std::size_t bytes);

	//�������� ��������� ���� ��������������
	void write_everyone(std::string& line);

	//�����������
	void autorization();

	//�������� ���������� ������� � ��
	void pruf(int ok, char* error);

	//�������������� ����� � ���������
	std::string sm(std::string& str);

	void _help();
	void _users();
	void _enter(std::string);

public:
	Session(boost::asio::ip::tcp::socket&& socket, std::unordered_set<std::shared_ptr<Session>>& clients);

	std::string name; //��� ������������
	std::string role;
	std::string password;

	//���������� �������� ������������ ���������
	void async_write(std::string message);

	//������ ������
	void async_read();

	//
	void recive_files();
};