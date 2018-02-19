/**
 * 			choose-proxy.h
 * 
 * Thu Jan 26 17:44:54 2006
 * Copyright (c) 2006  Alejandro Claro
 * ap0lly0n@users.sourceforge.net
 */

#ifndef _CHOOSE_PROXY_H
#define _CHOOSE_PROXY_H

/* GLOBAL VARIABLES *********************************************************/

extern long proxies_available;

/* PROTOTYPES ***************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

long choose_proxy(char *host, size_t host_size, char *port, size_t port_size, char *user_pass, size_t user_size);

#ifdef __cplusplus
}
#endif

#endif /* _CHOOSE_PROXY_H */
