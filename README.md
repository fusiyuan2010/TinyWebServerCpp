TinyWebServerCpp
================

A Tiny Web Server written in C++ based on boost-asio and threads, easy to be integrated into other projects

Info
--------
 - Programmed as a internal interface of server, to do non-critical task like monitoring and file downloading
 - Need libboost\_system-mt.so and c++11
 - Based on asynchronous-I/O, both single thread and thread-pool are supported
 - Basic features like GET/POST/PUT/HEAD supported, however most other HTTP specs may not conformed
 - HTTP deflate compression is supported
 - Very simple interface, only a callback function is necessary
 - DO NOT USE IN KEY BUSINESS, for personal use before, still be experimental
 - Any improvement is welcomed


TODO
--------
 - Refactor task-queue data structure (done)
 - Better programmed request parser, especially need to add request header parsing (done)
 - Get rid of boost::asio, refactor i/o directly on epoll instead (discarded)
 - More control for server, like stop() and interface for status

