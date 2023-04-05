# multithreadedHTTPserver
Done in class, with assignment helper functions to handle the listening of and acceptig of socket connections.

Everything else is original
##Design
Threadpool design, program can be executed with any number of worker threads which concurrently serve requests.
Utilized pthread's file locks to safely serve PUT and GET requests on any number of threads.
