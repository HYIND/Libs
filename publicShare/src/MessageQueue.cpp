#include "MessageQueue.h"
#include <iostream>
#include <memory.h>
#include <assert.h>

/* // test code
void consume()
{
    Consumer_MQ rabbitmq;
    if (!rabbitmq.Consumer_Connect())
        return;
    if (!rabbitmq.Consumer_BuildQueue())
        return;
    char *msg = nullptr;
    size_t length = 0;
    while (rabbitmq.Consumer(msg, length) > 0)
    {
        char m[20];
        memcpy(m, msg, length);
        cout << "consume , recv length :" << length << ",msg:" << msg << endl;
        delete (msg);
        msg = nullptr;
        sleep(1);
    }
    rabbitmq.Consumer_Close();
}

void produce()
{
    Producer_MQ rabbitmq;
    if (!rabbitmq.Producer_Connect())
        return;
    for (int i = 0; i < 5;)
    {
        char msg[] = "asjdha\0sdasd";
        msg[0] += i;
        int length = sizeof(msg);
        rabbitmq.Producer_Publish(msg, length);
        cout << "consume , send length :" << length << ",msg:" << msg << endl;

        // sleep(1);
    }
    rabbitmq.Producer_Close();
}
void messagequeuetest()
{
    thread T1(consume);
    thread T2(produce);
    T1.join();
    T2.join();
}
 */
MQ::MQ()
{
    m_honstname = "127.0.0.1";
    m_port = 5672;
    m_exchange = "ExchangeTest";
    m_routingkey = "RoutingkeyTest";
    reconnect = 0;
}

MQ::MQ(string &hostname, int port, string &exchange, string &routingkey)
    : m_honstname(hostname), m_port(port), m_exchange(exchange), m_routingkey(routingkey)
{
    reconnect = 0;
}

Consumer_MQ::Consumer_MQ()
{
}

Producer_MQ::Producer_MQ()
{
}

Consumer_MQ::Consumer_MQ(string &hostname, int port, string &exchange, string &routingkey)
    : MQ(hostname, port, exchange, routingkey)
{
}
Producer_MQ::Producer_MQ(string &hostname, int port, string &exchange, string &routingkey)
    : MQ(hostname, port, exchange, routingkey)
{
}

bool Consumer_MQ::Consumer_Connect()
{
    amqp_rpc_reply_t reply;
    amqp_socket_t *aqmp_socket = NULL;

    // 分配和初始化一个新的链接对象
    connection = amqp_new_connection();
    // 创建tcp socket链接
    aqmp_socket = amqp_tcp_socket_new(connection);
    if (!aqmp_socket)
    {
        perror("consumer: creating TCP socket");
        // 销毁链接
        amqp_destroy_connection(connection);
        return false;
    }
    struct timeval timeout = {10, 0};
    // 非阻塞方式打开socket链接
    int socket_fd = amqp_socket_open_noblock(aqmp_socket, m_honstname.c_str(), m_port, &timeout);
    if (socket_fd == -1)
    {
        perror("consumer: opening TCP socket");
        amqp_destroy_connection(connection);
        return false;
    }
    // 登录Rabbitmq
    reply = amqp_login(connection, "MQTest", 0, 131072, 20, AMQP_SASL_METHOD_PLAIN, "admin", "hyx26528");
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("consumer: failed to login rabbitmq");
        // 关闭链接
        reply = amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }
    // 打开链接通道
    amqp_channel_open(connection, 1);
    reply = amqp_get_rpc_reply(connection);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("consumer: Opening channel");
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }

    return true;
}

bool Consumer_MQ::Consumer_BuildQueue()
{
    amqp_rpc_reply_t reply;

    amqp_bytes_t queuename;
    amqp_bytes_t queue;
    char mac[18] = "b827eb1f1f98";
    queue.bytes = mac;
    queue.len = 12;
    // 声明队列
    amqp_queue_declare_ok_t *declare = amqp_queue_declare(connection, 1, queue, 0, 0, 0, 1, amqp_empty_table);
    reply = amqp_get_rpc_reply(connection);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("Declaring queue");
        amqp_channel_close(connection, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }

    queuename = amqp_bytes_malloc_dup(declare->queue);
    if (queuename.bytes == NULL)
    {
        cout << "out of memory while copying queue name" << endl;
        amqp_channel_close(connection, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }
    // 绑定交换机和队列
    amqp_queue_bind(connection, 1, queuename, amqp_cstring_bytes(m_exchange.c_str()), amqp_cstring_bytes(m_routingkey.c_str()), amqp_empty_table);
    reply = amqp_get_rpc_reply(connection);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("Binding queue");
        amqp_channel_close(connection, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }
    // 开启一个队列消费者
    amqp_basic_consume(connection, 1, queuename, amqp_empty_bytes, 0, 1, 0, amqp_empty_table);
    reply = amqp_get_rpc_reply(connection);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("Consuming");
        amqp_channel_close(connection, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }

    cout << "Consumer_MQ启动" << endl;
    return true;
}

int Consumer_MQ::Consumer(char *&msg, size_t &length)
{
    amqp_rpc_reply_t reply;
    int ret = 0;

    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(connection);
    struct timeval timeout = {7, 0};
    // 接收消息
    reply = amqp_consume_message(connection, &envelope, &timeout, 0);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        if (reply.library_error == AMQP_STATUS_TIMEOUT)
        {
            return 0;
        }
        else
        {
            perror("MessageQueue error:");
            amqp_channel_close(connection, 1, AMQP_REPLY_SUCCESS);
            amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
            amqp_destroy_connection(connection);
            reconnect = 1;
            return -1;
        }
    }

    length = envelope.message.body.len;
    msg = new char[length + 1];
    memcpy(msg, envelope.message.body.bytes, length);

    // 销毁消息包
    amqp_destroy_envelope(&envelope);

    return 1;
}

int Consumer_MQ::Consumer_Close()
{
    amqp_rpc_reply_t reply;
    int ret = 0;
    reply = amqp_channel_close(connection, 1, AMQP_REPLY_SUCCESS);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("consumer: closing channel");
        ret = -1;
    }
    reply = amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("consumer: closing connection");
        ret = -2;
    }
    ret = amqp_destroy_connection(connection);
    if (ret != AMQP_STATUS_OK)
    {
        perror("consumer: Ending connection");
        ret = -3;
    }

    cout << "Consumer_MQ退出" << endl;
    return ret;
}

bool Producer_MQ::Producer_Connect()
{
    amqp_socket_t *aqmp_socket = NULL;
    amqp_rpc_reply_t reply;

    // 分配和初始化一个新的链接对象
    connection = amqp_new_connection();
    // 创建tcp socket链接
    aqmp_socket = amqp_tcp_socket_new(connection);
    if (!aqmp_socket)
    {
        perror("producer: creating TCP socket");
        reconnect = 1;
        amqp_destroy_connection(connection);
        return false;
    }
    struct timeval timeout = {10, 0};
    // 非阻塞方式打开socket链接
    int socket_fd = amqp_socket_open_noblock(aqmp_socket, m_honstname.c_str(), m_port, &timeout);
    if (socket_fd == -1)
    {
        perror("producer: opening TCP socket");
        reconnect = 1;
        amqp_destroy_connection(connection);
        return false;
    }

    reply = amqp_login(connection, "MQTest", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, "admin", "hyx26528");
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("producer: failed to login rabbitmq");
        reconnect = 1;
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }

    amqp_channel_open(connection, 1);
    reply = amqp_get_rpc_reply(connection);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("producer: Opening channel");
        reconnect = 1;
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(connection);
        return false;
    }

    cout << "Producer_MQ启动" << endl;
    return true;
}

int Producer_MQ::Producer_Publish(char *msg, size_t length)
{
    assert(msg != nullptr);

    amqp_bytes_t message_bytes;
    message_bytes.bytes = msg;
    message_bytes.len = length;

    int ret = AMQP_STATUS_OK;

    amqp_basic_properties_t properties;
    properties._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    properties.content_type = amqp_cstring_bytes("text/plain");
    properties.delivery_mode = 2;
    // 发布消息
    ret = amqp_basic_publish(connection, 1, amqp_cstring_bytes(m_exchange.c_str()), amqp_cstring_bytes(m_routingkey.c_str()), 0, 0, &properties, message_bytes);

    if (ret != AMQP_STATUS_OK)
    {
        reconnect = 1;
        perror("producer: publish failed");
        return -1;
    }
    else
    {
        // cout << "producer publish: " << msg << endl;
        return 1;
    }
    return 0;
}

int Producer_MQ::Producer_Close()
{
    amqp_rpc_reply_t reply;
    int ret = 0;
    reply = amqp_channel_close(connection, 1, AMQP_REPLY_SUCCESS);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("producer: Closing channel failed");
        ret = -1;
    }
    reply = amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        perror("producer: Closing connection failed");
        ret = -2;
    }
    ret = amqp_destroy_connection(connection);
    if (ret != AMQP_STATUS_OK)
    {
        perror("producer: Ending connection");
        ret = -3;
    }

    cout << "Producer_MQ退出" << endl;
    return ret;
}