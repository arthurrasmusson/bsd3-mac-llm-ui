/*==============================================================================
 * file: src/sandbox.c
 * OpenBSD: pledge/unveil
 * Linux: seccomp (optional) to block connect() when --no-network
 * License: BSD3
 *============================================================================*/
#include "sandbox.h"
#include <stdio.h>

#if defined(__OpenBSD__)
#include <unistd.h>
#include <err.h>
int sandbox_init_web(int allow_outbound){
	(void)allow_outbound;
#if 1
	/* We must keep "inet" to accept(), pledge can't differentiate connect(). */
	const char *promises = "stdio rpath inet dns";
	if(pledge(promises, NULL)==-1) err(1,"pledge");
#endif
#if 0
	if(unveil("/", "")==-1) err(1,"unveil"); /* no FS access */
	if(unveil(NULL,NULL)==-1) err(1,"unveil lock");
#endif
	return 0;
}
int sandbox_block_connect_linux(void){ return 0; }
#elif defined(__linux__)
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stddef.h>
/* Block connect(2). Keep listen/accept. */
static int install_seccomp_block_connect(void){
	struct sock_filter filt[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_connect, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = { .len = (unsigned short)(sizeof(filt)/sizeof(filt[0])), .filter = filt };
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return -1;
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) return -1;
	return 0;
}
int sandbox_block_connect_linux(void){ return install_seccomp_block_connect(); }
int sandbox_init_web(int allow_outbound){
	(void)allow_outbound; return 0;
}
#else
int sandbox_init_web(int allow_outbound){ (void)allow_outbound; return 0; }
int sandbox_block_connect_linux(void){ return 0; }
#endif
