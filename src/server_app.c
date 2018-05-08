#include "datastream_server.h"
#include <stdio.h>
#include <iron/full.h>
int main(){
  datalog_server_run();

  static data_stream src = {.name = "Test1"};
  static data_stream src2 = {.name = "Test2"};
  int x = 2;
  while(true){
    iron_usleep(1000000);
    dmsg(src, "%i%i%i Hello?",x,x,x);
    if(x % 1000 == 0){
      
      dmsg(src2, "Hello? ????");
    }
    x++;
  }
  getchar();
  return 0;
  
}
