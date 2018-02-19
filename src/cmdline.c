/***************************************************************************
 *            cmdline.c
 *
 *  Sun Jan 29 20:12:03 2006
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <getopt.h>

#include "fbpss.h"
#include "cmdline.h"

/* PROTOTYPES ***************************************************************/

static void display_usage(void);

/* GLOBAL VARIABLES *********************************************************/

static struct option long_options[] = {{"help",    0, NULL, 'h'},
                                       {"port"   , 1, NULL, 'p'},
                                       {"daemon" , 0, NULL, 'd'},
                                       {"refresh", 1, NULL, 'r'},
                                       {"check",   1, NULL, 'c'},
                                       {"timeout", 1, NULL, 't'},
                                       {NULL,      0, NULL,  0}};

static char *short_options = "hdp:r:c:t:";

/*** FUNCTIONS **************************************************************/

/**
 * @brief Parses command line
 */
void
parse_command_line(int argc, char **argv)
{
  int c;
  long refresh, timeout;
  FILE *fp;

  while((c = getopt_long(argc, argv, short_options, long_options, NULL)) != -1)
  {
    switch(c)
    {
      case 'h': /* help */
        display_usage();
        exit(EXIT_SUCCESS);
        break;
      case 'p': /* listen port */
        globals.local_port = optarg;
        break;
      case 'd': /* change to daemon mode */
        if(globals.run_mode == CHECK)
          printf("Ignoring daemon mode flag.\n");
        else
          globals.run_mode = DAEMON;
        break;
      case 'r': /* change refresh time of curses interfase */
        refresh = strtol(optarg,NULL,10);
        if(refresh >= 0 && refresh < UINT_MAX)
          globals.curses_refresh_sec = (unsigned int)refresh;
        else
        {
          printf("Refresh time must be a positive integer number [1-%u].\n\n", UINT_MAX);
          exit(EXIT_FAILURE);
        }
        break;
      case 'c': /* change to check proxy list mode */
        if(globals.run_mode == DAEMON)
        {
          printf("Ignoring daemon mode flag.\n");
        }
        globals.run_mode = CHECK;
   
        globals.check_host = optarg;
        if((globals.check_port = strchr(optarg, ':')) != NULL)
          *(globals.check_port++) = '\0';
        else
          globals.check_port = DEF_CHECK_HOST_PORT;

        break;
      case 't':
        timeout = strtol(optarg,NULL,10);
        if(timeout >= 0 && timeout < UINT_MAX)
          globals.connection_timeout = (unsigned int)timeout;
        else
        {
          printf("Connection timeout must be a positive integer number [1-%u].\n\n", UINT_MAX);
          exit(EXIT_FAILURE);
        }
        break;
      case '?' : /* what did you say? */
        exit(EXIT_FAILURE);
        break;
    }
  }
  
  if(optind < argc)
    globals.proxy_list_filename = argv[optind];
  else
  {
    display_usage();
    exit(EXIT_FAILURE);
  }
    
  if((fp = fopen(globals.proxy_list_filename,"r"))==NULL)
  {
    printf("Can't open '%s' file.\n\n", globals.proxy_list_filename);
    exit(EXIT_FAILURE);
  }
  fclose(fp);

  return;
}

/**
 * @brief Display usage in std
 */
static void
display_usage(void)
{
  printf("Usage: %s [options] proxy_list_file_name\n\n", EXE_FILE_NAME);
  printf("    -h  --help                  display this help.\n"
         "    -p  --port    local_port    set the listen port (default %s).\n"
         "    -d  --daemon                run in (background & quiet) daemon mode.\n"
         "    -c  --check   host[:port]   check the proxy list connecting to 'host'.\n"
         "    -r  --refresh seconds       wait before update screen (default %ds).\n"
         "    -t  --timeout seconds       close connections without activity(default %ds).\n"
         "\n",
         DEF_LOCAL_PORT, DEF_CURSES_REFRESH, DEF_CONN_TIMEOUT);

  return;
}
