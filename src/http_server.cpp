#include "http_server.hpp"

using namespace tws;

HttpConnection::HttpConnection(HttpServer* http_server)
        : socket_(http_server->io_),
        http_server_(http_server), threaded_(false)
{
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
    http_ret_ = http_server_->req_handler_(shared_from_this());
    if (http_ret_ == HTTP_SWITCH_THREAD) {
        if (threaded_ == true) {
            http_ret_ = HTTP_508;
            set_body("");
            begin_response();
        } else {
            /* push to http_server's thread pool */
            threaded_ = true;
            http_server_->push_to_threadpool(shared_from_this());
        }
    } else {
        begin_response();
    }
}

int HttpConnection::try_parse_request(std::size_t bytes_transferred)
{
    if (state_ == kReadingHeader) {
        request_.append(buffer_.data(), bytes_transferred);
        size_t spos, spos2;
        if ((spos = request_.find("\r\n\r\n")) != std::string::npos) {
            if (request_.substr(0, 4) == "GET ") {
                type_ = HTTP_GET;
                spos2 = 4;
            } else if (request_.substr(0, 5) == "POST ") {
                type_ = HTTP_POST;
                state_ = kReadingPost;
                post_ = request_.substr(spos + 4);
                spos2 = 5;
            } else {
                // not begin with GET/POST/..
                return  -1;
            }

            size_t spos3 = request_.find(" HTTP/1.", spos2);
            if (spos3 == std::string::npos)
                return -1;

            path_ = request_.substr(spos2, spos3 - spos2);

            if (state_ == kReadingPost) {
                spos2 = request_.find("Content-Length: ");
                if (spos2 == std::string::npos)
                    return -1;
                spos2 += ::strlen("Content-Length: ");
                spos3 = request_.find("\r\n", spos2);
                if (spos3 == std::string::npos)
                    return -1;
                if (spos3 - spos2 > 16)
                    return -1;
                postsize_ = atoi(request_.substr(spos2, spos3).c_str());
            }
            
            if (state_ == kReadingPost && post_.size() < postsize_) 
                return 1;
            else {
                return 0;
            }
        }

        if (request_.size() > 4096)
            return -1;
        else 
            return 1;
    } else if (state_ == kReadingPost) {
        post_.append(buffer_.data(), bytes_transferred);
        if (post_.size() >= postsize_) 
            return 0;
        else 
            return 1;
    }

    return -1;
}

void HttpConnection::begin_response()
{
    if (http_ret_ < 0 || http_ret_ >= HTTP_END) {
        http_ret_ = HTTP_503;
    }
    if (http_ret_ != HTTP_200 && body_.empty()) {
        set_body("<html><body><h1>" + STATUS_CODE_STR[http_ret_] + "</h1></body></html>");
        set_header("Content-Type", "text/html");
    }
    if (headers_.count("Content-Length") == 0) 
        set_header("Content-Length", body_.size());
    if (headers_.count("Server") == 0) 
        set_header("Server", "SimpleWebSvr/1.0");
    set_header("Connection", "close");

    std::vector<boost::asio::const_buffer> buffers;
    if (http_ret_ >= 0 && http_ret_ < HTTP_END) {
        buffers.push_back(boost::asio::buffer(STATUS_CODE_STR[http_ret_]));
        for(std::map<std::string, std::string>::iterator it = headers_.begin();
                it != headers_.end(); it++) {
            buffers.push_back(boost::asio::buffer(it->first));
            buffers.push_back(boost::asio::buffer(KV_SEPARATOR));
            buffers.push_back(boost::asio::buffer(it->second));
            buffers.push_back(boost::asio::buffer(CRLF));
        }
        buffers.push_back(boost::asio::buffer(std::string(CRLF)));
        buffers.push_back(boost::asio::buffer(body_));
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
    header_.clear();
    request_.clear();
    body_.clear();
    post_.clear();
    path_.clear();
    headers_.clear();
    postsize_ = 0;
    start();
}

void HttpServer::start_accept()
{
    HttpConnPtr new_conn =
        HttpConnPtr(new HttpConnection(this));

    acceptor_.async_accept(new_conn->socket(),
        boost::bind(&HttpServer::handle_accept, this, new_conn,
        boost::asio::placeholders::error));
}

void HttpServer::handle_accept(HttpConnPtr new_conn,
  const boost::system::error_code& error)
{
    if (!error) {
        new_conn->start();
    }
    start_accept();
}

void HttpServer::push_to_threadpool(HttpConnPtr conn) 
{
    int choice = -1;
    for(int i = 0; i < threadnum_; i++) {
        if (threads_[i].t_ == NULL)
            break;
        if (choice < 0 || threads_[i].q_.size() < threads_[choice].q_.size())
            choice = i;
    }

    if ((choice < 0 || threads_[choice].q_.size())
            && threadnum_ < MAX_THREAD_SIZE) {
        /* no thread so far or too busy, create new thread */
        threads_[threadnum_].t_ = new std::thread(&HttpServer::thread_proc, this, threadnum_);
        choice = threadnum_;
        threadnum_++;
    }

    {
        /* push conn to queue belongs to least busy thread */
        std::lock_guard<std::mutex> lk(threads_[choice].m_);
        threads_[choice].q_.push_back(conn);
        threads_[choice].cv_.notify_one();
    }
}

void HttpServer::thread_proc(int id)
{
    for(;;) {
        std::unique_lock<std::mutex> lk(threads_[id].m_);
        while (threads_[id].q_.size() > 0) {
            HttpConnPtr conn = threads_[id].q_.front();
            threads_[id].q_.pop_front();
            conn->process_request();
        }
        threads_[id].cv_.wait(lk);
    }
}

HttpServer::HttpServer(boost::asio::io_service &io, unsigned short port,
        RequestHandler main_handler)
    : io_(io), 
      acceptor_(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      threadnum_(0)
{
    for(int i = 0; i < MAX_THREAD_SIZE; i++) 
        threads_[i].t_ = NULL;
    set_handler(main_handler);
    start_accept();
}


