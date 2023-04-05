// ++ Header file for thread pool module ++ //
#pragma once

#include "queue.h"
#include <stdint.h>

/** @struct threadpool_t
 *
 *  @brief This typedef renames the struct queue.  Your `c` file
 *  should define the variables that you need for your queue.
 */
typedef struct threadpool threadpool_t;

/** @brief Dynamically allocates and initializes a new threadpool with worker_threads
 *     assuming the calling party is acting as a dispatcher,
 *      worker threads get jobs off of the shared queue
 *  @return a pointer to a new queue_t
 */
threadpool_t *threadpool_new(queue_t *connection_queue, uint32_t worker_threads);

/** @brief Delete the threadpool and free all of its memory.
 */
void threadpool_delete(threadpool_t **t);
