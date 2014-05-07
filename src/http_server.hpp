#ifndef _HTTP_SERVER_
#define _HTTP_SERVER_
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>


namespace tws{

enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_HEAD,
    HTTP_INVALID,
};

enum STATUS_CODE {
    HTTP_SWITCH_THREAD = -100,
    HTTP_200 = 0,
    HTTP_404,
    HTTP_503,
    HTTP_508, /* unused by HTTP */
    HTTP_501,
    HTTP_END,
};

static const std::string STATUS_CODE_STR[] = {
    "HTTP/1.1 200 OK\r\n",
    "HTTP/1.1 404 Not Found\r\n",
    "HTTP/1.1 503 Service Unavailable\r\n",
    "HTTP/1.1 508 Bad Handler ( Recurive threaded switch)\r\n",
    "HTTP/1.1 501 Not Implemented\r\n",
};


static const std::string CRLF = "\r\n";
static const std::string KV_SEPARATOR = ": ";

class HttpServer;

class HttpConnection
    : public boost::enable_shared_from_this<HttpConnection>
{
    friend class HttpServer;
    enum State {
        kReadingHeader,
        kReadingPost,
        kProcessing,
        kWriteHeader,
        kWriteBody,
    };

    boost::asio::ip::tcp::socket socket_;
    HttpServer *http_server_;

    int state_;
    int type_;
    int http_ret_;
    bool threaded_;

    std::map<std::string, std::string> headers_;
    std::string path_;
    std::string post_;
    unsigned int postsize_;
    std::string body_;
    std::string request_;
    std::string header_;


    std::array<char, 8192> buffer_;

    boost::asio::ip::tcp::socket& socket() 
    {
        return socket_;
    }

    void start();
    void handle_read(const boost::system::error_code& e,
                std::size_t bytes_transferred);

    void process_request();
    int try_parse_request(std::size_t bytes_transferred);
    void begin_response();
    void handle_write(const boost::system::error_code& e);

public:
    HttpConnection(HttpServer* http_server);

    void set_header(const std::string& key, const std::string &value)
    {
        headers_[key] = value;
    }

    void set_header(const std::string& key, long value)
    {
        char strvalue[24];
        snprintf(strvalue, 24, "%ld", value);
        set_header(key, std::string(strvalue));
    }

    const std::string &path() const
    {
        return path_;
    }

    const std::string &post_data() const
    {
        return post_;
    }

    int req_type() const
    {
        return type_;
    }

    void set_body(const std::string &body)
    {
        body_ = body;
    }

    bool in_threadpool() const 
    {
        return threaded_;
    }
};


typedef boost::shared_ptr<HttpConnection> HttpConnPtr;
typedef int (*RequestHandler)(HttpConnPtr);


class HttpServer
{
    friend class HttpConnection;

private:
    static const int MAX_THREAD_SIZE = 16;

    struct ThreadState {
        std::thread *t_;
        std::list<HttpConnPtr> q_;
        std::mutex m_;
        std::condition_variable cv_;
    };


    boost::asio::io_service &io_;
    boost::asio::ip::tcp::acceptor acceptor_;

    ThreadState threads_[MAX_THREAD_SIZE];
    int threadnum_;
    RequestHandler req_handler_;
    
    void start_accept();
    void handle_accept(HttpConnPtr new_conn,
        const boost::system::error_code& error);
    void push_to_threadpool(HttpConnPtr conn);

    void thread_proc(int id);
    
public:
    HttpServer(boost::asio::io_service &io, unsigned short port,
            RequestHandler main_handler = &default_handler);

    void set_handler(RequestHandler handler)
    {
        req_handler_ = handler;
    }

    static int default_handler(HttpConnPtr conn)
    {
        std::string body;
        std::string type;
        
        if (conn->req_type() == HTTP_GET)
            type = "GET";
        else if (conn->req_type() == HTTP_POST)
            type = "POST";
 
        body = "<html><body><h1>Type: " + type + "    Path: " + conn->path() + "</h1></body></html>";
        conn->set_body(body);
        conn->set_header("Server", "TinyWebServer");
        conn->set_header("Content-Type", "text/html");
        return HTTP_200;
    }
};


/* 
//Demo:

#include <http_server.hpp>


static int my_handler(tws::HttpConnPtr conn)
{
    using namespace tws;
    std::string body;
    std::string type;
    
    //default callback will run in network I/O thread(single thread)  
    // but can switch to thread-pool if you think the callback will consume much time
    // use 'if (conn->in_threadpool())' too see if calling from threadpool
    
    if (conn->path().substr(0, 7) == "/thread" && !conn->in_threadpool())
        return HTTP_SWITCH_THREAD;

    if (conn->req_type() == HTTP_GET) {
        type = "GET";
    } else if (conn->req_type() == HTTP_POST)
        type = "POST";

    if (!conn->in_threadpool())
        body = "<html><body><h1>Type: " + type + "    Path: " + conn->path()
            + "</h1><h3>" + conn->post_data() + "</h3></body></html>\n";
    else
        body = "<html><body><h1>IN THREAD!</h1><h1>Path: " + conn->path();
            + "</h1><h3>" + conn->post_data() + "</h3></body></html>\n";

    conn->set_body(body);
    conn->set_header("Server", "TinyWebServer Demo 1.0");
    conn->set_header("Content-Type", "text/html");
    return HTTP_200;
}

#include <cstdio>
#include <cstdlib>

int main(int argc, char *argv[])
{
    int port = 8000;
    printf("Usage: %s [port=8000]\n", argv[0]);
    printf("try 'curl http://localhost:port/xxx/\n");
    printf(" or 'curl http://localhost:port/thread/xxx/\n");
    printf(" or 'curl -d \"MSG\" http://localhost:port/xxx/\n");
    printf(" or 'curl -d \"MSG\" http://localhost:port/thread/xxx/\n");
    if (argc > 1)
        port = atoi(argv[1]);
    
    boost::asio::io_service io_service;
    // should catch execeptions if not sure the port is valid
    tws::HttpServer http_server(io_service, port, &my_handler);
    io_service.run();
    return 0;
}

*/

}

#endif

