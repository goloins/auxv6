#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define READ_BUF_SZ 512
#define LINE_BUF_SZ 2048

static int out_fd = 1;
static int in_code_block;
static int in_ul;
static int in_ol;

static int
write_all(int fd, const char *buf, int n)
{
  int written;
  int m;

  written = 0;
  while(written < n){
    m = write(fd, buf + written, n - written);
    if(m <= 0)
      return -1;
    written += m;
  }
  return 0;
}

static void
emit(const char *s)
{
  int n;

  n = strlen(s);
  if(n > 0)
    write_all(out_fd, s, n);
}

static void
emit_n(const char *s, int n)
{
  if(n > 0)
    write_all(out_fd, s, n);
}

static void
emit_escaped(const char *s)
{
  int i;

  for(i = 0; s[i]; i++){
    switch(s[i]){
    case '&':
      emit("&amp;");
      break;
    case '<':
      emit("&lt;");
      break;
    case '>':
      emit("&gt;");
      break;
    case '"':
      emit("&quot;");
      break;
    default:
      emit_n(&s[i], 1);
      break;
    }
  }
}

static int
is_blank_line(const char *line)
{
  int i;

  for(i = 0; line[i]; i++)
    if(line[i] != ' ' && line[i] != '\t')
      return 0;
  return 1;
}

static void
close_lists(void)
{
  if(in_ul){
    emit("</ul>\n");
    in_ul = 0;
  }
  if(in_ol){
    emit("</ol>\n");
    in_ol = 0;
  }
}

static const char*
skip_spaces(const char *s)
{
  while(*s == ' ' || *s == '\t')
    s++;
  return s;
}

static int
parse_heading(const char *line, int *level_out, const char **text_out)
{
  int i;
  int level;

  i = 0;
  while(line[i] == '#')
    i++;
  level = i;

  if(level < 1 || level > 6)
    return 0;
  if(line[i] != ' ' && line[i] != '\t')
    return 0;

  *level_out = level;
  *text_out = skip_spaces(line + i);
  return 1;
}

static int
parse_ul_item(const char *line, const char **text_out)
{
  const char *p;

  p = skip_spaces(line);
  if((p[0] == '-' || p[0] == '*') && (p[1] == ' ' || p[1] == '\t')){
    *text_out = skip_spaces(p + 1);
    return 1;
  }
  return 0;
}

static int
parse_ol_item(const char *line, const char **text_out)
{
  const char *p;
  int i;

  p = skip_spaces(line);
  i = 0;
  while(p[i] >= '0' && p[i] <= '9')
    i++;

  if(i == 0)
    return 0;
  if(p[i] != '.' || (p[i + 1] != ' ' && p[i + 1] != '\t'))
    return 0;

  *text_out = skip_spaces(p + i + 1);
  return 1;
}

static void
render_line(const char *line)
{
  int level;
  const char *text;

  if(strcmp(line, "```") == 0){
    close_lists();
    if(!in_code_block){
      emit("<pre><code>\n");
      in_code_block = 1;
    } else {
      emit("</code></pre>\n");
      in_code_block = 0;
    }
    return;
  }

  if(in_code_block){
    emit_escaped(line);
    emit("\n");
    return;
  }

  if(is_blank_line(line)){
    close_lists();
    return;
  }

  if(parse_heading(line, &level, &text)){
    close_lists();
    if(level == 1)
      emit("<h1>");
    else if(level == 2)
      emit("<h2>");
    else if(level == 3)
      emit("<h3>");
    else if(level == 4)
      emit("<h4>");
    else if(level == 5)
      emit("<h5>");
    else
      emit("<h6>");

    emit_escaped(text);

    if(level == 1)
      emit("</h1>\n");
    else if(level == 2)
      emit("</h2>\n");
    else if(level == 3)
      emit("</h3>\n");
    else if(level == 4)
      emit("</h4>\n");
    else if(level == 5)
      emit("</h5>\n");
    else
      emit("</h6>\n");
    return;
  }

  if(parse_ul_item(line, &text)){
    if(in_ol){
      emit("</ol>\n");
      in_ol = 0;
    }
    if(!in_ul){
      emit("<ul>\n");
      in_ul = 1;
    }
    emit("<li>");
    emit_escaped(text);
    emit("</li>\n");
    return;
  }

  if(parse_ol_item(line, &text)){
    if(in_ul){
      emit("</ul>\n");
      in_ul = 0;
    }
    if(!in_ol){
      emit("<ol>\n");
      in_ol = 1;
    }
    emit("<li>");
    emit_escaped(text);
    emit("</li>\n");
    return;
  }

  close_lists();
  emit("<p>");
  emit_escaped(line);
  emit("</p>\n");
}

static const char*
basename_of(const char *path)
{
  int i;
  int last;

  last = 0;
  for(i = 0; path[i]; i++)
    if(path[i] == '/')
      last = i + 1;
  return path + last;
}

static void
emit_html_header(const char *input_path)
{
  const char *title;

  title = basename_of(input_path);
  emit("<!doctype html>\n");
  emit("<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
  emit_escaped(title);
  emit("</title>\n</head>\n<body>\n");
}

static void
emit_html_footer(void)
{
  if(in_code_block){
    emit("</code></pre>\n");
    in_code_block = 0;
  }
  close_lists();
  emit("</body>\n</html>\n");
}

static int
convert_markdown(int in_fd)
{
  char rbuf[READ_BUF_SZ];
  char line[LINE_BUF_SZ];
  int n;
  int i;
  int llen;
  int overflow;

  llen = 0;
  overflow = 0;

  while((n = read(in_fd, rbuf, sizeof(rbuf))) > 0){
    for(i = 0; i < n; i++){
      char c;

      c = rbuf[i];
      if(c == '\r')
        continue;

      if(c == '\n'){
        line[llen] = 0;
        render_line(line);
        llen = 0;
        overflow = 0;
        continue;
      }

      if(overflow)
        continue;

      if(llen >= LINE_BUF_SZ - 1){
        overflow = 1;
        continue;
      }

      line[llen++] = c;
    }
  }

  if(n < 0)
    return -1;

  if(llen > 0){
    line[llen] = 0;
    render_line(line);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  int in_fd;
  int out;

  if(argc < 2 || argc > 3){
    dprintf(2, "usage: 6doc input.md [output.html]\n");
    exit(1);
  }

  in_fd = open(argv[1], O_RDONLY);
  if(in_fd < 0){
    dprintf(2, "6doc: cannot open input %s\n", argv[1]);
    exit(1);
  }

  out = 1;
  if(argc == 3){
    out = open(argv[2], O_CREATE | O_WRONLY | O_TRUNC);
    if(out < 0){
      dprintf(2, "6doc: cannot open output %s\n", argv[2]);
      close(in_fd);
      exit(1);
    }
  }

  out_fd = out;
  in_code_block = 0;
  in_ul = 0;
  in_ol = 0;

  emit_html_header(argv[1]);
  if(convert_markdown(in_fd) < 0){
    dprintf(2, "6doc: read error on %s\n", argv[1]);
    close(in_fd);
    if(argc == 3)
      close(out);
    exit(1);
  }
  emit_html_footer();

  close(in_fd);
  if(argc == 3)
    close(out);
  exit(0);
}
