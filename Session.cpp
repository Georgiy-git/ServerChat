#include "Session.hpp"
#include "sqlite3.h"

#include <filesystem>
#include <fstream>

namespace B = boost::asio;
using tcp = B::ip::tcp;
using error_code = boost::system::error_code;

Session::Session(tcp::socket&& socket, std::unordered_set<std::shared_ptr<Session>>& clients)
    : socket{std::move(socket)}, clients{clients}
{
    sqlite3_open("info.db", &db);
    ok = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS users(login TEXT, password TEXT);", 
        nullptr, nullptr, &error_mess);
    ok = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS chats(chat_name TEXT, user_name TEXT);",
        nullptr, nullptr, &error_mess);
    ok = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS banlist(name TEXT);",
        nullptr, nullptr, &error_mess);
    pruf(ok, error_mess);
    flag_sql = false;
    role = "user";
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

void Session::async_recive_files()
{
    B::async_read_until(socket, streambuf, '\f',
        [self = shared_from_this()] (error_code error, std::size_t bytes) {
            self->read_chunk_from_buffer(error, bytes);
        });
}

int Session::callback_author(void* x, int column, char** data, char** column_name)
{
    Session* obj = static_cast<Session*>(x);
    obj->flag_sql = true;
    if (obj->password != std::string(data[1])) {
        obj->async_write("&NON_PASSWORD&");
    }
    else {
        obj->async_write("&OK_PASSWORD&");
        std::cout << obj->socket.remote_endpoint().address().to_string() <<
            " зашёл как " << obj->name << std::endl;
        int ok = sqlite3_exec(obj->db, std::string("SELECT chat_name FROM chats WHERE user_name='" + obj->name + "'").c_str(),
            callback_chats, obj, &obj->error_mess);
        obj->pruf(ok, obj->error_mess);
        if (!obj->chats.empty()) {
            for (const auto& i : obj->chats) {
                obj->async_write("&LOADCHAT&" + i);
            }

            for (const auto& entry : std::filesystem::directory_iterator("Files/")) {
                if (std::filesystem::is_regular_file(entry)) {
                    obj->async_write("&LOADFILE&" + entry.path().filename().string());
                }
            }
        }
    }
    return 0;
}

int Session::callback_chats(void* x, int column, char** data, char** column_name)
{
    Session* obj = static_cast<Session*>(x);
    obj->chats.push_back(std::string(*data));
    return 0;
}

int Session::callback_flag(void* x, int column, char** data, char** column_name)
{
    //std::cout << "callback!" << std::endl;
    Session* obj = static_cast<Session*>(x);
    obj->flag_sql = true;
    return 0;
}

int Session::callback_users_in_this_chat(void* x, int column, char** data, char** column_name)
{
    Session* obj = static_cast<Session*>(x);
    obj->users_in_this_chat.push_back(std::string(*data));
    return 0;
}

int Session::callback_banlist(void* x, int column, char** data, char** column_name)
{
    Session* obj = static_cast<Session*>(x);
    obj->banlist_v.append("<br>" + std::string(*data));
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
        return;
    }

    std::istream input(&streambuf);
    std::string line;
    std::getline(input, line);

    std::cout << line << std::endl;

    if (role == "admin") {
        change_admin(line);
    }
    else {
        change(line);
    }
}

void Session::read_chunk_from_buffer(const boost::system::error_code& error, std::size_t bytess)
{
    if (error) {
        std::cout << "Ошибка: " << error.message() << std::endl;
        
        file.close();
        async_read();
        return;
    }

    std::istream input(&streambuf);
    std::string line;
    std::getline(input, line, '\f');

    file.write(line.c_str(), line.size());
    file.close();
    async_write("&OKLOADFILE&" + file_n);
    async_write(sm("File has been load"));
    file_n.clear();
    async_read();
}

void Session::change(std::string line)
{
    if (line == "/admin") {
        role = "admin";
        async_write(sm("You have been receive admin rules"));
        async_read();
    }
    else if (line.starts_with("/help")) {
        _help();
    }
    else if (line.starts_with("/users local")) {
        _users_local();
    }
    else if (line.starts_with("/users chat")) {
        _users_chat();
    }
    else if (line.starts_with("/users")) {
        _users();
    }
    else if (line.starts_with("/banlist")) {
        _banlist();
    }
    else if (line.starts_with("&ENTER&")) {
        _enter(line);
    }
    else if (line.starts_with("&CREATECHAT&")) {
        _create_chat(line);
    }
    else if (line.starts_with("&ADDUSERTOCHAT&")) {
        _add_user_to_chat(line);
    }
    else if (line.starts_with("&OPENLOCALCHAT&")) {
        _open_lockal_chat(line);
    }
    else if (line.starts_with("&OPENGLOBALCHAT&")) {
        _open_global_chat();
    }
    else if (line.starts_with("&DELETECHAT&")) {
        _delete_chat(line);
    }
    else if (line.starts_with("&LOADFILE&")) {
        _load_file(line);
    }
    else if (line.starts_with("&LOADMEFILE&")) {
        _load_file_to_user(line);
    }
    else {
        write_everyone(line);
    }
}

void Session::change_admin(std::string line)
{
    if (line.starts_with("/admin")) {
        _admin_status();
    }
    else if (line.starts_with("/kik ")) {
        _kik(line);
    }
    else if (line.starts_with("/ban ")) {
        _ban(line);
    }
    else if (line.starts_with("/unban ")) {
        _unban(line);
    }
    else {
        change(line);
    }
}

void Session::write_everyone(std::string line)
{
    if (this_local_chat.empty()) {
        for (auto& i : clients) {
            if (i != shared_from_this() && i->this_local_chat.empty())
            {
                if (role == "user") {
                    i->async_write("<span style='color: green;'><b>" + name + ":</b></span> " + line + '\n');
                }
                else if (role == "admin") {
                    i->async_write("<span style='color: red;'><b>" + name + ":</b></span> " + line + '\n');
                }
            }
        }
    }
    else {
        for (auto& i : clients) {
            if (i != shared_from_this() && i->this_local_chat == this_local_chat)
            {
                i->async_write("<span style='color: green;'><b>" + name + ":</b></span> " + line + '\n');
            }
        }
    }

    async_read();
}

void Session::write_local_chat(std::string& line)
{

}

void Session::autorization()
{
    flag_sql = false;
    sql = "SELECT * FROM banlist WHERE name = '" + name + "';";
    ok = sqlite3_exec(db, sql.c_str(), callback_flag, this, &error_mess);
    pruf(ok, error_mess);
    if (flag_sql) {
        async_write("&PROFILINBANLIST&");
        return;
    }

    sql = "SELECT login, password FROM users WHERE login = '" + name + "';";
    ok = sqlite3_exec(db, sql.c_str(), callback_author, this, &error_mess);
    pruf(ok, error_mess);

    if (!flag_sql) {
        sql = std::format("INSERT INTO users (login, password) VALUES ('{}', '{}');",
            name, password);
        ok = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_mess);
        pruf(ok, error_mess);

        std::cout << socket.remote_endpoint().address().to_string() <<
            " зарегистрировался как " << name << std::endl;

        async_write("&OK_PASSWORD&");
    }
    flag_sql = false;
}

void Session::pruf(int ok, char* error)
{
    if (ok) {
        std::cerr << error << std::endl;
        sqlite3_free(error);
    }
}

std::string Session::sm(std::string str)
{
    return "<span style = 'color: blue;'>" + str + "</snap>";
}

void Session::load_file_callback()
{
    char v[100];
    std::streamsize size = file_i.read(v, 100).gcount();

    if (size > 0) {
        B::async_write(socket, B::buffer(v, size),
            [self = shared_from_this()](error_code error, std::size_t bytes) {
                if (error) {
                    std::cerr << error.message() << std::endl;
                }
                else {
                    self->load_file_callback();
                }
            });
    }
    else {
        file_i.close();
        async_write("\f"+sm("File has been download"));
        async_read();
    }
}

void Session::_help()
{
    std::string list = "Commands:<br>";
    list += "/users - online users<br>";
    list += "/users local - online users in local chat<br>";
    list += "/users chat - offline users in local chat<br>";
    list += "/banlist - locked users list";

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

void Session::_create_chat(std::string line)
{
    line.erase(0, line.rfind('&') + 1);
    ok = sqlite3_exec(db, std::string("SELECT chat_name FROM chats WHERE chat_name ='"+line+"';").c_str(),
        callback_flag, this, &error_mess);

    if (!flag_sql)
    {
        ok = sqlite3_exec(db, std::format("INSERT INTO chats (chat_name, user_name) VALUES ('{}', '{}');",
            line, name).c_str(), nullptr, nullptr, &error_mess);
        pruf(ok, error_mess);
        async_write("&CHATCREATEOK&"+line);
    }
    else {
        async_write("&CHATALREADYCREATED&");
    }
    flag_sql = false;
    async_read();
}

void Session::_add_user_to_chat(std::string line)
{
    flag_sql = false;
    line.erase(0, line.rfind('&') + 1);
    std::string user_name = line.substr(0, line.find('|'));
    std::string chat_name = line.substr(line.find('|') + 1);
    ok = sqlite3_exec(db, std::string("SELECT * FROM users WHERE login='" + user_name + "';").c_str(),
        callback_flag, this, &error_mess);
    pruf(ok, error_mess);
    //std::cout << flag_sql << std::boolalpha << std::endl;

    if (flag_sql) {

        flag_sql = false;
        ok = sqlite3_exec(db, std::format("SELECT * FROM chats WHERE chat_name='{}' AND user_name='{}';",
            chat_name, user_name).c_str(), callback_flag, this, &error_mess);
        pruf(ok, error_mess);
        //std::cout << flag_sql << std::boolalpha << std::endl;

        if (!flag_sql) {
            ok = sqlite3_exec(db, std::format("INSERT INTO chats (chat_name, user_name) VALUES ('{}', '{}');",
                chat_name, user_name).c_str(), nullptr, nullptr, &error_mess);
            pruf(ok, error_mess);
            for (const auto i : clients) {
                if (i->name == user_name) {
                    i->async_write("&LOADCHAT&" + chat_name);
                }
            }
        }
        else {
            async_write("&USERNOINDB&");
        }
    }
    else {
        async_write("&USERNOINDB&");
    }

    flag_sql = false;
    async_read();
}

void Session::_open_lockal_chat(std::string line)
{
    line.erase(0, line.rfind('&') + 1);
    this_local_chat = line;
    async_read();
}

void Session::_open_global_chat()
{
    this_local_chat.clear();
    async_read();
}

void Session::_users_local()
{
    int count = 1;
    std::string users = "Users in "+this_local_chat+":";
    for (const auto& i : clients) {
        if (i->this_local_chat == this_local_chat) {
            users += "<br>" + std::to_string(count) + '.' + i->name;
            count++;
        }
    }
    async_write(sm(users));
    async_read();
}

void Session::_delete_chat(std::string chat)
{
    chat.erase(0, chat.rfind('&') + 1);
    ok = sqlite3_exec(db, std::string("DELETE FROM chats WHERE user_name='" + name + "' AND chat_name='" + chat + "';").c_str(),
        nullptr, nullptr, &error_mess);
    pruf(ok, error_mess);
    async_read();
}

void Session::_users_chat()
{
    ok = sqlite3_exec(db, std::string("SELECT user_name FROM chats WHERE chat_name='" + this_local_chat+"';").c_str(),
        callback_users_in_this_chat, this, &error_mess);
    pruf(ok, error_mess);
    std::string line("All users in "+this_local_chat+":");
    for (const auto& i : users_in_this_chat) {
        line.append("<br>"+i);
    }
    users_in_this_chat.clear();
    async_write(sm(line));
    async_read();
}

void Session::_load_file(std::string line)
{
    line.erase(0, line.rfind('&') + 1);
    file.open("Files/" + line, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Не удалось открыть файл" << std::endl;
        async_read();
        return;
    }
    file_n = line;

    async_recive_files();
}

void Session::_load_file_to_user(std::string line)
{
    line.erase(0, line.rfind('&') + 1);
    file_i.open("Files/" + line);
    if (!file_i.is_open()) {
        std::cerr << "Ошибка: файл не открыт" << std::endl;
        async_read();
        return;
    }
    std::string exec = "&LOADMEFILE&" + line + "\f";
    B::async_write(socket, B::buffer(exec.data(), exec.size()), [self = shared_from_this()](error_code error, std::size_t bytes){
            
        self->load_file_callback();
        });
}

void Session::_admin_status()
{
    async_write(sm(std::string("Admins commands:<br>/kik (name)<br>/ban (name)<br>/unban (name)")));
    async_read();
}

void Session::_kik(std::string line)
{
    bool flag_a = false;
    line.erase(0, line.find(' ') + 1);
    for (const auto& i : clients) {
        if (i->name == line) {
            i->socket.close();
            flag_a = true;

            for (auto& i : clients) {
                if (i->this_local_chat.empty()) {
                    i->async_write("<span style='color: red';><i>" + name + " kik " + line + "</span></i>");
                }
            }
        }
    }
    if (!flag_a) {
        async_write(sm("User not found"));
    }
    async_read();
}

void Session::_ban(std::string line)
{
    line.erase(0, line.find(' ') + 1);
    
    flag_sql = false;

    ok = sqlite3_exec(db, std::string("SELECT * FROM users WHERE login ='" + line + "';").c_str(), callback_flag,
        this, &error_mess);
    pruf(ok, error_mess);

    if (flag_sql) {
        ok = sqlite3_exec(db, std::string("INSERT INTO banlist (name) VALUES ('" + line + "')").c_str(), nullptr,
            nullptr, &error_mess);
        pruf(ok, error_mess);
        for (auto& i : clients) {
            if (i->this_local_chat.empty()) {
                i->async_write("<span style='color: red';><i>" + name + " ban " + line + "</span></i>");
            }
        }
        for (const auto& i : clients) {
            if (i->name == line) {
                i->socket.close();
            }
        }
    }
    else {
        async_write(sm("User not found"));
    }

    flag_sql = false;
    async_read();
}

void Session::_unban(std::string line)
{
    line.erase(0, line.find(' ') + 1);

    flag_sql = false;

    ok = sqlite3_exec(db, std::string("SELECT * FROM banlist WHERE name ='" + line + "';").c_str(), callback_flag,
        this, &error_mess);
    pruf(ok, error_mess);

    if (flag_sql) {
        ok = sqlite3_exec(db, std::string("DELETE FROM banlist WHERE name='" + line + "'").c_str(), nullptr,
            nullptr, &error_mess);
        pruf(ok, error_mess);
        async_write(sm(line + " unban"));
    }
    else {
        async_write(sm("User not found"));
    }

    flag_sql = false;
    async_read();
}

void Session::_banlist()
{
    banlist_v = "Banlist:";
    ok = sqlite3_exec(db, "SELECT * FROM banlist", callback_banlist, this, &error_mess);
    pruf(ok, error_mess);
    async_write(sm(banlist_v));
    banlist_v.clear();
    async_read();
}
