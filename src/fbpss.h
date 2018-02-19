/**
 * 			fbpss.h
 * 
 * Thu Jan 26 12:29:14 2006
 * Copyright (c) 2006  Alejandro Claro
 * ap0lly0n@users.sourceforge.net
 */

#ifndef _FBP_PROXY_H
#define _FBP_PROXY_H

/* INCLUDES *****************************************************************/

#include <pthread.h> /* this should go first 'cos define _THREAD_SAFE */

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>

/* DEFINE & MACROS **********************************************************/

#ifdef PTHREAD_THREADS_MAX                           /* stdin,stdout,stderr */
#define DEF_MAX_CONN         (PTHREAD_THREADS_MAX-4) /* and proxy file      */
#else  /* I can't find why MacOSX don't have it */
#define DEF_MAX_CONN         2048
#endif

#define DEF_BUFF_SIZE        2048
#define DEF_LOCAL_PORT       "1080"
#define DEF_CURSES_REFRESH   5
#define DEF_CONN_TIMEOUT     14400
#define DEF_CHECK_HOST_PORT  "80"

#define PROGRAM_NAME  "Alex's Firewall ByPasser Socks Server"
#define EXE_FILE_NAME "fbpss"
#define VERSION       "0.5.2"

#ifndef MAX
#define MAX(x,y)	(((x)>(y)) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x,y)	(((x)<(y)) ? (x) : (y))
#endif

/* TYPEDEF ******************************************************************/

typedef enum {FREE=0, BICONNECTED, END_OF_CONNECTION, NOTHING_READ,
              PROTOCOL_V4_OK, PROTOCOL_V5_OK, METHOD_ACCEPTED_V4, 
              METHOD_ACCEPTED_V5, PROXY_OK, PROXY_FAULT} STATUS;

typedef enum {NORMAL=0, DAEMON, CHECK} RUNMODE;

typedef struct _TUNNELSOCK TUNNELSOCK;
typedef struct _GLOBALS GLOBALS;

struct _TUNNELSOCK
{
  pthread_t tid;  /* tunnel connection thread */

  struct sockaddr_in client_addr;  
  struct sockaddr_in server_addr;  /* socket addresses */
  struct sockaddr_in proxy_addr;   

  int client_socket, proxy_socket;    /* socket from client & to proxy */
  STATUS client_status, proxy_status; /* state of client & proxy connections */

  time_t start_time;              /* connection start time */
  size_t in_bytes, out_bytes;     /* bytes in & out */

  int nomethods;         /* numbers of methods in SOCKS5 Client */
  
  unsigned char *buffer; /* data buffer */
};

struct _GLOBALS
{
  pthread_t tid;  /* main Thread ID */

  char *proxy_list_filename;  /* proxies list file name */
  char *local_port;           /* listen local port */

  struct sockaddr_in listen_sockaddr; /* listen socket address */
  
  int listen_socket; /* socket for listen client connections */

  unsigned int max_connections;  /* max number of connecctions */
  unsigned int buffer_size;      /* thread buffer size */

  time_t start_time;                      /* program star time */
  size_t total_bytes_in, total_bytes_out; /* total bytes in & out */

  unsigned int curses_refresh_sec; /* curses ui refresh seconds */
  unsigned int connection_timeout; /* tunnel conn no activity timeout */

  RUNMODE run_mode;                /* work mode: normal, daemon, check */

  char *check_host, *check_port;   /* host to use for checking */
};

/* GLOBAL VARIABLES *********************************************************/

extern TUNNELSOCK *tunnel; 
extern GLOBALS    globals;

#endif /* _FBP_PROXY_H */
