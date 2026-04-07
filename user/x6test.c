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

int main(void) {
  test_ctx ctx = {0};
  struct sockaddr_in addr;
  unsigned wid = 0;

  printf("x6test: Phase 1.1 protocol validation harness\n\n");

  // Connect to x6
  ctx.fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx.fd < 0) {
    perror("socket");
    return 1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = (ushort)X6_PORT;
  addr.sin_addr = INADDR_LOOPBACK;

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
  test_unmap(&ctx, wid);
  test_destroy(&ctx, wid);
  test_quit(&ctx);

  close(ctx.fd);

  printf("\n=== Results ===\n");
  printf("Passed: %d\n", ctx.passed);
  printf("Failed: %d\n", ctx.failed);

  if (ctx.failed == 0) {
    printf("\n✓ All tests passed! Phase 1 exit gate unlocked.\n");
    return 0;
  } else {
    printf("\n✗ Some tests failed.\n");
    return 1;
  }
}
