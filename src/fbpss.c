/***************************************************************************
 *            fbpss.c
 *
 *  Wed Jan 25 22:56:38 2006
 *  Copyright  2006  Alejandro Claro
 *  Email ap0lly0n@users.sourceforge.net
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
/*
 * IDEA based on desproxy and transconnect.
 */

/* INCLUDES *****************************************************************/

#ifndef __APPLE__
#define _GNU_SOURCE 
#endif

#include <pthread.h> /* this should go first 'cos define _THREAD_SAFE */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include <syslog.h>

#include "cmdline.h"
#include "tunnel-io.h"
#include "curses-ui.h"
#include "choose-proxy.h"

/* PROTOTYPES ***************************************************************/

/* initiation functions */
static void check_max_connections(void);
static void daemon_mode(void);
static void free_client_sockets(void);
static int listen_port(uint16_t request_port);

/* CTRL-C & at exit functions */
static void signal_handler(int sig); 
static void normal_exit(void);

/* connections functions */
static int  process_connection_request(void);

/* GLOBAL VARIABLES *********************************************************/

TUNNELSOCK *tunnel;
GLOBALS     globals;

/*** FUNCTIONS **************************************************************/

int
main(int argc, char **argv)
{
  long connection;
  int  err;
  struct sigaction act;

  fprintf(stdout, "%s v%s\nCopyright (c) 2006 Alejandro Claro\n\n",
          PROGRAM_NAME, VERSION);

  /* init syslog */
  openlog(EXE_FILE_NAME, LOG_PID|LOG_CONS, LOG_DAEMON);

  /* init globals */
  globals.tid                 = pthread_self();
  globals.proxy_list_filename = NULL;
  globals.local_port          = DEF_LOCAL_PORT;
  globals.listen_socket       = 0;
  globals.max_connections     = DEF_MAX_CONN;
  globals.buffer_size         = DEF_BUFF_SIZE;
  globals.start_time          = time(NULL);
  globals.total_bytes_in      = 0;
  globals.total_bytes_out     = 0;
  globals.curses_refresh_sec  = DEF_CURSES_REFRESH;
  globals.connection_timeout  = DEF_CONN_TIMEOUT;
  globals.run_mode            = NORMAL;
  memset(&globals.listen_sockaddr, 0, sizeof(globals.listen_sockaddr));

  /* parse command line */
  parse_command_line(argc, argv);

  /* enter in check proxy list mode */
  if(globals.run_mode == CHECK)
  {
    printf("Checking Proxy list file connecting to %s:%s\n"
           "Don't get anxious! Connection test can take some time.\n\n", 
           globals.check_host, globals.check_port);

    connection=tunnel_check_proxy_list(globals.check_host,globals.check_port);

    printf("\nCheck complete. %ld proxies available from %ld in the list.\n\n",
           connection, proxies_available);
    /* in check mode every thing ends here */
    exit(EXIT_SUCCESS);
  }

  /* check/set max number of connections */
  check_max_connections();

  /* go to daemon mode */
  if(globals.run_mode == DAEMON)
    daemon_mode();

  /*
   * malloc tunnel array after check_max_connections 'cos 
   * globals.max_connections can change there 
   */
  if((tunnel = (TUNNELSOCK *)malloc(sizeof(TUNNELSOCK)*globals.max_connections)) == NULL)
  {
    syslog(LOG_ERR, "malloc (%d): %m", __LINE__);
    fprintf(stderr, "malloc (%d): %s.\n\n", __LINE__, strerror(errno));
    exit(EXIT_FAILURE);
  }
  free_client_sockets();

  /* prepare connection */
  globals.listen_socket = listen_port((uint16_t)strtol(globals.local_port,NULL,10));

  /* handle signals */
  atexit(normal_exit);  

  act.sa_handler = signal_handler;
  sigfillset(&act.sa_mask);
  act.sa_flags = 0;

  sigaction(SIGHUP,  &act, 0);
  sigaction(SIGINT,  &act, 0);
  sigaction(SIGTERM, &act, 0);

  sigaction(SIGPIPE, &act, 0);
  sigaction(SIGILL,  &act, 0);
  sigaction(SIGTRAP, &act, 0);
  sigaction(SIGFPE,  &act, 0);
  sigaction(SIGBUS,  &act, 0);
  sigaction(SIGSEGV, &act, 0);

  /* start curses interface */
  if(globals.run_mode != DAEMON)
  {
    if(pthread_create(&curses_tid, NULL, curses_thread, NULL) != 0)
    {
      syslog(LOG_ERR, "pthread_create (%d) error.", __LINE__);
      exit(EXIT_FAILURE);
    }    
    if(pthread_detach(curses_tid) != 0)
    {
      syslog(LOG_ERR, "pthread_detach (%d) error.", __LINE__);
      exit(EXIT_FAILURE);
    }    
  }

  /* wait for connections */
  for(;;)
  {
    /* really wait for connections */
    connection = process_connection_request();

    /* No errors? create a thread to manage this connection */
    if(connection >= 0)
      if((err = pthread_create(&tunnel[connection].tid, NULL, tunnel_io_thread,
                        (void *)&tunnel[connection])) != 0)
      {
        if(err != EAGAIN)
        {
          syslog(LOG_ERR, "pthread_create (%d) error.", __LINE__);
          exit(EXIT_FAILURE);
        }
        tunnel_close(&tunnel[connection]);
      }    
  }

  return EXIT_SUCCESS;
}

/**
 * @brief check the maximum number of connection that system lets to open 
 */
static void 
check_max_connections(void)
{
  struct rlimit rlim;
  /* 
   * globals.max_connections is the number of tunnel connections
   * 2 connections (1 to client + 1 to proxy) peer tunnel connection.
   */
  if(!getrlimit(RLIMIT_NOFILE, &rlim))
  {
    if(rlim.rlim_cur < globals.max_connections*2)
    {
      if(rlim.rlim_max < globals.max_connections*2)
      {
        rlim.rlim_cur = rlim.rlim_max;
        globals.max_connections = rlim.rlim_max/2;
        setrlimit(RLIMIT_NOFILE, &rlim);
      }
      else
      {
        rlim.rlim_cur = globals.max_connections*2;
        setrlimit(RLIMIT_NOFILE, &rlim);
      }
    }
  }
  printf("Maximum number of connections can handle: %d\n", 
         globals.max_connections);

  if(globals.max_connections < DEF_MAX_CONN)
  {
    printf("\n     if more connections are needed you have to\n"
           "     change the HARD limit of OPEN_MAX"
#if   defined(__linux__)
           " runnig\n"
           "     on bash 'ulimit -n %u' or modifying\n"
           "     '/etc/security/limits.conf' file:\n\n"
           "        \"*	hard	nofile	%u\"\n\n", 
           (unsigned int)DEF_MAX_CONN*2,
           (unsigned int)DEF_MAX_CONN*2);
#elif defined(__FreeBSD__) || defined(__APPLE__)
           " runnig\n"
           "     on bash 'ulimit -n %u' or modifying\n" 
           "     '/etc/sysctl.conf' file:\n\n"
           "          kern.maxfiles = %u\n"
           "          kern.maxfilesperproc = %u\n\n", 
           (unsigned int)DEF_MAX_CONN*2,
           (unsigned int)(DEF_MAX_CONN*2+0.1L*DEF_MAX_CONN*2),
           (unsigned int)DEF_MAX_CONN*2);
#else
           ".\n\n");
#endif
  }

  return;
}

/**
 * @brief change to daemon mode
 */
static void
daemon_mode(void)
{
  pid_t pid, sid;
  int   fd;

  /* wanna fork() or wanna spoon */
  if((pid = fork()) < 0)
  {
    syslog(LOG_ERR, "fork (%d): %m", __LINE__);
    fprintf(stderr, "fork (%d): %s.\n\n", __LINE__, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if(pid > 0) /* Luke, I'm your father! */
  {
    exit(EXIT_SUCCESS); /* fuc** u father. DIE! */
  }
  else  /* oh no! I'm your son? */
  {
    if((sid = setsid()) < 0) 
    {
      syslog(LOG_ERR, "setsid (%d): %m", __LINE__);
      exit(EXIT_FAILURE);
    }

    umask(0);
    fd = open("/dev/null", O_RDWR); 
    dup2(fd, STDIN_FILENO);         /* stdin = fd */
    dup2(fd, STDOUT_FILENO);        /* stdout= fd */
    dup2(fd, STDERR_FILENO);        /* stderr= fd */
  }

  globals.tid = pthread_self();
  return;
}

/**
 * @brief initializes client sockets, marking all of them as free
 */
static void
free_client_sockets(void)
{
  memset(tunnel, 0, sizeof(TUNNELSOCK)*globals.max_connections);
  return;
}

/**
 * @brief listens in requested TCP port for incoming connections
 */
static int
listen_port(uint16_t request_port)
{
  int sock;
  int optval;
  
  if((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
  {
    syslog(LOG_ERR, "socket (%d): %m", __LINE__);
    fprintf(stderr, "socket (%d): %s\n\n", __LINE__, strerror(errno));
    exit(EXIT_FAILURE);
  }

  optval = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  memset(&globals.listen_sockaddr, 0, sizeof(globals.listen_sockaddr));
  globals.listen_sockaddr.sin_family = AF_INET;
  globals.listen_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  globals.listen_sockaddr.sin_port = htons(request_port);
  if(bind(sock, (struct sockaddr *)&globals.listen_sockaddr, 
           sizeof(globals.listen_sockaddr)) < 0)
  {
    syslog(LOG_ERR, "bind (%d): %m", __LINE__);
    fprintf(stderr, "bind (%d): %s\n\n", __LINE__, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if(listen(sock, SOMAXCONN) < 0)
  {
    syslog(LOG_ERR, "listen (%d): %m", __LINE__);
    fprintf(stderr, "listen (%d): %s\n\n", __LINE__, strerror(errno));
    exit(EXIT_FAILURE);
  }

  return sock;
}

/**
 * @brief clean & free everything and exit
 */
static void
signal_handler(int sig)
{
  struct sigaction act;

  switch(sig)
  {
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
       act.sa_handler = SIG_IGN;
       sigfillset(&act.sa_mask);
       act.sa_flags = 0;
       /* ignore every signal while normal_exit 'cos we're alredy ending */
       sigaction(SIGHUP,  &act, 0);
       sigaction(SIGINT,  &act, 0);
       sigaction(SIGTERM, &act, 0);
       /* normal exit */
       exit(EXIT_SUCCESS);
       break;
    default:
       syslog(LOG_DEBUG, "Critic %d signal was ignored! continuing execution.", sig);
       break;
  }

  return;
}

/**
 * @brief Before exit do this.
 */
static void
normal_exit(void)
{
  unsigned int i;

  if(tunnel)
  {
    for(i=0; i < globals.max_connections; i++)
    {
      if(tunnel[i].client_status != FREE)
      {
        pthread_cancel(tunnel[i].tid);
        if(pthread_join(tunnel[i].tid, NULL)<0)
          syslog(LOG_ERR, "pthread_join (%d): %m", __LINE__);
  
        tunnel_close(&tunnel[i]);
      }

      /* even if tunnel is free, may is not the buffer. 
       * now malloc is called from tunnel_io_thread() just once per tunnel
       * structure element, instead of every time the connection is started.
       */
      if(tunnel[i].buffer != NULL)
        free(tunnel[i].buffer);
    }
    free(tunnel);
  }

  close(globals.listen_socket);
  closelog();
  syslog(LOG_NOTICE, "normal exit success.");
  return;
}

/**
 * @brief Processes connection requests
 */
static int
process_connection_request(void)
{
  unsigned int connection;
  socklen_t client_length;

  for(connection = 0; connection < globals.max_connections; connection++)
    if (tunnel[connection].client_status == FREE)
      break;

  if(connection == globals.max_connections)
  {
    syslog(LOG_NOTICE, 
           "MAX_CONNECTIONS (%d) reached. Can't open more connections.",
           globals.max_connections);
    return EMFILE;
  }
  else
  {
    client_length = sizeof(tunnel[connection].client_addr);
    tunnel[connection].client_socket = accept(
                           globals.listen_socket,
                           (struct sockaddr *)&tunnel[connection].client_addr, 
                           &client_length
                           );

    if(tunnel[connection].client_socket < 0)
	{
        syslog(LOG_WARNING, "accept (%d): %m", __LINE__);
	    tunnel_close(&tunnel[connection]);
	    return tunnel[connection].client_socket;
    }

    tunnel[connection].client_status = NOTHING_READ;
  }

  return connection;
}
