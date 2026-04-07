#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "signal.h"
#include "socket.h"
#include "stdio.h"

#define X6_DEFAULT_PORT 6006
#define X6_BACKLOG 16
#define X6_PROTO_VERSION 1
#define X6_PROC_PATH "/proc/server7"

#define X6_MAX_WINDOWS 128
#define X6_MAX_EVENTS_PER_CLIENT 64

// Event types
#define X6_EVENT_MAP_REQUEST 1
#define X6_EVENT_CONFIGURE_REQUEST 2
#define X6_EVENT_FOCUS_IN 3
#define X6_EVENT_FOCUS_OUT 4
#define X6_EVENT_DESTROY_NOTIFY 5

struct x6_event {
  int type;
  uint wid;      // window ID
  int x, y, w, h; // geometry for configure requests
};

struct x6_event_queue {
  struct x6_event events[X6_MAX_EVENTS_PER_CLIENT];
  int head;
  int tail;
};

struct x6_window {
  int in_use;
  uint id;
  int x;
  int y;
  int w;
  int h;
  int mapped;
};

// Per-client context (for future expansion)
struct x6_client {
  int fd;
  struct x6_event_queue queue;
};

static volatile sig_atomic_t keep_running = 1;
static struct x6_window wins[X6_MAX_WINDOWS];

// Per-client context (simplified for MVP: one connection at a time)
static struct x6_event_queue *current_event_queue = 0;

// WM state (Phase 2.1b: SubstructureRedirect semantics)
static int wm_has_redirect = 0;  // Does WM hold SubstructureRedirect on root?
static int wm_redirect_root = 1; // Root window ID is always 1

// Focus and keyboard state (Phase 2.1c)
static uint focus_wid = 0;          // Currently focused window (0 = no focus)
static uint keyboard_grab_owner = 0; // Who holds exclusive keyboard grab (0 = nobody, typically WM)
static int wm_has_kb_grab = 0;      // Does WM hold keyboard grab?

static void
usage(void)
{
  dprintf(2, "usage: x6 [-f] [-p port]\n");
  dprintf(2, "       -f   run in foreground (no daemonize)\n");
  dprintf(2, "       -p   listen port (default %d)\n", X6_DEFAULT_PORT);
  exit(1);
}

static int
daemonize_self(void)
{
  int pid;
  int fd;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  if(setsid() < 0)
    return -1;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  chdir("/");

  close(0);
  close(1);
  close(2);

  fd = open("/dev/console", O_RDWR);
  if(fd < 0)
    return 0;

  if(fd != 0) {
    dup2(fd, 0);
    close(fd);
  }
  dup(0);
  dup(0);

  return 0;
}

static void
on_term(int signo)
{
  if(signo == SIGTERM || signo == SIGINT)
    keep_running = 0;
}

static int
parse_port(const char *s)
{
  int p;

  if(s == 0)
    return -1;
  p = atoi(s);
  if(p < 1 || p > 65535)
    return -1;
  return p;
}

static int
x6_proc_write(const char *cmd)
{
  int fd;
  int n;

  fd = open(X6_PROC_PATH, O_RDWR);
  if(fd < 0)
    return -1;

  n = strlen(cmd);
  if(write(fd, (char *)cmd, n) != n) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int
x6_claim_display(void)
{
  return x6_proc_write("claim\n");
}

static void
x6_release_display(void)
{
  x6_proc_write("release\n");
}

static void
x6_send_line(int cfd, const char *s)
{
  if(cfd < 0 || s == 0)
    return;
  send(cfd, (void *)s, strlen(s));
}

static int
x6_recv_line(int cfd, char *buf, int buflen)
{
  int n;
  int i;

  if(cfd < 0 || buf == 0 || buflen <= 1)
    return -1;

  n = recv(cfd, buf, buflen - 1);
  if(n <= 0)
    return -1;
  buf[n] = 0;

  for(i = 0; i < n; i++) {
    if(buf[i] == '\n' || buf[i] == '\r') {
      buf[i] = 0;
      break;
    }
  }
  return 0;
}

static struct x6_window *
find_window(uint id)
{
  int i;

  for(i = 0; i < X6_MAX_WINDOWS; i++) {
    if(wins[i].in_use && wins[i].id == id)
      return &wins[i];
  }
  return 0;
}

static struct x6_window *
alloc_window(uint id)
{
  int i;

  for(i = 0; i < X6_MAX_WINDOWS; i++) {
    if(!wins[i].in_use) {
      wins[i].in_use = 1;
      wins[i].id = id;
      wins[i].x = 0;
      wins[i].y = 0;
      wins[i].w = 1;
      wins[i].h = 1;
      wins[i].mapped = 0;
      return &wins[i];
    }
  }
  return 0;
}

static void
destroy_window(uint id)
{
  struct x6_window *w;

  w = find_window(id);
  if(w == 0)
    return;
  memset(w, 0, sizeof(*w));
}

static void
x6_event_queue_init(struct x6_event_queue *q)
{
  q->head = 0;
  q->tail = 0;
}

static int
x6_event_queue_empty(struct x6_event_queue *q)
{
  return q->head == q->tail;
}

static int
x6_event_queue_enqueue(struct x6_event_queue *q, struct x6_event *evt)
{
  int next_tail;

  next_tail = (q->tail + 1) % X6_MAX_EVENTS_PER_CLIENT;
  if(next_tail == q->head)
    return -1; // Queue full, drop oldest
  q->events[q->tail] = *evt;
  q->tail = next_tail;
  return 0;
}

static int
x6_event_queue_dequeue(struct x6_event_queue *q, struct x6_event *evt)
{
  if(q->head == q->tail)
    return -1; // Empty
  *evt = q->events[q->head];
  q->head = (q->head + 1) % X6_MAX_EVENTS_PER_CLIENT;
  return 0;
}

static void
handle_one_command(int cfd, char *cmd)
{
  uint id;
  int x, y, w, h;
  struct x6_window *win;
  int i;
  int listed;

  if(strncmp(cmd, "HELLO x6/1", 10) == 0) {
    char out[128];
    snprintf(out, sizeof(out),
             "OK proto=%d transport=tcp-loopback screen=0 root=1 visual=truecolor depth=32\n",
             X6_PROTO_VERSION);
    x6_send_line(cfd, out);
    return;
  }

  if(strncmp(cmd, "PING", 4) == 0) {
    x6_send_line(cfd, "PONG\n");
    return;
  }

  if(strncmp(cmd, "QUIT", 4) == 0) {
    x6_send_line(cfd, "BYE\n");
    keep_running = 0;
    return;
  }

  if(sscanf(cmd, "CREATE %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    if(find_window(id) != 0) {
      x6_send_line(cfd, "ERR exists\n");
      return;
    }
    win = alloc_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR no-slots\n");
      return;
    }
    if(w < 1)
      w = 1;
    if(h < 1)
      h = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    x6_send_line(cfd, "OK create\n");
    return;
  }

  if(sscanf(cmd, "MAP %u", &id) == 1) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    
    // Phase 2.1b: If WM holds SubstructureRedirect, queue MapRequest for WM approval
    if(wm_has_redirect) {
      struct x6_event evt;
      evt.type = X6_EVENT_MAP_REQUEST;
      evt.wid = id;
      if(current_event_queue != 0) {
        x6_event_queue_enqueue(current_event_queue, &evt);
      }
      x6_send_line(cfd, "PENDING map\n");  // Client is notified of pending state
      return;
    }
    
    // Otherwise, map directly
    win->mapped = 1;
    x6_send_line(cfd, "OK map\n");
    return;
  }

  if(sscanf(cmd, "UNMAP %u", &id) == 1) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    win->mapped = 0;
    x6_send_line(cfd, "OK unmap\n");
    return;
  }

  if(sscanf(cmd, "CONFIGURE %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    
    // Phase 2.1b: If WM holds SubstructureRedirect, queue ConfigureRequest for WM approval
    if(wm_has_redirect) {
      struct x6_event evt;
      evt.type = X6_EVENT_CONFIGURE_REQUEST;
      evt.wid = id;
      evt.x = x;
      evt.y = y;
      evt.w = (w < 1) ? 1 : w;
      evt.h = (h < 1) ? 1 : h;
      if(current_event_queue != 0) {
        x6_event_queue_enqueue(current_event_queue, &evt);
      }
      x6_send_line(cfd, "PENDING configure\n");  // Client is notified of pending state
      return;
    }
    
    // Otherwise, configure directly
    if(w < 1)
      w = 1;
    if(h < 1)
      h = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    x6_send_line(cfd, "OK configure\n");
    return;
  }

  if(sscanf(cmd, "DESTROY %u", &id) == 1) {
    destroy_window(id);
    x6_send_line(cfd, "OK destroy\n");
    return;
  }

  // Phase 2.1b: REQUEST_REDIRECT for WM to claim SubstructureRedirect on root
  if(sscanf(cmd, "REQUEST_REDIRECT %u", &id) == 1) {
    if(id != wm_redirect_root) {
      x6_send_line(cfd, "ERR invalid-window\n");
      return;
    }
    if(wm_has_redirect) {
      x6_send_line(cfd, "ERR redirect-in-use\n");
      return;
    }
    wm_has_redirect = 1;
    x6_send_line(cfd, "OK redirect_granted\n");
    return;
  }

  // Phase 2.1b: WM-specific CONFIGURE response to honor child ConfigureRequest
  // Format: WM_CONFIGURE <wid> <x> <y> <w> <h>
  // Different from client CONFIGURE which is denied if WM holds redirect
  if(sscanf(cmd, "WM_CONFIGURE %u %d %d %d %d", &id, &x, &y, &w, &h) == 5) {
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR not-wm\n");
      return;
    }
    win = find_window(id);
    if(win == 0) {
      x6_send_line(cfd, "ERR not-found\n");
      return;
    }
    if(w < 1) w = 1;
    if(h < 1) h = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    x6_send_line(cfd, "OK configured\n");
    return;
  }

  if(sscanf(cmd, "QUEUE_EVENT %d %u", &x, &id) == 2) {
    // Test command: manually queue an event for testing infrastructure (Phase 2.1a)
    if(current_event_queue == 0) {
      x6_send_line(cfd, "ERR not-ready\n");
      return;
    }
    struct x6_event evt;
    evt.type = x;  // x is reused as event type here
    evt.wid = id;
    evt.x = evt.y = evt.w = evt.h = 0;
    if(x6_event_queue_enqueue(current_event_queue, &evt) < 0) {
      x6_send_line(cfd, "ERR queue-full\n");
      return;
    }
    x6_send_line(cfd, "OK queued\n");
    return;
  }

  if(strncmp(cmd, "LIST", 4) == 0) {
    char out[128];
    listed = 0;
    for(i = 0; i < X6_MAX_WINDOWS; i++) {
      if(!wins[i].in_use)
        continue;
      snprintf(out, sizeof(out), "WIN id=%u map=%d geom=%d,%d %dx%d\n",
               wins[i].id,
               wins[i].mapped,
               wins[i].x,
               wins[i].y,
               wins[i].w,
               wins[i].h);
      x6_send_line(cfd, out);
      listed++;
    }
    snprintf(out, sizeof(out), "OK list count=%d\n", listed);
    x6_send_line(cfd, out);
    return;
  }

  // Phase 2.1c: Focus and keyboard grab
  if(sscanf(cmd, "SET_FOCUS %u", &id) == 1) {
    struct x6_event evt;
    uint old_focus = focus_wid;
    
    // Allow both WM and clients to set focus
    focus_wid = id;
    
    // Queue FocusOut for old focus window FIRST
    if(current_event_queue != 0 && old_focus != 0 && old_focus != focus_wid) {
      evt.type = X6_EVENT_FOCUS_OUT;
      evt.wid = old_focus;
      x6_event_queue_enqueue(current_event_queue, &evt);
    }
    
    // Queue FocusIn for new focus window AFTER
    if(current_event_queue != 0 && focus_wid != 0) {
      evt.type = X6_EVENT_FOCUS_IN;
      evt.wid = focus_wid;
      x6_event_queue_enqueue(current_event_queue, &evt);
    }
    
    x6_send_line(cfd, "OK focused\n");
    return;
  }

  if(strncmp(cmd, "GRAB_KEYBOARD", 13) == 0) {
    // Only WM (client with redirect) can grab keyboard
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR permission-denied\n");
      return;
    }
    if(wm_has_kb_grab) {
      x6_send_line(cfd, "ERR already-grabbed\n");
      return;
    }
    wm_has_kb_grab = 1;
    keyboard_grab_owner = focus_wid;  // Start with current focus
    x6_send_line(cfd, "OK grab_active\n");
    return;
  }

  if(strncmp(cmd, "UNGRAB_KEYBOARD", 15) == 0) {
    // Only WM can release
    if(!wm_has_redirect) {
      x6_send_line(cfd, "ERR permission-denied\n");
      return;
    }
    if(!wm_has_kb_grab) {
      x6_send_line(cfd, "ERR not-grabbed\n");
      return;
    }
    wm_has_kb_grab = 0;
    keyboard_grab_owner = 0;
    x6_send_line(cfd, "OK ungrab_done\n");
    return;
  }

  x6_send_line(cfd, "ERR unknown\n");
}

static void
handle_client(int cfd)
{
  char line[192];
  char eventbuf[256];
  struct x6_event_queue q;
  struct x6_event evt;

  x6_event_queue_init(&q);
  current_event_queue = &q;

  x6_send_line(cfd, "X6/1 READY\n");
  
  while(keep_running) {
    // Drain any queued events and send them to client
    while(!x6_event_queue_empty(&q)) {
      if(x6_event_queue_dequeue(&q, &evt) == 0) {
        if(evt.type == X6_EVENT_MAP_REQUEST) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT MapRequest wid=%u\n", evt.wid);
        } else if(evt.type == X6_EVENT_CONFIGURE_REQUEST) {
          snprintf(eventbuf, sizeof(eventbuf), 
                   "EVENT ConfigureRequest wid=%u geom=%d,%d %dx%d\n",
                   evt.wid, evt.x, evt.y, evt.w, evt.h);
        } else if(evt.type == X6_EVENT_FOCUS_IN) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT FocusIn wid=%u\n", evt.wid);
        } else if(evt.type == X6_EVENT_FOCUS_OUT) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT FocusOut wid=%u\n", evt.wid);
        } else if(evt.type == X6_EVENT_DESTROY_NOTIFY) {
          snprintf(eventbuf, sizeof(eventbuf), "EVENT DestroyNotify wid=%u\n", evt.wid);
        } else {
          continue;
        }
        x6_send_line(cfd, eventbuf);
      }
    }

    if(x6_recv_line(cfd, line, sizeof(line)) < 0)
      break;
    if(line[0] == 0)
      continue;
    handle_one_command(cfd, line);
    if(strncmp(line, "QUIT", 4) == 0)
      break;
  }

  current_event_queue = 0;
}

int
main(int argc, char **argv)
{
  int i;
  int foreground;
  int port;
  int fd;
  struct sockaddr_in src;
  struct sigaction sa;

  foreground = 0;
  port = X6_DEFAULT_PORT;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-f") == 0) {
      foreground = 1;
      continue;
    }
    if(strcmp(argv[i], "-p") == 0) {
      if(i + 1 >= argc)
        usage();
      port = parse_port(argv[++i]);
      if(port < 0)
        usage();
      continue;
    }
    usage();
  }

  if(!foreground) {
    if(daemonize_self() < 0) {
      dprintf(2, "x6: daemonize failed\n");
      exit(1);
    }
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigaction(SIGTERM, &sa, 0);
  sigaction(SIGINT, &sa, 0);

  if(x6_claim_display() < 0) {
    dprintf(2, "x6: display claim failed via %s\n", X6_PROC_PATH);
    exit(1);
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    x6_release_display();
    dprintf(2, "x6: socket failed\n");
    exit(1);
  }

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_port = (ushort)port;
  src.sin_addr = INADDR_LOOPBACK;

  if(bind(fd, &src, sizeof(src)) < 0) {
    dprintf(2, "x6: bind failed on 127.0.0.1:%d\n", port);
    close(fd);
    x6_release_display();
    exit(1);
  }

  if(listen(fd, X6_BACKLOG) < 0) {
    dprintf(2, "x6: listen failed\n");
    close(fd);
    x6_release_display();
    exit(1);
  }

  dprintf(1, "x6: phase1 skeleton active proto=%d on 127.0.0.1:%d\n",
          X6_PROTO_VERSION, port);

  while(keep_running) {
    int cfd;

    cfd = accept(fd);
    if(cfd < 0)
      continue;

    handle_client(cfd);
    close(cfd);
  }

  close(fd);
  x6_release_display();
  dprintf(1, "x6: exiting\n");
  return 0;
}
