#include "datastream_server.h"
#include <stdio.h>
#include <iron/full.h>

static void listen_activity(const data_stream * s, const void * data, size_t length, void * userdata){
  UNUSED(length);
  UNUSED(userdata);
  logd("%s: %s\n", s->name, data);
}
u64 testvalue = 5;

data_stream console_log = {.name = "Console"};

static void handle_console_data(datastream_server * srv, const char * message, void * userdata){
  UNUSED(srv);
  ASSERT(userdata == &testvalue);
  dmsg(console_log, "%s", message);
}

int main(){
  data_stream_listener * activity_listener = alloc0(sizeof(data_stream_listener));
  activity_listener->process = listen_activity;
  data_stream_listen_all(activity_listener);

  var serv = datastream_server_run();
  datastream_server_set_console_handler(serv, handle_console_data, &testvalue);
  datastream_server_wait_for_connect(serv);

  static data_stream src = {.name = "Test1"};
  static data_stream src2 = {.name = "Test2"};
  static data_stream src3 = {.name = "Testing 3"};
  int x = 2;
  for(int i = 0; i < 30000; i++){
    iron_usleep(100000);
    dmsg(src, "%i Hello?", x);
    dmsg(src3, "_____ %i Hello?", x);
    if(x % 1000 == 0){
      
      dmsg(src2, "Hello? ????");
    }
    x++;
  }
  datastream_server_flush(serv);
  return 0;
  
}
