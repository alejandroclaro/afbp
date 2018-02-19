/***************************************************************************
 *            tunnel-io.c
 *
 *  Thu Jan 26 12:35:51 2006
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
 * IDEA based on desproxy and transconnect
 */

/* INCLUDES *****************************************************************/

#ifndef __APPLE__
#define _GNU_SOURCE 
#endif

#include <pthread.h> /* this should go first 'cos define _THREAD_SAFE */

#ifndef __APPLE__
#include <sys/select.h>
#endif

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include <syslog.h>

#include "tunnel-io.h"
#include "http.h"
#include "choose-proxy.h"

/* PROTOTYPES ***************************************************************/

/* connect to remote host trough a http proxy server */
static int connect_host_to_proxy(TUNNELSOCK *ptunnel, const char *remote_host,
                                 const char *remote_port);

/* copy from/to proxy to/from client */
static ssize_t tunnel_copy(int in_socket, int out_socket, void *buffer, size_t buf_size);

/* Socks protocol functions */
static void nothing_read(TUNNELSOCK *ptunnel);
static void protocol_v4_ok(TUNNELSOCK *ptunnel);
static void method_accepted_v4(TUNNELSOCK *ptunnel);
static void protocol_v5_ok(TUNNELSOCK *ptunnel);
static void method_accepted_v5(TUNNELSOCK *ptunnel);

/*** FUNCTIONS **************************************************************/

/**
 * @brief Client Incoming connection proccessing thread
 */
void *
tunnel_io_thread(void *arg)
{
  fd_set readfds, exceptfds;
  int maxfd, ret;
  struct timeval timeout;
  ssize_t count;
  
  TUNNELSOCK *ptunnel = (TUNNELSOCK*)arg;

  /* call malloc just once per tunnel structure element.
   * if the buffer have been alredy initializate don't 
   * do it again. [it is free() in normal_exit()]
   */
  if(ptunnel->buffer == NULL)
    if((ptunnel->buffer = (unsigned char *)malloc(globals.buffer_size)) == NULL)
    {
      syslog(LOG_WARNING, "malloc (%d): %m", __LINE__);
      tunnel_close(ptunnel);
      pthread_detach(ptunnel->tid);
      pthread_exit(NULL);
    }
  
  memset(&ptunnel->server_addr, 0, sizeof(ptunnel->server_addr));
  memset(&ptunnel->proxy_addr, 0, sizeof(ptunnel->proxy_addr));

  ptunnel->in_bytes  = 0;  
  ptunnel->out_bytes = 0;
  ptunnel->start_time = time(NULL);

  maxfd = ptunnel->client_socket;

  while(ptunnel->client_status != END_OF_CONNECTION)
  {
    /* Clear the socket sets */
    FD_ZERO(&readfds);
    FD_ZERO(&exceptfds);

    /* We want to know whether client or proxy is ready for reading */
    FD_SET(ptunnel->client_socket, &readfds);
    if(ptunnel->proxy_status == BICONNECTED)
      FD_SET(ptunnel->proxy_socket,  &readfds);

    /* Exceptional conditions on sockets */
    FD_SET(ptunnel->client_socket,&exceptfds);
    if(ptunnel->proxy_status == BICONNECTED)
      FD_SET(ptunnel->proxy_socket, &exceptfds);
   
    /* wait time out */
    timeout.tv_sec = globals.connection_timeout;
    timeout.tv_usec = 0;
  
    if((ret = select(maxfd+1, &readfds, NULL, &exceptfds, &timeout))<=0)
    {
      if(ret == 0)
        syslog(LOG_NOTICE, "select (%d): connection 'no activity' timeout", __LINE__);
      else
        syslog(LOG_ERR, "select (%d): %m", __LINE__);

      ptunnel->client_status = END_OF_CONNECTION;
      break;
    }
 
    if(FD_ISSET(ptunnel->client_socket, &readfds))
    {
      switch(ptunnel->client_status)
      {
        case NOTHING_READ:
          nothing_read(ptunnel);
          break;
        case BICONNECTED:
            if((count = tunnel_copy(ptunnel->client_socket, 
                                    ptunnel->proxy_socket, 
                                    ptunnel->buffer, 
                                    globals.buffer_size)) <= 0)
            {
              ptunnel->client_status = END_OF_CONNECTION;
            }
            else
            {
              ptunnel->out_bytes += count;
              globals.total_bytes_out += count;
            }
	        break;
        case PROTOCOL_V4_OK:
          protocol_v4_ok(ptunnel);
          break;
        case METHOD_ACCEPTED_V4:
          method_accepted_v4(ptunnel);
          break;
        case PROTOCOL_V5_OK:
          protocol_v5_ok(ptunnel);
          break;
        case METHOD_ACCEPTED_V5:
          method_accepted_v5(ptunnel);
          break;
        case END_OF_CONNECTION:
          break;
        default:
          syslog(LOG_NOTICE, "socket %d status %d unknowed.", 
                 ptunnel->client_socket, ptunnel->client_status);
          break;
      } 
    }
  
    if(ptunnel->proxy_status == BICONNECTED)
      if(FD_ISSET(ptunnel->proxy_socket, &readfds))
      {
        if((count = tunnel_copy(ptunnel->proxy_socket, ptunnel->client_socket,
                                ptunnel->buffer, globals.buffer_size)) <= 0)
        {
          ptunnel->client_status = END_OF_CONNECTION;
          break; 
        }
        else
        {
          ptunnel->in_bytes += count;
          globals.total_bytes_in += count;
        }
      }  

    if(FD_ISSET(ptunnel->client_socket, &exceptfds) ||
       FD_ISSET(ptunnel->proxy_socket, &exceptfds))
    {
       ptunnel->client_status = END_OF_CONNECTION; 
       break;
    }

    maxfd = MAX(ptunnel->client_socket, ptunnel->proxy_socket);
  }

  tunnel_close(ptunnel);
  pthread_detach(ptunnel->tid);
  pthread_exit(NULL);
  return NULL;
}

/**
 * @brief terminates connection gracefully
 */
void
tunnel_close(TUNNELSOCK *ptunnel)
{
  if(ptunnel->proxy_status != FREE)
    if(close(ptunnel->proxy_socket) != 0)
      syslog(LOG_ERR, "close (%d): %m", __LINE__);

  if(ptunnel->client_status != FREE)
    if(close(ptunnel->client_socket)!= 0)
      syslog(LOG_ERR, "close (%d): %m", __LINE__);

  ptunnel->client_status = FREE;
  ptunnel->proxy_status = FREE;
  return;
}

/**
 * @brief check the proxy list connecting to host 
 *
 * it requiere 'proxies_available' to be less than 0 before reach the 
 * end of proxy list file, and more than cero or cero after that.
 *
 * @return successful connections
 */
long
tunnel_check_proxy_list(const char *host, const char *port)
{
  TUNNELSOCK tunnel;
  int        bad;
  long       count;
  char       proxy_str[INET_ADDRSTRLEN];
  time_t     start;
  double     ct;

  memset(&tunnel, 0, sizeof(tunnel));  

  if((tunnel.buffer = (unsigned char *)malloc(globals.buffer_size)) == NULL)
  {
    fprintf(stderr, "malloc (%d): %s", __LINE__, strerror(errno));
    exit(EXIT_FAILURE);
  }

  count = -1;
  while(proxies_available < 0)
  {
    start = time(NULL);
    bad = connect_host_to_proxy(&tunnel, host, port);
    ct =  difftime(time(NULL),start);
 
    inet_ntop(AF_INET,&tunnel.proxy_addr.sin_addr,proxy_str,sizeof(proxy_str));
    printf("%s:%u", proxy_str, ntohs(tunnel.proxy_addr.sin_port));

    if(bad)
      printf(" # FAIL. (error %d)\n", bad);
    else
    {
      count++;
      printf(" # OK. %.0fs\n", ct);
    }

    tunnel_close(&tunnel);
  }

  free(tunnel.buffer);
  return count;
}

/**
 * @brief connects to remote_host:remote_port trough proxy_host:proxy_port 
 */
static int
connect_host_to_proxy(TUNNELSOCK *ptunnel, const char *remote_host, 
                      const char *remote_port)
{
  char            proxy_authorization_base64[256];
  char            string[512];
  char            proxy_host[128], proxy_port[10], proxy_user[128];
  int             HTTP_return_code;
  struct addrinfo hints, *res;
  int             r;

  hints.ai_flags     = 0;
  hints.ai_family    = AF_INET;
  hints.ai_socktype  = SOCK_STREAM;
  hints.ai_protocol  = IPPROTO_TCP;
  hints.ai_addrlen   = 0;
  hints.ai_addr      = NULL;
  hints.ai_canonname = NULL;
  hints.ai_next      = NULL;
  res                = NULL;

  choose_proxy(proxy_host, 128, proxy_port, 10, proxy_user, 128);

  /* store remote server address information */
  memset(&ptunnel->server_addr, 0, sizeof(ptunnel->server_addr));
  if((r = getaddrinfo(remote_host, NULL, &hints, &res)))
  {
    syslog(LOG_NOTICE, "getaddrinfo (%d): %s.\n", __LINE__, gai_strerror(r));
    if(r == EAI_SYSTEM)
      syslog(LOG_NOTICE, "getaddrinfo (%d): %m.\n", __LINE__);
  }
  else
  {
    memcpy(&ptunnel->server_addr, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
  }

  /* connect to proxy */
  if((r = getaddrinfo(proxy_host, NULL, &hints, &res)))
  {
    syslog(LOG_NOTICE, "getaddrinfo (%d): %s.\n", __LINE__, gai_strerror(r));
    if(r == EAI_SYSTEM)
      syslog(LOG_NOTICE, "getaddrinfo (%d): %m.\n", __LINE__);
  
    return -1;
  }
  memcpy(&ptunnel->proxy_addr, res->ai_addr, res->ai_addrlen);
  ptunnel->proxy_addr.sin_port = htons(strtol(proxy_port,NULL,10));
  freeaddrinfo(res);

  if((ptunnel->proxy_socket = socket(ptunnel->proxy_addr.sin_family, 
                                     SOCK_STREAM, 0)) < 0)
  {
    syslog(LOG_WARNING, "socket (%d): %m", __LINE__);
    return -2;
  }
  ptunnel->proxy_status = NOTHING_READ;

  if(connect(ptunnel->proxy_socket,(struct sockaddr *)&(ptunnel->proxy_addr),
             sizeof(ptunnel->proxy_addr)) < 0)
  {
    syslog(LOG_WARNING, "connect (%d): %m", __LINE__);
    return -3;
  }
  ptunnel->proxy_status = PROXY_OK;

  strcpy(string, "CONNECT ");
  strcat(string, remote_host);
  strcat(string, ":");
  strcat(string, remote_port);
  strcat(string, " HTTP/1.1\r\nHost: ");
  strcat(string, remote_host);
  strcat(string, ":");
  strcat(string, remote_port);
  strcat(string, "\r\nUser-Agent: ");

  if(getenv("USER_AGENT") != NULL)
    strncat(string, getenv("USER_AGENT"), 128);
  else
   strncat(string, "Mozilla/4.0 (compatible; MSIE 5.5; Windows 98)", 128);

  if(proxy_user[0] != '\0')
  {
    base64_encode(proxy_user, proxy_authorization_base64);
    strcat(string, "\r\nProxy-authorization: Basic ");
    strcat(string, proxy_authorization_base64);
  }

  strcat(string, "\r\n\r\n");
  if(write(ptunnel->proxy_socket, string, strlen(string))<0)
  {
    syslog(LOG_WARNING, "write (%d): %m", __LINE__);
    return -3;
  }

  while(ptunnel->proxy_status == PROXY_OK)
  {
    memset(ptunnel->buffer, 0, globals.buffer_size);
    if(wait_for_crlf(ptunnel->proxy_socket,ptunnel->buffer,globals.buffer_size)<0)
      return -4;
    
    HTTP_return_code = parse_HTTP_return_code(ptunnel->buffer, globals.buffer_size);

    if(HTTP_return_code == HTTP_STATUS_OK)
    {
      ptunnel->proxy_status = BICONNECTED;
    }
    else
    {
      ptunnel->proxy_status = PROXY_FAULT;
      return -5;
    }
  }
    
  /* discard the rest of HTTP header until CR LF CR LF
   * (that is, the beginning of the real connection)
   */
  if(wait_for_2crlf(ptunnel->proxy_socket,ptunnel->buffer,globals.buffer_size)<0)
    return -6;

  return 0;
}

/**
 * @brief Copy a block of data from one socket descriptor to another.
 * @return 0 as read when EOF. On  error, -1 is returned, and errno is
 *         set appropriately. 
 */
static ssize_t
tunnel_copy(int in_socket, int out_socket, void *buffer, size_t buf_size)
{
  ssize_t count;

  count = read(in_socket, buffer, buf_size);

  if(count > 0)
    count = write(out_socket, buffer, count);

  return count;
}

/**
 * @brief invoked for the first time something is read
 */
static void
nothing_read(TUNNELSOCK *ptunnel)
{
  char version;

  if(read(ptunnel->client_socket, ptunnel->buffer, 1) == -1)
  {
    syslog(LOG_WARNING, "read (%d): %m", __LINE__);  
    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }

  version = ptunnel->buffer[0];
  if(version == 4 || version == 5)
  {
    ptunnel->client_status = (version==4)?PROTOCOL_V4_OK:PROTOCOL_V5_OK;
    return;
  }

  /* send general SOCKS error */
  syslog(LOG_DEBUG, "Invalid client protocol version %d.", version);
  if(write(ptunnel->client_socket, "\x05\x01\x00\x00", 4)<0)
    syslog(LOG_WARNING, "write (%d): %m", __LINE__);  

  ptunnel->client_status = END_OF_CONNECTION;
  return;
}

/**
 * @brief invoked when we read the protocol header and it is 4
 */
static void
protocol_v4_ok(TUNNELSOCK *ptunnel)
{
  if(read(ptunnel->client_socket, ptunnel->buffer, 1) == -1)
  {
    syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }

  if(ptunnel->buffer[0] != 1) /* only support CONNECT method (FIXME!) */
  {    
    /* SOCKS4 reply packet code 91: request rejected or failed */
    syslog(LOG_DEBUG, "SOCK4 method '%d' not supported.", ptunnel->buffer[0]);
    if(write(ptunnel->client_socket, "\x00\x5B\x00\x00\x00\x00\x00\x00", 8)<0)
      syslog(LOG_WARNING, "write (%d): %m", __LINE__);  

    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }

  ptunnel->client_status = METHOD_ACCEPTED_V4;
  return;
}

/**
 * @brief Invoked when the method is accepted for protocol version 4
 */
static void
method_accepted_v4(TUNNELSOCK *ptunnel)
{
  char remote_host[256], remote_port[10];
  int err;

  if(read(ptunnel->client_socket, ptunnel->buffer, 6) == -1)
  {
    syslog(LOG_WARNING, "read (%d): %m", __LINE__);
    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }

  sprintf(remote_host, "%d.%d.%d.%d", ptunnel->buffer[2], ptunnel->buffer[3],
          ptunnel->buffer[4], ptunnel->buffer[5]);
  sprintf(remote_port, "%d", ptunnel->buffer[0] * 256 + ptunnel->buffer[1]);

  for(ptunnel->buffer[0] = 0xff; ptunnel->buffer[0] != 0;)
  {
    if(read(ptunnel->client_socket, ptunnel->buffer, 1) == -1)
	{
      syslog(LOG_WARNING, "read (%d): %m", __LINE__);
      ptunnel->client_status = END_OF_CONNECTION;
	  return;
	}
  }

  if((err = connect_host_to_proxy(ptunnel, remote_host, remote_port)) < 0)
  {
    /* SOCKS4 reply packet code 91: request rejected or failed */
    syslog(LOG_DEBUG, "SOCK4 method '%d' not supported.", ptunnel->buffer[0]);
    if(write(ptunnel->client_socket, "\x00\x5B\x00\x00\x00\x00\x00\x00", 8)<0)
      syslog(LOG_WARNING, "write (%d): %m", __LINE__);  

    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }
  else
  {
	/* SOCKS4 reply packet code 90: request granted */
    if(write(ptunnel->client_socket, "\x00\x5A\x00\x00\x00\x00\x00\x00", 8)<0)
    {
      syslog(LOG_WARNING, "write (%d): %m", __LINE__);  
      ptunnel->client_status = END_OF_CONNECTION;
    }
    else  
      ptunnel->client_status = BICONNECTED;
  }

  return;
}

/**
 * @brief invoked when we read the protocol header and it is 5
 */
static void
protocol_v5_ok(TUNNELSOCK *ptunnel)
{
  unsigned char i;

  if(read(ptunnel->client_socket, ptunnel->buffer, 1) == -1)
  {
    syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }

  ptunnel->nomethods = ptunnel->buffer[0];

  if(read(ptunnel->client_socket, ptunnel->buffer, ptunnel->nomethods) == -1)
  {
    syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }

  /* looking for method */
  for(i = 0; i < ptunnel->nomethods; i++)
  {
    /* accept method 0 (NOAUTH) */
    if(ptunnel->buffer[i] == 0)
	{
      /* SOCKS5 reply packet accepting method 0 */
      if(write(ptunnel->client_socket, "\x05\x00", 2)<0)
      {
        syslog(LOG_WARNING, "write (%d): %m", __LINE__); 
        ptunnel->client_status = END_OF_CONNECTION;
      }
      else
        ptunnel->client_status = METHOD_ACCEPTED_V5;

      return;
    }
  }

  /* SOCKS5 reply packet NO ACCEPTABLE METHODS */
  syslog(LOG_DEBUG,"Replying 'NO ACCEPTABLE METHODS' to client. (%d)",__LINE__); 
  if(write(ptunnel->client_socket, "\x05\xff", 2)<0)
    syslog(LOG_WARNING, "write (%d): %m", __LINE__); 

  ptunnel->client_status = END_OF_CONNECTION;
  return;
}

/**
 * @brief Invoked when the method is accepted for protocol version 5
 */
static void
method_accepted_v5(TUNNELSOCK *ptunnel)
{
  unsigned char i;
  char remote_host[256], remote_port[10];
  int err;

  if(read(ptunnel->client_socket, ptunnel->buffer, 4) == -1)
  {
    syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }

  /* we only understand command CONNECT */
  if(ptunnel->buffer[1] != 1)
  {
    syslog(LOG_DEBUG, "SOCKS5 Method '%d' not supported. (%d)\n", 
           ptunnel->buffer[1], __LINE__);  
    /* Send 07: "Command not supported" */
    if(write(ptunnel->client_socket, "\x05\x07\x00\x01", 4)<0)
      syslog(LOG_WARNING, "write (%d): %m", __LINE__); 

    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }
  
  /* ATYP address type of following address =  IP V4 address */
  if(ptunnel->buffer[3] == 1)
  {
     if(read(ptunnel->client_socket, ptunnel->buffer, 6) == -1)
     {
       syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
       ptunnel->client_status = END_OF_CONNECTION;
       return;
     }

     sprintf(remote_host, "%d.%d.%d.%d", ptunnel->buffer[0], 
             ptunnel->buffer[1], ptunnel->buffer[2], ptunnel->buffer[3]);
     sprintf(remote_port, "%d", ptunnel->buffer[4] * 256 + ptunnel->buffer[5]);
  }
  else if(ptunnel->buffer[3] == 3) /* ATYP = DOMAINNAME */
  {
     /* read the length of the domain name */
     if(read(ptunnel->client_socket, ptunnel->buffer, 1) == -1)
     { 
       syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
       ptunnel->client_status = END_OF_CONNECTION;
       return;
     }

     /* read the domain name */
     strcpy(remote_host, "");
     for(i = ptunnel->buffer[0]; i > 0; i--)
     { 
        if(read(ptunnel->client_socket, ptunnel->buffer, 1) == -1)
        {
          syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
          ptunnel->client_status = END_OF_CONNECTION;
          return;
        }

        strncat(remote_host, (const char *)ptunnel->buffer, 1);
     }

     strncat(remote_host, "\0", 1);

     if(read(ptunnel->client_socket, ptunnel->buffer, 2) == -1)
     {
       syslog(LOG_WARNING, "read (%d): %m", __LINE__); 
       ptunnel->client_status = END_OF_CONNECTION;
       return;
     }

     sprintf(remote_port, "%d", ptunnel->buffer[0] * 256 + ptunnel->buffer[1]);
  }

  if((err = connect_host_to_proxy(ptunnel, remote_host, remote_port)) < 0)
  {
    /* SOCKS5 reply packet code 04: host unreachable */
    if(write(ptunnel->client_socket, "\x05\x04\x00\x01", 4)<0)
      syslog(LOG_WARNING, "write (%d): %m", __LINE__); 

    syslog(LOG_DEBUG, "Connection to proxy failed with error code %d. (%d)", 
           err, __LINE__);
    ptunnel->client_status = END_OF_CONNECTION;
    return;
  }
  else
  {
    /* SOCKS5 reply packet code 00: succeeded */
    memcpy(ptunnel->buffer  , "\x05\x00\x00\x01", 4);
    memcpy(ptunnel->buffer+4, &globals.listen_sockaddr.sin_addr.s_addr, 4);
    memcpy(ptunnel->buffer+8, &globals.listen_sockaddr.sin_port, 2);
    if(write(ptunnel->client_socket, ptunnel->buffer, 10)<0)
    {
      syslog(LOG_WARNING, "write (%d): %m", __LINE__); 
      ptunnel->client_status = END_OF_CONNECTION;
    }
    else  
      ptunnel->client_status = BICONNECTED;
  }

  return;
}
