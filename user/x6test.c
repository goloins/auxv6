#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

#define X6_PORT 6006

// Test harness for x6 protocol (Phase 1.1)
// Validates: HELLO, CREATE, MAP, LIST, UNMAP, DESTROY, QUIT

typedef struct {
  int passed;
  int failed;
  char buf[512];
  int fd;
} test_ctx;

void test_send(test_ctx *ctx, const char *cmd) {
  if (send(ctx->fd, cmd, strlen(cmd)) < 0) {
    perror("send");
    ctx->failed++;
  }
}

int test_recv_line(test_ctx *ctx, char *line, int maxlen) {
  int pos = 0;
  while (pos < maxlen - 1) {
    char ch;
    int n = recv(ctx->fd, &ch, 1);
    if (n <= 0) return -1;
    if (ch == '\n') {
      line[pos] = '\0';
      return pos;
    }
    line[pos++] = ch;
  }
  return -1;
}

void test_hello(test_ctx *ctx) {
  printf("TEST: HELLO handshake\n");
  test_send(ctx, "HELLO x6/1\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strncmp(ctx->buf, "OK proto=", 9) == 0 && strstr(ctx->buf, "tcp-loopback")) {
    printf("  PASS: got OK proto with tcp-loopback\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK proto=..tcp-loopback..', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_create(test_ctx *ctx, unsigned *wid) {
  printf("TEST: CREATE window\n");
  *wid = 1;  // Use fixed wid for simplicity
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "CREATE %u 100 100 200 150\n", *wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK create") == 0) {
    printf("  PASS: got OK create for wid=%u\n", *wid);
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK create', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_map(test_ctx *ctx, unsigned wid) {
  printf("TEST: MAP window %u\n", wid);
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "MAP %u\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK map") == 0) {
    printf("  PASS: got OK map\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK map', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_event_infrastructure(test_ctx *ctx) {
  printf("TEST: Event infrastructure (Phase 2.1a, QUEUE_EVENT)\n");
  
  // Manually queue a test MapRequest event using test command
  test_send(ctx, "QUEUE_EVENT 1 42\n");  // type=1 (X6_EVENT_MAP_REQUEST), wid=42
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  
  if (strcmp(ctx->buf, "OK queued") == 0) {
    printf("  PASS: event queued successfully\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK queued', got '%s'\n", ctx->buf);
    ctx->failed++;
    return;
  }
  
  // Now receive the queued event
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strncmp(ctx->buf, "EVENT MapRequest wid=", 21) == 0) {
    unsigned evt_wid;
    if (sscanf(ctx->buf, "EVENT MapRequest wid=%u", &evt_wid) == 1 && evt_wid == 42) {
      printf("  PASS: received EVENT MapRequest wid=42\n");
      ctx->passed++;
    } else {
      printf("  FAIL: event wid mismatch, got '%s'\n", ctx->buf);
      ctx->failed++;
    }
  } else {
    printf("  FAIL: expected 'EVENT MapRequest', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_list(test_ctx *ctx) {
  printf("TEST: LIST windows\n");
  test_send(ctx, "LIST\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strncmp(ctx->buf, "WIN id=", 7) == 0) {
    printf("  PASS: got WIN line: %s\n", ctx->buf);
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'WIN id=...', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  // consume OK list count marker
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
}

void test_unmap(test_ctx *ctx, unsigned wid) {
  printf("TEST: UNMAP window %u\n", wid);
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "UNMAP %u\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK unmap") == 0) {
    printf("  PASS: got OK unmap\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK unmap', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_destroy(test_ctx *ctx, unsigned wid) {
  printf("TEST: DESTROY window %u\n", wid);
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "DESTROY %u\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK destroy") == 0) {
    printf("  PASS: got OK destroy\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK destroy', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_quit(test_ctx *ctx) {
  printf("TEST: QUIT\n");
  test_send(ctx, "QUIT\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "BYE") == 0) {
    printf("  PASS: got BYE\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'BYE', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_redirect_flow(test_ctx *ctx) {
  printf("TEST: SubstructureRedirect + WM flow (Phase 2.1b)\n");
  
  // Step 1: WM claims SubstructureRedirect on root
  test_send(ctx, "REQUEST_REDIRECT 1\n");  // root is wid=1
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK redirect_granted") == 0) {
    printf("  PASS: WM claimed SubstructureRedirect\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK redirect_granted', got '%s'\n", ctx->buf);
    ctx->failed++;
    return;
  }
  
  // Step 2: Create a window
  unsigned wid2 = 2;
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "CREATE %u 50 50 100 100\n", wid2);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK create") != 0) {
    printf("  FAIL: CREATE failed: '%s'\n", ctx->buf);
    ctx->failed++;
    return;
  }
  
  // Step 3: Client tries to MAP - should get PENDING (queues MapRequest for WM)
  snprintf(cmd, sizeof(cmd), "MAP %u\n", wid2);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "PENDING map") == 0) {
    printf("  PASS: MAP queued (PENDING), WM should receive MapRequest\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'PENDING map', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 4: Receive the queued MapRequest event
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strncmp(ctx->buf, "EVENT MapRequest wid=", 21) == 0) {
    unsigned evt_wid;
    if (sscanf(ctx->buf, "EVENT MapRequest wid=%u", &evt_wid) == 1 && evt_wid == wid2) {
      printf("  PASS: received EVENT MapRequest for window %u\n", evt_wid);
      ctx->passed++;
    } else {
      printf("  FAIL: MapRequest wid mismatch, got '%s'\n", ctx->buf);
      ctx->failed++;
    }
  } else {
    printf("  FAIL: expected 'EVENT MapRequest', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 5: WM applies the MAP via WM_CONFIGURE (resolves MapRequest)
  // Note: In Phase 2.1b, we use WM_CONFIGURE syntactically (Phase 2.1c will add explicit map handling)
  snprintf(cmd, sizeof(cmd), "WM_CONFIGURE %u 50 50 100 100\n", wid2);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK configured") == 0) {
    printf("  PASS: WM configured window, resolving MapRequest\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK configured', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_focus_and_grabs(test_ctx *ctx) {
  printf("TEST: Focus and keyboard grabs (Phase 2.1c)\n");
  
  // Step 1: SET_FOCUS on a window, get FocusIn event
  test_send(ctx, "SET_FOCUS 99\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK focused") == 0) {
    printf("  PASS: SET_FOCUS succeeded\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK focused', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Receive FocusIn event
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strncmp(ctx->buf, "EVENT FocusIn wid=", 17) == 0) {
    unsigned evt_wid;
    if (sscanf(ctx->buf, "EVENT FocusIn wid=%u", &evt_wid) == 1 && evt_wid == 99) {
      printf("  PASS: received EVENT FocusIn wid=99\n");
      ctx->passed++;
    } else {
      printf("  FAIL: FocusIn wid mismatch, got '%s'\n", ctx->buf);
      ctx->failed++;
    }
  } else {
    printf("  FAIL: expected 'EVENT FocusIn', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 2: GRAB_KEYBOARD (succeeds only if WM has redirect, which it does)
  test_send(ctx, "GRAB_KEYBOARD\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK grab_active") == 0) {
    printf("  PASS: GRAB_KEYBOARD succeeded\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK grab_active', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 3: UNGRAB_KEYBOARD
  test_send(ctx, "UNGRAB_KEYBOARD\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK ungrab_done") == 0) {
    printf("  PASS: UNGRAB_KEYBOARD succeeded\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK ungrab_done', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 4: Focus change generates FocusOut for old, FocusIn for new
  test_send(ctx, "SET_FOCUS 100\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));  // GET OK focused
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));  // GET FocusOut for 99
  if (strncmp(ctx->buf, "EVENT FocusOut wid=99", 20) == 0) {
    printf("  PASS: received EVENT FocusOut for previous focus\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'EVENT FocusOut wid=99', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));  // GET FocusIn for 100
  if (strncmp(ctx->buf, "EVENT FocusIn wid=100", 20) == 0) {
    printf("  PASS: received EVENT FocusIn for new focus\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'EVENT FocusIn wid=100', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

void test_properties(test_ctx *ctx, unsigned wid) {
  printf("TEST: Properties/Atoms (Phase 2.1d)\n");
  
  // Step 1: SET_PROPERTY on the created window
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "SET_PROPERTY %u WM_NAME TestWindow\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK property_set") == 0) {
    printf("  PASS: SET_PROPERTY succeeded\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK property_set', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 2: GET_PROPERTY on existing property
  snprintf(cmd, sizeof(cmd), "GET_PROPERTY %u WM_NAME\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "VALUE WM_NAME TestWindow") == 0) {
    printf("  PASS: GET_PROPERTY returned correct value\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'VALUE WM_NAME TestWindow', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 3: GET_PROPERTY on non-existent property
  snprintf(cmd, sizeof(cmd), "GET_PROPERTY %u WM_CLASS\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "ERR no-such-property") == 0) {
    printf("  PASS: GET_PROPERTY on missing property returns error\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'ERR no-such-property', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 4: SET_PROPERTY twice (update)
  snprintf(cmd, sizeof(cmd), "SET_PROPERTY %u WM_NAME UpdatedWindow\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "OK property_set") == 0) {
    printf("  PASS: SET_PROPERTY update succeeded\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'OK property_set' for update, got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 5: GET_PROPERTY to verify update
  snprintf(cmd, sizeof(cmd), "GET_PROPERTY %u WM_NAME\n", wid);
  test_send(ctx, cmd);
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "VALUE WM_NAME UpdatedWindow") == 0) {
    printf("  PASS: GET_PROPERTY verified updated value\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'VALUE WM_NAME UpdatedWindow', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
  
  // Step 6: GET_PROPERTY on non-existent window
  test_send(ctx, "GET_PROPERTY 9999 WM_NAME\n");
  test_recv_line(ctx, ctx->buf, sizeof(ctx->buf));
  if (strcmp(ctx->buf, "ERR no-such-window") == 0) {
    printf("  PASS: GET_PROPERTY on non-existent window returns error\n");
    ctx->passed++;
  } else {
    printf("  FAIL: expected 'ERR no-such-window', got '%s'\n", ctx->buf);
    ctx->failed++;
  }
}

int main(void) {
  test_ctx ctx = {0};
  struct sockaddr_in addr;
  unsigned wid = 0;

  printf("x6test: Phase 2.1d properties/atoms validation\n\n");

  // Connect to x6
  ctx.fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx.fd < 0) {
    perror("socket");
    return 1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = (ushort)X6_PORT;
  addr.sin_addr.s_addr = INADDR_LOOPBACK;

  if (connect(ctx.fd, &addr, sizeof(addr)) < 0) {
    perror("connect to x6 failed");
    return 1;
  }

  printf("Connected to x6 at 127.0.0.1:%d\n\n", X6_PORT);

  // Read initial greeting from x6
  test_recv_line(&ctx, ctx.buf, sizeof(ctx.buf));
  if (strcmp(ctx.buf, "X6/1 READY") == 0) {
    printf("Got initial greeting: X6/1 READY\n\n");
  } else {
    printf("Warning: Expected 'X6/1 READY', got '%s'\n\n", ctx.buf);
  }

  // Run test suite
  test_hello(&ctx);
  test_create(&ctx, &wid);
  test_map(&ctx, wid);
  test_list(&ctx);
  
  // Test event infrastructure (Phase 2.1a)
  test_event_infrastructure(&ctx);
  
  // Test SubstructureRedirect + WM flow (Phase 2.1b, uses new connection)
  test_redirect_flow(&ctx);
  
  // Test focus and keyboard grabs (Phase 2.1c)
  test_focus_and_grabs(&ctx);
  
  // Test properties/atoms (Phase 2.1d)
  test_properties(&ctx, wid);
  
  test_unmap(&ctx, wid);
  test_destroy(&ctx, wid);
  test_quit(&ctx);

  close(ctx.fd);

  printf("\n=== Results ===\n");
  printf("Passed: %d\n", ctx.passed);
  printf("Failed: %d\n", ctx.failed);

  if (ctx.failed == 0) {
    printf("\n✓ All tests passed! Phase 2.1d properties/atoms substrate validated.\n");
    return 0;
  } else {
    printf("\n✗ Some tests failed.\n");
    return 1;
  }
}
