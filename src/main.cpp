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


