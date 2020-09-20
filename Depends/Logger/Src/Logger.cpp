#include "Logger/Logger.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/async.h"
#include "spdlog/async_logger.h"
#include "spdlog/fmt/fmt.h"
#include <array>
#include <cassert>

using namespace spdlog;

static std::string logName_;

std::shared_ptr<spdlog::logger> Logger::logger()
{
    return spdlog::get(logName_);
}

void Logger::init(const std::string& name, const std::string& level /*= "debug"*/, int logFileMode /* = 1*/)
{
    try
    {
        auto log = spdlog::get(name);
        if (log == nullptr)
        {
            spdlog::init_thread_pool(1024 * 9, 1);

            std::array<sink_ptr, 8> sinks;
            size_t sinks_count = 0;

            //4: disable console
            if ((logFileMode & 4) == 0)
            {
                sinks[sinks_count++] = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            }

            //1: enable log file and enable rotate
            //2: or other numbers: enable log file but disable rotate
            if ((logFileMode & 3) != 0)
            {
                auto s = time(nullptr);
                tm* t = localtime(&s);
                char ts[20];
                strftime(ts, 20, "%Y%m%d_%H%M%S", t);
                std::string path = fmt::format("./logs/{}_{}.log", name, ts);
                bool rotate_on_open = (logFileMode & 3) == 1;

                sinks[sinks_count++] = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, 100 * 1024 * 1024, 10, rotate_on_open);
            }

            assert(sinks_count <= sinks.size());
            log = std::make_shared<spdlog::async_logger>(name, sinks.data(), sinks.data() + sinks_count, spdlog::thread_pool());

            log->set_pattern("[%^%L%$][%H:%M:%S.%e] %v. [%s:%#, %t]");
            spdlog::register_logger(log);
            spdlog::set_default_logger(log);
            setLevel(level);
            spdlog::flush_every(std::chrono::seconds(1));
            spdlog::flush_on(spdlog::level::info);
        }
    }
    catch (...)
    {

    }
    logName_ = name;
}

void Logger::setLevel(const std::string& level)
{
    spdlog::set_level(spdlog::level::from_str(level));
}

void Logger::drop()
{
    spdlog::shutdown();
}
