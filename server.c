/* csci4061 S2016 Assignment 4 
* section: one_digit_number 
* date: 12/07/16 
* names: Kenton Smith, Derek Spaulding, Derek Goodwin
* UMN Internet ID, Student ID (smit7519, 4380739), (spaul048, 4994234), (goodw171, 3862569)
* Person submitting: Kenton Smith smit7519 4380739
*/

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"
#include <errno.h>

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 100
#define MAX_REQUEST_LENGTH 64
#define MAX_PATH_LENGTH 1024
#define MAX_BYTES_FILE_SIZE_RETURNED 500000

char FILE_NOT_FOUND_TEXT[] = "File not found."; // pass this string to return_error when open() failed
#define FILE_NOT_FOUND_ERROR -1 // log "File not found." when this error code is passed to logging message function

char NO_BYTES_READ_TEXT[] = "Reading file failed."; // pass this string to return_error when read() failed
#define NO_BYTES_READ_ERROR -2 // log "Reading file failed." when this error code is passed to logging message function

// define two functions we've implement later
void bounded_buffer_insert(int fd, char* filepath);
void thread_safe_log_message(int threadNum, int requestNum, int fd, char* requestString, int numBytes);

// following synchronization code is from lecture slides 
pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ring_access= PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t some_content = PTHREAD_COND_INITIALIZER; // consumer: wait for content
pthread_cond_t free_slot = PTHREAD_COND_INITIALIZER; // producer: wait for a free slot

//Structure for queue.
typedef struct request_queue {
        int             m_socket;
        char    m_szRequest[MAX_REQUEST_LENGTH];
} request_queue_t;

request_queue_t* bounded_buffer; 

// define global variables
int number_active_dispatcher_threads;
int port;
char* web_root_path;
int num_dispatcher;
int num_workers;
int queue_length;
int num_elements_in_buffer;

int insert_work_position; // for dispatcher threads and bounded buffer
int remove_work_position; // for worker threads and bounded buffer

FILE *logging_file;

/*
 * accept_connection()
 * If valid return value from get_request, put work in buffer
 * Else, through an error, but do not exit thread
 * Only exit dispatcher thread if accept_connection returns a negative file descriptor
 * */
void * dispatch(void * arg) {

    while (1) {

      int fd = accept_connection();
      if (fd < 0) {
        number_active_dispatcher_threads--;
        pthread_exit(NULL);
      } else {
        char temp_buf[MAX_PATH_LENGTH];

        int return_val = get_request(fd, temp_buf); 
        
        if (return_val == 0) {
          // put work in buffer
          bounded_buffer_insert(fd, temp_buf);
                    
        } else {
          perror("get_request returned non-zero value in this thread\n\
                  not exiting thread and not putting work in bounded_buffer");
        }        
      }
      
    }

    return NULL;
}

/*
 * This code follows from the lecture slides on condition variables and bounded buffer
 * If do not have a ring access lock (allows you to modify bounded buffer), try to acquire it
 * If buffer is full, wait for a free_slot signal that's triggered when element removed from buffer by separate thread
 * */
void bounded_buffer_insert(int fd, char* filepath) {
  pthread_mutex_lock(&ring_access);

  while (num_elements_in_buffer == queue_length) {
    pthread_cond_wait(&free_slot, &ring_access);
  }
  
  request_queue_t request_to_add_to_buffer;
  
  request_to_add_to_buffer.m_socket = fd;
  
  strcpy(request_to_add_to_buffer.m_szRequest, filepath);
  
  bounded_buffer[insert_work_position] = request_to_add_to_buffer;

  insert_work_position = (insert_work_position + 1) % queue_length;

  num_elements_in_buffer++;
  
  pthread_cond_signal(&some_content);

  pthread_mutex_unlock(&ring_access);  

}


/*
 * This code follows from the lecture slides on condition variables and bounded buffer
 * If do not have a ring access lock (allows you to modify bounded buffer), try to acquire it
 * If buffer does not have content, wait for signal from buffer insert function then can proceed to remove work
 * 
 * */
void bounded_buffer_remove(request_queue_t* request_struct) {
    pthread_mutex_lock(&ring_access);

    while (num_elements_in_buffer == 0) {
      pthread_cond_wait(&some_content, &ring_access);
    }

    strcpy(request_struct->m_szRequest, bounded_buffer[remove_work_position].m_szRequest);
    
    request_struct->m_socket = bounded_buffer[remove_work_position].m_socket;

    remove_work_position = (remove_work_position + 1) % queue_length;

    num_elements_in_buffer--;

    pthread_cond_signal(&free_slot);

    pthread_mutex_unlock(&ring_access);       
}

/*
 * If the number of active dispatcher threads is greater than 0, and there is work in the buffer, 
 * go into loop to pull work from buffer.
 * Step 1) Remove work from buffer
 * Step 2) If request URL is in cache, set bytes to be what is cache.  Else, open/read file to get bytes for file 
 * Step 3) If no errors, return the bytes and set the content type
 * Step 4) Log message to web_server_log
 * Step 5) Free anything we've allocated in worker thread function, close file descriptor.  
 * */
void * worker(void * arg) {

    int thread_number = *(int*) arg;

    int num_requests_serviced = 0;

    // need to make sure num_active_dispatcher_threads > 0 and there is work in the buffer
    while (number_active_dispatcher_threads > 0 || num_elements_in_buffer > 0) {
        request_queue_t* my_request_ptr = (request_queue_t *) malloc(sizeof(request_queue_t));

        if (my_request_ptr == NULL) {
          perror("Failed to allocate memory for my_request_ptr");
          exit(1);
        }

        bounded_buffer_remove(my_request_ptr);

        // fprintf(stderr,"%s", "Worker just pulled work from request queue\n");
        // fprintf(stderr,"Request string: %s\n", my_request_ptr->m_szRequest);
        // fprintf(stderr,"Request file descriptor = %d\n", my_request_ptr->m_socket);

        int fileFD = 0;
    
        int connectionFD = my_request_ptr->m_socket;

        num_requests_serviced++;

        char includesDot[MAX_PATH_LENGTH + 1];

        includesDot[0] = '.';
        includesDot[1] = '\0';
        strcat(includesDot, my_request_ptr->m_szRequest);

        fileFD = open(includesDot, O_RDONLY);  // NEED THE . in front of path in order for open to work

        // fprintf(stderr, "fileFD = %d, errno = %d\n", fileFD, errno);

        if (fileFD < 0) 
        {
          //fprintf(stderr, "Failed to open file %s in worker thread\n", my_request_ptr->m_szRequest);
          //perror("The error is....");
          int return_error_status = return_error(connectionFD, FILE_NOT_FOUND_TEXT);

          if(return_error_status != 0) {
            //fprintf(stderr, "%s", "return_error had bad return value\n");
          } 
          else
          {
            //fprintf(stderr, "%s", "return_error had good return value\n");
          }

          thread_safe_log_message(thread_number, num_requests_serviced, connectionFD, my_request_ptr->m_szRequest, FILE_NOT_FOUND_ERROR);
        } 
        else 
        {
          // try to read file
          char* readBuffer = (char *) malloc(sizeof(char) * MAX_BYTES_FILE_SIZE_RETURNED);

          if (readBuffer == NULL)
          {
             perror("Allocating memory for a read buffer failed");
             exit(1);
          }

          int numBytesReturnedFromRead = read(fileFD, readBuffer, MAX_BYTES_FILE_SIZE_RETURNED);

          if (numBytesReturnedFromRead < 0) 
          {
          return_error(connectionFD, NO_BYTES_READ_TEXT);
            thread_safe_log_message(thread_number, num_requests_serviced, connectionFD, my_request_ptr->m_szRequest, NO_BYTES_READ_ERROR);
          } 
          else 
          {  
            char* content_type;

            //TODO: Handle errors on malloc
            char* request_string = my_request_ptr->m_szRequest;
            char* file_type;
              
            file_type = strrchr(request_string, '.');

            if(strcmp(file_type, ".jpg") == 0) {
              content_type = "image/jpeg";
            }
            else if(strcmp(file_type, ".gif") == 0) {
              content_type = "image/gif";
            }
            else if(strcmp(file_type, ".htm") == 0 || strcmp(file_type, ".html") == 0) {
              content_type = "text/html";
            }
            else {
              content_type = "text/plain";
            }

            // make sure we got the correct content_type
            // fprintf(stderr, "%s\n", content_type); 

            return_result(connectionFD, content_type, readBuffer, numBytesReturnedFromRead);
            thread_safe_log_message(thread_number, num_requests_serviced, connectionFD, my_request_ptr->m_szRequest, numBytesReturnedFromRead);
            free(readBuffer);
          }
        }

        free(my_request_ptr);
        if(fileFD != 0) {
          close(fileFD);
        }
    }

    pthread_exit(NULL);

    return NULL;
}

// Logs to web_server_log in the following format:
// [ThreadID#][Request#][fd][Request string][bytes/error][(optional)time][(optional) Cache HIT/MISS] 
// TODO currently doesn't take in time and Cache HIT/MISS
void thread_safe_log_message(int threadNum, int requestNum, int fd, char* requestString, int numBytes) {
  //acquire lock for logging
  pthread_mutex_lock(&loglock);

  //sprintf to format string
  char str[MAX_PATH_LENGTH];
  
  if (numBytes == FILE_NOT_FOUND_ERROR) {
    sprintf(str, "[%d][%d][%d][%s][%s]\n", threadNum, requestNum, fd, requestString, FILE_NOT_FOUND_TEXT);
  } 
  else if (numBytes == NO_BYTES_READ_ERROR) {
    sprintf(str, "[%d][%d][%d][%s][%s]\n", threadNum, requestNum, fd, requestString, NO_BYTES_READ_TEXT);
  }
  else {
    sprintf(str, "[%d][%d][%d][%s][%d]\n", threadNum, requestNum, fd, requestString, numBytes);
  }

  fputs(str, logging_file);
  fflush(logging_file);
 
  //release lock for logging
  pthread_mutex_unlock(&loglock);
}

int main(int argc, char **argv)
{
    // Error check first.
  if(argc != 6 && argc != 7) {
    printf("usage: %s port path num_dispatcher num_workers queue_length [cache_size]\n", argv[0]);
        return -1;
    }

  // set some global variables
  insert_work_position = 0;
  remove_work_position = 0;
  num_elements_in_buffer = 0;

  // Parse input from command line into these allocated variables
  port = atoi(argv[1]);
  web_root_path = argv[2];
  num_dispatcher = atoi(argv[3]); 
  num_workers = atoi(argv[4]);
  queue_length = atoi(argv[5]);

  number_active_dispatcher_threads = num_dispatcher;  

  // test we received correct arguments
  printf("Arguments received: %d, %s, %d, %d, %d\n", port, web_root_path, num_dispatcher, num_workers, queue_length);

  // Ensure we have variables in correct ranges: i.e. port number in correct range, non-zero number of threads, etc. 
  if (port < 1025 || port > 65535) {
    fprintf(stderr, "%s", "Port number passed in outside legal range\n");
    exit(1);
  }

  if (num_dispatcher > MAX_THREADS || num_workers > MAX_THREADS || num_dispatcher <= 0 || num_workers <= 0) {
    fprintf(stderr, "%s", "Cannot run more than MAX_THREADS or less than/equal to 0 threads\n");
    exit(1);
  }

  int* thread_number_array = malloc(sizeof(int) * num_workers);
  
  if (thread_number_array == NULL) {
        perror("malloc call to create thread_number_array failed");
        exit(1);
  }

  if (queue_length > MAX_QUEUE_SIZE || queue_length <= 0) {
      fprintf(stderr, "%s", "Queue length passed in larger than MAX_QUEUE_SIZE or less than/equal to 0\n");
      exit(1);
  } else {
      bounded_buffer = (request_queue_t*) malloc(sizeof(request_queue_t) * queue_length);
      if (bounded_buffer == NULL) {
          fprintf(stderr, "%s", "malloc call to create bounded buffer failed\n");
          exit(1);
      }
  }

  // create the web_server_log file that we will be writing to
  char log_name[] = "web_server_log";
  logging_file = fopen(log_name, "w"); 

  if (logging_file == NULL) {
  perror("Failed to open logging file");
  exit(1);
  }
  
  printf("Call init() first and make a dispather and worker threads\n");

  // change directories to web_root_path
  if (chdir(web_root_path) == -1) {
    perror("Failed to change current working directory to web root");
    exit(1);
  }

  // lab description says we need to do this
  init(port);
  
  // create all dispatcher threads
  int i;
  for (i = 0; i < num_dispatcher; i++) {
    pthread_t tid;

    int error;

    if (error = pthread_create(&tid, NULL, dispatch, (void * ) NULL)) {
      perror("Failed creating a dispatcher thread");
      exit(1);
    }
    
    if (error = pthread_detach(tid)) {
      perror("Failed detaching a dispatcher thread");
      exit(1);
    }

  }

  // create all worker threads
  for (i = 0; i < num_workers; i++) {
    pthread_t tid;

    int error;         

    thread_number_array[i] = i;  // give each worker thread their own thread number 0 .. num_workers - 1 to identify them in log file       

    if (error = pthread_create(&tid, NULL, worker, (void * ) &thread_number_array[i])) {
      perror("Failed creating a worker thread");
      exit(1);
    }

    if (error = pthread_detach(tid)) {
      perror("Failed detaching a worker thread");
      exit(1);
    }       
  } 

  pthread_exit(NULL);
  // return 0;
}
