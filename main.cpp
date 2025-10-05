#define DEBUG_LOG

#include "service.hpp"
#include "../LogSystem/utils.hpp"
#include "../LogSystem/manage.hpp"
#include <thread>

wwstorage::DataManager *data_;

void service_module()
{
    wwstorage::Service service;
    wwlog::GetLogger("asynclogger")->Info("service step in RunModule.");
    service.RunModule();
}

void log_system_module_init()
{
    auto logger_config = &wwlog::Utils::LoggerConfig::Instance("/home/airkke/airkke/LogSystem/config.conf");
    auto thread_pool_ptr = new ThreadPool(logger_config->thread_count);
    std::shared_ptr<wwlog::LoggerBuilder> logger_builder(new wwlog::LoggerBuilder());
    logger_builder->SetLoggerName("asynclogger");
    logger_builder->AddLoggerFlush<wwlog::FileFlush>("./logfile/FileLog.log");
    logger_builder->AddLoggerFlush<wwlog::StdoutFlush>();
    logger_builder->SetThreadPool(std::shared_ptr<ThreadPool>(thread_pool_ptr));

    wwlog::LoggerManager::GetInstance().AddLogger(logger_builder->Build());
    wwlog::GetLogger("asynclogger")->Fatal("log_system_module_init success.");
}

int main(void)
{
    log_system_module_init();
    data_ = new wwstorage::DataManager();

    std::thread t1(service_module);
    t1.join();
    return 0;
}
