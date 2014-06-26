#include <http_server.hpp>
#include <zlib_compression.hpp>
#include <exception>
#include <cstring>

#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>



namespace {
const std::string CRLF = "\r\n";
const std::string KV_SEPARATOR = ": ";
const std::string STATUS_CODE_STR[] = {
    "HTTP/1.1 200 OK\r\n",
    "HTTP/1.1 404 Not Found\r\n",
    "HTTP/1.1 503 Service Unavailable\r\n",
    "HTTP/1.1 508 Bad Handler ( Recurive threaded switch)\r\n",
    "HTTP/1.1 501 Not Implemented\r\n",
};


}

namespace tws {

class ServerException: public std::exception {
    char msg_[64];
public:
    ServerException(const char *msg) { strncpy(msg_, msg, 64); }
    const char *what() const noexcept { return msg_; }
};

class HttpConnection
    : public boost::enable_shared_from_this<HttpConnection>
{
    enum State {
        kReadingHeader,
        kReadingPost,
        kProcessing,
        kWriteHeader,
        kWriteBody,
    };

    boost::asio::ip::tcp::socket socket_;
    HttpServerInter *http_server_;

    int state_;
    int http_ret_;

    unsigned int postsize_;
    std::string request_;


    std::array<char, 8192> buffer_;

    Request req_;
    Response resp_;


public:
    HttpConnection(HttpServerInter* http_server);


    void start();
    void handle_read(const boost::system::error_code& e,
                std::size_t bytes_transferred);

    void process_request();
    int parse_request_header();
    int try_parse_request(std::size_t bytes_transferred);
    void begin_response();
    void handle_write(const boost::system::error_code& e);
    boost::asio::ip::tcp::socket& socket()  { return socket_;}
};


typedef boost::shared_ptr<HttpConnection> HttpConnPtr;
class HttpServerInter
{
    //friend class HttpServer;
    friend class HttpConnection;

private:
    boost::asio::io_service io_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::thread **threads_;
    int threadnum_;
    RequestHandler req_handler_;
    
    std::list<HttpConnPtr> threadpool_q_;
    std::condition_variable threadpool_cv_;
    std::mutex threadpool_m_;

    void start_accept();
    void handle_accept(HttpConnPtr new_conn,
        const boost::system::error_code& error);

    void push_to_threadpool(HttpConnPtr conn);

    void thread_proc(int id);
public:
    HttpServerInter(unsigned short port,
            RequestHandler main_handler, int threadnum);
    void set_handler(RequestHandler handler);
    void run();
    void stop();
};

void Request::clear()
{
    type_ = HTTP_INVALID;
    path_.clear();
    postdata_.clear();
    threaded_ = false;
}

void Response::clear()
{
    headers_.clear();
    body_.clear();
#ifdef HTTP_COMPRESSION
    compression_ = 5;
#endif

}

#ifdef HTTP_COMPRESSION
void Response::set_compression(int level)
{
    compression_ = level;
    if (compression_ < 0)
        compression_ = 0;
    if (compression_ > 9)
        compression_ = 9;
}
#endif

HttpConnection::HttpConnection(HttpServerInter* http_server)
        : socket_(http_server->io_),
        http_server_(http_server)
{
    req_.clear();
    resp_.clear();
}

void HttpConnection::start() 
{
    state_ = kReadingHeader;
    socket_.async_read_some(boost::asio::buffer(buffer_),
        boost::bind(&HttpConnection::handle_read, shared_from_this(),
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
}

void HttpConnection::handle_read(const boost::system::error_code& e,
    std::size_t bytes_transferred)
{
    if (!e) {
        int ret = try_parse_request(bytes_transferred);
        if (ret == 0) {
            //pasre succeed
            state_ = kProcessing;
            process_request();
        } else if (ret > 0) {
            //go on read
            socket_.async_read_some(boost::asio::buffer(buffer_),
                boost::bind(&HttpConnection::handle_read, shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
        } else {
            // < 0 parse failed
            socket_.close();
        }
    } else {
        socket_.close();
    }
}

void HttpConnection::process_request()
{
    http_ret_ = http_server_->req_handler_(resp_, req_);
    if (http_ret_ == HTTP_SWITCH_THREAD) {
        if (req_.threaded_ == true) {
            http_ret_ = HTTP_508;
            resp_.set_body("");
            begin_response();
        } else {
            /* push to http_server's thread pool */
            req_.threaded_ = true;
            if (http_server_->threadnum_ == 0) {
                ServerException e("Useing threadpool in callback handler"
                        "but thread num set to zero");
                throw e;
            }
            http_server_->push_to_threadpool(shared_from_this());
        }
    } else {
        begin_response();
    }
}

int HttpConnection::parse_request_header()
{
    size_t spos;
    const char *h;
    if ((spos = request_.find("\r\n\r\n")) == std::string::npos) 
        return -1;
    spos += 4; //after \r\n\r\n
    h = request_.c_str();

#define PREFIX_EQ(haystack, needle) (::strncmp(haystack, needle, ::strlen(needle)) == 0)
    if (PREFIX_EQ(h, "GET ")) {
        req_.type_ = HTTP_GET;
        h += ::strlen("GET ");
    } else if (PREFIX_EQ(h, "POST ")) {
        req_.type_ = HTTP_POST;
        state_ = kReadingPost;
        req_.postdata_ = request_.substr(spos);
        h += ::strlen("POST ");
    } else if (PREFIX_EQ(h, "HEAD ")) {
        req_.type_ = HTTP_HEAD;
        h += ::strlen("HEAD ");
    } else if (PREFIX_EQ(h, "PUT ")) {
        req_.type_ = HTTP_PUT;
        state_ = kReadingPost;
        req_.postdata_ = request_.substr(spos);
        h += ::strlen("PUT ");
    } else {
        // not begin with GET/POST/PUT/HEAD..
        return  -1;
    }


    while(!PREFIX_EQ(h, " HTTP/1.")) {
        req_.path_.append(1, *h);
        h++;
    }

    h += ::strlen(" HTTP/1.1\r\n");

    auto split_kv = [this](const char *s) {
        const char *p = s;
        std::string key, value;
        bool inkey = true;
        for( ; *p != '\0'; ) {
            if (PREFIX_EQ(p, "\r\n")) {
                // \r\n found
                if (!key.empty())
                    this->req_.headers_[key] = value;
                return p + 2;
            }
            if (inkey) {
                // before  " : "
                if (PREFIX_EQ(p, ": ")) {
                    p += 2;
                    inkey = false;
                    continue;
                } else if (PREFIX_EQ(p, " : ")) {
                    p += 3;
                    inkey = false;
                    continue;
                } else {
                    key.append(1, *p);
                }
            } else {
                // after " : "
                value.append(1, *p);
            }
            p++;
        }
        return p;
    };
    while(!PREFIX_EQ(h, "\r\n")) {
        h = split_kv(h);
    }
#undef PREFIX_EQ
    auto it = req_.headers_.find("Content-Length");
    if (it != req_.headers_.end()) {
        postsize_ = ::atol(it->second.c_str());
    }
    return 0;
}

int HttpConnection::try_parse_request(std::size_t bytes_transferred)
{
    if (state_ == kReadingHeader) {
        request_.append(buffer_.data(), bytes_transferred);
        if (parse_request_header() < 0) {
            if (request_.size() > 8192)
                // header size less than 8K count as valid
                return -1;
            else 
                return 1;
        }
        if (state_ == kReadingPost && req_.postdata_.size() < postsize_) 
            return 1;
        else 
            return 0;
    } else if (state_ == kReadingPost) {
        req_.postdata_.append(buffer_.data(), bytes_transferred);
        if (req_.postdata_.size() >= postsize_) 
            return 0;
        else 
            return 1;
    }
    // impossible to reach
    return -1;
}

void HttpConnection::begin_response()
{
    if (http_ret_ < 0 || http_ret_ >= HTTP_END) {
        http_ret_ = HTTP_503;
    }
    if (http_ret_ != HTTP_200 && resp_.body_.empty()) {
        resp_.set_body("<html><body><h1>" + STATUS_CODE_STR[http_ret_] + "</h1></body></html>");
        resp_.set_header("Content-Type", "text/html");
    }
    if (resp_.headers_.count("Server") == 0) 
        resp_.set_header("Server", "SimpleWebSvr/1.0");
    if (resp_.headers_.count("Connection") == 0) 
        resp_.set_header("Connection", "Keep-Alive");
#ifdef HTTP_COMPRESSION
    auto it = req_.headers_.find("Accept-Encoding");
    if (it != req_.headers_.end()
            && it->second.find("deflate") != std::string::npos) {
        // client support deflate compression
        if (!resp_.body_.empty()) {
            std::string deflated = zlib_compress(resp_.body_);
            if (!deflated.empty()) {
                resp_.body_ = deflated;
                resp_.set_header("Content-Encoding", "deflate");
            }
        }
    }
#endif
    if (resp_.headers_.count("Content-Length") == 0) 
        resp_.set_header("Content-Length", resp_.body_.size());

    std::vector<boost::asio::const_buffer> buffers;
    if (http_ret_ >= 0 && http_ret_ < HTTP_END) {
        buffers.push_back(boost::asio::buffer(STATUS_CODE_STR[http_ret_]));
        for(auto it = resp_.headers_.begin();
                it != resp_.headers_.end(); it++) {
            buffers.push_back(boost::asio::buffer(it->first));
            buffers.push_back(boost::asio::buffer(KV_SEPARATOR));
            buffers.push_back(boost::asio::buffer(it->second));
            buffers.push_back(boost::asio::buffer(CRLF));
        }
        buffers.push_back(boost::asio::buffer(std::string(CRLF)));
        buffers.push_back(boost::asio::buffer(resp_.body_));
        boost::asio::async_write(socket_, buffers,
            boost::bind(&HttpConnection::handle_write, shared_from_this(),
            boost::asio::placeholders::error));
    } else {
        socket_.close();
    }
}

void HttpConnection::handle_write(const boost::system::error_code& e) 
{
    if (!e) {
        boost::system::error_code ignored_ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    }
    //socket_.close();
    request_.clear();
    req_.clear();
    resp_.clear();
    postsize_ = 0;
    start();
}

void Response::set_header(const std::string& key, const std::string &value)
{ 
    headers_[key] = value; 
}

void Response::set_header(const std::string& key, long value)
{
    char strvalue[24];
    snprintf(strvalue, 24, "%ld", value);
    set_header(key, std::string(strvalue));
}

void HttpServerInter::start_accept()
{
    HttpConnPtr new_conn =
        HttpConnPtr(new HttpConnection(this));

    acceptor_.async_accept(new_conn->socket(),
        boost::bind(&HttpServerInter::handle_accept, this, new_conn,
        boost::asio::placeholders::error));
}

void HttpServerInter::handle_accept(HttpConnPtr new_conn,
  const boost::system::error_code& error)
{
    if (!error) {
        new_conn->start();
    }
    start_accept();
}

void HttpServerInter::push_to_threadpool(HttpConnPtr conn) 
{
    /* push conn to queue belongs to least busy thread */
    std::lock_guard<std::mutex> lk(threadpool_m_);
    threadpool_q_.push_back(conn);
    threadpool_cv_.notify_one();
}

void HttpServerInter::thread_proc(int id)
{
    std::list<HttpConnPtr> &q = threadpool_q_; //alias

    (void)(id);
    for(;;) {
        HttpConnPtr conn;
        std::unique_lock<std::mutex> lk(threadpool_m_);
        if (q.size() > 0) {
            conn = q.front();
            q.pop_front();
            lk.unlock();
        } else {
            threadpool_cv_.wait(lk);
            continue;
        }
        conn->process_request();
    }
}

void HttpServerInter::set_handler(RequestHandler handler)
{
    req_handler_ = handler;
}

void HttpServerInter::run()
{
    io_.run();
}

void HttpServerInter::stop()
{
    //not implemented yet
}

HttpServerInter::HttpServerInter(unsigned short port,
        RequestHandler main_handler, int threadnum)
    : io_(), 
      acceptor_(io_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      threadnum_(threadnum)
{
    threads_ = new std::thread*[threadnum_];
    for(int i = 0; i < threadnum_; i++)
        threads_[i] = new std::thread(
                &HttpServerInter::thread_proc, this, i);
    req_handler_ = main_handler;
    start_accept();
}

HttpServer::HttpServer(unsigned short port,
        RequestHandler main_handler, int threadnum)
    : inter_(new HttpServerInter(port, main_handler, threadnum))
{
}

void HttpServer::set_handler(RequestHandler handler)
{
    inter_->set_handler(handler);
}

void HttpServer::run()
{
    inter_->run();
}

void HttpServer::stop()
{
    inter_->stop();
}

void HttpServer::stat()
{
    //not implement
}

}
