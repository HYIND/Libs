#include "Mysql_Server.h"

using namespace std;

Mysql_Server::Mysql_Server()
{
    connection = mysql_init(NULL);
    Stop = true;
}

Mysql_Server::~Mysql_Server()
{
    Stop = true;
    HeartBeat_cv.notify_all();
    if (!connection)
    {
        mysql_close(connection);
    }
}

bool Mysql_Server::CreateConnect(const std::string &host, int port, const std::string &database, const std::string &user, const std::string &password)
{
    // LOGINFO("Connect Database host: {},port: {},database: {}, user: {}, password: {}", host, port, database, user, password);

    if (port == 0)
        return false;

    if (!mysql_real_connect(this->connection, host.c_str(), user.c_str(), password.c_str(), database.c_str(), port, nullptr, 0))
    {
        return false;
        // LOGINFO("Connect Database Success!");
    }

    // LOGINFO("Connect Database Success! host: {},port: {},database: {}", host, port, database);
    Stop = false;
    HeartBeat_thread = thread(&Mysql_Server::HeartBeat_Task, this);
    HeartBeat_thread.detach();
    return true;
}

bool Mysql_Server::Query(const string &command, MYSQL_RES **result, int *result_count)
{
    if (Stop)
        return false;

    if (mysql_query(connection, command.c_str()) != 0)
        return false;
    MYSQL_RES *Res = mysql_store_result(connection);
    if (result)
        *result = Res;
    if (result_count)
        *result_count = mysql_num_rows(Res);

    return true;
}
bool Mysql_Server::Query(const string &&command, MYSQL_RES **result, int *result_count)
{
    if (Stop)
        return false;

    if (mysql_query(connection, command.c_str()) != 0)
        return false;
    MYSQL_RES *Res = mysql_store_result(connection);
    if (result)
        *result = Res;
    if (result_count)
        *result_count = mysql_num_rows(Res);

    return true;
}

bool Mysql_Server::Update(const string &command, int *affected_row_num)
{
    if (Stop)
        return false;

    if (mysql_query(connection, command.c_str()) != 0)
        return false;

    if (affected_row_num)
        *affected_row_num = mysql_affected_rows(connection);

    return true;
}

bool Mysql_Server::Update(const string &&command, int *affected_row_num)
{
    if (Stop)
        return false;

    if (mysql_query(connection, command.c_str()) != 0)
        return false;

    if (affected_row_num)
        *affected_row_num = mysql_affected_rows(connection);

    return true;
}

void Mysql_Server::HeartBeat_Task()
{
    do
    {
        Query("select * from Account where id = '47'", nullptr, nullptr);
        unique_lock<mutex> HeartBeat_lck(HeartBeat_mutex);
        HeartBeat_cv.wait_for(HeartBeat_lck, std::chrono::seconds(3600));
        HeartBeat_lck.release()->unlock();
    } while (!Stop);
}