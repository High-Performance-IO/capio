#ifndef REDIS_HPP
#define REDIS_HPP

#include "capio/logger.hpp"

#include <hiredis.h>

class RedisConnector {

    redisContext *context;

    std::string get_key(const std::string &key) const {
        if (const redisReply *reply =
                static_cast<redisReply *>(redisCommand(context, "GET %s", key.c_str()));
            reply != nullptr && reply->type == REDIS_REPLY_STRING) {
            return reply->str;
        }
        return "";
    }

  public:
    RedisConnector(const std::string &address, const int port) {
        START_LOG(gettid(), "call(addr=%s, port=%ld)", address.c_str(), port);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << node_name << " ] "
                  << "RedisConnector: connecting to " << address << ":" << port << std::endl;

        context = redisConnect(address.c_str(), port);
        if (context == NULL || context->err) {
            if (context) {
                ERR_EXIT("Redis connect error: %s\n", context->errstr);
            } else {
                ERR_EXIT("Can't allocate redis context\n");
            }
        }

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "RedisConnector initialization completed." << std::endl;
    };

    ~RedisConnector() { redisFree(context); }

    std::filesystem::path get_capio_dir() const {
        START_LOG(gettid(), "call()");
        return get_key("CAPIO_DIR");
    }

    std::string get_workflow_name() const {
        START_LOG(gettid(), "call()");
        if (auto name = get_key("WORKFLOW_NAME"); !name.empty()) {
            return name;
        }
        return CAPIO_DEFAULT_WORKFLOW_NAME;
    }

    std::string get_log_level() const {
        START_LOG(gettid(), "call()");
        if (auto level = get_key("CAPIO_LOG_LEVEL"); !level.empty()) {
            return level;
        }
        return "-1";
    }

    std::filesystem::path get_metadata_path() const {
        START_LOG(gettid(), "call()");
        if (auto path = get_key("CAPIO_METADATA_DIR"); !path.empty()) {
            return path;
        }
        return get_capio_dir() / ".capio_metadata";
    }
};

RedisConnector *redis_connector;

#endif // REDIS_HPP
