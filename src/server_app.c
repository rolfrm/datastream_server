#include "datastream_server.h"
#include <stdio.h>
#include <iron/full.h>
int main(){
  datalog_server_run();

  static data_stream src = {.name = "Test1"};
  static data_stream src2 = {.name = "Test2"};
  int x = 2;
  while(true){
    iron_usleep(100000);
    logd("Messaging..\n");
    dmsg(src, "Hello?");
    if(x % 1000 == 0){
      
      dmsg(src2, "Hello? ????");
    }
    x++;
  }
  getchar();
  return 0;
  
}
