/**
 * 			curses-ui.h
 * 
 * Sat Jan 28 00:46:17 2006
 * Copyright (c) 2006  Alejandro Claro
 * ap0lly0n@users.sourceforge.net
 */

#ifndef _CURSES_UI_H
#define _CURSES_UI_H

/* GLOBAL VARIABLES *********************************************************/

extern pthread_t curses_tid;

/* PROTOTYPES ***************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

void *curses_thread(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _CURSES_UI_H */
