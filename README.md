How to Complie the Program:
  
  module unload soft/gcc
  
  make
  
  ./web_server port_number ./testing num_dispatcher_threads num_worker_threads queue_length (example: ./web_server 9000 ./testing 10 10 10)


How to Access Files Accross a Network/Locally:

    open a separate terminal within the project directory (this is where the files you get will go. We recommend creating a separate directory and then navigating in there for the following commands)

  To Get Files Locally:
  
    Single File:

      wget http://127.0.0.1:portNumber/path_from_web_root/filename (example: wget http://127.0.0.1:9000/image/jpg/29.jpg)

    Multiple Files:

       more urls | xargs -n num_args -P num_threads_to_send_concurrently command (example: more urls | xargs -n 1 -P 5 wget)

  To Get Files Accross a Network:
  
    Single File:

      wget http://ip_address:portNumber/path_from_web_root/filename (example: wget http://134.84.62.109:9000/image/jpg/29.jpg)

    Multiple Files:

      more urls | xargs -n num_args -P num_threads_to_send_concurrently command (example: more urls | xargs -n 1 -P 5 wget)


How our program works:

  Our program creates a server with specifications mentioned above. First we validate that all arguments are correct. We then create a web server log which you can view in the main directory called web_server_log. We then create worker and dispatcher threads then pthread_exit out of the main method. Dispatcher threads get a connection and if it is valid, put the request information in a bounded buffer that uses locks and condiditon variables. The worker threads remove work from the bounded buffer and try to open the file in the request. If there are bytes to write back, we call return_result. Otherwise, we call return_error with file not found. Once this is done we call the thread_safe_log_message function to add a log to the log file in the format [ThreadID#][Request#][fd][Request string][bytes/error] . The thread_safe_log_message function uses its own lock to ensure that two worker threads do not write at the same time.
