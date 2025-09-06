/*==============================================================================
 * src/tmpl.h
 * License: BSD3
 *============================================================================*/
#ifndef TMPL_H
#define TMPL_H
char *render_page(const char *app_title,
                  const char *css,
                  const char *model,
                  double temperature,
                  const char *transcript_pre, /* already HTML-escaped */
                  const char *history_raw,    /* raw hidden field */
                  const char *error_html);    /* optional */
#endif
