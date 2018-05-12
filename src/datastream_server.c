#include <sys/types.h>
#ifndef _WIN32
#include <sys/select.h>
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif
#include <microhttpd.h>
#include <stdio.h>
#include <iron/full.h>
#define PORT 8888

typedef enum{
  ACTIVITY_DISABLED = 0,
  ACTIVITY_WAITING_FOR_ENABLED = 1,
  ACTIVITY_ENABLED = 2,
  ACTIVITY_WAITING_FOR_DISABLED = 3
}activity_state;

struct _message_list;
typedef struct _message_list message_list;

struct _message_list{
  size_t length;
  void * message;
  const char * name;
  message_list * next;
};

typedef struct{
  data_stream_listener * listener;
  data_stream_listener * data_listener;
  char ** activitys;
  size_t * activity_length;
  activity_state * activity_state;
  size_t activity_count;
  iron_mutex lock;
  message_list * messages;
  message_list * prev;
}listen_ctx;

static data_stream weblog = {.name = "dlog web"};


static void mark_activity(listen_ctx * ctx, const char * name, activity_state state, bool override_state){
  iron_mutex_lock(ctx->lock);
  for(size_t i = 0; i < ctx->activity_count; i++){
    if(memcmp(ctx->activitys[i], name, ctx->activity_length[i]) == 0){

      if(override_state){
	var curstate = ctx->activity_state[ctx->activity_count - 1];
	if(curstate == ACTIVITY_ENABLED && curstate == ACTIVITY_WAITING_FOR_ENABLED && state == ACTIVITY_DISABLED)
	  ctx->activity_state[ctx->activity_count - 1] = ACTIVITY_WAITING_FOR_DISABLED;
	if(curstate == ACTIVITY_DISABLED && curstate == ACTIVITY_WAITING_FOR_DISABLED && state == ACTIVITY_ENABLED)
	  ctx->activity_state[ctx->activity_count - 1] = ACTIVITY_WAITING_FOR_ENABLED;
      }
      iron_mutex_unlock(ctx->lock);
      return;
    }
  }

  ctx->activitys = ralloc(ctx->activitys, sizeof(ctx->activitys[0]) * (ctx->activity_count += 1));
  ctx->activity_length = ralloc(ctx->activity_length, sizeof(ctx->activity_length[0]) * ctx->activity_count);
  ctx->activity_state = ralloc(ctx->activity_state, sizeof(ctx->activity_state[0]) * ctx->activity_count);
  ctx->activitys[ctx->activity_count - 1]= fmtstr("%s", name);
  ctx->activity_length[ctx->activity_count - 1] = strlen(name) + 1;
  ctx->activity_state[ctx->activity_count - 1] = state;
  var curstate = ctx->activity_state[ctx->activity_count - 1];
  if(curstate == ACTIVITY_ENABLED && curstate == ACTIVITY_WAITING_FOR_ENABLED && state == ACTIVITY_DISABLED)
    ctx->activity_state[ctx->activity_count - 1] = ACTIVITY_WAITING_FOR_DISABLED;
  if(curstate == ACTIVITY_DISABLED && curstate == ACTIVITY_WAITING_FOR_DISABLED && state == ACTIVITY_ENABLED)
    ctx->activity_state[ctx->activity_count - 1] = ACTIVITY_WAITING_FOR_ENABLED;
  
  iron_mutex_unlock(ctx->lock);
}

static void get_system_update(listen_ctx * ctx, void (*fmt)(const char * _fmt, ...)){
  if(ctx->messages != NULL){
    var to_send = __sync_lock_test_and_set(&(ctx->messages), NULL);
    var it = to_send;
    var last = to_send;
    while(it != NULL){
      if(strcmp(it->name, weblog.name) != 0)
	fmt("%s    %s\n", it->name, it->message);
      if(it->next == NULL){
	last = it;
	break;
      }
      it = it->next;
    }
    
  retry_set:;
    last->next = ctx->prev;
    if(!__sync_bool_compare_and_swap(&(ctx->prev), ctx->prev, to_send))
      goto retry_set; //someone else set prev, retry instead.    
  }
}

static void get_activities(listen_ctx * ctx, void (*fmt)(const char * _fmt, ...), char * body){
  char * str = body;
  while(str != NULL){
    char * val = strstr(str, ":");
    if(val == NULL) break;
    val += 1;
    char * nxt = strstr(val, ",");
    if(nxt == NULL){
      nxt = strlen(val) + val;
    }
    char name[val - str];
    memcpy(name, str, val - str);
    name[val - str - 1] = 0;

    char value[nxt - val + 1];
    memcpy(value, val, nxt - val + 1);
    value[nxt - val - 1 + 1] = 0;
    mark_activity(ctx, name, value[0] == '0' ? ACTIVITY_DISABLED : ACTIVITY_ENABLED, true);
    
    str = nxt + 1;
  }

  //char buffer[100];
  for(size_t i = 0; i< ctx->activity_count; i++){
    dlog(weblog, ctx->activitys[i], ctx->activity_length[i]);

    //char * inner = strstr(str, "|");
    //char * nxt = strstr(inner, ";");
    //logd("Inner: %s\n, nxt: %s\n", inner, nxt);
    //str = nxt + 1;
    bool active = false;
    if(ctx->activity_state[i] == ACTIVITY_ENABLED || ctx->activity_state[i] == ACTIVITY_WAITING_FOR_ENABLED)
      active = true;
    fmt("%s: %s\n", ctx->activitys[i], active ? "checked" : "");    
  }
}

static void read_index_file(listen_ctx * ctx, void (*fmt)(const char * _fmt, ...), const char * file){
  UNUSED(ctx);
  var filecontent = read_file_to_string(file);
  fmt("%s", filecontent);
  dealloc(filecontent);
}

typedef struct{
  listen_ctx * ctx;
  void * data;
  size_t count;
}post_ctx;

// [index] -> load the page file
// get_streams -> get the list of streams
// enable_streams -> enables a set of streams
// listen -> Gets buffered activitys.
static int
answer_to_connection (void * cls
		      , struct MHD_Connection *connection,
                      const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls)
{

  UNUSED(method);
  UNUSED(version);
  UNUSED(upload_data);
  UNUSED(upload_data_size);
  UNUSED(con_cls);

  dmsg(weblog, "URL: '%s'\n", url);
  iron_usleep(100000);

  size_t count = 4;
  char * message = alloc(count);
  char * msg = message;
  listen_ctx * ctx = cls;
  
  void fmt(const char * _fmt, ...){

  retry_printf:;
    size_t n = msg - message;
    size_t max_write = count - n;
    size_t cnt;
    if(max_write <= 1){
      cnt = strlen(_fmt);
      message = ralloc(message, (count = (count + cnt) * 2));
      msg = message + n;
      goto retry_printf;
    }else{
      va_list args;
      va_start (args, _fmt);
      cnt = vsnprintf(msg, max_write, _fmt, args);
      va_end(args);
    }
    if(cnt > 0){
      if(cnt >= max_write){
	message = ralloc(message, (count = (count + cnt) * 2));
	msg = message + n;
	goto retry_printf;
      }
      msg += cnt;
    }
  }

  if(strcmp("/activities", url) == 0){
    post_ctx * pp = *con_cls;
    void * activities_data = NULL;
    if(strcmp(method, "POST") == 0){

      if(pp == NULL){
	pp = alloc0(sizeof(*pp));
	*con_cls = pp;
	dealloc(message);
	return MHD_YES;
      }
      
      if(*upload_data_size){
	
	pp->data = ralloc(pp->data, *upload_data_size + pp->count);
	void * newdata = pp->data + pp->count;
	memcpy(newdata, upload_data, *upload_data_size);
	pp->count += *upload_data_size;
	*upload_data_size = 0;
	
	dealloc(message);
	return MHD_YES;
      }else if(pp != NULL){
	activities_data = pp->data;
	free(pp);
	*con_cls = NULL;
      }
    }
    get_activities(ctx, fmt, (char *) activities_data);
    if(activities_data != NULL)
      free(activities_data);
    
  }else if(strcmp("/update", url) == 0){
    var time1 = timestampf();
  goback:;
    while((timestampf() - time1) < 5){
      if(ctx->messages != NULL)
	break;
    }
    if(ctx->messages != NULL)
      get_system_update(ctx, fmt);
    if(msg - message == 0)
      goto goback;
  }else if(strcmp("/style.css", url) == 0){
    read_index_file(ctx, fmt, "style.css");
  }else if(strcmp("/page.js", url) == 0){
    read_index_file(ctx, fmt, "page.js");
  }else
    read_index_file(ctx, fmt, "page_header.html");
  message[msg - message] = 0;
  dmsg(weblog, "Sending %i", strlen(message));
  var response = MHD_create_response_from_buffer (strlen(message), message,MHD_RESPMEM_MUST_FREE);
   
  int ret =  MHD_queue_response (connection, MHD_HTTP_OK,response);
  MHD_destroy_response(response);

  return ret;
}


static void activity_update(const data_stream * s, const void * data, size_t length, void * userdata){
  UNUSED(data);
  UNUSED(length);
  listen_ctx * ctx = userdata;
  mark_activity(ctx, s->name, ACTIVITY_ENABLED, false);
  data_stream_listen(ctx->data_listener, (data_stream *) s);
}

static void data_update(const data_stream * s, const void * data, size_t length, void * userdata){
  if(data == NULL) return;
  //ASSERT(data != NULL);
  size_t _length = strlen(s->name) + 1 + length + sizeof(message_list) + 1;
  listen_ctx * ctx = userdata;
  message_list * thing = NULL;

  { // interlocked pop the first item.
  try_again:;
    var prev = ctx->prev;
    message_list * nxt = NULL;
    if(prev != NULL)
      nxt = prev->next;
    
    if(!__sync_bool_compare_and_swap(&ctx->prev, prev, nxt))
      goto try_again;
    thing = prev;
  }
  
  thing = ralloc(thing, _length);
  void * buf = thing;
  
  thing->length = length;
  thing->name = buf + sizeof(message_list);
  memcpy((char *)thing->name, s->name, strlen(s->name) + 1);
  thing->message = buf + (_length - length) - 1;
  memcpy(thing->message, data, length);
  ((char *)thing->message)[length] = 0;

  { // interlocked replace
    
  try_again2:;

    message_list * nxt = ctx->messages;
    thing->next = nxt;
    if(!__sync_bool_compare_and_swap(&ctx->messages, nxt, thing))
      goto try_again2;
  }
}

static struct MHD_Daemon * start_server(){
  listen_ctx * ctx = alloc0(sizeof(*ctx));
  logd("CTX1: %p\n", ctx);
  data_stream_listener * activity_listener = alloc0(sizeof(data_stream_listener));
  data_stream_listen_activity(activity_listener);
  activity_listener->process = activity_update;
  activity_listener->userdata = ctx;

  data_stream_listener * data_listener = alloc0(sizeof(data_stream_listener));
  data_listener->process = data_update;
  data_listener->userdata = ctx;

  
  ctx->listener = activity_listener;
  ctx->data_listener = data_listener;
  ctx->activitys = NULL;
  ctx->activity_length = NULL;
  ctx->activity_count = 0;
  ctx->lock = iron_mutex_create();
  //iron_mutex_unlock(ctx->lock);
  struct MHD_Daemon *daemon;

  daemon = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG, PORT, NULL, NULL, answer_to_connection,ctx, MHD_OPTION_END);
  return daemon;
}

int run_server_main ()
{
  
  struct MHD_Daemon *daemon = start_server();

  if (NULL == daemon)
    return 1;

  (void) getchar ();

  MHD_stop_daemon (daemon);
  return 0;
}

void datalog_server_run(){
  start_server();
}
