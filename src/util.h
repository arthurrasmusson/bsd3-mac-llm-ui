/*==============================================================================
 * src/util.h
 * License: BSD3
 *============================================================================*/
#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <stdint.h>

void die(const char *fmt, ...);
void warnx(const char *fmt, ...);

void *xmalloc(size_t);
void *xrealloc(void*, size_t);
char *xstrdup(const char *);

char *html_escape(const char *s);
void str_trim(char *s);
void urldecode_inplace(char *s);
char *form_get(const char *body, const char *key); /* malloc'd or NULL */

struct sbuf {
	char *s; size_t len, cap;
};
void  sb_init(struct sbuf *b);
void  sb_free(struct sbuf *b);
void  sb_puts(struct sbuf *b, const char *s);
void  sb_putc(struct sbuf *b, char c);
void  sb_printf(struct sbuf *b, const char *fmt, ...);
char *sb_steal(struct sbuf *b); /* return s and reset */

char *read_file(const char *path, size_t *outlen);
uint64_t now_ms(void);

int split_host_port(const char *hp, char *host, size_t hsz, char *port, size_t psz);

#endif
