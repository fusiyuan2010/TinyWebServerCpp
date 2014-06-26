#ifndef _HTTP_SERVER_
#define _HTTP_SERVER_
#include <map>
#include <string>

namespace tws{

enum RequestType {
    HTTP_GET,
    HTTP_POST,
    HTTP_HEAD,
    HTTP_PUT,
    HTTP_INVALID,
};

enum HttpCode {
    HTTP_SWITCH_THREAD = -100,
    HTTP_200 = 0,
    HTTP_404,
    HTTP_503,
    HTTP_508, // unused by HTTP, means recursive thread switch, for internal use 
    HTTP_501,
    HTTP_END,
};

class Request
{
    friend class HttpConnection;
    std::map<std::string, std::string> headers_;
    RequestType type_;
    std::string path_;
    std::string postdata_;
    bool threaded_;
    void clear();

public:
    const std::string &path() const { return path_;}
    const std::string &postdata() const { return postdata_;}
    RequestType type() const { return type_; }
    bool in_threadpool() const { return threaded_; }
    const std::map<std::string, std::string> &headers() const {return headers_;}
};

class Response
{
    friend class HttpConnection;
    std::map<std::string, std::string> headers_;
    std::string body_;
    void clear();
#ifdef HTTP_COMPRESSION
    int compression_;
#endif

public:
    void set_header(const std::string& key, const std::string &value);
    void set_header(const std::string& key, long value);
    void set_body(const std::string &body) { body_ = body;}

    // 0 to 9, 0 means no compression support
#ifdef HTTP_COMPRESSION
    void set_compression(int level = 0);
#endif
};

typedef int (*RequestHandler)(Response&, const Request&);

class HttpServerInter;
class HttpServer
{
    HttpServerInter *inter_;
    
public:
    HttpServer(unsigned short port,
            RequestHandler main_handler = &default_handler, int threadnum = 0);

    void set_handler(RequestHandler handler);

    void run();

    void stop(); //not implement yet

    void stat(); //not implement yet

    static int default_handler(Response& resp, const Request& req)
    {
        std::string body;
        std::string type;
        
        // default callback will run in network I/O thread(single thread)  
        // but can switch to thread-pool if you think the callback will consume much time
        // use 'if (req.in_threadpool())' too see if calling from threadpool
        if (req.path().substr(0, 7) == "/thread" && !req.in_threadpool())
            return HTTP_SWITCH_THREAD;

        switch(req.type()) {
            case HTTP_GET:
                type = "GET";
                break;
            case HTTP_POST:
                type = "POST";
                break;
            default:
                break;
        }

        if (!req.in_threadpool())
            body = "<html><body><h1>Type: " + type + "    Path: " + req.path()
                + "</h1><h3>" + req.postdata() + "</h3></body></html>\n";
        else
            body = "<html><body><h1>IN THREAD!</h1><h1>Path: " + req.path();
                + "</h1><h3>" + req.postdata() + "</h3></body></html>\n";

        resp.set_body(body);
        resp.set_header("Server", "TinyWebServer Demo 1.0");
        resp.set_header("Content-Type", "text/html");
        return HTTP_200;
    }
};


/* 
//Demo:

#include <http_server.hpp>
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
    
    // should catch execeptions if not sure the port is valid
    tws::HttpServer http_server(port, &tws::HttpServer::default_handler, 4);
    http_server.run();
    return 0;
}

*/

}

#endif

