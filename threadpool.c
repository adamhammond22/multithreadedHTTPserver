// ++ Implementation file for thread pool module  ++ //

#include "debug.h"
#include "threadpool.h"
#include "connection.h"
#include "request.h"
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h> // for file stats
#include <sys/types.h> // for file stats
#include <stdlib.h> //memory
#include <unistd.h>
#include <sys/file.h> //flock
#include <errno.h>
#include <assert.h> //asserts
#include <stdio.h>
#include <pthread.h> //threads

void *worker_thread(void *args);

// Declare lower functions here so we can put them at bottom of file
void handle_connection(int, pthread_mutex_t *fclptr);
void handle_get(conn_t *, pthread_mutex_t *fclptr);
void handle_put(conn_t *, pthread_mutex_t *fclptr);
void handle_unsupported(conn_t *);
void printAuditLog(conn_t *conn, uint16_t code);

struct threadpool {
    uint32_t worker_threads; //size of queue defined on creation
    uintptr_t *workerArray;
    queue_t *connection_queue;
    int test;
    //Global mutex and cv's
    //file creation lock
    pthread_mutex_t fcl;
    pthread_mutex_t flocking;
};

//Threadpool constructor
threadpool_t *threadpool_new(queue_t *connection_queue, uint32_t worker_threads) {
    //alloc size for entire threadpool
    threadpool_t *t = (threadpool_t *) calloc(1, sizeof(threadpool_t));

    //Init array of references to all threads
    t->workerArray = (uintptr_t *) malloc(sizeof(pthread_t) * worker_threads);
    //init other vars
    t->worker_threads = worker_threads;
    t->connection_queue = connection_queue;

    //init mutexs and cvs. assert they don't fail.
    assert(pthread_mutex_init(&(t->fcl), NULL) == 0);
    assert(pthread_mutex_init(&(t->flocking), NULL) == 0);

    //Create each worker thread, and store them
    for (uintptr_t i = 0; i < worker_threads; i++) {

        //we pass the threadpool struct ref to each thread
        pthread_create(&(t->workerArray[i]), NULL, worker_thread, (void *) t);
    }

    return t;
}

//Threadpool Destructor:
void threadpool_delete(threadpool_t **t) {
    //join each worker thread
    uintptr_t rc;
    for (uint32_t i = 0; i < (*t)->worker_threads; i++) {
        pthread_join((*t)->workerArray[i], (void **) &rc);
    }
    //free worker array
    free((*t)->workerArray);

    //free all mutexes.
    assert(pthread_mutex_destroy(&((*t)->fcl)) == 0);
    assert(pthread_mutex_destroy(&((*t)->flocking)) == 0);

    //free the struct itself
    free(*t);
    *t = NULL;
    return;
}

//worker thread of the threadpool
void *worker_thread(void *args) {
    //extract our threadpool memory & store it in a var
    threadpool_t *threadpool = (threadpool_t *) args;

    while (1) {
        uintptr_t connfdptr;
        //pop new connection fd pointer from q. DONT
        queue_pop(threadpool->connection_queue, (void **) (&connfdptr));
        int connfd = (int) connfdptr;

        //handle the connection
        handle_connection(connfd, &(threadpool->fcl));
        //close the connection
        close(connfd);
    }
    return 0;
}
//handles connection
void handle_connection(int connfd, pthread_mutex_t *fclptr) {
    conn_t *conn = conn_new(connfd);

    const Response_t *res = conn_parse(conn);

    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn, fclptr);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn, fclptr);
        } else {
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn);
}

void handle_get(conn_t *conn, pthread_mutex_t *fclptr) {

    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;

    //Shared flock, so other gets can read this file too.
    pthread_mutex_lock(fclptr);
    // Open the file
    int fd = open(uri, O_RDONLY);

    flock(fd, LOCK_EX);
    pthread_mutex_unlock(fclptr);

    if (fd < 0) {
        uint16_t code = 500;
        if (errno == EACCES || errno == EISDIR) {
            res = &RESPONSE_FORBIDDEN;
            code = 403;
        } else if (errno == ENOENT) {
            res = &RESPONSE_NOT_FOUND;
            code = 404;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            code = 500;
        }

        //send our response
        conn_send_response(conn, res);
        //print audit log
        printAuditLog(conn, code);
        //for extra measure, releases flock implicitly, may be dletable
        close(fd);

        return;
    }

    // Check size of file
    //make stat struct and get stats of file
    struct stat st;
    fstat(fd, &st);
    uint32_t filesize = st.st_size;

    if (S_ISDIR(st.st_mode)) {
        //not sure if its meant to be forbidden like last time?
        res = &RESPONSE_FORBIDDEN;
        //releases flock implicitly
        //send our response
        conn_send_response(conn, res);
        //print audit log
        printAuditLog(conn, 403);

        close(fd);

        return;
    }

    //send the contents of file in body AND response
    conn_send_file(conn, fd, filesize);
    //print audit log
    printAuditLog(conn, 200);

    //releases flock implicitly
    close(fd);
    return;
}

//Handles unsupported requests
void handle_unsupported(conn_t *conn) {

    // send responses
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
}

//Handles put requests
void handle_put(conn_t *conn, pthread_mutex_t *fclptr) {

    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;

    //Init file creation lock, puts cannot happen completely simultaneously
    pthread_mutex_lock(fclptr);

    // Check if file already exists
    bool existed = access(uri, F_OK) == 0;
    int fd;
    if (existed) {
        //if it exists, create it
        // Open the file.. (DO NOT TRUNCATE BEFORE WE FLOCK)
        fd = open(uri, O_WRONLY);
    } else {
        //otherwise open normally
        // Open the file.. (DO NOT TRUNCATE BEFORE WE FLOCK)
        fd = open(uri, O_CREAT | O_WRONLY, 0600);
    }

    //If fd < 0, release mutex asap and exit
    if (fd < 0) {
        uint16_t code = 500;
        if (errno == EACCES || errno == EISDIR || errno == ENOENT) {
            res = &RESPONSE_FORBIDDEN;
            code = 403;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            code = 500;
        }

        //send our response
        conn_send_response(conn, res);
        //print audit log
        printAuditLog(conn, code);
        //unlock our mutex
        pthread_mutex_unlock(fclptr);
        //close for good measure to make sure flock is released, may be removable?
        close(fd);
        return;
    }

    //acquire exclusive lock for file to modify
    //will block untill acquired
    //pthread_mutex_lock(flockingptr);
    flock(fd, LOCK_EX);
    //pthread_mutex_unlock(flockingptr);
    //release our fcl so other puts can go through
    pthread_mutex_unlock(fclptr);

    //truncate file to 0 length
    ftruncate(fd, 0);

    //write our file
    res = conn_recv_file(conn, fd);

    //form correct response
    uint16_t code = 500;
    if (res == NULL && existed) {
        res = &RESPONSE_OK;
        code = 200;
    } else if (res == NULL && !existed) {
        res = &RESPONSE_CREATED;
        code = 201;
    }

    //send our response
    conn_send_response(conn, res);

    //print audit log
    printAuditLog(conn, code);

    //releases flock implicitly
    close(fd);
}

//prints audit log for all
void printAuditLog(conn_t *conn, uint16_t code) {

    const char *oper = request_get_str(conn_get_request(conn));
    char *uri = conn_get_uri(conn);
    char *rid = conn_get_header(conn, "Request-Id");
    if (rid == NULL) {
        rid = "0";
    }
    fprintf(stderr, "%s,%s,%d,%s\n", oper, uri, code, rid);
    return;
}
