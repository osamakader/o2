// web [url]: a small web browser on the desktop, for plain HTTP (no
// TLS: http:// only). It shows HTML's text: paragraphs, headings,
// lists, preformatted text, simple tables as lines, links, and GET
// forms' text fields and buttons; no CSS, scripts or images. The page
// is laid out while it is parsed, into items (words, fields, rules)
// placed on the page; drawing shows those in view.
//
// Click a link or a field; type into a field and Enter submits its
// form. The wheel, arrows, Page Up/Down, space, Home/End scroll;
// Backspace goes back; ^L goes to the address bar, Enter there loads.
// The start page is the URL in /homepage, or FrogFind (frogfind.com),
// a search engine that serves any page as simple HTML.

#include "types.h"
#include "user.h"
#include "win.h"
#include "libnet.h"
#include "font.h"
#include "fcntl.h"

#define HOME "http://frogfind.com/"

#define MAXPAGE  (1024 * 1024)
#define MAXITEM  20000
#define MAXLINK  2000
#define MAXFIELD 64
#define MAXFORM  16
#define NHIST    32

#define TOOL_H   28
#define STATUS_H 20
#define PAD      8
#define BACK_W   28
#define GO_W     36
#define C(x)     ((x) & 0x1f)

#define C_BG     0xffffff
#define C_TEXT   0x1a1a1a
#define C_LINK   0x1a4fd0
#define C_TOOL   0xe4e6ea
#define C_FIELD  0xffffff
#define C_EDGE   0x9aa0a8
#define C_FOCUS  0x2f6fb8
#define C_DIM    0x60666c
#define C_RULE   0xc8ccd0
#define C_BUTTON 0xd4d8de
#define C_PRE    0xf2f3f5

// items on the page
enum { K_TEXT, K_FIELD, K_BUTTON, K_RULE };
#define S_BOLD  1
#define S_BIG   2   // twice the size
#define S_PRE   4

struct item {
  int x, y, w, h;   // on the page, in pixels
  int off, len;     // its text, in pool
  uchar kind, style;
  short link;       // or -1
  short field;      // or -1
};

struct field {
  int form;
  int type;          // 0 text, 1 submit, 2 hidden
  char name[32];
  char value[128];
  int len;
};

static struct item *items;
static int nitem;
static char *pool;          // the items' text, then links' targets
static int npool, poolmax;
static int links[MAXLINK], nlink;   // offsets in pool
static struct field fields[MAXFIELD];
static int nfield;
static char forms[MAXFORM][256];    // their actions
static int nform, curform = -1;
static char title[64];
static int pageh;           // the page's height

static char *page;          // what came
static int pagelen;
static char url[512];       // where it came from
static char hist[NHIST][512];
static int nhist;

static int scroll;          // the page's y at the top of the view
static int viewh, cw;       // the view's height, the content's width
static char addr[512];      // the address bar
static int alen, afocus;
static int focus = -1;      // the field being typed into
static char status[128];

// ---- layout state, while parsing

static int lx, ly, lineh;   // where the next word goes; the line's height
static int indent;
static int bold, big, pre, inlink = -1, space, ingap;
static int listn[8], listdepth;   // ol numbers (0: ul)

static int
pooladd(const char *s, int n)
{
  if(npool + n + 1 > poolmax)
    return -1;
  memmove(pool + npool, s, n);
  pool[npool + n] = 0;
  int off = npool;
  npool += n + 1;
  return off;
}

static int
charw(void)
{
  return big ? 2 * FONT_W : FONT_W;
}

static int
lineheight(void)
{
  return (big ? 2 * FONT_H : FONT_H) + 2;
}

static void
newline(void)
{
  if(lx > indent || lineh){
    ly += lineh ? lineh : lineheight();
  }
  lx = indent;
  lineh = 0;
  space = 0;
}

// A block's start or end: a new line, with a gap unless one is there.
static void
block(int gap)
{
  newline();
  if(gap && !ingap){
    ly += FONT_H / 2;
    ingap = 1;
  }
}

static struct item *
additem(int kind, int w, int h)
{
  if(nitem == MAXITEM)
    return 0;
  struct item *it = &items[nitem++];
  memset(it, 0, sizeof(*it));
  it->kind = kind;
  it->w = w;
  it->h = h;
  it->link = -1;
  it->field = -1;
  return it;
}

// Place something w wide, h high, after a space if one came before.
static void
place(struct item *it)
{
  int sw = space && lx > indent ? charw() : 0;
  if(lx + sw + it->w > cw && lx > indent){
    newline();
    sw = 0;
  }
  it->x = lx + sw;
  it->y = ly;
  lx = it->x + it->w;
  if(it->h > lineh)
    lineh = it->h;
  space = 0;
  ingap = 0;
}

static char word[256];
static int wlen;

static void
flushword(void)
{
  if(wlen == 0)
    return;
  int off = pooladd(word, wlen);
  struct item *it = additem(K_TEXT, wlen * charw(), lineheight());
  if(off < 0 || it == 0){
    wlen = 0;
    return;
  }
  it->off = off;
  it->len = wlen;
  it->style = (bold ? S_BOLD : 0) | (big ? S_BIG : 0) | (pre ? S_PRE : 0);
  it->link = inlink;
  place(it);
  wlen = 0;
}

static void
addchar(int c)
{
  if(wlen == sizeof(word) || (pre && wlen * charw() >= cw))
    flushword();
  word[wlen++] = c;
}

// text: whitespace between words, or kept in <pre>
static void
text(int c)
{
  if(pre){
    if(c == '\n'){
      flushword();
      newline();
    } else if(c == '\t'){
      do
        addchar(' ');
      while(wlen % 8);
    } else if(c != '\r')
      addchar(c);
    return;
  }
  if(c == ' ' || c == '\n' || c == '\t' || c == '\r'){
    flushword();
    space = 1;
  } else
    addchar(c);
}

static void
textstr(const char *s)
{
  for(; *s; s++)
    text(*s);
}

// ---- characters: entities and UTF-8, as ASCII

static void
unicode(int cp)
{
  if(cp >= 32 && cp < 127)
    text(cp);
  else if(cp == '\n' || cp == '\t')
    text(cp);
  else if(cp == 0xa0)
    text(' ');
  else if(cp == 0x2018 || cp == 0x2019 || cp == 0xb4)
    text('\'');
  else if(cp == 0x201c || cp == 0x201d || cp == 0xab || cp == 0xbb)
    text('"');
  else if(cp == 0x2013 || cp == 0x2014 || cp == 0x2212)
    text('-');
  else if(cp == 0x2026)
    textstr("...");
  else if(cp == 0x2022 || cp == 0xb7)
    text('*');
  else if(cp == 0xa9)
    textstr("(c)");
  else if(cp == 0xae)
    textstr("(R)");
  else if(cp == 0x2122)
    textstr("TM");
  else if(cp == 0xd7)
    text('x');
  else if(cp >= 0xc0 && cp <= 0xff){
    // Latin-1 letters without their accents
    static const char *l = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    text(l[cp - 0xc0]);
  } else if(cp >= 0x80)
    text('?');
}

static struct { char *name; int cp; } entities[] = {
  { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
  { "nbsp", 0xa0 }, { "copy", 0xa9 }, { "reg", 0xae }, { "trade", 0x2122 },
  { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "laquo", 0xab },
  { "raquo", 0xbb }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201c },
  { "rdquo", 0x201d }, { "bull", 0x2022 }, { "middot", 0xb7 }, { "times", 0xd7 },
};

// The entity at s (after '&'): its code point, and its length in *n.
static int
entity(const char *s, int max, int *n)
{
  int i = 0;
  while(i < max && i < 10 && s[i] != ';' && s[i] != '<' && s[i] != ' ' && s[i] != '&')
    i++;
  if(i >= max || s[i] != ';')
    return -1;
  *n = i + 1;
  if(s[0] == '#'){
    int cp = 0;
    if(s[1] == 'x' || s[1] == 'X'){
      for(int k = 2; k < i; k++){
        int c = s[k] | 0x20;
        cp = cp * 16 + (c >= 'a' ? c - 'a' + 10 : c - '0');
      }
    } else {
      for(int k = 1; k < i; k++)
        cp = cp * 10 + s[k] - '0';
    }
    return cp;
  }
  for(int k = 0; k < sizeof(entities) / sizeof(entities[0]); k++)
    if(strlen(entities[k].name) == i && strncmp(entities[k].name, s, i) == 0)
      return entities[k].cp;
  return -1;
}

// ---- tags

// attribute name's value from a tag's attributes a (n bytes), into v
static int
attr(const char *a, int n, const char *name, char *v, int max)
{
  int nl = strlen(name);
  for(int i = 0; i < n; i++){
    if((i > 0 && a[i - 1] != ' ' && a[i - 1] != '\t' && a[i - 1] != '\n') ||
       i + nl >= n)
      continue;
    int k;
    for(k = 0; k < nl && (a[i + k] | 0x20) == name[k]; k++)
      ;
    if(k < nl)
      continue;
    int j = i + nl;
    while(j < n && a[j] == ' ')
      j++;
    if(j >= n || a[j] != '=')
      continue;
    j++;
    while(j < n && a[j] == ' ')
      j++;
    char q = j < n && (a[j] == '"' || a[j] == '\'') ? a[j++] : 0;
    int m = 0;
    while(j < n && (q ? a[j] != q : a[j] != ' ' && a[j] != '>') && m < max - 1){
      // entities in values: just &amp;
      if(a[j] == '&' && strncmp(a + j, "&amp;", 5) == 0){
        v[m++] = '&';
        j += 5;
        continue;
      }
      v[m++] = a[j++];
    }
    v[m] = 0;
    return 1;
  }
  return 0;
}

static int
is(const char *tag, const char *name)
{
  return strcmp(tag, name) == 0;
}

static void
addfield(int type, const char *a, int n)
{
  if(nfield == MAXFIELD)
    return;
  struct field *f = &fields[nfield];
  memset(f, 0, sizeof(*f));
  f->form = curform;
  f->type = type;
  attr(a, n, "name", f->name, sizeof(f->name));
  if(!attr(a, n, "value", f->value, sizeof(f->value)) && type == 1)
    strcpy(f->value, "Submit");
  f->len = strlen(f->value);
  if(type == 2){
    nfield++;
    return;
  }
  char sz[8];
  int w = type == 0 ? (attr(a, n, "size", sz, sizeof(sz)) ? atoi(sz) : 24) : f->len + 2;
  if(w < 4)
    w = 4;
  if(w * FONT_W + 8 > cw)
    w = (cw - 8) / FONT_W;
  int wasbig = big;
  big = 0;
  flushword();
  struct item *it = additem(type == 0 ? K_FIELD : K_BUTTON, w * FONT_W + 8, FONT_H + 6);
  if(it){
    it->field = nfield;
    place(it);
    space = 1;
  }
  big = wasbig;
  nfield++;
}

static void
tag(char *name, int closing, const char *a, int n)
{
  if(is(name, "br")){
    flushword();
    int h = lineh;
    newline();
    if(h == 0)
      ly += lineheight();
  } else if(is(name, "p") || is(name, "div") || is(name, "section") || is(name, "article") ||
            is(name, "header") || is(name, "footer") || is(name, "nav") || is(name, "main") ||
            is(name, "center") || is(name, "form") || is(name, "table") || is(name, "dl") ||
            is(name, "figure")){
    flushword();
    block(is(name, "p") || is(name, "table") || is(name, "dl"));
    if(is(name, "form")){
      if(!closing && nform < MAXFORM){
        curform = nform++;
        if(!attr(a, n, "action", forms[curform], sizeof(forms[curform])))
          forms[curform][0] = 0;
      } else if(closing)
        curform = -1;
    }
  } else if(name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == 0){
    flushword();
    big = !closing && name[1] <= '2';
    bold = !closing;
    block(1);
  } else if(is(name, "tr") || is(name, "dt") || is(name, "dd")){
    flushword();
    newline();
  } else if(is(name, "td") || is(name, "th")){
    flushword();
    space = 1;
    if(!closing && lx > indent)
      text(' ');
  } else if(is(name, "ul") || is(name, "ol")){
    flushword();
    block(listdepth == 0);
    if(!closing && listdepth < 8){
      listn[listdepth++] = is(name, "ol") ? 1 : 0;
      indent += 3 * FONT_W;
    } else if(closing && listdepth > 0){
      listdepth--;
      indent -= 3 * FONT_W;
    }
    lx = indent;
  } else if(is(name, "li")){
    flushword();
    newline();
    if(!closing){
      lx = indent - 2 * FONT_W;
      if(listdepth > 0 && listn[listdepth - 1]){
        // "1." before it, a little further out
        char s[8];
        int k = 0, v = listn[listdepth - 1]++;
        char d[6];
        int nd = 0;
        do
          d[nd++] = '0' + v % 10;
        while((v /= 10) > 0);
        while(nd > 0)
          s[k++] = d[--nd];
        s[k++] = '.';
        lx -= (k - 1) * FONT_W;
        for(int i = 0; i < k; i++)
          addchar(s[i]);
      } else
        addchar('*');
      flushword();
      lx = indent;
      space = 0;
    }
  } else if(is(name, "blockquote")){
    flushword();
    block(1);
    indent += closing ? -4 * FONT_W : 4 * FONT_W;
    if(indent < 0)
      indent = 0;
    lx = indent;
  } else if(is(name, "pre")){
    flushword();
    block(1);
    pre = !closing;
  } else if(is(name, "hr")){
    flushword();
    block(0);
    struct item *it = additem(K_RULE, cw - indent, FONT_H / 2 + 2);
    if(it)
      place(it);
    newline();
  } else if(is(name, "b") || is(name, "strong")){
    bold = !closing;
  } else if(is(name, "a")){
    flushword();
    if(closing){
      inlink = -1;
    } else {
      char href[512];
      if(attr(a, n, "href", href, sizeof(href)) && nlink < MAXLINK){
        int off = pooladd(href, strlen(href));
        if(off >= 0){
          links[nlink] = off;
          inlink = nlink++;
        }
      }
    }
  } else if(is(name, "img") && !closing){
    char alt[128];
    if(attr(a, n, "alt", alt, sizeof(alt)) && alt[0]){
      text('[');
      textstr(alt);
      text(']');
      space = 1;
    }
  } else if(is(name, "input") && !closing){
    char type[16] = "text";
    attr(a, n, "type", type, sizeof(type));
    for(char *c = type; *c; c++)
      *c |= 0x20;
    if(is(type, "text") || is(type, "search") || is(type, "email") || is(type, "url"))
      addfield(0, a, n);
    else if(is(type, "submit"))
      addfield(1, a, n);
    else if(is(type, "hidden"))
      addfield(2, a, n);
  } else if(is(name, "button") && !closing){
    addfield(1, a, n);
  }
}

// Skip to the end of element name (after "</name"), from i.
static int
skipto(const char *s, int i, int n, const char *name)
{
  int nl = strlen(name);
  for(; i + nl + 2 < n; i++){
    if(s[i] != '<' || s[i + 1] != '/')
      continue;
    int k;
    for(k = 0; k < nl && (s[i + 2 + k] | 0x20) == name[k]; k++)
      ;
    if(k == nl){
      while(i < n && s[i] != '>')
        i++;
      return i + 1;
    }
  }
  return n;
}

static void
layout_start(void)
{
  nitem = npool = nlink = nfield = nform = 0;
  curform = -1;
  lx = ly = indent = 0;
  lineh = 0;
  bold = big = pre = space = 0;
  ingap = 1;
  inlink = -1;
  listdepth = 0;
  wlen = 0;
  title[0] = 0;
}

static void
layout_end(void)
{
  flushword();
  newline();
  pageh = ly + PAD;
}

static void
parse_html(const char *s, int n)
{
  int body = 0;
  for(int i = 0; i < n; ){
    if(s[i] == '<'){
      if(strncmp(s + i, "<!--", 4) == 0){
        char *e = strstr(s + i + 4, "-->");
        i = e ? e - s + 3 : n;
        continue;
      }
      // the tag's name, and its attributes
      int j = i + 1, closing = 0;
      if(j < n && s[j] == '/'){
        closing = 1;
        j++;
      }
      char name[16];
      int k = 0;
      while(j < n && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') ||
                      (s[j] >= '0' && s[j] <= '9')) && k < 15)
        name[k++] = s[j++] | 0x20;
      name[k] = 0;
      if(k == 0 && !closing && s[j] != '!'){
        text('<');   // a lone '<'
        i++;
        continue;
      }
      int a = j;
      char q = 0;
      while(j < n && (q || s[j] != '>')){
        if(q && s[j] == q)
          q = 0;
        else if(!q && (s[j] == '"' || s[j] == '\''))
          q = s[j];
        j++;
      }
      int alen_ = j - a;
      i = j + 1;
      if(is(name, "script") || is(name, "style") || is(name, "noscript") ||
         is(name, "svg") || is(name, "select") || is(name, "textarea")){
        if(!closing)
          i = skipto(s, i, n, name);
        continue;
      }
      if(is(name, "title") && !closing){
        int e = skipto(s, i, n, "title"), t = 0;
        for(int p = i; p < e && s[p] != '<' && t < sizeof(title) - 1; p++)
          title[t++] = s[p] == '\n' ? ' ' : s[p];
        title[t] = 0;
        i = e;
        continue;
      }
      if(is(name, "body"))
        body = 1;
      if(is(name, "head")){
        if(!closing && !body)
          i = skipto(s, i, n, "head");
        continue;
      }
      tag(name, closing, s + a, alen_);
    } else if(s[i] == '&'){
      int len, cp = entity(s + i + 1, n - i - 1, &len);
      if(cp < 0){
        text('&');
        i++;
      } else {
        unicode(cp);
        i += 1 + len;
      }
    } else if((uchar)s[i] >= 0xc0){
      // UTF-8
      int c = (uchar)s[i], cp, more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
      cp = c & (0x3f >> more);
      int k;
      for(k = 1; k <= more && i + k < n && ((uchar)s[i + k] & 0xc0) == 0x80; k++)
        cp = cp << 6 | ((uchar)s[i + k] & 0x3f);
      unicode(cp);
      i += k;
    } else {
      unicode((uchar)s[i]);
      i++;
    }
  }
}

static void
parse_text(const char *s, int n)
{
  pre = 1;
  for(int i = 0; i < n; i++)
    text((uchar)s[i] < 127 ? s[i] : '?');
}

// ---- URLs

// target, relative to the page's URL, as a URL in out.
static void
absurl(const char *target, char *out, int max)
{
  out[0] = 0;
  if(strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0){
    if(strlen(target) < max)
      strcpy(out, target);
    return;
  }
  if(strncmp(target, "//", 2) == 0){
    if(strlen(target) + 6 < max){
      strcpy(out, "http:");
      strcpy(out + 5, target);
    }
    return;
  }
  // the page's "http://host" and its path (static: big for the stack)
  static char base[512], path[512], full[1024], norm[1024];
  strcpy(base, url);
  char *hostend = strchr(base + 7, '/');
  strcpy(path, "/");
  if(hostend){
    strcpy(path, hostend);
    *hostend = 0;
  }
  char *q = strchr(path, '?');
  if(target[0] == '#' || target[0] == 0){
    strcpy(out, url);
    char *f = strchr(out, '#');
    if(f)
      *f = 0;
    return;
  }
  if(target[0] == '/'){
    strcpy(full, target);
  } else if(target[0] == '?'){
    if(q)
      *q = 0;
    strcpy(full, path);
    strcpy(full + strlen(full), target);
  } else {
    if(q)
      *q = 0;
    char *slash = path + strlen(path);
    while(slash > path && slash[-1] != '/')
      slash--;
    *slash = 0;
    if(strlen(path) + strlen(target) >= sizeof(full))
      return;
    strcpy(full, path);
    strcpy(full + strlen(full), target);
  }
  // "./" and "../" in the path part
  int m = 0;
  for(char *p = full; *p && *p != '?'; ){
    if(strncmp(p, "/./", 3) == 0){
      p += 2;
    } else if(strncmp(p, "/../", 4) == 0 || strcmp(p, "/..") == 0){
      while(m > 0 && norm[m - 1] != '/')
        m--;
      if(m > 0)
        m--;
      p += 3;
      if(*p == 0)
        norm[m++] = '/';
    } else
      norm[m++] = *p++;
  }
  char *rest = strchr(full, '?');
  norm[m] = 0;
  if(strlen(base) + m + (rest ? strlen(rest) : 0) >= max)
    return;
  strcpy(out, base);
  strcpy(out + strlen(out), norm);
  if(rest)
    strcpy(out + strlen(out), rest);
}

// ---- drawing

static void
glyph(int x, int y, int c, uint32 color, int scale)
{
  if(c <= ' ' || c >= 127)
    return;
  if(scale == 1){
    win_char(x, y, c, color);
    return;
  }
  const unsigned char *g = font[c - 32];
  for(int j = 0; j < FONT_H; j++)
    for(int i = 0; i < FONT_W; i++)
      if(g[j] & (0x80 >> i))
        win_fill(x + i * scale, y + j * scale, scale, scale, color);
}

static void
drawtext(int x, int y, const char *s, int n, uint32 color, int style)
{
  int scale = style & S_BIG ? 2 : 1;
  for(int i = 0; i < n; i++){
    glyph(x + i * FONT_W * scale, y, s[i], color, scale);
    if(style & S_BOLD)
      glyph(x + i * FONT_W * scale + 1, y, s[i], color, scale);
  }
}

static void
draw(void)
{
  int top = TOOL_H, bottom = win_h - STATUS_H;
  win_fill(0, top, win_w, bottom - top, C_BG);
  for(int i = 0; i < nitem; i++){
    struct item *it = &items[i];
    int x = PAD + it->x, y = top + PAD + it->y - scroll;
    if(y + it->h < top || y >= bottom)
      continue;
    switch(it->kind){
    case K_TEXT: {
      uint32 c = it->link >= 0 ? C_LINK : C_TEXT;
      if(it->style & S_PRE)
        win_fill(x, y, it->w, it->h, C_PRE);
      drawtext(x, y, pool + it->off, it->len, c, it->style);
      if(it->link >= 0)
        win_fill(x, y + it->h - 2, it->w, 1, C_LINK);
      // a link's words: the space between them underlined too
      if(it->link >= 0 && i + 1 < nitem && items[i + 1].link == it->link &&
         items[i + 1].y == it->y && items[i + 1].x > it->x + it->w)
        win_fill(x + it->w, y + it->h - 2, items[i + 1].x - it->x - it->w, 1, C_LINK);
      break;
    }
    case K_FIELD: {
      struct field *f = &fields[it->field];
      win_fill(x, y, it->w, it->h, focus == it->field ? C_FOCUS : C_EDGE);
      win_fill(x + 1, y + 1, it->w - 2, it->h - 2, C_FIELD);
      int shown = (it->w - 8) / FONT_W, from = f->len > shown - 1 ? f->len - shown + 1 : 0;
      drawtext(x + 4, y + 3, f->value + from, f->len - from, C_TEXT, 0);
      if(focus == it->field)
        win_fill(x + 4 + (f->len - from) * FONT_W, y + 3, 2, FONT_H, C_FOCUS);
      break;
    }
    case K_BUTTON: {
      struct field *f = &fields[it->field];
      win_fill(x, y, it->w, it->h, C_EDGE);
      win_fill(x + 1, y + 1, it->w - 2, it->h - 2, C_BUTTON);
      drawtext(x + 4 + FONT_W, y + 3, f->value, f->len, C_TEXT, 0);
      break;
    }
    case K_RULE:
      win_fill(x, y + it->h / 2, it->w, 1, C_RULE);
      break;
    }
  }
  // where in the page we are
  if(pageh > viewh){
    int h = viewh * viewh / pageh, y = top + scroll * viewh / pageh;
    win_fill(win_w - 5, top, 5, viewh, 0xeeeeee);
    win_fill(win_w - 5, y, 5, h < 8 ? 8 : h, 0xb0b4ba);
  }

  // the toolbar: back, the address, go
  win_fill(0, 0, win_w, TOOL_H, C_TOOL);
  win_fill(4, 4, BACK_W, TOOL_H - 8, nhist ? C_BUTTON : C_TOOL);
  win_char(4 + (BACK_W - FONT_W) / 2, 6, '<', nhist ? C_TEXT : C_EDGE);
  int ax = 8 + BACK_W, aw = win_w - ax - GO_W - 8;
  win_fill(ax, 4, aw, TOOL_H - 8, afocus ? C_FOCUS : C_EDGE);
  win_fill(ax + 1, 5, aw - 2, TOOL_H - 10, C_FIELD);
  int shown = (aw - 10) / FONT_W, from = alen > shown - 1 ? alen - shown + 1 : 0;
  drawtext(ax + 5, 6, addr + from, alen - from, C_TEXT, 0);
  if(afocus)
    win_fill(ax + 5 + (alen - from) * FONT_W, 6, 2, FONT_H, C_FOCUS);
  win_fill(win_w - GO_W - 4, 4, GO_W, TOOL_H - 8, C_BUTTON);
  win_text(win_w - GO_W - 4 + (GO_W - 2 * FONT_W) / 2, 6, "Go", C_TEXT);

  win_fill(0, bottom, win_w, STATUS_H, C_TOOL);
  win_text(PAD, bottom + 2, status, C_DIM);
  win_update(0, 0, win_w, win_h);
}

// ---- loading

static void
setaddr(const char *s)
{
  alen = strlen(s) < sizeof(addr) - 1 ? strlen(s) : sizeof(addr) - 1;
  memmove(addr, s, alen);
  addr[alen] = 0;
}

static void
setstatus(const char *a, const char *b)
{
  int n = strlen(a);
  if(n > sizeof(status) - 1)
    n = sizeof(status) - 1;
  memmove(status, a, n);
  int m = b ? strlen(b) : 0;
  if(n + m > sizeof(status) - 1)
    m = sizeof(status) - 1 - n;
  memmove(status + n, b, m);
  status[n + m] = 0;
}

// A page of our own: a message.
static void
message(const char *head, const char *msg)
{
  layout_start();
  big = bold = 1;
  textstr(head);
  big = bold = 0;
  block(1);
  textstr(msg);
  layout_end();
}

// Load u; with push, the page shown now goes on the history.
static void
go(const char *u, int push)
{
  char target[512];
  if(strlen(u) >= sizeof(target) - 8)
    return;
  if(strstr(u, "://") == 0){
    strcpy(target, "http://");
    strcpy(target + 7, u);
  } else
    strcpy(target, u);
  if(push && url[0]){
    if(nhist == NHIST){
      memmove(hist[0], hist[1], sizeof(hist[0]) * (NHIST - 1));
      nhist--;
    }
    strcpy(hist[nhist++], url);
  }
  setaddr(target);
  afocus = 0;
  focus = -1;
  setstatus("Loading ", target);
  draw();

  static struct http h;
  int r = http_get(target, &h);
  scroll = 0;
  strcpy(url, target);
  win_title("web");
  if(r < 0){
    message("Can't show this page", http_error(r));
    setstatus(target, 0);
    draw();
    return;
  }
  strcpy(url, h.url);
  setaddr(url);
  pagelen = 0;
  int n;
  while(pagelen < MAXPAGE && (n = http_read(&h, page + pagelen, MAXPAGE - pagelen)) > 0)
    pagelen += n;
  http_close(&h);

  char type[64] = "";
  http_field(&h, "content-type:", type, sizeof(type));
  for(char *c = type; *c; c++)
    *c |= 0x20;
  layout_start();
  if(strstr(type, "html") || type[0] == 0){
    parse_html(page, pagelen);
  } else if(strncmp(type, "text/", 5) == 0){
    parse_text(page, pagelen);
  } else {
    textstr("This is ");
    textstr(type);
    textstr(", which web can't show.");
  }
  layout_end();

  char s[64], *e = s;
  int v = h.status;
  char d[12];
  int nd = 0;
  do
    d[nd++] = '0' + v % 10;
  while((v /= 10) > 0);
  while(nd > 0)
    *e++ = d[--nd];
  strcpy(e, h.status == 200 ? " OK" : " (not found or failed)");
  setstatus(s, 0);
  char t[44] = "web: ";
  int tl = strlen(title[0] ? title : url);
  memmove(t + 5, title[0] ? title : url, tl < 38 ? tl : 38);
  win_title(t);
  draw();
}

static void
back(void)
{
  if(nhist > 0)
    go(hist[--nhist], 0);
}

// ---- forms

static void
urlencode(char *out, int max, const char *s)
{
  int m = strlen(out);
  for(; *s && m < max - 4; s++){
    int c = (uchar)*s;
    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
       c == '-' || c == '_' || c == '.' || c == '~')
      out[m++] = c;
    else if(c == ' ')
      out[m++] = '+';
    else {
      out[m++] = '%';
      out[m++] = "0123456789ABCDEF"[c >> 4];
      out[m++] = "0123456789ABCDEF"[c & 15];
    }
  }
  out[m] = 0;
}

// Submit field fi's form (by a button, or Enter in a text field).
static void
submit(int fi)
{
  int form = fields[fi].form;
  static char action[512], q[1024];
  absurl(form >= 0 && forms[form][0] ? forms[form] : url, action, sizeof(action));
  char *qm = strchr(action, '?');
  if(qm)
    *qm = 0;
  strcpy(q, action);
  strcpy(q + strlen(q), "?");
  int first = 1;
  for(int i = 0; i < nfield; i++){
    struct field *f = &fields[i];
    if(f->form != form || f->name[0] == 0 || (f->type == 1 && i != fi))
      continue;
    if(!first)
      strcpy(q + strlen(q), "&");
    first = 0;
    urlencode(q, sizeof(q), f->name);
    strcpy(q + strlen(q), "=");
    urlencode(q, sizeof(q), f->value);
  }
  go(q, 1);
}

// ---- input

static void
scrollto(int y)
{
  if(y > pageh - viewh)
    y = pageh - viewh;
  if(y < 0)
    y = 0;
  scroll = y;
}

static void
click(int x, int y)
{
  if(y < TOOL_H){
    int ax = 8 + BACK_W;
    afocus = 0;
    if(x >= 4 && x < 4 + BACK_W)
      back();
    else if(x >= win_w - GO_W - 4)
      go(addr, 1);
    else if(x >= ax){
      afocus = 1;
      focus = -1;
    }
    return;
  }
  afocus = 0;
  int px = x - PAD, py = y - TOOL_H - PAD + scroll;
  for(int i = 0; i < nitem; i++){
    struct item *it = &items[i];
    if(px < it->x || px >= it->x + it->w || py < it->y || py >= it->y + it->h)
      continue;
    if(it->kind == K_FIELD){
      focus = it->field;
      return;
    }
    if(it->kind == K_BUTTON){
      submit(it->field);
      return;
    }
    if(it->link >= 0){
      char target[512];
      absurl(pool + links[it->link], target, sizeof(target));
      if(strncmp(target, "https://", 8) == 0){
        setstatus("https isn't supported: ", target);
        return;
      }
      if(target[0])
        go(target, 1);
      return;
    }
  }
  focus = -1;
}

// Type c into the line s (n long now, max room).
static void
edit(char *s, int *n, int max, int c)
{
  if((c == '\b' || c == 127) && *n > 0)
    (*n)--;
  else if(c == C('U'))
    *n = 0;
  else if(c >= ' ' && c < 127 && *n < max - 1)
    s[(*n)++] = c;
  s[*n] = 0;
}

static void
key(int c)
{
  if(c == C('L')){
    afocus = 1;
    focus = -1;
    return;
  }
  if(afocus){
    if(c == '\n' || c == '\r')
      go(addr, 1);
    else if(c == '\033')
      afocus = 0;
    else
      edit(addr, &alen, sizeof(addr), c);
    return;
  }
  if(focus >= 0){
    struct field *f = &fields[focus];
    if(c == '\n' || c == '\r')
      submit(focus);
    else if(c == '\033')
      focus = -1;
    else
      edit(f->value, &f->len, sizeof(f->value), c);
    return;
  }
  int line = FONT_H + 2;
  switch(c){
  case KEY_UP: scrollto(scroll - 3 * line); break;
  case KEY_DOWN: scrollto(scroll + 3 * line); break;
  case KEY_PGUP: scrollto(scroll - viewh + 2 * line); break;
  case KEY_PGDN: case ' ': scrollto(scroll + viewh - 2 * line); break;
  case KEY_HOME: scrollto(0); break;
  case KEY_END: scrollto(pageh); break;
  case '\b': case 127: back(); break;
  }
}

int
main(int argc, char *argv[])
{
  int w, h;
  win_screen(&w, &h);
  if(w > 840)
    w = 840;
  if(h > 640)
    h = 640;
  items = malloc(sizeof(struct item) * MAXITEM);
  poolmax = MAXPAGE + 64 * 1024;
  pool = malloc(poolmax);
  page = malloc(MAXPAGE);
  if(items == 0 || pool == 0 || page == 0){
    fprintf(2, "web: out of memory\n");
    exit(1);
  }
  if(win_open(w, h, "web") < 0)
    exit(1);
  viewh = win_h - TOOL_H - STATUS_H - PAD;
  cw = win_w - 2 * PAD - 6;
  // the start page
  static char home[512] = HOME;
  int fd = open("/homepage", O_RDONLY);
  if(fd >= 0){
    int n = read(fd, home, sizeof(home) - 1);
    close(fd);
    home[n > 0 ? n : 0] = 0;
    char *nl = strchr(home, '\n');
    if(nl)
      *nl = 0;
    if(home[0] == 0)
      strcpy(home, HOME);
  }
  go(argc > 1 ? argv[1] : home, 0);

  struct winmsg m;
  int r;
  while((r = win_event(&m, 1)) >= 0){
    if(r == 0)
      continue;
    if(m.type == WE_CLOSE)
      break;
    if(m.type == WE_KEY)
      key(m.a);
    else if(m.type == WE_MOUSE && m.b == MOUSE_DOWN && m.a == 1)
      click(m.x, m.y);
    else if(m.type == WE_MOUSE && m.b == MOUSE_WHEEL)
      scrollto(scroll - 3 * (FONT_H + 2) * m.a);
    else
      continue;
    draw();
  }
  exit(0);
}
