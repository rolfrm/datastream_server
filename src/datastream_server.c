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
  listen_ctx * ctx = cls;

  char message[4000];
  char * msg = message;
  msg += sprintf(msg,"<html><body>");

  for(size_t i = 0; i< ctx->activity_count; i++){
    dlog(weblog, ctx->activitys[i], ctx->activity_length[i]);  
    msg += sprintf(msg,"<p>%s</p>", ctx->activitys[i]);    
  }
  msg += sprintf(msg,"</body></html>");
  size_t len = msg - message;
  dmsg(weblog, "Response of : %i bytes\n", len);  
  var response =
     MHD_create_response_from_buffer (len, message,MHD_RESPMEM_MUST_COPY);
   
  int ret =  MHD_queue_response (connection, MHD_HTTP_OK,response);
  MHD_destroy_response(response);

  return ret;
}

static void activity_update(const data_stream * s, const void * data, size_t length, void * userdata){
  UNUSED(data);
  UNUSED(length);
  listen_ctx * ctx = userdata;
  for(size_t i = 0; i < ctx->activity_count; i++){
    if(memcmp(ctx->activitys[i], s->name, ctx->activity_length[i]) == 0)
      return;
  }
  iron_mutex_lock(ctx->lock);
  ctx->activitys = ralloc(ctx->activitys, sizeof(ctx->activitys[0]) * (ctx->activity_count += 1));
  ctx->activity_length = ralloc(ctx->activity_length, sizeof(ctx->activity_length[0]) * ctx->activity_count);
  ctx->activity_state = ralloc(ctx->activity_state, sizeof(ctx->activity_state[0]) * ctx->activity_count);
  ctx->activitys[ctx->activity_count - 1]= iron_clone(s->name, strlen(s->name) + 1);
  ctx->activity_length[ctx->activity_count - 1] = strlen(s->name) + 1;
  ctx->activity_state[ctx->activity_count - 1] = ENABLED;
  data_stream_listen(ctx->data_listener, (data_stream *) s);
  iron_mutex_unlock(ctx->lock);
}

static void data_update(const data_stream * s, const void * data, size_t length, void * userdata){
  logd("Got data.. %s\n", data);
  //ASSERT(data != NULL);
  size_t _length = strlen(s->name) + 1 + length + sizeof(message_list);
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
  thing->message = buf + (_length - length);
  memcpy(thing->message, data, length);

  { // interlocked replace
    
  try_again2:;
    message_list * nxt = ctx->messages;
    thing->next = nxt;
    if(!__sync_bool_compare_and_swap(&ctx->messages, thing, nxt))
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
  activity_listener->process = data_update;
  activity_listener->userdata = ctx;

  
  ctx->listener = activity_listener;
  ctx->data_listener = data_listener;
  ctx->activitys = NULL;
  ctx->activity_length = NULL;
  ctx->activity_count = 0;
  ctx->lock = iron_mutex_create();
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
