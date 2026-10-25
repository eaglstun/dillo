/*
 * File: control.c
 *
 * Copyright (C) 2025-2026 Rodrigo Arias Mallo <rodarima@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

/** @file
 * Dillo remote control socket.
 *
 * Allow other programs to interact with Dillo.
 */

#include "control.h"

#include "config.h"

#if ENABLE_CONTROL_SOCKET

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "iowatch.hh"
#include "uicmd.hh"
#include "capi.h"
#include "dlib/dlib.h"
#include "msg.h"
#include "timeout.hh"
#include "html.hh"   /* a_Html_* introspection; url.h comes via uicmd.hh */
#include "prefs.h"

enum control_req_stage {
   REQ_READ_COMMAND,
   REQ_READ_BODY,
   REQ_PREPARE_REPLY,
   REQ_SWITCH_WRITE,
   REQ_WRITE_REPLY,
   REQ_CLOSE,
};

struct control_req {
   int fd;
   enum control_req_stage stage;
   Dstr *cmd;
   Dstr *body;
   Dstr *reply;
   size_t reply_offset;
};

/* One control fd per process */
static int control_fd = -1;
static char *control_path = NULL;

/* The BrowserWindow that we are currently waiting to finish */
static BrowserWindow *bw_waiting = NULL;
static struct control_req *bw_waiting_req = NULL;

static void handle_wait_timeout(void *data);

/* Read the given non-blocking fd, read available data into output.
 * Returns:
 *    0:  when the end of file has been reached.
 *   +1:  if there is more data to be read.
 *   <0: other unexpected error.
 */
static int
Control_read_fd(int fd, Dstr *output)
{
   ssize_t r;
   do {
      char buf[BUFSIZ];
      r = read(fd, buf, BUFSIZ);
      _MSG("read buf = %zd\n", r);
      if (r < 0) {
         if (errno == EINTR) {
            continue;
         } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Reached end of available data, try later */
            return +1;
         } else {
            MSG_ERR("Control_read_fd: read failed: %s\n", dStrerror(errno));
            return -1;
         }
      } else if (r > 0) {
         dStr_append_l(output, buf, r);
      }
   } while (r);

   return 0;
}

static int
Control_write_fd(int fd, Dstr *input, size_t *ptr_offset)
{
   size_t offset = *ptr_offset;
   ssize_t w;

   while ((size_t) input->len > offset) {
      const char *src = input->str + offset;
      size_t left = input->len - offset;
      w = write(fd, src, left);
      if (w < 0) {
         if (errno == EINTR) {
            continue;
         } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Cannot write more data, return error */
            *ptr_offset = offset;
            return +1;
         } else {
            MSG_ERR("Control_write_fd: write failed: %s\n", dStrerror(errno));
            return -1;
         }
      } else if (w > 0) {
         offset += w;
      }
   }

   /* Save offset just in case */
   *ptr_offset = offset;

   return 0;
}

/* Create new request structure */
static struct control_req *Control_req_new(int fd)
{
   struct control_req *req = dMalloc0(sizeof(struct control_req));
   req->fd = fd;
   req->cmd = dStr_sized_new(128);
   req->body = dStr_sized_new(1);
   req->reply = dStr_sized_new(64);
   req->stage = REQ_READ_COMMAND;

   return req;
}

static void Control_req_free(struct control_req *req)
{
   dStr_free(req->cmd, 1);
   dStr_free(req->body, 1);
   dStr_free(req->reply, 1);
   dFree(req);
}

static void Control_req_read_cmd(struct control_req *req)
{
   ssize_t r;
   int err = 0;
   do {
      /* Read characters one by one */
      char buf[1];
      r = read(req->fd, buf, 1);
      if (r < 0) {
         if (errno == EINTR) {
            continue;
         } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Reached end of available data for now, try later */
            return;
         } else {
            MSG_ERR("Control_req_read_cmd: read failed: %s\n", dStrerror(errno));
            err = 1;
            break;
         }
      } else if (r == 1) {
         dStr_append_l(req->cmd, buf, 1);
      }
      if (buf[0] == '\n')
         break;
   } while (r);

   if (err || req->cmd->len == 0) {
      /* Problem reading command or empty (may be caused by a reset
       * connection). Go straight to close the request. */
      req->stage = REQ_CLOSE;
   } else {
      /* Got command, strip new line and line feed */
      char *cmd = req->cmd->str;
      for (char *p = cmd; *p != '\0'; p++) {
         if (*p == '\n' || *p == '\r') {
            *p = '\0';
            break;
         }
      }

      /* Only determine if we need to read the body or not */
      if (!strcmp(cmd, "load") || !strcmp(cmd, "rawload"))
         req->stage = REQ_READ_BODY;
      else
         req->stage = REQ_PREPARE_REPLY;
   }
}

/* True if cmd is exactly name, or name followed by a space and arguments */
static int Control_cmd_is(const char *cmd, const char *name)
{
   size_t n = strlen(name);
   return strncmp(cmd, name, n) == 0 && (cmd[n] == '\0' || cmd[n] == ' ');
}

/* Skip the verb and any spaces, returning the argument string */
static const char *Control_cmd_args(const char *cmd, const char *name)
{
   const char *p = cmd + strlen(name);
   while (*p == ' ')
      p++;
   return p;
}

/* Parse a non-negative decimal index at *p. On success store it in *n,
 * advance *p past the number and any following spaces, and return 0. */
static int Control_parse_index(const char **p, int *n)
{
   const char *s = *p;
   long v = 0;

   if (!dIsdigit(*s))
      return -1;
   while (dIsdigit(*s)) {
      v = v * 10 + (*s - '0');
      if (v > 1000000)
         return -1;
      s++;
   }
   if (*s != '\0' && *s != ' ')
      return -1;
   while (*s == ' ')
      s++;
   *p = s;
   *n = (int) v;
   return 0;
}

/* Copy one space-delimited word from *p (NULL if none), advance *p past it
 * and any following spaces. The caller frees the result. */
static char *Control_parse_word(const char **p)
{
   const char *s = *p;
   const char *e = s;

   while (*e && *e != ' ')
      e++;
   if (e == s)
      return NULL;
   char *word = dStrndup(s, e - s);
   while (*e == ' ')
      e++;
   *p = e;
   return word;
}

/* The HTML document of the current tab. On failure (no window, or the tab
 * holds no HTML document) write an error reply and return NULL. */
static void *Control_get_doc(BrowserWindow *bw, Dstr *r)
{
   if (bw == NULL) {
      dStr_sprintfa(r, "1\nno browser window\n");
      return NULL;
   }
   void *doc = a_Bw_get_current_doc(bw);
   if (doc == NULL)
      dStr_sprintfa(r, "1\ncurrent tab has no HTML document\n");
   return doc;
}

static void Control_req_prepare_reply(struct control_req *req)
{
   int done = 1;
   const char *cmd = req->cmd->str;
   Dstr *r = req->reply;
   BrowserWindow *bw = a_UIcmd_get_first_active_bw();

   if (strcmp(cmd, "help") == 0) {
      dStr_sprintfa(r, "0\n");
      dStr_sprintfa(r, "Commands return 0 on success or non-zero on error.\n");
      dStr_sprintfa(r, "Available commands:\n");
      dStr_sprintfa(r, " ping          Check if dillo replies correctly:\n");
      dStr_sprintfa(r, " pid           Print PID of selected dillo process\n");
      dStr_sprintfa(r, " reload        Reload the current tab\n");
      dStr_sprintfa(r, " ready         Exits with 0 if finished loading, 1 otherwise\n");
      dStr_sprintfa(r, " open URL      Open the given URL in the current tab\n");
      dStr_sprintfa(r, " url           Print the url in the current tab\n");
      dStr_sprintfa(r, " title         Print the title of page in the current tab\n");
      dStr_sprintfa(r, " status [MSG]  Set the status bar to MSG\n");
      dStr_sprintfa(r, " dump          Print the content of the current tab\n");
      dStr_sprintfa(r, " hdump         Print the HTTP headers of the current tab\n");
      dStr_sprintfa(r, " load          Replace the content in the current tab by stdin\n");
      dStr_sprintfa(r, " rawload       Replace the HTTP headers and content of the current\n");
      dStr_sprintfa(r, "               tab by stdin\n");
      dStr_sprintfa(r, " quit          Close dillo\n");
      dStr_sprintfa(r, " wait [T]      Wait until the current tab has finished loading\n");
      dStr_sprintfa(r, "               at most T seconds (default 60.0). Wait forever with\n");
      dStr_sprintfa(r, "               T set to 0.\n");
      dStr_sprintfa(r, "Page introspection (indices are 0-based, fields are TAB separated):\n");
      dStr_sprintfa(r, " links         List links: index, absolute href (empty if none)\n");
      dStr_sprintfa(r, " forms         List forms: index, method, action, enctype\n");
      dStr_sprintfa(r, " inputs N      List inputs of form N: index, type, name, value.\n");
      dStr_sprintfa(r, "               Checkbox/radio values read on:VALUE or off:VALUE.\n");
      dStr_sprintfa(r, "               Tab, newline and backslash in values are escaped\n");
      dStr_sprintfa(r, "               as \\t \\n \\\\ so every input is one line.\n");
      dStr_sprintfa(r, " resolve HREF  Print HREF resolved against the current page URL\n");
      dStr_sprintfa(r, "Forms and links:\n");
      dStr_sprintfa(r, " set N NAME [VALUE]\n");
      dStr_sprintfa(r, "               Set input NAME of form N to VALUE, the rest of the\n");
      dStr_sprintfa(r, "               line (spaces are fine, newlines cannot be sent).\n");
      dStr_sprintfa(r, "               text/password/textarea/hidden take text; checkbox\n");
      dStr_sprintfa(r, "               takes on/off; radio and select take an option value.\n");
      dStr_sprintfa(r, "               File inputs cannot be set. Status: 1 no such form,\n");
      dStr_sprintfa(r, "               2 no such input, 3 not settable, 4 value rejected.\n");
      dStr_sprintfa(r, " submit N [NAME]\n");
      dStr_sprintfa(r, "               Submit form N as if submit button NAME was clicked.\n");
      dStr_sprintfa(r, "               Starts a page load, so run \"wait\" afterwards.\n");
      dStr_sprintfa(r, "               Status: 1 no such form, 2 no such button, 3 refused\n");
      dStr_sprintfa(r, "               (insecure submission cancelled), 4 bad method.\n");
      dStr_sprintfa(r, " reset N       Reset form N to its initial values\n");
      dStr_sprintfa(r, " follow N      Open link N from \"links\". Starts a page load, so\n");
      dStr_sprintfa(r, "               run \"wait\" afterwards.\n");
      dStr_sprintfa(r, " noninteractive [0|1]\n");
      dStr_sprintfa(r, "               Print or set non-interactive mode, in which modal\n");
      dStr_sprintfa(r, "               dialogs (insecure form submission, file chooser, TLS\n");
      dStr_sprintfa(r, "               warnings, ...) are answered with their safe default\n");
      dStr_sprintfa(r, "               instead of blocking. Also the dillorc option.\n");
   } else if (strcmp(cmd, "ping") == 0) {
      dStr_sprintfa(r, "0\npong\n");
   } else if (strcmp(cmd, "pid") == 0) {
      dStr_sprintfa(r, "0\n%d\n", getpid());
   } else if (strcmp(cmd, "reload") == 0) {
      a_UIcmd_reload_all_active();
      dStr_sprintfa(r, "0\n");
   } else if (strcmp(cmd, "ready") == 0) {
      dStr_sprintfa(r, "%d\n", a_UIcmd_has_finished(bw) ? 0 : 1);
   } else if (strncmp(cmd, "open ", 5) == 0) {
      a_UIcmd_open_urlstr(bw, cmd+5);
      dStr_sprintfa(r, "0\n");
   } else if (strcmp(cmd, "url") == 0) {
      char *location = a_UIcmd_get_location_text(bw);
      dStr_sprintfa(r, "0\n%s\n", location);
      dFree(location);
   } else if (strcmp(cmd, "title") == 0) {
      const char *title = a_UIcmd_get_page_title(bw);
      if (title)
         dStr_sprintfa(r, "0\n%s\n", title);
      else
         dStr_sprintfa(r, "1\n");
   } else if (!strcmp(cmd, "status") || !strncmp(cmd, "status ", 7)) {
      if (cmd[6] == ' ')
         a_UIcmd_set_msg(bw, "%s", cmd + 7);
      else
         a_UIcmd_set_msg(bw, "");
      dStr_sprintfa(r, "0\n");
   } else if (strcmp(cmd, "dump") == 0) {
      char *location = a_UIcmd_get_location_text(bw);
      DilloUrl *url = a_Url_new(location, NULL);
      dFree(location);
      char *buf;
      int size;
      dStr_sprintfa(r, "0\n");
      if (a_Capi_get_buf(url, &buf, &size))
         dStr_append_l(r, buf, size);
      a_Url_free(url);
   } else if (strcmp(cmd, "hdump") == 0) {
      char *location = a_UIcmd_get_location_text(bw);
      DilloUrl *url = a_Url_new(location, NULL);
      dFree(location);
      Dstr *header = a_Cache_get_header(url);
      if (header == NULL) {
         dStr_sprintfa(r, "1\nCurrent url not cached");
      } else {
         dStr_sprintfa(r, "0\n");
         dStr_append_l(r, header->str, header->len);
      }
      a_Url_free(url);
   } else if (strcmp(cmd, "load") == 0) {
      char *location = a_UIcmd_get_location_text(bw);
      DilloUrl *url = a_Url_new(location, NULL);
      dFree(location);
      if (url) {
         a_Cache_entry_inject(url, req->body->str, req->body->len,
               CA_GotHeader | CA_GotLength);
         a_UIcmd_repush(bw);
         dStr_sprintfa(r, "0\n");
         a_Url_free(url);
      } else {
         dStr_sprintfa(r, "1\ncannot get current url");
      }
   } else if (strcmp(cmd, "rawload") == 0) {
      char *location = a_UIcmd_get_location_text(bw);
      DilloUrl *url = a_Url_new(location, NULL);
      dFree(location);
      if (url) {
         a_Cache_entry_inject(url, req->body->str, req->body->len, 0);
         a_UIcmd_repush(bw);
         dStr_sprintfa(r, "0\n");
         a_Url_free(url);
      } else {
         dStr_sprintfa(r, "1\ncannot get current url");
      }
   } else if (strcmp(cmd, "quit") == 0) {
      a_UIcmd_close_all_bw((void *)1);
      dStr_sprintfa(r, "0\n");
   } else if (strncmp(cmd, "cmd ", 4) == 0) {
      const char *cmdname = cmd + 4;
      int ret = a_UIcmd_by_name(bw, cmdname);
      dStr_sprintfa(r, "%d\n", ret == 0 ? 0 : 1);
   } else if (strcmp(cmd, "wait") == 0 || strncmp(cmd, "wait ", 5) == 0) {
      float timeout = 60.0f; /* 1 minute by default */
      /* Contains timeout argument? */
      if (cmd[4] == ' ')
         timeout = atof(&cmd[5]);
      if (a_UIcmd_has_finished(bw)) {
         dStr_sprintfa(r, "0\n");
      } else {
         if (bw_waiting) {
            dStr_sprintfa(r, "1\nalready waiting\n");
         } else {
            bw_waiting = bw;
            bw_waiting_req = req;
            done = 0;
            /* If timeout is 0 or negative, wait forever */
            if (timeout > 0.0f)
               a_Timeout_add(timeout, handle_wait_timeout, NULL);
         }
      }
   } else if (strcmp(cmd, "links") == 0) {
      void *doc = Control_get_doc(bw, r);
      if (doc) {
         dStr_sprintfa(r, "0\n");
         a_Html_links_describe(doc, r);
      }
   } else if (strcmp(cmd, "forms") == 0) {
      void *doc = Control_get_doc(bw, r);
      if (doc) {
         dStr_sprintfa(r, "0\n");
         a_Html_forms_describe(doc, r);
      }
   } else if (Control_cmd_is(cmd, "inputs")) {
      const char *p = Control_cmd_args(cmd, "inputs");
      int n;
      void *doc = Control_get_doc(bw, r);
      if (doc) {
         if (Control_parse_index(&p, &n) != 0 || *p) {
            dStr_sprintfa(r, "1\nusage: inputs N\n");
         } else {
            Dstr *out = dStr_new("");
            if (a_Html_inputs_describe(doc, n, out) == 0) {
               dStr_sprintfa(r, "0\n");
               dStr_append_l(r, out->str, out->len);
            } else {
               dStr_sprintfa(r, "1\nno such form: %d\n", n);
            }
            dStr_free(out, 1);
         }
      }
   } else if (Control_cmd_is(cmd, "resolve")) {
      const char *href = Control_cmd_args(cmd, "resolve");
      if (bw == NULL) {
         dStr_sprintfa(r, "1\nno browser window\n");
      } else if (*href == '\0') {
         dStr_sprintfa(r, "1\nusage: resolve HREF\n");
      } else {
         char *base = a_UIcmd_get_location_text(bw);
         DilloUrl *url = a_Url_new(href, base);
         if (url) {
            dStr_sprintfa(r, "0\n%s\n", URL_STR(url));
            a_Url_free(url);
         } else {
            dStr_sprintfa(r, "1\ncannot resolve: %s\n", href);
         }
         dFree(base);
      }
   } else if (Control_cmd_is(cmd, "set")) {
      const char *p = Control_cmd_args(cmd, "set");
      int n;
      void *doc = Control_get_doc(bw, r);
      if (doc) {
         char *name = NULL;
         if (Control_parse_index(&p, &n) != 0 ||
             (name = Control_parse_word(&p)) == NULL) {
            dStr_sprintfa(r, "1\nusage: set N NAME [VALUE]\n");
         } else {
            /* p is the rest of the line: the value, possibly empty */
            int ret = a_Html_input_set(doc, n, name, p);
            switch (ret) {
            case 0:
               dStr_sprintfa(r, "0\n");
               break;
            case 1:
               dStr_sprintfa(r, "1\nno such form: %d\n", n);
               break;
            case 2:
               dStr_sprintfa(r, "2\nno such input in form %d: %s\n", n, name);
               break;
            case 3:
               dStr_sprintfa(r, "3\ninput cannot be set remotely: %s\n", name);
               break;
            default:
               dStr_sprintfa(r, "4\nvalue not accepted for %s: %s\n", name, p);
               break;
            }
         }
         dFree(name);
      }
   } else if (Control_cmd_is(cmd, "submit")) {
      const char *p = Control_cmd_args(cmd, "submit");
      int n;
      void *doc = Control_get_doc(bw, r);
      if (doc) {
         if (Control_parse_index(&p, &n) != 0) {
            dStr_sprintfa(r, "1\nusage: submit N [NAME]\n");
         } else {
            char *button = Control_parse_word(&p);
            int ret = a_Html_form_submit_named(doc, n, button);
            switch (ret) {
            case 0:
               dStr_sprintfa(r, "0\n");
               break;
            case 1:
               dStr_sprintfa(r, "1\nno such form: %d\n", n);
               break;
            case 2:
               dStr_sprintfa(r, "2\nno such submit button in form %d: %s\n",
                             n, button);
               break;
            case 3:
               dStr_sprintfa(r, "3\nsubmission refused: insecure form "
                                "submission cancelled\n");
               break;
            default:
               dStr_sprintfa(r, "4\ncannot build request: unknown form "
                                "method\n");
               break;
            }
            dFree(button);
         }
      }
   } else if (Control_cmd_is(cmd, "reset")) {
      const char *p = Control_cmd_args(cmd, "reset");
      int n;
      void *doc = Control_get_doc(bw, r);
      if (doc) {
         if (Control_parse_index(&p, &n) != 0 || *p)
            dStr_sprintfa(r, "1\nusage: reset N\n");
         else if (a_Html_form_reset_n(doc, n) == 0)
            dStr_sprintfa(r, "0\n");
         else
            dStr_sprintfa(r, "1\nno such form: %d\n", n);
      }
   } else if (Control_cmd_is(cmd, "follow")) {
      const char *p = Control_cmd_args(cmd, "follow");
      int n;
      void *doc = Control_get_doc(bw, r);
      if (doc) {
         if (Control_parse_index(&p, &n) != 0 || *p) {
            dStr_sprintfa(r, "1\nusage: follow N\n");
         } else if (n >= a_Html_link_count(doc)) {
            dStr_sprintfa(r, "1\nno such link: %d\n", n);
         } else {
            DilloUrl *url = a_Html_link_url(doc, n);
            if (url == NULL) {
               dStr_sprintfa(r, "1\nlink %d has no href\n", n);
            } else {
               /* Loading a page frees the document that owns url */
               DilloUrl *copy = a_Url_dup(url);
               a_UIcmd_open_url(bw, copy);
               a_Url_free(copy);
               dStr_sprintfa(r, "0\n");
            }
         }
      }
   } else if (Control_cmd_is(cmd, "noninteractive")) {
      const char *p = Control_cmd_args(cmd, "noninteractive");
      if (*p == '\0') {
         dStr_sprintfa(r, "0\n%d\n", prefs.noninteractive ? 1 : 0);
      } else if (strcmp(p, "0") == 0 || strcmp(p, "1") == 0) {
         prefs.noninteractive = (*p == '1');
         dStr_sprintfa(r, "0\n");
      } else {
         dStr_sprintfa(r, "1\nusage: noninteractive [0|1]\n");
      }
   } else {
      dStr_sprintfa(r, "1\nunknown command: %s\n", cmd);
   }

   if (done)
      req->stage = REQ_SWITCH_WRITE;
}

/* Process incoming or outgoing data from a control request */
static void Control_req_read_cb(int fd, void *data)
{
   struct control_req *req = data;

   /* Note: These are not if-elses on purpose. */

   if (req->stage == REQ_READ_COMMAND)
      Control_req_read_cmd(req);

   if (req->stage == REQ_READ_BODY) {
      /* Read until the end of the data or error */
      int ret = Control_read_fd(fd, req->body);
      if (ret == 0)
         req->stage = REQ_PREPARE_REPLY;
      else if (ret < 0)
         req->stage = REQ_CLOSE;
   }

   if (req->stage == REQ_PREPARE_REPLY)
      Control_req_prepare_reply(req);

   if (req->stage == REQ_SWITCH_WRITE) {
      /* Switch callback to write */
      a_IOwatch_remove_fd(fd, DIO_READ);
      a_IOwatch_add_fd(fd, DIO_WRITE, Control_req_read_cb, req);
      req->stage = REQ_WRITE_REPLY;
   }

   if (req->stage == REQ_WRITE_REPLY) {
      /* Keep writing until end or error */
      if (Control_write_fd(fd, req->reply, &req->reply_offset) <= 0)
         req->stage = REQ_CLOSE;
   }

   if (req->stage == REQ_CLOSE) {
      /* Remove any callback */
      a_IOwatch_remove_fd(fd, DIO_READ | DIO_WRITE);
      dClose(fd);
      Control_req_free(req);
   }
}

/* Process incoming connection to control socket */
static void Control_listen_cb(int fd, void *data)
{
   _MSG("Control_listen_cb called\n");

   int new_fd = accept(control_fd, NULL, NULL);
   if (new_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
         /* Nothing? */
         return;
      } else {
         MSG_ERR("accept failed: %s\n", strerror(errno));
         exit(1);
      }
   }

   /* From accept(2) man page:
    *
    * On Linux, the new socket returned by accept() does not inherit
    * file status flags such as O_NONBLOCK and O_ASYNC from the
    * listening socket. This behavior differs from the canonical BSD
    * sockets implementation. Portable programs should not rely on
    * inheritance or noninheritance of file status flags and always
    * explicitly set all required flags on the socket returned from
    * accept().
    *
    * So, enable O_NONBLOCK flag explicitly.
    */
   if (fcntl(new_fd, F_SETFL, O_NONBLOCK) == -1) {
      MSG_ERR("control socket fcntl failed: %s\n", strerror(errno));
      exit(1);
   }

   struct control_req *req = Control_req_new(new_fd);
   a_IOwatch_add_fd(new_fd, DIO_READ, Control_req_read_cb, req);
}

/* Timeout has passed, return error */
static void handle_wait_timeout(void *data)
{
   assert(bw_waiting);
   assert(bw_waiting_req);

   struct control_req *req = bw_waiting_req;
   dStr_sprintfa(req->reply, "1\nCurrent url not cached");
   req->stage = REQ_SWITCH_WRITE;

   bw_waiting = NULL;

   /* Run another pass to do progress */
   Control_req_read_cb(req->fd, req);
}

int a_Control_is_waiting(void)
{
   return bw_waiting != NULL;
}

void a_Control_notify_finish(BrowserWindow *bw)
{
   if (bw_waiting == NULL || bw_waiting != bw)
      return;

   _MSG("a_Control_notify_finish matched\n");

   assert(bw_waiting_req);

   struct control_req *req = bw_waiting_req;
   dStr_sprintfa(req->reply, "0\n");
   req->stage = REQ_SWITCH_WRITE;

   a_Timeout_actually_remove(handle_wait_timeout, NULL);
   bw_waiting = NULL;
   bw_waiting_req = NULL;

   /* Run another pass to do progress */
   Control_req_read_cb(req->fd, req);
}

int a_Control_init(void)
{
   int fd;
   if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
      MSG("cannot create control socket: %s\n", strerror(errno));
      return -1;
   }

   struct sockaddr_un addr;
   addr.sun_family = AF_UNIX;

#define LEN ((int) sizeof(addr.sun_path))

   char ctldir[LEN];
   if (snprintf(ctldir, LEN, "%s/.dillo/ctl", dGethomedir()) >= LEN) {
      MSG("control socket dir path too long: %s/.dillo/ctl\n", dGethomedir());
      return -1;
   }

   /* Only the user should have access, otherwise other users could control the
    * browser remotely */
   if (mkdir(ctldir, 0700) != 0) {
      if (errno != EEXIST) {
         MSG("cannot create ctl dir: %s\n", strerror(errno));
         return -1;
      }
   }

   if (snprintf(addr.sun_path, LEN, "%s/%d", ctldir, (int) getpid()) >= LEN) {
      MSG("control socket path too long: %s/%d\n", ctldir, (int) getpid());
      return -1;
   }

   int slen = sizeof(addr);

   if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
      MSG("control socket fcntl failed: %s\n", strerror(errno));
      return -1;
   }

   int on = 1;
   if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0) {
      MSG("control socket setsockopt failed: %s\n", strerror(errno));
      return -1;
   }

   if (ioctl(fd, FIONBIO, (char *)&on) != 0) {
      MSG("control socket ioctl failed: %s\n", strerror(errno));
      return -1;
   }

   if (bind(fd, (struct sockaddr *) &addr, slen) != 0) {
      MSG("cannot bind control socket: %s\n", strerror(errno));
      return -1;
   }

   if (listen(fd, 32) != 0) {
      MSG("cannot listen control socket: %s\n", strerror(errno));
      return -1;
   }

   control_fd = fd;
   control_path = dStrdup(addr.sun_path);

   a_IOwatch_add_fd(control_fd, DIO_READ, Control_listen_cb, NULL);

   return 0;
}

int a_Control_free(void)
{
   /* Nothing to do */
   if (control_fd == -1)
      return 0;

   assert(control_path);

   a_IOwatch_remove_fd(control_fd, DIO_READ);
   if (dClose(control_fd) != 0) {
      MSG_ERR("close ctl socket failed\n");
      return -1;
   }
   if (unlink(control_path) != 0) {
      MSG_ERR("unlink ctl socket failed\n");
      return -1;
   }
   dFree(control_path);
   return 0;
}

#else

int a_Control_init(void) { return 0; }
int a_Control_is_waiting(void) { return 0; }
void a_Control_notify_finish(BrowserWindow *bw) { (void) bw; }
int a_Control_free(void) { return 0; }

#endif
