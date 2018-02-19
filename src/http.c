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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include <syslog.h>

#include "http.h"

/* FUNCTIONS **************************************************************/

/**
 * @brief creates a base 64 encoded string
 *
 * this function was taken from transconnect.
 */
void
base64_encode(const char *s, char *p)
{
  char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                  "abcdefghijklmnopqrstuvwxyz"
                  "0123456789+/";
  int i, length;

  length = strlen (s);
  for (i = 0; i < length; i += 3)
  {
    *p++ = base64[s[0] >> 2];
    *p++ = base64[((s[0] & 3) << 4) + (s[1] >> 4)];
    *p++ = base64[((s[1] & 0xf) << 2) + (s[2] >> 6)];
    *p++ = base64[s[2] & 0x3f];
    s += 3;
  }
  if (i == length + 1)
  {
    *(p - 1) = '=';
  }
  else if (i == length + 2)
  {
    *(p - 1) = *(p - 2) = '='; 
  } 

  *p = '\0';
  
  return;
}

/**
 *  @brief reads data from connection file descriptor until sequence CR LF found
 *         (data is stored in variable buffer)
 */
int
wait_for_crlf(int fd, unsigned char *buffer, size_t buf_size)
{
  unsigned char previous_byte = 0;
  size_t count;

  for(count = 0;;)
  {
    if(count >= buf_size)
    {
      syslog(LOG_NOTICE, "wait_for_crlf (%d): Buffer Overflow!", __LINE__);
	  return -1;
	}

    if(read(fd, &buffer[count], 1) < 0)
    {
	  syslog(LOG_WARNING, "read (%d): %m", __LINE__);
      return -2;
    }
    
    if((buffer[count] == '\n') && (previous_byte == '\r'))
  	  break;
    
    previous_byte = buffer[count];
    count++;
  }

  buffer[count + 1] = 0;

  return 0;
}

/**
 * @brief reads file descriptor until sequence CR LF CR LF found
 *        (that sequence is used to mark HTTP header end)
 */
int
wait_for_2crlf(int fd, unsigned char *buffer, size_t buf_size)
{
  while(memcmp(buffer, "\r\n", 2)) /* end if buffer begins with the 2nd CRLF */
  {
    /* find the first CR LF */
    if(wait_for_crlf(fd, buffer, buf_size) < 0)
	  return -1;
  }

  return 0;
}

/**
 * @brief gets a parsed HTTP return code from line in buffer
 *        (returns a pointer to a malloced pointer)
 */
int
parse_HTTP_return_code(unsigned char *buffer, size_t buf_size)
{
  unsigned char *begin, *end;

  end = &buffer[buf_size];

#ifndef _GNU_SOURCE
  buffer[buf_size-1] = '\0';
  if((begin = strstr(buffer,"HTTP")) != NULL)
#else
  if((begin = memmem(buffer, buf_size, "HTTP",4)) != NULL)
#endif
  {
    /* Match the HTTP version.  This is optional because some
     * servers have been reported to not specify HTTP version. 
     */
     begin += 4; /* + strlen("HTTP") */
     if(begin < end && *begin == '/')
     {
       begin++;
       while(begin < end && isdigit(*begin))
	     begin++;
       if(begin < end && *begin == '.')
	     begin++; 
       while(begin < end && isdigit(*begin))
	     begin++;
     }

     /* jump spaces */
     while(begin < end && isspace(*begin))
       begin++;

     /* here must come the return code */
     if((end - begin) > 3)
       return (100*(begin[0]-'0') + 10*(begin[1]-'0') + (begin[2]-'0'));
  }

  syslog(LOG_NOTICE, "Bad proxy response (%d): bad HTTP header.", __LINE__);
  return -1;
}
