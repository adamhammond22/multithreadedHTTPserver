#include "asgn2_helper_funcs.h"
#include "response.h"
#include "request.h"
#include "queue.h"
#include "threadpool.h"

#include <err.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

//exits main because of bad args
void bad_args_exit(char **argv) {
    warnx("wrong arguments: %s [-t threads] port_num", argv[0]);
    fprintf(stderr, "usage: %s [-t threads] <port>\n", argv[0]);
}

int main(int argc, char **argv) {
    // ========== Initial Parsing ========== //

    //reject incorrect arg, 2 and 4 are allowed#
    if (argc != 2 && argc != 4) {
        bad_args_exit(argv);
        return EXIT_FAILURE;
    }

    size_t port;
    uint32_t worker_threads;
    char *endptr = NULL; //for strtoull conver

    //If our argc is 4, we need to proccess #of threads
    if (argc == 4) {
        //ensure threads specified in correct place
        if (strcmp(argv[1], "-t") != 0) {
            bad_args_exit(argv);
            return EXIT_FAILURE;
        }
        //convert worker threads
        worker_threads = atoi(argv[2]);
        //convert port
        port = (size_t) strtoull(argv[3], &endptr, 10);

    } else {
        //Otherwise we can use the thread default of 4
        port = (size_t) strtoull(argv[1], &endptr, 10);
        worker_threads = 4;
    }

    //reject invalid ports
    if (endptr && *endptr != '\0') {
        warnx("invalid port number: %zu", port);
        return EXIT_FAILURE;
    }
    //reject invalid worker threads number
    if (worker_threads < 1 || worker_threads > 3000) {
        warnx("invalid worker threads: %s", argv[2]);
        return EXIT_FAILURE;
    }

    // ========== Initialization ========== //

    //Init connection queue of size 5 worker_threads
    queue_t *connection_queue = queue_new(5 * worker_threads);

    //init our thread pool of worker threads
    threadpool_t *threadpool = threadpool_new(connection_queue, worker_threads);

    //absolutely no clue what this is
    signal(SIGPIPE, SIG_IGN);
    //init our listener socket for incoming requests
    Listener_Socket sock;
    listener_init(&sock, port);

    // ========== Thread dispatcher loop ========== //
    while (1) {
        //create new socket when we get a new client
        int connfd = listener_accept(&sock);
        //fprintf(stdout, "dispatch loop\n");
        //push that connfd to the connection queue
        //will block if queue is full!
        queue_push(connection_queue, (void *) (long) connfd);
        //fprintf(stdout, "pushed\n");
    }
    // ========== Cleanup ========== //

    //free threadpool
    threadpool_delete(&threadpool);
    //free queue
    queue_delete(&connection_queue);

    return EXIT_SUCCESS;
}
