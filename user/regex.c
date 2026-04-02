#include "types.h"
#include "auxv6/user.h"
#include "regex.h"

enum rnode_type {
  RN_EMPTY = 0,
  RN_LITERAL,
  RN_DOT,
  RN_CLASS,
  RN_NCLASS,
  RN_BOL,
  RN_EOL,
  RN_WB_START,
  RN_WB_END,
  RN_CONCAT,
  RN_ALT,
  RN_STAR,
  RN_PLUS,
  RN_QMARK,
};

struct rnode {
  int type;
  int ch;
  uchar cls[32];
  struct rnode *left;
  struct rnode *right;
};

struct parser {
  const char *p;
  int extended;
  int error;
};

static int
is_word_char(int c)
{
  if((c >= 'a' && c <= 'z') ||
     (c >= 'A' && c <= 'Z') ||
     (c >= '0' && c <= '9') ||
     c == '_')
    return 1;
  return 0;
}

static int
char_eq(int a, int b, int icase)
{
  if(!icase)
    return a == b;

  if(a >= 'A' && a <= 'Z')
    a = a - 'A' + 'a';
  if(b >= 'A' && b <= 'Z')
    b = b - 'A' + 'a';
  return a == b;
}

static struct rnode *
new_node(int type)
{
  struct rnode *n;

  n = (struct rnode*)malloc(sizeof(*n));
  if(n == 0)
    return 0;
  memset(n, 0, sizeof(*n));
  n->type = type;
  return n;
}

static void
free_node(struct rnode *n)
{
  if(n == 0)
    return;
  free_node(n->left);
  free_node(n->right);
  free(n);
}

static int
is_bre_alt(const char *p)
{
  return p[0] == '\\' && p[1] == '|';
}

static int
is_bre_group_open(const char *p)
{
  return p[0] == '\\' && p[1] == '(';
}

static int
is_bre_group_close(const char *p)
{
  return p[0] == '\\' && p[1] == ')';
}

static int
is_bre_plus(const char *p)
{
  return p[0] == '\\' && p[1] == '+';
}

static int
is_bre_qmark(const char *p)
{
  return p[0] == '\\' && p[1] == '?';
}

static int
at_end_or_delim(struct parser *ps)
{
  if(ps->p[0] == 0)
    return 1;
  if(ps->extended){
    if(ps->p[0] == ')' || ps->p[0] == '|')
      return 1;
  } else {
    if(is_bre_group_close(ps->p) || is_bre_alt(ps->p))
      return 1;
  }
  return 0;
}

static struct rnode *parse_expr(struct parser *ps);

static int
class_add(uchar cls[32], int c)
{
  if(c < 0 || c > 255)
    return -1;
  cls[c >> 3] |= (1U << (c & 7));
  return 0;
}

static int
parse_class(struct parser *ps, struct rnode *n)
{
  int negate;
  int first;
  int last;

  memset(n->cls, 0, sizeof(n->cls));
  ps->p++; /* skip '[' */

  negate = 0;
  if(*ps->p == '^'){
    negate = 1;
    ps->p++;
  }

  first = 1;
  last = -1;
  while(*ps->p && *ps->p != ']'){
    int c;

    if(*ps->p == '\\' && ps->p[1]){
      ps->p++;
      c = (uchar)*ps->p++;
    } else {
      c = (uchar)*ps->p++;
    }

    if(!first && c == '-' && *ps->p && *ps->p != ']'){
      int endc;
      if(*ps->p == '\\' && ps->p[1]){
        ps->p++;
        endc = (uchar)*ps->p++;
      } else {
        endc = (uchar)*ps->p++;
      }
      if(last <= endc){
        int k;
        for(k = last; k <= endc; k++)
          class_add(n->cls, k);
      } else {
        int k;
        for(k = endc; k <= last; k++)
          class_add(n->cls, k);
      }
      last = endc;
      first = 0;
      continue;
    }

    class_add(n->cls, c);
    last = c;
    first = 0;
  }

  if(*ps->p != ']'){
    ps->error = REG_EBRACK;
    return -1;
  }
  ps->p++; /* skip ']' */

  n->type = negate ? RN_NCLASS : RN_CLASS;
  return 0;
}

static struct rnode *
parse_atom(struct parser *ps)
{
  struct rnode *n;
  int c;

  if(ps->error)
    return 0;

  if(ps->p[0] == 0)
    return new_node(RN_EMPTY);

  if(ps->extended){
    if(ps->p[0] == ')')
      return new_node(RN_EMPTY);
  } else {
    if(is_bre_group_close(ps->p))
      return new_node(RN_EMPTY);
  }

  if(ps->extended && ps->p[0] == '('){
    ps->p++;
    n = parse_expr(ps);
    if(ps->error)
      return n;
    if(ps->p[0] != ')'){
      ps->error = REG_EPAREN;
      free_node(n);
      return 0;
    }
    ps->p++;
    return n;
  }

  if(!ps->extended && is_bre_group_open(ps->p)){
    ps->p += 2;
    n = parse_expr(ps);
    if(ps->error)
      return n;
    if(!is_bre_group_close(ps->p)){
      ps->error = REG_EPAREN;
      free_node(n);
      return 0;
    }
    ps->p += 2;
    return n;
  }

  if(ps->p[0] == '^'){
    ps->p++;
    return new_node(RN_BOL);
  }
  if(ps->p[0] == '$'){
    ps->p++;
    return new_node(RN_EOL);
  }
  if(ps->p[0] == '.'){
    ps->p++;
    return new_node(RN_DOT);
  }
  if(ps->p[0] == '['){
    n = new_node(RN_CLASS);
    if(n == 0){
      ps->error = REG_ESPACE;
      return 0;
    }
    if(parse_class(ps, n) < 0){
      free_node(n);
      return 0;
    }
    return n;
  }

  if(ps->p[0] == '\\'){
    if(ps->p[1] == 0){
      ps->error = REG_EESCAPE;
      return 0;
    }

    if(ps->p[1] == '<'){
      ps->p += 2;
      return new_node(RN_WB_START);
    }
    if(ps->p[1] == '>'){
      ps->p += 2;
      return new_node(RN_WB_END);
    }

    ps->p++;
    c = (uchar)*ps->p++;
    n = new_node(RN_LITERAL);
    if(n == 0){
      ps->error = REG_ESPACE;
      return 0;
    }
    n->ch = c;
    return n;
  }

  c = (uchar)*ps->p++;
  n = new_node(RN_LITERAL);
  if(n == 0){
    ps->error = REG_ESPACE;
    return 0;
  }
  n->ch = c;
  return n;
}

static struct rnode *
parse_factor(struct parser *ps)
{
  struct rnode *a;
  struct rnode *q;

  a = parse_atom(ps);
  if(ps->error || a == 0)
    return a;

  while(1){
    if(ps->extended){
      if(ps->p[0] == '*'){
        ps->p++;
        q = new_node(RN_STAR);
      } else if(ps->p[0] == '+'){
        ps->p++;
        q = new_node(RN_PLUS);
      } else if(ps->p[0] == '?'){
        ps->p++;
        q = new_node(RN_QMARK);
      } else {
        break;
      }
    } else {
      if(ps->p[0] == '*'){
        ps->p++;
        q = new_node(RN_STAR);
      } else if(is_bre_plus(ps->p)){
        ps->p += 2;
        q = new_node(RN_PLUS);
      } else if(is_bre_qmark(ps->p)){
        ps->p += 2;
        q = new_node(RN_QMARK);
      } else {
        break;
      }
    }

    if(q == 0){
      ps->error = REG_ESPACE;
      free_node(a);
      return 0;
    }
    q->left = a;
    a = q;
  }

  return a;
}

static struct rnode *
parse_term(struct parser *ps)
{
  struct rnode *left;

  left = 0;
  while(!at_end_or_delim(ps)){
    struct rnode *f;

    f = parse_factor(ps);
    if(ps->error){
      free_node(left);
      return 0;
    }

    if(left == 0)
      left = f;
    else {
      struct rnode *c;
      c = new_node(RN_CONCAT);
      if(c == 0){
        ps->error = REG_ESPACE;
        free_node(left);
        free_node(f);
        return 0;
      }
      c->left = left;
      c->right = f;
      left = c;
    }
  }

  if(left == 0)
    left = new_node(RN_EMPTY);
  return left;
}

static struct rnode *
parse_expr(struct parser *ps)
{
  struct rnode *left;

  left = parse_term(ps);
  if(ps->error)
    return 0;

  while(1){
    int has_alt;

    if(ps->extended)
      has_alt = (ps->p[0] == '|');
    else
      has_alt = is_bre_alt(ps->p);

    if(!has_alt)
      break;

    if(ps->extended)
      ps->p++;
    else
      ps->p += 2;

    {
      struct rnode *right;
      struct rnode *a;

      right = parse_term(ps);
      if(ps->error){
        free_node(left);
        return 0;
      }

      a = new_node(RN_ALT);
      if(a == 0){
        ps->error = REG_ESPACE;
        free_node(left);
        free_node(right);
        return 0;
      }
      a->left = left;
      a->right = right;
      left = a;
    }
  }

  return left;
}

static int
out_add(int *outs, int n, int cap, int pos)
{
  int i;
  if(pos < 0)
    return n;
  for(i = 0; i < n; i++)
    if(outs[i] == pos)
      return n;
  if(n < cap)
    outs[n++] = pos;
  return n;
}

static int
char_in_class(const uchar cls[32], int c)
{
  return (cls[(c & 0xff) >> 3] & (1U << (c & 7))) != 0;
}

static int
match_all(const struct rnode *n, const char *s, int slen, int pos, int icase,
          int eflags, int *outs, int cap);

static int
match_star(const struct rnode *child, const char *s, int slen, int start,
           int icase, int eflags, int *outs, int cap)
{
  int *queue;
  uchar *seen;
  int qh, qt;
  int nout;

  queue = (int*)malloc((uint)(slen + 1) * sizeof(int));
  seen = (uchar*)malloc((uint)(slen + 1));
  if(queue == 0 || seen == 0){
    if(queue) free(queue);
    if(seen) free(seen);
    return 0;
  }

  memset(seen, 0, (uint)(slen + 1));
  qh = 0;
  qt = 0;
  nout = 0;

  seen[start] = 1;
  queue[qt++] = start;
  nout = out_add(outs, nout, cap, start);

  while(qh < qt){
    int p = queue[qh++];
    int mids[256];
    int nm;
    int i;

    nm = match_all(child, s, slen, p, icase, eflags, mids, 256);
    for(i = 0; i < nm; i++){
      int np = mids[i];
      if(np < 0 || np > slen)
        continue;
      if(np == p)
        continue;
      if(!seen[np]){
        seen[np] = 1;
        queue[qt++] = np;
        nout = out_add(outs, nout, cap, np);
      }
    }
  }

  free(queue);
  free(seen);
  return nout;
}

static int
match_all(const struct rnode *n, const char *s, int slen, int pos, int icase,
          int eflags, int *outs, int cap)
{
  int nout;

  if(n == 0)
    return 0;

  nout = 0;

  switch(n->type){
  case RN_EMPTY:
    nout = out_add(outs, nout, cap, pos);
    break;

  case RN_LITERAL:
    if(pos < slen && char_eq((uchar)s[pos], n->ch, icase))
      nout = out_add(outs, nout, cap, pos + 1);
    break;

  case RN_DOT:
    if(pos < slen && s[pos] != '\n')
      nout = out_add(outs, nout, cap, pos + 1);
    break;

  case RN_CLASS:
  case RN_NCLASS:
    if(pos < slen){
      int in = char_in_class(n->cls, (uchar)s[pos]);
      if(n->type == RN_NCLASS)
        in = !in;
      if(in)
        nout = out_add(outs, nout, cap, pos + 1);
    }
    break;

  case RN_BOL:
    if(pos == 0 && !(eflags & REG_NOTBOL))
      nout = out_add(outs, nout, cap, pos);
    break;

  case RN_EOL:
    if(pos == slen && !(eflags & REG_NOTEOL))
      nout = out_add(outs, nout, cap, pos);
    break;

  case RN_WB_START:
    {
      int prev = (pos > 0) ? (uchar)s[pos - 1] : 0;
      int cur = (pos < slen) ? (uchar)s[pos] : 0;
      if((pos == 0 || !is_word_char(prev)) && (pos < slen && is_word_char(cur)))
        nout = out_add(outs, nout, cap, pos);
    }
    break;

  case RN_WB_END:
    {
      int prev = (pos > 0) ? (uchar)s[pos - 1] : 0;
      int cur = (pos < slen) ? (uchar)s[pos] : 0;
      if((pos > 0 && is_word_char(prev)) && (pos == slen || !is_word_char(cur)))
        nout = out_add(outs, nout, cap, pos);
    }
    break;

  case RN_ALT:
    {
      int l[256], r[256];
      int nl, nr, i;

      nl = match_all(n->left, s, slen, pos, icase, eflags, l, 256);
      nr = match_all(n->right, s, slen, pos, icase, eflags, r, 256);

      for(i = 0; i < nl; i++)
        nout = out_add(outs, nout, cap, l[i]);
      for(i = 0; i < nr; i++)
        nout = out_add(outs, nout, cap, r[i]);
    }
    break;

  case RN_CONCAT:
    {
      int mids[256];
      int nm;
      int i;

      nm = match_all(n->left, s, slen, pos, icase, eflags, mids, 256);
      for(i = 0; i < nm; i++){
        int tail[256];
        int nt;
        int j;

        nt = match_all(n->right, s, slen, mids[i], icase, eflags, tail, 256);
        for(j = 0; j < nt; j++)
          nout = out_add(outs, nout, cap, tail[j]);
      }
    }
    break;

  case RN_STAR:
    nout = match_star(n->left, s, slen, pos, icase, eflags, outs, cap);
    break;

  case RN_PLUS:
    {
      int mids[256];
      int nm;
      int i;

      nm = match_all(n->left, s, slen, pos, icase, eflags, mids, 256);
      for(i = 0; i < nm; i++){
        int tails[256];
        int nt;
        int j;

        nt = match_star(n->left, s, slen, mids[i], icase, eflags, tails, 256);
        for(j = 0; j < nt; j++)
          nout = out_add(outs, nout, cap, tails[j]);
      }
    }
    break;

  case RN_QMARK:
    {
      int mids[256];
      int nm;
      int i;

      nout = out_add(outs, nout, cap, pos);
      nm = match_all(n->left, s, slen, pos, icase, eflags, mids, 256);
      for(i = 0; i < nm; i++)
        nout = out_add(outs, nout, cap, mids[i]);
    }
    break;

  default:
    break;
  }

  return nout;
}

int
regcomp(regex_t *preg, const char *regex, int cflags)
{
  struct parser ps;
  struct rnode *root;

  if(preg == 0 || regex == 0)
    return REG_BADPAT;

  preg->re_ptr = 0;
  preg->pattern = 0;
  preg->cflags = cflags;

  ps.p = regex;
  ps.extended = (cflags & REG_EXTENDED) != 0;
  ps.error = 0;

  root = parse_expr(&ps);
  if(ps.error){
    free_node(root);
    return ps.error;
  }

  if(ps.p[0] != 0){
    free_node(root);
    return REG_BADPAT;
  }

  preg->re_ptr = root;
  preg->pattern = (char*)malloc(strlen(regex) + 1);
  if(preg->pattern == 0){
    regfree(preg);
    return REG_ESPACE;
  }
  strcpy(preg->pattern, regex);

  return REG_OK;
}

int
regexec(const regex_t *preg, const char *string, size_t nmatch,
        regmatch_t pmatch[], int eflags)
{
  const struct rnode *root;
  int slen;
  int start;

  if(preg == 0 || preg->re_ptr == 0 || string == 0)
    return REG_BADPAT;

  root = (const struct rnode*)preg->re_ptr;
  slen = (int)strlen(string);

  for(start = 0; start <= slen; start++){
    int ends[256];
    int nend;

    nend = match_all(root, string, slen, start,
                     (preg->cflags & REG_ICASE) != 0,
                     eflags, ends, 256);

    if(nend > 0){
      if(nmatch > 0 && pmatch){
        int i;
        int best = ends[0];
        for(i = 1; i < nend; i++)
          if(ends[i] > best)
            best = ends[i];
        pmatch[0].rm_so = start;
        pmatch[0].rm_eo = best;
      }
      return REG_OK;
    }
  }

  return REG_NOMATCH;
}

size_t
regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
  const char *msg;
  size_t len;

  (void)preg;

  switch(errcode){
  case REG_OK: msg = "no error"; break;
  case REG_NOMATCH: msg = "no match"; break;
  case REG_BADPAT: msg = "invalid regular expression"; break;
  case REG_EESCAPE: msg = "trailing backslash"; break;
  case REG_EPAREN: msg = "unmatched parenthesis"; break;
  case REG_EBRACK: msg = "unmatched bracket"; break;
  case REG_ESPACE: msg = "out of memory"; break;
  default: msg = "regex error"; break;
  }

  len = strlen(msg) + 1;
  if(errbuf && errbuf_size > 0){
    size_t copy = (len > errbuf_size) ? errbuf_size - 1 : len - 1;
    if(copy > 0)
      memmove(errbuf, msg, (int)copy);
    errbuf[copy] = 0;
  }
  return len;
}

void
regfree(regex_t *preg)
{
  if(preg == 0)
    return;

  if(preg->re_ptr)
    free_node((struct rnode*)preg->re_ptr);
  preg->re_ptr = 0;

  if(preg->pattern)
    free(preg->pattern);
  preg->pattern = 0;
}
