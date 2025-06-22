#include <pqxx/pqxx>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <iostream>

class PostgresConnectionPool {
public:
    class ConnectionHandle {
    public:
        ConnectionHandle(pqxx::connection* conn, PostgresConnectionPool& pool)
            : conn_(conn), pool_(pool) {}

        pqxx::connection* get() { return conn_; }

        ~ConnectionHandle() {
            pool_.release(conn_);
        }

        // Disallow copying
        ConnectionHandle(const ConnectionHandle&) = delete;
        ConnectionHandle& operator=(const ConnectionHandle&) = delete;

    private:
        pqxx::connection* conn_;
        PostgresConnectionPool& pool_;
    };

    PostgresConnectionPool(const std::string& connStr, size_t maxSize)
        : connectionString(connStr), maxPoolSize(maxSize) {
        for (size_t i = 0; i < maxPoolSize; ++i) {
            connectionPool.push(new pqxx::connection(connectionString));
        }
    }

    std::unique_ptr<ConnectionHandle> acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [&] { return !connectionPool.empty(); });

        auto* conn = connectionPool.front();
        connectionPool.pop();
        return std::make_unique<ConnectionHandle>(conn, *this);
    }

    ~PostgresConnectionPool() {
        while (!connectionPool.empty()) {
            auto* conn = connectionPool.front();
            connectionPool.pop();
            delete conn;
        }
    }

    // Prevent copying
    PostgresConnectionPool(const PostgresConnectionPool&) = delete;
    PostgresConnectionPool& operator=(const PostgresConnectionPool&) = delete;

private:
    void release(pqxx::connection* conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        connectionPool.push(conn);
        condVar_.notify_one();
    }

    std::string connectionString;
    size_t maxPoolSize;
    std::queue<pqxx::connection*> connectionPool;
    std::mutex mutex_;
    std::condition_variable condVar_;
};
