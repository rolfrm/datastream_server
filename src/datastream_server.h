struct _datastream_server;
typedef struct _datastream_server datastream_server;

datastream_server * datastream_server_run();
void datastream_server_wait_for_connect(datastream_server *);
void datastream_server_flush(datastream_server *);
