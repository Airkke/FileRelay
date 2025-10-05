#include <assert.h>
#include <jsoncpp/json/json.h>
#include <sys/stat.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "../LogSystem/wwlog.hpp"
#include "lib/bundle.h"

namespace wwstorage {

static unsigned char ToHex(unsigned char x)
{
    if (x > 9)
        return x - 10 + 'A';
    else
        return x + '0';
}

static unsigned char FromHex(unsigned char x)
{
    unsigned char y;
    if (x >= 'A' && x <= 'Z')
        y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z')
        y = x - 'a' + 10;
    else if (x >= '0' && x <= '9')
        y = x - '0';
    else
        assert(0);
    return y;
}

static std::string UrlDecode(const std::string &str)
{
    std::string str_temp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++) {
        if (str[i] == '%') {
            assert(i + 2 < length);
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            str_temp += high * 16 + low;
        } else
            str_temp += str[i];
    }
    return str_temp;
}

class File {
public:
    File(const std::string &file_name) : file_name_(file_name) {}

    int64_t Size()
    {
        struct stat file_stat;
        auto ret = stat(file_name_.c_str(), &file_stat);
        if (ret == -1) {
            wwlog::GetLogger("asynclogger")->Info("%s, get file size failed: %s", file_name_.c_str(), strerror(errno));
            return -1;
        }
        return file_stat.st_size;
    }
    time_t LastAccessTime()
    {
        struct stat file_stat;
        auto ret = stat(file_name_.c_str(), &file_stat);
        if (ret == -1) {
            wwlog::GetLogger("asynclogger")
                ->Info("%s, get file access time failed: %s", file_name_.c_str(), strerror(errno));
            return -1;
        }
        return file_stat.st_atime;
    }
    time_t LastModifyTime()
    {
        struct stat file_stat;
        auto ret = stat(file_name_.c_str(), &file_stat);
        if (ret == -1) {
            wwlog::GetLogger("asynclogger")
                ->Info("%s, get file modify time failed: %s", file_name_.c_str(), strerror(errno));
            return -1;
        }
        return file_stat.st_mtime;
    }
    std::string FileName()
    {
        auto pos = file_name_.find_last_of("/");
        if (pos == std::string::npos) {
            return file_name_;
        }
        return file_name_.substr(pos + 1, std::string::npos);
    }
    bool GetPosLen(std::string *content, size_t pos, size_t len)
    {
        if (pos + len > Size()) {
            wwlog::GetLogger("asynclogger")->Info("needed data larger than file size.");
            return false;
        }

        std::ifstream ifs;
        ifs.open(file_name_, std::ios::binary);
        if (ifs.is_open() == false) {
            wwlog::GetLogger("asynclogger")->Info("%s, file open error.", file_name_.c_str());
            return false;
        }

        ifs.seekg(pos, std::ios::beg);
        content->resize(len);
        ifs.read(&(*content)[0], len);
        if (!ifs.good()) {
            wwlog::GetLogger("asynclogger")->Info("%s, read file content error.", file_name_.c_str());
            ifs.close();
            return false;
        }
        ifs.close();
        return true;
    }
    bool GetContent(std::string *content) { return GetPosLen(content, 0, Size()); }
    bool SetContent(const char *content, size_t len)
    {
        std::ofstream ofs;
        ofs.open(file_name_, std::ios::binary);
        if (ofs.is_open() == false) {
            wwlog::GetLogger("asynclogger")->Info("%s, file open error.", file_name_.c_str());
            return false;
        }

        ofs.write(content, len);
        if (!ofs.good()) {
            wwlog::GetLogger("asynclogger")->Info("%s, write file content error.", file_name_.c_str());
            ofs.close();
            return false;
        }
        ofs.close();
        return true;
    }
    bool Compress(const std::string &content, int format)
    {
        std::string packed = bundle::pack(format, content);
        if (packed.size() == 0) {
            wwlog::GetLogger("asynclogger")->Info("compress package size checked error.");
            return false;
        }
        File file(file_name_);
        if (file.SetContent(packed.c_str(), packed.size()) == false) {
            wwlog::GetLogger("asynclogger")->Info("filename: %s, compress file SetContent error.", file_name_.c_str());
            return false;
        }
        return true;
    }
    bool UnCompress(std::string &download_path)
    {
        std::string file_content;
        if (this->GetContent(&file_content) == false) {
            wwlog::GetLogger("asynclogger")
                ->Info("filename: %s, UnCompress read file content error.", file_name_.c_str());
            return false;
        }
        std::string unpacked = bundle::unpack(file_content);
        File file(download_path);
        if (file.SetContent(unpacked.c_str(), unpacked.size()) == false) {
            wwlog::GetLogger("asynclogger")
                ->Info("filename: %s, UnCompress write file content error.", file_name_.c_str());
            return false;
        }
        return true;
    }
    bool Exists() { return std::filesystem::exists(file_name_); }
    bool CreateDirectory()
    {
        if (Exists()) return true;
        return std::filesystem::create_directories(file_name_);
    }
    bool ScanDirectory(std::vector<std::string> *array)
    {
        for (auto &p : std::filesystem::directory_iterator(file_name_)) {
            if (std::filesystem::is_directory(p) == true) continue;
            array->push_back(std::filesystem::path(p).relative_path().string());
        }
        return true;
    }

private:
    std::string file_name_;
};

class JsonConveter {
public:
    static bool ToString(const Json::Value &input, std::string *output)
    {
        Json::StreamWriterBuilder write_builder;
        write_builder["emitUTF8"] = true;
        std::unique_ptr<Json::StreamWriter> writer(write_builder.newStreamWriter());
        std::stringstream json_stream;
        if (writer->write(input, &json_stream) != 0) {
            wwlog::GetLogger("asynclogger")->Info("Serialize error.");
            return false;
        }
        *output = json_stream.str();
        return true;
    }

    static bool FromJsonString(const std::string &input, Json::Value *output)
    {
        Json::CharReaderBuilder read_builder;
        std::unique_ptr<Json::CharReader> reader(read_builder.newCharReader());
        std::string err;
        if (reader->parse(input.c_str(), input.c_str() + input.size(), output, &err)) {
            wwlog::GetLogger("asynclogger")->Info("parse error");
            return false;
        }
        return true;
    }
};

}  // namespace wwstorage