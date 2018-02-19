/***************************************************************************
 *            choose-proxy.c
 *
 *  Thu Jan 26 17:43:39 2006
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

/* INCLUDES *****************************************************************/

#ifndef __APPLE__
#define _GNU_SOURCE 
#endif

#include <pthread.h> /* this should go first 'cos define _THREAD_SAFE */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <syslog.h>

#include "fbpss.h"
#include "choose-proxy.h"

/* DEFINE & MACROS **********************************************************/

#define PROXY_LIST_LINE_MAX	80

/* PROTOTYPES ***************************************************************/

static void init_choose_proxy(void);
static void end_choose_proxy(void);

static char *trim(const char *str, int c);
static char *read_field(char *str, int delim, char *field, size_t field_size);

/* GLOBAL VARIABLES *********************************************************/

long proxies_available = -1;

static FILE *proxies_fp = NULL;

static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex;

/* FUNCTIONS **************************************************************/

/**
 * @brief search for a proxy in the proxy list file.
 *
 * @return proxy ID number
 */
long
choose_proxy(char *host, size_t host_size, 
             char *port, size_t port_size,
             char *user_pass, size_t user_size)
{
  static long proxy_id = -1L;

  char *line, *p;
  char string[PROXY_LIST_LINE_MAX];

  pthread_once(&init_once, init_choose_proxy);

  pthread_mutex_lock(&mutex);

  /* search for a not comment line and parse it */
  memset(host,      0, host_size);
  memset(port,      0, port_size);
  memset(user_pass, 0, user_size);
  while(proxies_fp != NULL)
  {
    /* read line */
    if(fgets(string, PROXY_LIST_LINE_MAX, proxies_fp) == NULL)
    {
      proxies_available = proxy_id+1L;
      proxy_id = -1L;
      rewind(proxies_fp);

      if(fgets(string, PROXY_LIST_LINE_MAX, proxies_fp) == NULL)
      {    
        syslog(LOG_ERR, "fgets (%d): %m", __LINE__);
        pthread_kill(globals.tid, SIGTERM);
        break;        
      }
    }
    line = trim(string, ' ');

    /* it's a comment or empty line? */
    if(string[0] == '#' || string[0] == ';' || string[0] == '\n') 
      continue;

    /* read host*/
    if((line = read_field(line, ':', host, host_size)) == NULL)
    {
      syslog(LOG_ERR,"Bad proxy list file syntax in %ld.(%d)",ftell(proxies_fp),__LINE__);
      continue;
    }
    /* read port */
    if((p = read_field(line, ' ', port, port_size)) == NULL)
      if((p = read_field(line, '\n', port, port_size)) == NULL)
      {
        syslog(LOG_ERR,"Bad proxy list file syntax in %ld.(%d)",ftell(proxies_fp),__LINE__);
        continue;
      }
    line = p;

    /* read username and password */
    if((p = read_field(line, ' ', user_pass, user_size)) == NULL)
      if((p = read_field(line, '\n', user_pass, user_size)) == NULL)
        memset(user_pass, 0, user_size);
    
    /* remove spaces and comments at the end of line */
    if((p = strchr(user_pass, ' ')) != NULL)
      *p = '\0';
    
    if(user_pass[0] == '#')
      memset(user_pass, 0, user_size);

    /* every thing OK */
    proxy_id++;
    break;
  }

  pthread_mutex_unlock(&mutex);
  return (proxies_fp != NULL)?(proxy_id):(-1);
}

/*
 * @brief The firts time choose_proxy is call do this
 */
static void 
init_choose_proxy(void)
{
  pthread_mutex_init(&mutex, NULL);

  /* open the proxies list file */
  if((proxies_fp = fopen(globals.proxy_list_filename,"r"))==NULL)
  {
    syslog(LOG_ERR, "fopen (%d): %m", __LINE__);
    pthread_kill(globals.tid, SIGTERM);
  }

  /* set what to do at the end of the program */
  atexit(end_choose_proxy);

  return;
}

/*
 * @brief Before exit do this.
 */
static void 
end_choose_proxy(void)
{
  if(proxies_fp)
  {
    fclose(proxies_fp);
    proxies_fp = NULL;
  }

  pthread_mutex_destroy(&mutex);

  syslog(LOG_DEBUG, "choose proxy manager normal exit success.");
  return;
}

/** 
 * @brief remove a character c from from the begining of a string str
 */
static char *
trim(const char *str, int c)
{
  register char *p = (char*)str;

  if(p != NULL)
    while(*p == c) p++;

  return p;
}

/**
 * @brief read a proxy line 'str' field into 'field"
 *
 * note that str is modified
 */
static char *
read_field(char *str, int delim, char *field, size_t field_size)
{
  char *p;

  if((p = strchr(str, delim)) == NULL)
    return NULL;

  *p = '\0';
  strncpy(field, str, field_size);
  return trim((p + 1), ' ');
}
