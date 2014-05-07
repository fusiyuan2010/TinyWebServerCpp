TinyWebServerCpp
================

A Tiny Web Server written in C++ based on boost-asio and threads, easy to be integrated into orther projects

Info
--------
 - Programmed as a internal interface of server, to do non-critical task like monitoring and file downloading
 - Need libboost\_system-mt.so and c++11
 - Based on asynchronous-I/O, both single thread and thread-pool are supported
 - Basic features like GET/POST supported, however most other HTTP spec. not conformed
 - Very simple interface, only a callback function is necessary
 - DO NOT USE IN KEY BUSINESS, for personal use before, still be experimental
 - Any improvement is welcomed
