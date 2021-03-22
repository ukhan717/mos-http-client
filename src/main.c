/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mgos.h"
#include "mgos_jstore.h"
#include "mgos_cron.h"
#include "mgos_rpc.h"

struct state {
  struct mg_rpc_request_info *ri; /* RPC request info */
  int status;                     /* Request status */
  int64_t written;                /* Number of bytes written */
  FILE *fp;                       /* File to write to */
};

static char *cron_expression = "0 */1 * * * *";

static void log_message(const char *filename, const char *message) {
  FILE *fp = fopen(filename, "a");
  if (fp != NULL) {
    fprintf(fp, "%s", message);
    fclose(fp);
  }
  else
    LOG(LL_INFO, ("\r\nFile Not Opened\r\n"));
}

static void http_cb(struct mg_connection *c, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;
  struct state *state = (struct state *) ud;

  switch (ev) {
    case MG_EV_CONNECT:
      state->status = *(int *) ev_data;
      break;
    case MG_EV_HTTP_CHUNK: {
      /*
       * Write data to file. 
       */
      size_t n = 0;
          if(state->fp != NULL)
              {
                n = fwrite(hm->body.p, 1, hm->body.len, state->fp);
                LOG(LL_INFO, ("Writing File %s",hm->body.p));
                log_message("LogFile.txt", hm->body.p);
              }
      if (n != hm->body.len) {
        c->flags |= MG_F_CLOSE_IMMEDIATELY;
        state->status = 500;
      }
      state->written += n;
      c->flags |= MG_F_DELETE_CHUNK;
      break;
    }
    case MG_EV_HTTP_REPLY:
      /* Only when we successfully got full reply, set the status. */
      state->status = hm->resp_code;
      LOG(LL_INFO, ("Finished fetching"));
      c->flags |= MG_F_CLOSE_IMMEDIATELY;
      break;
    case MG_EV_CLOSE:
      LOG(LL_INFO, ("status %d bytes %llu", state->status, state->written));
      if (state->status == 200) {
        /* Report success only for HTTP 200 downloads */
        LOG(LL_INFO, ("*******200*********"));
      } else {
        LOG(LL_INFO, ("*******ERR RSP********"));
      }
      if (state->fp != NULL) fclose(state->fp);
      free(state);
      break;
  }
}


static void cron_1min_cb(void *user_data, mgos_cron_id_t id) {
  (void) user_data;
  (void) id;
  struct state *state;
  FILE *fp = NULL;
  char *url = "http://info.cern.ch", *path = "LogFile.txt";

  if (path != NULL && (fp = fopen(path, "w")) == NULL) {
    LOG(LL_INFO, ("********MALFORMED URL***********"));//mg_rpc_send_errorf((struct mg_rpc_request_info *)user_data, 500, "cannot open %s", path);
    goto done;
  }

  if ((state = calloc(1, sizeof(*state))) == NULL) {
    LOG(LL_INFO, ("********MALFORMED URL***********"));// mg_rpc_send_errorf((struct mg_rpc_request_info *)user_data, 500, "OOM");
    goto done;
  }
  state->fp = fp;
  if (!mg_connect_http(mgos_get_mgr(), http_cb, state, url, NULL, NULL)) {
    free(state);
    //mg_rpc_send_errorf((struct mg_rpc_request_info *)user_data, 500, "malformed URL");
    LOG(LL_INFO, ("********MALFORMED URL***********"));
    goto done;
  }

done:
  LOG(LL_INFO, ("********cron JOB Done***********"));
}

static void http_cb_rpc(struct mg_connection *c, int ev, void *ev_data, void *ud) {
  struct http_message *hm = (struct http_message *) ev_data;
  struct state *state = (struct state *) ud;

  switch (ev) {
    case MG_EV_CONNECT:
      state->status = *(int *) ev_data;
      break;
    case MG_EV_HTTP_CHUNK: {
      /*
       * Write data to file
       */
      size_t n = 0;
      if (state->fp != NULL)
        n = fwrite(hm->body.p, 1, hm->body.len, state->fp);

      if (n != hm->body.len) {
        c->flags |= MG_F_CLOSE_IMMEDIATELY;
        state->status = 500;
      }
      state->written += n;
      c->flags |= MG_F_DELETE_CHUNK;
      break;
    }
    case MG_EV_HTTP_REPLY:
      /* Only when we successfully got full reply, set the status. */
      state->status = hm->resp_code;
      LOG(LL_INFO, ("Finished fetching"));
      c->flags |= MG_F_CLOSE_IMMEDIATELY;
      break;
    case MG_EV_CLOSE:
      LOG(LL_INFO, ("status %d bytes %llu", state->status, state->written));
      if (state->status == 200) {
        /* Report success only for HTTP 200 downloads */
        mg_rpc_send_responsef(state->ri, "{written: %llu}", state->written);
      } else {
        mg_rpc_send_errorf(state->ri, state->status, NULL);
      }
      if (state->fp != NULL) fclose(state->fp);
      free(state);
      break;
  }
}

static void http_fetch_handler(struct mg_rpc_request_info *ri, void *cb_arg,
                          struct mg_rpc_frame_info *fi, struct mg_str args) {
  struct state *state;
  FILE *fp = NULL;
  char *url = NULL, *path = NULL;

  LOG(LL_INFO, ("********Fetch Handler***********"));

  json_scanf(args.p, args.len, ri->args_fmt, &url, &path);

  if (url == NULL || (path == NULL)) {
    mg_rpc_send_errorf(ri, 500, "expecting url or file");
    goto done;
  }

  if (path != NULL && (fp = fopen(path, "w")) == NULL) {
    mg_rpc_send_errorf(ri, 500, "cannot open %s", path);
    goto done;
  }

  if ((state = calloc(1, sizeof(*state))) == NULL) {
    mg_rpc_send_errorf(ri, 500, "OOM");
    goto done;
  }

  state->fp = fp;
  state->ri = ri;

  LOG(LL_INFO, ("Fetching %s to %s", url, path ? path : ""));
  if (!mg_connect_http(mgos_get_mgr(), http_cb_rpc, state, url, NULL, NULL)) {
    free(state);
    mg_rpc_send_errorf(ri, 500, "malformed URL");
    goto done;
  }

  (void) cb_arg;
  (void) fi;

done:
  free(url);
  free(path);
}


enum mgos_app_init_result mgos_app_init(void) {  
  mgos_cron_add(cron_expression, cron_1min_cb, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "http_fetch_handler",
                     "{url: %Q, file: %Q}", http_fetch_handler, NULL);
  return MGOS_APP_INIT_SUCCESS;
}
