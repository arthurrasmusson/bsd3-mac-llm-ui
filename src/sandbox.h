/*==============================================================================
 * src/sandbox.h
 * License: BSD3
 *============================================================================*/
#ifndef SANDBOX_H
#define SANDBOX_H
int sandbox_init_web(int allow_outbound);
int sandbox_block_connect_linux(void); /* best-effort on Linux */
#endif
