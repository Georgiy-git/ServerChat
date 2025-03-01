#include "Session.hpp"
#include "sqlite3.h"

namespace B = boost::asio;
using tcp = B::ip::tcp;
using error_code = boost::system::error_code;

Session::Session(tcp::socket&& socket, std::unordered_set<std::shared_ptr<Session>>& clients)
    : socket{std::move(socket)}, clients{clients}
{
    sqlite3_open("info.db", &db);
    ok = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS users(login TEXT, password TEXT);", 
        nullptr, nullptr, &error_mess);
    pruf(ok, error_mess);
}

void Session::async_write(std::string message)
{
    message += '\f';
    B::async_write(socket, B::buffer(message.data(), message.size()),
        [self = shared_from_this()](error_code error, std::size_t bytes) {
            if (error) {
                std::cerr << error.message() << std::endl;
            }
        });
}

void Session::async_read() {
    B::async_read_until(socket, streambuf, '\n',
        [self = shared_from_this()] (error_code error, std::size_t bytes) {
            self->read_from_buffer(error, bytes);
        });
}

int Session::callback_author(void* x, int column, char** data, char** column_name)
{
    Session* obj = static_cast<Session*>(x);
    obj->found = 0;
    if (obj->password != std::string(data[1])) {
        obj->async_write("&NON_PASSWORD&");
    }
    else {
        obj->async_write("&OK_PASSWORD&");
        std::cout << obj->socket.remote_endpoint().address().to_string() <<
            " зашёл как " << obj->name << std::endl;
    }
    return 0;
}

void Session::read_from_buffer(const error_code& error, std::size_t bytes)
{
    if (error) {
        if (error == B::error::eof || error == B::error::connection_reset) {
            if (!name.empty())
            {
                std::cout << name << " отключился." << std::endl;
            }
            else {
                std::cout << socket.remote_endpoint().address().to_string() << " отключился." << std::endl;
            }
        }
        else {
            std::cout << "Ошибка: " << error.message() << std::endl;
        }
        clients.erase(shared_from_this());
        socket.close();
        return;
    }

    std::istream input(&streambuf);
    std::string line;
    std::getline(input, line);

    std::cout << line << std::endl;

    if (line == "/!!!!") {
        //std::shared_ptr<Session1> session1 = std::make_shared<Session1>();
    }
    else if (line.starts_with("/help")) {
        _help();
    }
    else if (line.starts_with("/users")) {
        _users();
    }
    else if (line.starts_with("&ENTER&")) {
        _enter(line);
    }
    else {
        write_everyone(line);
    }
}

void Session::write_everyone(std::string& line)
{
    for (auto& i : clients) {
        if (i != shared_from_this())
        {
            i->async_write("<span style='color: green;'><b>"+name+":</b></span> " + line + '\n');
        }
    }

    async_read();
}

void Session::autorization()
{
    found = 1;
    sql = "SELECT login, password FROM users WHERE login = '" + name + "';";
    ok = sqlite3_exec(db, sql.c_str(), callback_author, this, &error_mess);
    pruf(ok, error_mess);

    if (found == 1) {
        sql = std::format("INSERT INTO users (login, password) VALUES ('{}', '{}');",
            name, password);
        ok = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_mess);
        pruf(ok, error_mess);

        std::cout << socket.remote_endpoint().address().to_string() <<
            " зарегистрировался как " << name << std::endl;

        async_write("&OK_PASSWORD&");
    }
}

void Session::pruf(int ok, char* error)
{
    if (ok) {
        std::cerr << error << std::endl;
        sqlite3_free(error);
    }
}

std::string Session::sm(std::string& str)
{
    return "<span style = 'color: blue;'>" + str + "</snap>";
}

void Session::_help()
{
    std::string list = "Commands:<br>";
    list += "/users<br>";
    list += "/exit";

    async_write(sm(list));
    async_read();
}

void Session::_users()
{
    int count = 1;
    std::string users = "Users:";
    for (const auto& i : clients) {
        users += "<br>" + std::to_string(count) + '.' + i->name;
        count++;
    }
    async_write(sm(users));
    async_read();
}

void Session::_enter(std::string line)
{
    line.erase(0, line.rfind('&')+1);
    name = line.substr(0, line.find('|'));
    password = line.substr(line.find('|') + 1);

    autorization();

    clients.insert(shared_from_this());

    async_read();
}
