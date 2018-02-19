/***************************************************************************
 *            curses-ui.c
 *
 *  Sat Jan 28 00:44:31 2006
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

#ifndef __APPLE__
#include <sys/select.h>
#endif

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <unistd.h>

#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <curses.h>

#include <syslog.h>

#include "curses-ui.h"
#include "fbpss.h"

/* PROTOTYPES ***************************************************************/

static void size_handler(int sig);
static void curses_exit(void);

static void paintscr(void);

static char *convert_to_human(char *str, size_t size, double number, 
                              const char *suffix);

static char *difftime_to_human(char *str, size_t size, time_t time1, 
                               time_t time0);

/* DEFINE & MACROS **********************************************************/

/* GLOBAL VARIABLES *********************************************************/

pthread_t curses_tid;

static char *ui_status_name[] = {"FREE", "BICONNECTED", "ENDING", 
                                 "CHECKING SOCKS VERSION",
                                 "ACCEPTING V4 METHOD", "ACCEPTING V5 METHOD", 
                                 "CONNECTING (S4)", "CONNECTING (S5)",
                                 "PROXY OK", "PROXY FAULT"};

/*** FUNCTIONS **************************************************************/

/**
 * @brief Curses User Interface Thread
 */
void *
curses_thread(void *arg)
{
  struct sigaction act;
  struct timeval pause;
  fd_set readfds;

  (void)arg;  

  initscr();               /* initialize the curses library */
  keypad(stdscr, TRUE);    /* enable keyboard mapping */
  nonl();                  /* tell curses not to do NL->CR/NL on output */
  cbreak();                /* take input chars one at a time,no wait for '\n'*/
  noecho();                /* don't echo input */

  atexit(curses_exit);     /* end curses at exit */

  /* handle change xterm size */
  act.sa_handler = size_handler;
  sigfillset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGWINCH,  &act, 0);

  /* paint the screen & read keyboard */
  for(;;)
  {
    erase();
    paintscr();
    refresh();
    
    FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);
    pause.tv_sec  = globals.curses_refresh_sec;
    pause.tv_usec = 0;

	if((select(STDIN_FILENO+1, &readfds, NULL, NULL, &pause))<0)
    {
	  if(errno == EINTR)
	  {
        syslog(LOG_WARNING, "Select (%d): %m", __LINE__);
	    continue;
	  }
      syslog(LOG_ERR, "Select (%d): %m", __LINE__);
      pthread_kill(globals.tid, SIGTERM);
      break;
    }
		
    if(FD_ISSET(STDIN_FILENO, &readfds))
    {
      switch(getch())
      {
        case 'q': /* quit */
          pthread_kill(globals.tid, SIGTERM);
          break;
        case 'r': /* redraw screen */
          clearok(curscr, TRUE);
        default:
          break;
      }
    }
  } 

  pthread_exit(NULL);
}

/*
 * @brief term size change handler
 */
static void
size_handler(int sig)
{
  struct winsize ws;

  (void)sig;

  /* get term size throuth termios */
  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
    resizeterm(ws.ws_row, ws.ws_col);

  /* repaint the screen */
  paintscr();
  refresh();

  return;
}

/*
 * @brief Before exit do this.
 */
static void
curses_exit(void)
{
  endwin();
  syslog(LOG_DEBUG, "curses-ui normal close success.");
  pthread_cancel(curses_tid);
  return;
}

/*
 * @brief paint the screen
 */
static void
paintscr(void)
{
  char line[LINE_MAX];
  char client_str[INET_ADDRSTRLEN];
  char server_str[INET_ADDRSTRLEN];
  char proxy_str[INET_ADDRSTRLEN];
  char in_str[6], out_str[6], time_str[6];
  unsigned int i,j, active;
  size_t len;

  difftime_to_human(time_str, sizeof(time_str), time(NULL), globals.start_time);
  snprintf(line, MIN(COLS+1,LINE_MAX), "%s - up %s", PROGRAM_NAME, time_str);
  mvprintw(0,0, "%s", line);

  attron(A_REVERSE);
  snprintf(line, MIN(COLS+1,LINE_MAX), "  CID CLIENT          SERVER          PROXY            IN    OUT   UP  STATE");
  len = strlen(line);
  if(len < (size_t)(COLS+1)) 
     memset(line+len, ' ', COLS-len);
  line[COLS] = '\0';
  mvprintw(3,0, "%s", line);
  attroff(A_REVERSE);

  for(i=4, j=0, active = 0; j < globals.max_connections; j++)
  {
    if(tunnel[j].client_status != FREE)
    {
      active++;

      if(i < (unsigned int)LINES)
      {  
        convert_to_human(in_str, sizeof(in_str), tunnel[j].in_bytes, NULL);
        convert_to_human(out_str, sizeof(out_str), tunnel[j].out_bytes, NULL);
        difftime_to_human(time_str, sizeof(time_str), time(NULL), tunnel[j].start_time);
        inet_ntop(AF_INET, &tunnel[j].client_addr.sin_addr, client_str, sizeof(client_str));
        inet_ntop(AF_INET, &tunnel[j].server_addr.sin_addr, server_str, sizeof(server_str));
        inet_ntop(AF_INET, &tunnel[j].proxy_addr.sin_addr,  proxy_str,  sizeof(proxy_str));

        snprintf(line, MIN(COLS+1, LINE_MAX), "%5u %-15s %-15s %-15s %5s %5s %4s %s",
                 j, client_str, server_str, proxy_str, in_str, out_str, 
                 time_str, ui_status_name[tunnel[j].client_status]); 

        mvprintw(i++,0, "%s", line);
      }
    }
  }

  convert_to_human(in_str,  sizeof(in_str), globals.total_bytes_in, NULL);
  convert_to_human(out_str, sizeof(out_str), globals.total_bytes_out, NULL);
  snprintf(line, COLS+1, "Total Transfer: %s in, %s out, Active: %u", in_str, 
           out_str, active);
  mvprintw(1,0, "%s", line);
  move(2,0);
  return;
}

/**
 * @brief Convert a number to human readable.
 *
 * @param number: the number to convert.
 * @param suffix: the unit suffix to use.
 * @return str.
 */
static char *
convert_to_human(char *str, size_t size, double number, const char *suffix)
{
  double new_number;
  char presuffix[] = "kmgt";
  unsigned int i;
  
  for(i=0, new_number=number; i<4; i++)
    if(new_number>1024.0l)
      new_number /= 1024.0l;
    else
      break;      

    if(i==0)
      snprintf(str, size, "%i%s", (int)new_number, suffix?suffix:""); 
    else
      snprintf(str,size,"%.0f%c%s",new_number,presuffix[i-1],suffix?suffix:""); 
    
  return str;
}

/**
 * @brief Convert a time in seconds to human readable.
 *
 * @param time1: final time.
 * @param time0: initial time.
 * @return str.
 */
static char *
difftime_to_human(char *str, size_t size, time_t time1, time_t time0)
{
  double new_number;
  char presuffix[] = "smhdy";
  double div[] = {60.0L, 60.0L, 24.0L, 365.0L};
  unsigned int i;
  
  for(i=0, new_number=difftime(time1,time0); i<4; i++)
    if(new_number>div[i])
      new_number /= div[i];
    else
      break;      
  
  snprintf(str, size, "%.0f%c", new_number, presuffix[i]);
  return str;
}
