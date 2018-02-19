/**
 * 			tunnel-io.h
 * 
 * Thu Jan 26 12:39:36 2006
 * Copyright (c) 2006  Alejandro Claro
 * ap0lly0n@users.sourceforge.net
 */

#ifndef _TUNNEL_IO_H
#define _TUNNEL_IO_H

/* INCLUDES *****************************************************************/

#include "fbpss.h"

/* PROTOTYPES ***************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

void *tunnel_io_thread(void *arg);
void tunnel_close(TUNNELSOCK *ptunnel);
long tunnel_check_proxy_list(const char *host, const char *port); 

#ifdef __cplusplus
}
#endif

#endif /* _TUNNEL_IO_H */
