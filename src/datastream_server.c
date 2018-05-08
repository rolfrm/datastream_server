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
  DISABLED = 0,
  WAITING_FOR_ENABLED,
  ENABLED,
  WAITING_FOR_DISABLED
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

// [index] -> load the page file
// get_streams -> get the list of streams
// enable_streams -> enables a set of streams
// listen -> Gets buffered activitys.
static int
answer_to_connection (void *cls, struct MHD_Connection *connection,
                      const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls)
{
  
  UNUSED(method);
  UNUSED(version);
  UNUSED(upload_data);
  UNUSED(upload_data_size);
  UNUSED(con_cls);
  dmsg(weblog, "URL: %s\n", url);
  size_t count = 4;
  char * message = alloc(count);
  char * msg = message;

  void fmt(const char * _fmt, ...){

    va_list args;
    va_start (args, _fmt);
  retry_printf:;
    size_t n = msg - message;
    size_t max_write = count - n;
    size_t cnt;
    if(max_write <= 1){
      cnt = strlen(_fmt);
    }else{
      cnt = vsnprintf(msg, max_write, _fmt, args);
    }
    if(cnt > 0){
      //logd("cnt: %i %i %i\n", cnt, n, count);
      if(cnt >= max_write){
	message = ralloc(message, (count = (count + cnt) * 2));
	msg = message + n;
	goto retry_printf;
      }
      msg += cnt;
    }
    va_end(args);
  }


  fmt("<html><body>");
  listen_ctx * ctx = cls;
  for(size_t i = 0; i< ctx->activity_count; i++){
    dlog(weblog, ctx->activitys[i], ctx->activity_length[i]);  
    fmt("<input type=\"checkbox\">%s</input>", ctx->activitys[i]);    
  }
  fmt("<h3>Messages:</h3>");  
  if(ctx->messages != NULL){
    var to_send = __sync_lock_test_and_set(&(ctx->messages), NULL);
    var it = to_send;
    var last = to_send;
    while(it != NULL){
      fmt("<p>%s</p>", it->message);
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
  
  fmt("</body></html>");
  size_t len = msg - message;
  dmsg(weblog, "Response of : %i bytes\n", len);  
  var response =
     MHD_create_response_from_buffer (len, message,MHD_RESPMEM_MUST_FREE);
   
  int ret =  MHD_queue_response (connection, MHD_HTTP_OK,response);
  MHD_destroy_response(response);

  return ret;
}

static void activity_update(const data_stream * s, const void * data, size_t length, void * userdata){
  UNUSED(data);
  UNUSED(length);
  listen_ctx * ctx = userdata;
  iron_mutex_lock(ctx->lock);
  for(size_t i = 0; i < ctx->activity_count; i++){
    if(memcmp(ctx->activitys[i], s->name, ctx->activity_length[i]) == 0){
      iron_mutex_unlock(ctx->lock);
      return;
    }
  }

  ctx->activitys = ralloc(ctx->activitys, sizeof(ctx->activitys[0]) * (ctx->activity_count += 1));
  ctx->activity_length = ralloc(ctx->activity_length, sizeof(ctx->activity_length[0]) * ctx->activity_count);
  ctx->activity_state = ralloc(ctx->activity_state, sizeof(ctx->activity_state[0]) * ctx->activity_count);
  ctx->activitys[ctx->activity_count - 1]= fmtstr("%s", s->name);// iron_clone(s->name, strlen(s->name) + 1);
  ctx->activity_length[ctx->activity_count - 1] = strlen(s->name) + 1;
  ctx->activity_state[ctx->activity_count - 1] = ENABLED;

  iron_mutex_unlock(ctx->lock);
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
    if(prev != NULL){
      //logd("Reuse! %p %p\n", prev, ctx->prev);
    }
  }
  
  thing = ralloc(thing, _length);
  void * buf = thing;
  
  thing->length = length;
  thing->name = buf + sizeof(message_list);
  memcpy((char *)thing->name, s->name, strlen(s->name) + 1);
  thing->message = buf + (_length - length) - 2;
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

  daemon = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
                             &answer_to_connection, ctx, MHD_OPTION_END);
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
