struct _datastream_server;
typedef struct _datastream_server datastream_server;

datastream_server * datastream_server_run();
void datastream_server_wait_for_connect(datastream_server *);
void datastream_server_flush(datastream_server *);

typedef void (* console_handler)(datastream_server * server, const char * message, void * userdata);
void datastream_server_set_console_handler(datastream_server * server, console_handler handler, void * userdata);
