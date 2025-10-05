#pragma once

#include <memory>
#include <mutex>

#include "utils.hpp"

namespace wwstorage {
const char *ConfigFile = "config.conf";

class Config {
public:
    bool ReadConfig()
    {
        wwlog::GetLogger("asynclogger")->Info("ReadConfig start.");

        wwstorage::File file(ConfigFile);
        std::string content;
        if (!file.GetContent(&content)) {
            return false;
        }

        Json::Value root;
        wwstorage::JsonConveter::FromJsonString(content, &root);

        server_ip_ = root["server_ip"].asString();
        server_port_ = root["server_port"].asInt();
        download_prefix_ = root["download_prefix"].asString();
        deep_storage_dir_ = root["deep_storage_dir"].asString();
        low_storage_dir_ = root["low_storage_dir"].asString();
        storage_info_ = root["storage_info"].asString();
        bundle_format_ = root["bundle_format"].asInt();

        return true;
    }
    static Config* GetInstance()
    {
        if (instance_ == nullptr) {
            mutex_.lock();
            if (instance_ == nullptr) {
                instance_ = new Config();
            }
            mutex_.unlock();
        }
        return instance_;
    }
    int GetServerPort() { return server_port_; }
    std::string GetServerIp() { return server_ip_; }
    std::string GetDownloadPrefix() { return download_prefix_; }
    std::string GetDeepStorageDir() { return deep_storage_dir_; }
    std::string GetLowStorageDir() { return low_storage_dir_; }
    std::string GetStorageInfo() { return storage_info_; }
    int GetBundleFormat() { return bundle_format_; }


private:
    static std::mutex mutex_;
    static Config *instance_;
    Config()
    {
        if (ReadConfig() == false) {
            wwlog::GetLogger("asynclogger")->Fatal("ReadConfig failed.");
            return ;
        }
        wwlog::GetLogger("asynclogger")->Info("ReadConfig complicate.");
    }

private:
    int server_port_;
    std::string server_ip_;
    std::string download_prefix_;
    std::string deep_storage_dir_;
    std::string low_storage_dir_;
    std::string storage_info_;
    int bundle_format_;
};

std::mutex Config::mutex_;
Config *Config::instance_ = nullptr;
}  // namespace wwstorage