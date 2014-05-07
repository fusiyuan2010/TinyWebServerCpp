#include <http_server.hpp>


static int my_handler(tws::HttpConnPtr conn)
{
    using namespace tws;
    std::string body;
    std::string type;
    
    /* default callback will run in network I/O thread(single thread)  
       but can switch to thread-pool if you think the callback will consume much time
       use 'if (conn->in_threadpool())' too see if calling from threadpool
    */
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


