#pragma once

#include <pthread.h>

#include <unordered_map>

#include "config.hpp"

namespace wwstorage {

typedef struct StorageInfo {
    time_t mtime_;
    time_t atime_;
    size_t fsize_;
    std::string storage_path_;
    std::string url_;

    bool NewStorageInfo(const std::string &storage_path)
    {
        wwlog::GetLogger("asynclogger")->Info("NewStoarageInfo start.");
        File info_file(storage_path);
        if (!info_file.Exists()) {
            wwlog::GetLogger("asynclogger")->Info("info file not exists.");
            return false;
        }
        mtime_ = info_file.LastModifyTime();
        atime_ = info_file.LastAccessTime();
        fsize_ = info_file.Size();
        storage_path_ = storage_path;
        wwstorage::Config *config = wwstorage::Config::GetInstance();
        url_ = config->GetDownloadPrefix() + info_file.FileName();
        wwlog::GetLogger("asynclogger")
            ->Info("download_url:%s, mtime:%s, atime:%s, fsize:%u", url_.c_str(), ctime(&mtime_), ctime(&atime_),
                   fsize_);
        wwlog::GetLogger("asynclogger")->Info("NewStorageInfo end.");
        return true;
    }
} StorageInfo;

class DataManager {
public:
    DataManager()
    {
        wwlog::GetLogger("asynclogger")->Info("DataManager construct start.");
        storage_file_ = wwstorage::Config::GetInstance()->GetStorageInfo();
        pthread_rwlock_init(&rwlock_, NULL);
        need_presist_ = false;
        InitLoad();
        need_presist_ = true;
        wwlog::GetLogger("asynclogger")->Info("DataManager construct end.");
    }
    ~DataManager() { pthread_rwlock_destroy(&rwlock_); }

    bool InitLoad()
    {
        wwlog::GetLogger("asynclogger")->Info("init data manager");
        wwstorage::File storage_file(storage_file_);
        if (!storage_file.Exists()) {
            wwlog::GetLogger("asynclogger")->Info("there is no storage file info need to load.");
            return true;
        }

        std::string body;
        if (!storage_file.GetContent(&body)) return false;

        Json::Value root;
        wwstorage::JsonConveter::FromJsonString(body, &root);
        for (int i = 0; i < root.size(); i++) {
            StorageInfo info;
            info.fsize_ = root[i]["fsize_"].asInt();
            info.mtime_ = (time_t)root[i]["mtime_"].asInt64();
            info.atime_ = (time_t)root[i]["atime_"].asInt64();
            info.url_ = root[i]["url_"].asString();
            info.storage_path_ = root[i]["storage_path_"].asString();
            Insert(info);
        }
        return true;
    }
    bool Storage()
    {
        wwlog::GetLogger("asynclogger")->Info("message storage start.");
        std::vector<StorageInfo> arr;
        if (!GetAll(&arr)) {
            wwlog::GetLogger("asynclogger")->Warn("GetAll fail, can't get StorageInfo.");
            return false;
        }

        Json::Value root;
        for (auto &e : arr) {
            Json::Value item;
            item["mtime_"] = (Json::Int64)e.mtime_;
            item["atime_"] = (Json::Int64)e.atime_;
            item["fsize_"] = (Json::Int64)e.fsize_;
            item["url_"] = e.url_.c_str();
            item["storage_path_"] = e.storage_path_.c_str();
            root.append(item);
        }

        std::string body;
        JsonConveter::ToString(root, &body);
        wwlog::GetLogger("asynclogger")->Info("new message for StorageInfo%s", body.c_str());

        File file(storage_file_);
        if (file.SetContent(body.c_str(), body.size()) == false)
            wwlog::GetLogger("asynclogger")->Error("SetContent for StorageInfo Error");

        wwlog::GetLogger("asynclogger")->Info("message storage end.");
        return true;
    }
    bool Insert(const StorageInfo &info)
    {
        wwlog::GetLogger("asynclogger")->Info("data_message Insert start.");
        pthread_rwlock_wrlock(&rwlock_);
        table_[info.url_] = info;
        pthread_rwlock_unlock(&rwlock_);
        if (need_presist_ && Storage() == false) {
            wwlog::GetLogger("asynclogger")->Error("data_message Insert::Storage Error.");
            return false;
        }
        wwlog::GetLogger("asynclogger")->Info("data_message Insert end.");
        return true;
    }
    bool Update(const StorageInfo &info)
    {
        wwlog::GetLogger("asynclogger")->Info("data_message Update start.");
        pthread_rwlock_wrlock(&rwlock_);
        table_[info.url_] = info;
        pthread_rwlock_unlock(&rwlock_);
        if (Storage() == false) {
            wwlog::GetLogger("asynclogger")->Error("data_message Update::Storage Error.");
            return false;
        }
        wwlog::GetLogger("asynclogger")->Info("data_message Update end.");
        return true;
    }
    bool GetOneByURL(const std::string &key, StorageInfo *info)
    {
        pthread_rwlock_rdlock(&rwlock_);
        if (table_.find(key) == table_.end()) {
            pthread_rwlock_unlock(&rwlock_);
            return false;
        }
        *info = table_[key];
        pthread_rwlock_unlock(&rwlock_);
        return true;
    }
    bool GetOneByStoragePath(const std::string &storage_path, StorageInfo *info)
    {
        pthread_rwlock_rdlock(&rwlock_);
        for (auto &e : table_) {
            if (e.second.storage_path_ == storage_path) {
                *info = e.second;
                pthread_rwlock_unlock(&rwlock_);
                return true;
            }
        }
        pthread_rwlock_unlock(&rwlock_);
        return false;
    }
    bool GetAll(std::vector<StorageInfo> *array)
    {
        pthread_rwlock_rdlock(&rwlock_);
        for (auto &e : table_) array->emplace_back(e.second);
        pthread_rwlock_unlock(&rwlock_);
        return true;
    }

private:
    std::string storage_file_;
    pthread_rwlock_t rwlock_;
    std::unordered_map<std::string, StorageInfo> table_;
    bool need_presist_;
};

}  // namespace wwstorage