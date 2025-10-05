#pragma once

#include <event.h>
#include <event2/http.h>
#include <evhttp.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <regex>

#include "data_manager.hpp"
#include "lib/base64.h"

extern wwstorage::DataManager *data_;

namespace wwstorage {
class Service {
public:
    Service()
    {
#ifdef DEBUG_LOG
        wwlog::GetLogger("asynclogger")->Debug("Service construct start.");
#endif
        server_port_ = Config::GetInstance()->GetServerPort();
        server_ip_ = Config::GetInstance()->GetServerIp();
        download_prefix_ = Config::GetInstance()->GetDownloadPrefix();
#ifdef DEBUG_LOG
        wwlog::GetLogger("asynclogger")->Debug("Service construct end.");
#endif
    }
    bool RunModule()
    {
        // 初始化 libevent 和 HTTP 服务器
        event_base *base = event_base_new();
        if (base == nullptr) {
            wwlog::GetLogger("asynclogger")->Fatal("event_base_new error!");
            return false;
        }
        evhttp *httpd = evhttp_new(base);
        if (httpd == nullptr) {
            wwlog::GetLogger("asynclogger")->Fatal("evhttp_new error!");
            event_base_free(base);
            return false;
        }

        // 设置监听的地址和端口
        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port_);

        // 绑定端口和 ip
        if (evhttp_bind_socket(httpd, "0.0.0.0", server_port_) != 0) {
            wwlog::GetLogger("asynclogger")->Fatal("evhttp_bind_socket error!");
            evhttp_free(httpd);
            event_base_free(base);
            return false;
        }

        // 设置请求处理函数
        evhttp_set_gencb(httpd, GenHandler, nullptr);

        if (base) {
#ifdef DEBUG_LOG
            wwlog::GetLogger("asynclogger")->Debug("event_base_dispatch.");
#endif
            if (event_base_dispatch(base) == -1) {
                wwlog::GetLogger("asynclogger")->Fatal("event_base_dispatch error!");
            }
        }

        if (httpd) evhttp_free(httpd);
        if (base) event_base_free(base);
        return true;
    }

private:
    static std::string GenerateModernFileList(const std::vector<StorageInfo> &files)
    {
        std::stringstream ss_html;
        ss_html << "<div class='file-list'><h3>已上传文件</h3>";

        for (const auto &file : files) {
            std::string file_name = File(file.storage_path_).FileName();
            std::string storage_type = "low";
            if (file.storage_path_.find("deep") != std::string::npos) {
                storage_type = "deep";
            }

            ss_html << "<div class='file-item'>";
            ss_html << "<div class='file-info'>";
            ss_html << "<span>📄" << file_name << "</span>";
            ss_html << "<span class='file-type'>";
            ss_html << (storage_type == "deep" ? "持久存储" : "普通存储");
            ss_html << "</span>";
            ss_html << "<span>" << FormatSize(file.fsize_) << "</span>";
            ss_html << "<span>" << TimeToString(file.mtime_) << "</span>";
            ss_html << "</div>";
            ss_html << "<button onclick=\"window.location='" << file.url_ << "'\">⬇️下载</button>";
            ss_html << "</div>";
        }

        ss_html << "</div>";
        return ss_html.str();
    }
    static void GenHandler(struct evhttp_request *request, void *arg)
    {
        std::string path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(request));
        path = UrlDecode(path);
        wwlog::GetLogger("asynclogger")->Info("request path: %s", path.c_str());

        if (path.find("/download/") != std::string::npos) {
            Download(request, arg);
        } else if (path.find("/upload") != std::string::npos) {
            Upload(request, arg);
        } else if (path.find("/") != std::string::npos) {
            ListShow(request, arg);
        } else {
            evhttp_send_error(request, HTTP_NOTFOUND, "Not Found");
        }
    }
    static void Upload(struct evhttp_request *request, void *arg)
    {
        wwlog::GetLogger("asynclogger")->Info("Upload() start.");

        struct evbuffer *buffer = evhttp_request_get_input_buffer(request);
        if (buffer == nullptr) {
            wwlog::GetLogger("asynclogger")->Error("evhttp_request_get_input_buffer is empty.");
            evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
            return;
        }
        size_t len = evbuffer_get_length(buffer);
        wwlog::GetLogger("asynclogger")->Info("Upload() receive data length: %u", len);
        if (len == 0) {
            wwlog::GetLogger("asynclogger")->Error("Upload() receive data length is zero.");
            evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
            return;
        }
        std::string content(len, 0);
        if (-1 == evbuffer_copyout(buffer, (void *)content.c_str(), len)) {
            wwlog::GetLogger("asynclogger")->Error("evbuffer_copyout error.");
            evhttp_send_error(request, HTTP_INTERNAL, NULL);
            return;
        }

        // 获取并解码文件名
        std::string filename = evhttp_find_header(request->input_headers, "FileName");
        filename = base64_decode(filename);

        std::string storage_type = evhttp_find_header(request->input_headers, "StorageType");
        std::string storage_path;
        if (storage_type == "low") {
            storage_path = Config::GetInstance()->GetLowStorageDir();
        } else if (storage_type == "deep") {
            storage_path = Config::GetInstance()->GetDeepStorageDir();
        } else {
            wwlog::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_BADREQUEST");
            evhttp_send_reply(request, HTTP_BADREQUEST, "Illegal storage type.", nullptr);
            return;
        }

        // 如果不存在就创建 low 或者 deep 目录
        File dir_create(storage_path);
        dir_create.CreateDirectory();

        // 目录创建后加上文件名就是最终的存储路径
        storage_path += filename;
#ifdef DEBUG_LOG
        wwlog::GetLogger("asynclogger")->Debug("final storage path: %s", storage_path.c_str());
#endif

        // 看路径里是 low 还是 deep 存储，是 deep 就压缩，是 low 就直接写入
        File file(storage_path);
        if (storage_path.find("low_storage") != std::string::npos) {
            if (file.SetContent(content.c_str(), content.size()) == false) {
                wwlog::GetLogger("asynclogger")->Error("low_storage write error.");
                evhttp_send_reply(request, HTTP_INTERNAL, "Internal Server Error", nullptr);
                return;
            } else {
                wwlog::GetLogger("asynclogger")->Info("low_storage success.");
            }
        } else {
            if (file.Compress(content, Config::GetInstance()->GetBundleFormat()) == false) {
                wwlog::GetLogger("asynclogger")->Error("deep_storage compress error.");
                evhttp_send_reply(request, HTTP_INTERNAL, "Internal Server Error", nullptr);
                return;
            } else {
                wwlog::GetLogger("asynclogger")->Info("deep_storage success.");
            }
        }

        // 添加存储文件信息
        StorageInfo info;
        info.NewStorageInfo(storage_path);
        data_->Insert(info);

        // 返回成功响应
        evhttp_send_reply(request, HTTP_OK, "OK", nullptr);
        wwlog::GetLogger("asynclogger")->Info("upload finish!");
    }
    static void Download(struct evhttp_request *request, void *arg)
    {
        // 1. 获取客户端请求的资源路径path   req.path
        // 2. 根据资源路径，获取StorageInfo
        StorageInfo info;
        std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(request));
        resource_path = UrlDecode(resource_path);
        data_->GetOneByURL(resource_path, &info);
        wwlog::GetLogger("asynclogger")->Info("request resource_path:%s", resource_path.c_str());

        std::string download_path = info.storage_path_;
        // 2.如果压缩过了就解压到新文件给用户下载
        if (info.storage_path_.find(Config::GetInstance()->GetLowStorageDir()) == std::string::npos) {
            wwlog::GetLogger("asynclogger")->Info("uncompressing:%s", info.storage_path_.c_str());
            File fu(info.storage_path_);
            download_path =
                Config::GetInstance()->GetLowStorageDir() +
                std::string(download_path.begin() + download_path.find_last_of('/') + 1, download_path.end());
            File dirCreate(Config::GetInstance()->GetLowStorageDir());
            dirCreate.CreateDirectory();
            fu.UnCompress(download_path);  // 将文件解压到low_storage下去或者再创一个文件夹做中转
        }
        wwlog::GetLogger("asynclogger")->Info("request download_path:%s", download_path.c_str());
        File fu(download_path);
        if (fu.Exists() == false && info.storage_path_.find("deep_storage") != std::string::npos) {
            // 如果是压缩文件，且解压失败，是服务端的错误
            wwlog::GetLogger("asynclogger")->Info("evhttp_send_reply: 500 - UnCompress failed");
            evhttp_send_reply(request, HTTP_INTERNAL, NULL, NULL);
        } else if (fu.Exists() == false && info.storage_path_.find("low_storage") == std::string::npos) {
            // 如果是普通文件，且文件不存在，是客户端的错误
            wwlog::GetLogger("asynclogger")->Info("evhttp_send_reply: 400 - bad request,file not exists");
            evhttp_send_reply(request, HTTP_BADREQUEST, "file not exists", NULL);
        }

        // 3.确认文件是否需要断点续传
        bool retrans = false;
        std::string old_etag;
        auto if_range = evhttp_find_header(request->input_headers, "If-Range");
        if (NULL != if_range) {
            old_etag = if_range;
            // 有If-Range字段且，这个字段的值与请求文件的最新etag一致则符合断点续传
            if (old_etag == GetETag(info)) {
                retrans = true;
                wwlog::GetLogger("asynclogger")
                    ->Info("%s need breakpoint continuous transmission", download_path.c_str());
            }
        }

        // 4. 读取文件数据，放入rsp.body中
        if (fu.Exists() == false) {
            wwlog::GetLogger("asynclogger")->Info("%s not exists", download_path.c_str());
            download_path += "not exists";
            evhttp_send_reply(request, 404, download_path.c_str(), NULL);
            return;
        }
        evbuffer *outbuf = evhttp_request_get_output_buffer(request);
        int fd = open(download_path.c_str(), O_RDONLY);
        if (fd == -1) {
            wwlog::GetLogger("asynclogger")->Error("open file error: %s -- %s", download_path.c_str(), strerror(errno));
            evhttp_send_reply(request, HTTP_INTERNAL, strerror(errno), NULL);
            return;
        }
        // 和前面用的evbuffer_add类似，但是效率更高，具体原因可以看函数声明
        if (-1 == evbuffer_add_file(outbuf, fd, 0, fu.Size())) {
            wwlog::GetLogger("asynclogger")
                ->Error("evbuffer_add_file: %d -- %s -- %s", fd, download_path.c_str(), strerror(errno));
        }
        // 5. 设置响应头部字段： ETag， Accept-Ranges: bytes
        evhttp_add_header(request->output_headers, "Accept-Ranges", "bytes");
        evhttp_add_header(request->output_headers, "ETag", GetETag(info).c_str());
        evhttp_add_header(request->output_headers, "Content-Type", "application/octet-stream");
        if (retrans == false) {
            evhttp_send_reply(request, HTTP_OK, "Success", NULL);
            wwlog::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_OK");
        } else {
            evhttp_send_reply(request, 206, "breakpoint continuous transmission", NULL);  // 区间请求响应的是206
            wwlog::GetLogger("asynclogger")->Info("evhttp_send_reply: 206");
        }
        if (download_path != info.storage_path_) {
            remove(download_path.c_str());  // 删除文件
        }
    }
    static void ListShow(struct evhttp_request *request, void *arg)
    {
        wwlog::GetLogger("asynclogger")->Info("ListShow()");

        // 获取所有文件的存储信息
        std::vector<StorageInfo> infos;
        data_->GetAll(&infos);

        // 读取 HTML 模板文件
        std::ifstream template_file("www/template.html");
        std::string template_content((std::istreambuf_iterator<char>(template_file)), std::istreambuf_iterator<char>());

        // 替换占位符
        template_content =
            std::regex_replace(template_content, std::regex("\\{\\{FILE_LIST\\}\\}"), GenerateModernFileList(infos));
        template_content = std::regex_replace(template_content, std::regex("\\{\\{BACKEND_URL\\}\\}"),
                                              "http://" + wwstorage::Config::GetInstance()->GetServerIp() + ":" +
                                                  std::to_string(wwstorage::Config::GetInstance()->GetServerPort()));
        // 获取请求的输出 evbuffer
        struct evbuffer *buffer = evhttp_request_get_output_buffer(request);
        auto response_body = template_content;
        evbuffer_add(buffer, (const void *)response_body.c_str(), response_body.size());
        evhttp_add_header(request->output_headers, "Content-Type", "text/html; charset=UTF-8");
        evhttp_send_reply(request, HTTP_OK, "OK", buffer);
        wwlog::GetLogger("asynclogger")->Info("ListShow() finish.");
    }
    static std::string GetETag(const StorageInfo &info)
    {
        // 自定义 ETag：filename-fsize-mtime
        File file(info.storage_path_);
        std::string etag = file.FileName();
        etag += "-" + std::to_string(info.fsize_);
        etag += "-" + std::to_string(info.mtime_);
        return etag;
    }
    static std::string FormatSize(uint64_t bytes)
    {
        const char *units[] = {"B", "KB", "MB", "GB"};
        int unit_index = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024 && unit_index < 3) {
            size /= 1024;
            unit_index++;
        }

        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
        return ss.str();
    }
    static std::string TimeToString(time_t t) { return std::ctime(&t); }

private:
    uint16_t server_port_;
    std::string server_ip_;
    std::string download_prefix_;
};
}  // namespace wwstorage