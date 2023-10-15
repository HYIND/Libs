#include "Redis_Server.h"

using namespace std;
using namespace sw::redis;

#define RedsiErrorCatch(code)                 \
    try                                       \
    {                                         \
        code                                  \
    }                                         \
    catch (const ReplyError &err)             \
    {                                         \
        perror(err.what());                   \
        return RedisResult::RedisReplyError;  \
    }                                         \
    catch (const TimeoutError &err)           \
    {                                         \
        perror(err.what());                   \
        return RedisResult::RedisTimeOut;     \
    }                                         \
    catch (const ClosedError &err)            \
    {                                         \
        perror(err.what());                   \
        connect = false;                      \
        return RedisResult::RedisClosedError; \
    }                                         \
    catch (const IoError &err)                \
    {                                         \
        perror(err.what());                   \
        return RedisResult::RedisIoError;     \
    }                                         \
    catch (const Error &err)                  \
    {                                         \
        perror(err.what());                   \
        return RedisResult::RedisOtherError;  \
    }

RedisResult Redis_Server::Connect()
{
    return Connect(string("127.0.0.1"));
}
RedisResult Redis_Server::Connect(string &&host, string &&password, int port, int Database)
{
    return Connect(host, password, port, Database);
}

RedisResult Redis_Server::Connect(string &host, string &password, int port, int Database)
{
    ConnectionOptions opts;
    opts.host = host;
    opts.port = port;
    opts.db = Database;
    if (password != "")
        opts.password = password;
    opts.socket_timeout = std::chrono::milliseconds(500);

    ConnectionPoolOptions pool_opts;
    pool_opts.size = 3;
    pool_opts.wait_timeout = std::chrono::milliseconds(500);

    try
    {
        // Create an Redis object, which is movable but NOT copyable.
        redis = Redis(opts, pool_opts);
        connect = true;
        return RedisResult::RedisOK;
    }
    catch (const ReplyError &err)
    {
        // WRONGTYPE Operation against a key holding the wrong kind of value
        perror(err.what());
        connect = false;
        return RedisResult::RedisReplyError;
    }
    catch (const TimeoutError &err)
    {
        // reading or writing timeout
        perror(err.what());
        connect = false;
        return RedisResult::RedisTimeOut;
    }
    catch (const ClosedError &err)
    {
        // the connection has been closed.
        perror(err.what());
        connect = false;
        return RedisResult::RedisClosedError;
    }
    catch (const IoError &err)
    {
        // there's an IO error on the connection.
        perror(err.what());
        connect = false;
        return RedisResult::RedisIoError;
    }
    catch (const Error &err)
    {
        // other errors
        perror(err.what());
        connect = false;
        return RedisResult::RedisOtherError;
    }
}

RedisResult Redis_Server::Set(string &key, string &value)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.set(key, value, std::chrono::milliseconds(0), UpdateType::EXIST);
        return RedisResult::RedisOK;)
}
RedisResult Redis_Server::Set(string &key, const char *ch, size_t length)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.set(key, string(ch,length), std::chrono::milliseconds(0), UpdateType::EXIST);
        return RedisResult::RedisOK;)
}
RedisResult Redis_Server::Get(string &key, string &value_out)
{
    if (!connect)
        return RedisResult::RedisNotConnect;

    RedsiErrorCatch(
        OptionalString val = redis.get(key);
        if (val) {
            value_out = *val;
            return RedisResult::RedisOK;
            // Dereference val to get the returned value of std::string type.
        } else return RedisResult::RedisNotFind;)
}
RedisResult Redis_Server::Get(string &key, char *ch_out, size_t &length_out)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        return RedisResult::RedisOK;)
}
RedisResult Redis_Server::Get(std::vector<std::string> &keys, std::vector<OptionalString> &values_out)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        // redis.mget(keys.begin(), keys.end(), values_out);
        return RedisResult::RedisOK;)
}
RedisResult Redis_Server::Get(std::vector<std::string> &keys, std::unordered_map<std::string, std::string> &values_out)
{
    std::vector<OptionalString> values;
    RedisResult result = Get(keys, values);
    if (result != RedisResult::RedisOK)
        return result;

    assert(keys.size() == values.size());
    for (int i = 0; i < values.size(); i++)
    {
        if (values[i])
        {
            values_out[keys[i]] = *(values[i]);
        }
        else
            values_out[keys[i]] = "";
    }
    return result;
}

RedisResult Redis_Server::PushList(string &key, std::vector<std::string> &list)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.rpush(key, list.begin(), list.end());
        return RedisResult::RedisOK;)
}

RedisResult Redis_Server::GetList(string &key, std::vector<std::string> &list_out)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.lrange(key, 0, -1, std::back_inserter(list_out));
        return RedisResult::RedisOK;)
}

RedisResult Redis_Server::SetHash(string &key, string &hash_key, string &hash_value)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.hset(key, hash_key, hash_value);
        return RedisResult::RedisOK;)
}
RedisResult Redis_Server::SetHash(string &key, std::unordered_map<std::string, std::string> &hash_map)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.hmset(key, hash_map.begin(), hash_map.end());
        return RedisResult::RedisOK;)
}

RedisResult Redis_Server::GetHashMap(string &key, std::unordered_map<std::string, std::string> &hash_map_out)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.hgetall(key, std::inserter(hash_map_out, hash_map_out.begin()));
        return RedisResult::RedisOK;)
}

RedisResult Redis_Server::GetHashMapByKeys(string &key, std::vector<string> &hash_keys, std::vector<OptionalString> &hash_values_out)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        // redis.hmget(key, hash_keys.begin(), hash_keys.end(), std::back_inserter(hash_values_out));
        return RedisResult::RedisOK;)
}
RedisResult Redis_Server::GetHashMapByKeys(string &key, std::vector<string> &hash_keys, std::unordered_map<std::string, std::string> &hash_map_out)
{
    std::vector<OptionalString> hash_values;
    RedisResult result = GetHashMapByKeys(key, hash_keys, hash_values);
    if (result != RedisResult::RedisOK)
        return result;

    assert(hash_keys.size() == hash_values.size());
    for (int i = 0; i < hash_values.size(); i++)
    {
        if (hash_values[i])
        {
            hash_map_out[hash_keys[i]] = *(hash_values[i]);
        }
        else
            hash_map_out[hash_keys[i]] = "";
    }
    return result;
}

RedisResult Redis_Server::AddSet(std::string &key, std::string &value)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.sadd(key, value);
        return RedisResult::RedisOK;)
}
RedisResult Redis_Server::AddSet(std::string &key, std::unordered_set<std::string> &values)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.sadd(key, values.begin(), values.end());
        return RedisResult::RedisOK;)
}

RedisResult Redis_Server::GetSets(std::string &key, std::vector<std::string> &values_out)
{
    std::unordered_set<std::string> set;
    RedisResult result = GetSets(key, set);
    if (result != RedisResult::RedisOK)
        return result;

    values_out.assign(set.begin(), set.end());
    return result;
}
RedisResult Redis_Server::GetSets(std::string &key, std::unordered_set<std::string> &values_out)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        redis.smembers(key, std::inserter(values_out, values_out.begin()));
        return RedisResult::RedisOK;)
}

RedisResult Redis_Server::IsSetMemberExist(string &key, string &value)
{
    if (!connect)
        return RedisResult::RedisNotConnect;
    RedsiErrorCatch(
        if (redis.sismember(key, value)) return RedisResult::RedisOK;
        else return RedisResult::RedisNotFind;)
}

// cout << vector
template <typename T>
std::ostream &operator<<(std::ostream &out, const std::vector<T> &v)
{
    if (!v.empty())
    {
        out << '[';
        std::copy(v.begin(), v.end(), std::ostream_iterator<T>(out, ", "));
        out << "\b\b]"; // 删除末尾的", "
    }
    return out;
}

// cout << unordered_map
template <typename T, typename U>
std::ostream &operator<<(std::ostream &out, const std::unordered_map<T, U> &umap)
{
    out << '[';
    for (auto item : umap)
    {
        out << "(" << item.first << "," << item.second << "),";
    }
    out << "\b]"; // 删除末尾的","

    return out;
}

// cout << unorderd_set
template <typename T>
std::ostream &operator<<(std::ostream &out, const std::unordered_set<T> &uset)
{
    out << '(';
    for (auto item : uset)
    {
        cout << item << ",";
    }
    out << "\b)"; // 删除末尾的","

    return out;
}

/* // test code
int redistest()
{
    try
    {

        auto redis = Redis("tcp://127.0.0.1");
        /// ***** STRING commands *****
        redis.set("key", "val");
        auto val = redis.get("key"); // val is of type OptionalString. See 'API Reference' section for details.
        if (val)
        {
            // Dereference val to get the returned value of std::string type.
            std::cout << *val << std::endl;
        } // else key doesn't exist.

        /// ***** LIST commands *****
        // std::vector<std::string> to Redis LIST.
        std::vector<std::string> vec = {"a", "b", "c"};
        redis.rpush("list", vec.begin(), vec.end());

        // std::initializer_list to Redis LIST.
        redis.rpush("list", {"a", "b", "c"});

        // Redis LIST to std::vector<std::string>.
        vec.clear();
        redis.lrange("list", 0, -1, std::back_inserter(vec));

        cout << "list: " << vec << endl;

        /// ***** HASH commands *****
        redis.hset("hash", "field", "val");

        // Another way to do the same job.
        redis.hset("hash", std::make_pair("field", "val"));

        // std::unordered_map<std::string, std::string> to Redis HASH.
        std::unordered_map<std::string, std::string> m = {
            {"field1", "val1"},
            {"field2", "val2"}};
        redis.hmset("hash", m.begin(), m.end());

        // Redis HASH to std::unordered_map<std::string, std::string>.
        m.clear();
        redis.hgetall("hash", std::inserter(m, m.begin()));

        cout << "hash:" << m << endl;

        // Get value only.
        // NOTE: since field might NOT exist, so we need to parse it to OptionalString.
        std::vector<OptionalString> vals;
        redis.hmget("hash", {"field1", "field2"}, std::back_inserter(vals));

        /// ***** SET commands *****
        redis.sadd("set", "m1");

        // std::unordered_set<std::string> to Redis SET.
        std::unordered_set<std::string> set = {"m2", "m3"};
        redis.sadd("set", set.begin(), set.end());

        // std::initializer_list to Redis SET.
        redis.sadd("set", {"m2", "m3"});

        // Redis SET to std::unordered_set<std::string>.
        set.clear();
        redis.smembers("set", std::inserter(set, set.begin()));

        cout << "set:" << set << endl;

        if (redis.sismember("set", "m1"))
        {
            std::cout << "m1 exists" << std::endl;
        } // else NOT exist.

        /// ***** SORTED SET commands *****
        redis.zadd("sorted_set", "m1", 1.3);

        // std::unordered_map<std::string, double> to Redis SORTED SET.
        std::unordered_map<std::string, double> scores = {
            {"m2", 2.3},
            {"m3", 4.5}};
        redis.zadd("sorted_set", scores.begin(), scores.end());

        // Redis SORTED SET to std::vector<std::pair<std::string, double>>.
        // NOTE: The return results of zrangebyscore are ordered, if you save the results
        // in to `std::unordered_map<std::string, double>`, you'll lose the order.
        std::vector<std::pair<std::string, double>> zset_result;
        redis.zrangebyscore("sorted_set",
                            UnboundedInterval<double>{}, // (-inf, +inf)
                            std::back_inserter(zset_result));

        // Only get member names:
        // pass an inserter of std::vector<std::string> type as output parameter.
        std::vector<std::string> without_score;
        redis.zrangebyscore("sorted_set",
                            BoundedInterval<double>(1.5, 3.4, BoundType::CLOSED), // [1.5, 3.4]
                            std::back_inserter(without_score));

        // Get both member names and scores:
        // pass an back_inserter of std::vector<std::pair<std::string, double>> as output parameter.
        std::vector<std::pair<std::string, double>> with_score;
        redis.zrangebyscore("sorted_set",
                            BoundedInterval<double>(1.5, 3.4, BoundType::LEFT_OPEN), // (1.5, 3.4]
                            std::back_inserter(with_score));

        /// ***** SCRIPTING commands *****
        // Script returns a single element.
        auto num = redis.eval<long long>("return 1", {}, {});

        // Script returns an array of elements.
        std::vector<std::string> nums;
        redis.eval("return {ARGV[1], ARGV[2]}", {}, {"1", "2"}, std::back_inserter(nums));

        // mset with TTL
        auto mset_with_ttl_script = R"(
        local len = #KEYS
        if (len == 0 or len + 1 ~= #ARGV) then return 0 end
        local ttl = tonumber(ARGV[len + 1])
        if (not ttl or ttl <= 0) then return 0 end
        for i = 1, len do redis.call("SET", KEYS[i], ARGV[i], "EX", ttl) end
        return 1
    )";

        // Set multiple key-value pairs with TTL of 60 seconds.
        auto keys = {"key1", "key2", "key3"};
        std::vector<std::string> args = {"val1", "val2", "val3", "60"};
        redis.eval<long long>(mset_with_ttl_script, keys.begin(), keys.end(), args.begin(), args.end());

        /// ***** Pipeline *****
        // Create a pipeline.
        auto pipe = redis.pipeline();

        // Send mulitple commands and get all replies.
        auto pipe_replies = pipe.set("key", "value")
                                .get("key")
                                .rename("key", "new-key")
                                .rpush("list", {"a", "b", "c"})
                                .lrange("list", 0, -1)
                                .exec();

        // Parse reply with reply type and index.
        auto set_cmd_result = pipe_replies.get<bool>(0);

        auto get_cmd_result = pipe_replies.get<OptionalString>(1);

        // rename command result
        pipe_replies.get<void>(2);

        auto rpush_cmd_result = pipe_replies.get<long long>(3);

        std::vector<std::string> lrange_cmd_result;
        pipe_replies.get(4, back_inserter(lrange_cmd_result));

        /// ***** Transaction *****
        // Create a transaction.
        auto tx = redis.transaction();

        // Run multiple commands in a transaction, and get all replies.
        auto tx_replies = tx.incr("num0")
                              .incr("num1")
                              .mget({"num0", "num1"})
                              .exec();

        // Parse reply with reply type and index.
        auto incr_result0 = tx_replies.get<long long>(0);

        auto incr_result1 = tx_replies.get<long long>(1);

        std::vector<OptionalString> mget_cmd_result;
        tx_replies.get(2, back_inserter(mget_cmd_result));

        /// ***** Generic Command Interface *****
        // There's no *Redis::client_getname* interface.
        // But you can use *Redis::command* to get the client name.
        val = redis.command<OptionalString>("client", "getname");
        if (val)
        {
            std::cout << *val << std::endl;
        }

        // Same as above.
        auto getname_cmd_str = {"client", "getname"};
        val = redis.command<OptionalString>(getname_cmd_str.begin(), getname_cmd_str.end());

        // There's no *Redis::sort* interface.
        // But you can use *Redis::command* to send sort the list.
        std::vector<std::string> sorted_list;
        redis.command("sort", "list", "ALPHA", std::back_inserter(sorted_list));

        // Another *Redis::command* to do the same work.
        auto sort_cmd_str = {"sort", "list", "ALPHA"};
        redis.command(sort_cmd_str.begin(), sort_cmd_str.end(), std::back_inserter(sorted_list));

        /// ***** Redis Cluster *****
        // Create a RedisCluster object, which is movable but NOT copyable.
        auto redis_cluster = RedisCluster("tcp://127.0.0.1:7000");

        // RedisCluster has similar interfaces as Redis.
        redis_cluster.set("key", "value");
        val = redis_cluster.get("key");
        if (val)
        {
            std::cout << *val << std::endl;
        } // else key doesn't exist.

        // Keys with hash-tag.
        redis_cluster.set("key{tag}1", "val1");
        redis_cluster.set("key{tag}2", "val2");
        redis_cluster.set("key{tag}3", "val3");

        std::vector<OptionalString> hash_tag_res;
        redis_cluster.mget({"key{tag}1", "key{tag}2", "key{tag}3"},
                           std::back_inserter(hash_tag_res));
    }
    catch (const Error &e)
    {
        // Error handling.
        perror(e.what());
    }

    return 0;
}
 */