#include "types.h"
#include "fcntl.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "auxv6/user.h"

#define COW_TEMPLATE_MAX 2048
#define MESSAGE_MAX      512

static const char fallback_template[] =
  "        {{THOUGHTS}}   ^__^\n"
  "         {{THOUGHTS}}  ({{EYES}})\\_______\n"
  "            (__)\\       )\\/\\\n"
  "             U  ||----w |\n"
  "                ||     ||\n";

static int
load_template(const char *name, char *buf, int buflen)
{
  int fd;
  int n;
  char path[128];

  if(name == 0 || name[0] == 0)
    name = "default";

  snprintf(path, sizeof(path), "/usr/share/games/cows/%s.cow", name);
  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, buflen - 1);
  close(fd);
  if(n <= 0)
    return -1;

  buf[n] = '\0';
  return 0;
}

static void
print_bubble(const char *msg)
{
  int width;
  int i;

  width = strlen(msg);
  dprintf(1, " ");
  for(i = 0; i < width + 2; i++)
    dprintf(1, "_");
  dprintf(1, "\n");

  dprintf(1, "< %s >\n", msg);

  dprintf(1, " ");
  for(i = 0; i < width + 2; i++)
    dprintf(1, "-");
  dprintf(1, "\n");
}

static void
render_template(const char *tpl, const char *eyes, const char *thoughts)
{
  const char *p;

  p = tpl;
  while(*p) {
    if(strncmp(p, "{{EYES}}", 8) == 0) {
      dprintf(1, "%s", eyes);
      p += 8;
      continue;
    }
    if(strncmp(p, "{{THOUGHTS}}", 12) == 0) {
      dprintf(1, "%s", thoughts);
      p += 12;
      continue;
    }
    write(1, p, 1);
    p++;
  }
}

static void
build_message(int argc, char *argv[], int start, char *msg, int msgsz)
{
  int i;

  msg[0] = '\0';
  for(i = start; i < argc; i++) {
    if(i != start)
      strncat(msg, " ", msgsz - strlen(msg) - 1);
    strncat(msg, argv[i], msgsz - strlen(msg) - 1);
  }

  if(msg[0] == '\0')
    snprintf(msg, msgsz, "Moo");
}

int
main(int argc, char *argv[])
{
  int i;
  const char *cow;
  char eyes[3];
  char msg[MESSAGE_MAX];
  char tpl[COW_TEMPLATE_MAX];

  cow = "default";
  snprintf(eyes, sizeof(eyes), "oo");

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-f") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: cowsay [-f cowfile] [-e eyes] [message ...]\n");
        exit(1);
      }
      cow = argv[++i];
      continue;
    }
    if(strcmp(argv[i], "-e") == 0) {
      if(i + 1 >= argc) {
        dprintf(2, "usage: cowsay [-f cowfile] [-e eyes] [message ...]\n");
        exit(1);
      }
      snprintf(eyes, sizeof(eyes), "%c%c",
               argv[i + 1][0] ? argv[i + 1][0] : 'o',
               argv[i + 1][1] ? argv[i + 1][1] : 'o');
      i++;
      continue;
    }
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      dprintf(1, "usage: cowsay [-f cowfile] [-e eyes] [message ...]\n");
      exit(0);
    }
    break;
  }

  build_message(argc, argv, i, msg, sizeof(msg));
  print_bubble(msg);

  if(load_template(cow, tpl, sizeof(tpl)) < 0)
    snprintf(tpl, sizeof(tpl), "%s", fallback_template);

  render_template(tpl, eyes, "\\");
  exit(0);
}
