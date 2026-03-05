#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <mutex>
#include <thread>
#include <chrono>
#include <iomanip>
#include <filesystem>

// --------------------------------------------------------------------------------
// [新增] 智能类型适配器
// 作用：如果类型 T (比如 QString) 拥有 .toStdString() 方法，则自动转换为 std::string 输出
// 优点：解决了 AppConfig.cpp 中 QString 无法输出的问题，同时不需要在 Logger.h 中包含 Qt 头文件
//       从而保证了 WGCCapture 等纯 C++ 模块的兼容性。
// --------------------------------------------------------------------------------
template<typename T>
inline auto operator<<(std::ostream& os, const T& t) -> decltype(t.toStdString(), os) {
    return os << t.toStdString();
}
// --------------------------------------------------------------------------------

// 简单的日志级别枚举
enum class LogLevel { INFO, WARN, ERR };

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void log(LogLevel level, const std::string& msg) {
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&in_time_t), "%H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";

        switch (level) {
        case LogLevel::INFO: ss << "[INFO]"; break;
        case LogLevel::WARN: ss << "[WARN]"; break;
        case LogLevel::ERR:  ss << "[ERR ]"; break;
        }

        ss << "[" << std::this_thread::get_id() << "] " << msg;
        std::string finalMsg = ss.str();

        std::lock_guard<std::mutex> lock(m_mutex);

#ifdef NDEBUG
        // Release 模式：只写文件
        if (m_logFile.is_open()) {
            m_logFile << finalMsg << std::endl;
            m_logFile.flush(); // 确保奔溃前日志已写入
        }
#else \
    // Debug 模式：写控制台 (也可以同时写文件)
        std::cout << finalMsg << std::endl;
#endif
    }

private:
    Logger() {
#ifdef NDEBUG
        // Release 模式下打开日志文件
        // 获取当前执行目录（C++17）
        try {
            std::string path = "wgc_capture.log";
            m_logFile.open(path, std::ios::out | std::ios::app);
        } catch (...) {}
#endif
    }

    ~Logger() {
        if (m_logFile.is_open()) m_logFile.close();
    }

    std::mutex m_mutex;
    std::ofstream m_logFile;
};

// 宏定义适配旧接口，支持流式输入
#define LOG_INTERNAL(level, msg_stream) \
{ \
        std::stringstream _ss; \
        _ss << msg_stream; \
        Logger::instance().log(level, _ss.str()); \
}

#define LOG_INFO(msg)  LOG_INTERNAL(LogLevel::INFO, msg)
#define LOG_WARN(msg)  LOG_INTERNAL(LogLevel::WARN, msg)
#define LOG_ERROR(msg) LOG_INTERNAL(LogLevel::ERR, msg)
