/**
 * 			http.h
 * 
 * Thu Jan 26 12:39:36 2006
 * Copyright (c) 2006  Alejandro Claro
 * ap0lly0n@users.sourceforge.net
 */

#ifndef _HTTP_H
#define _HTTP_H

/* DEFINE & MACROS **********************************************************/

/* HTTP/1.0 status codes from RFC1945 */
/* Successful 2xx.  */
#define HTTP_STATUS_OK                    200
#define HTTP_STATUS_CREATED               201
#define HTTP_STATUS_ACCEPTED              202
#define HTTP_STATUS_NO_CONTENT            204
#define HTTP_STATUS_PARTIAL_CONTENTS      206
/* Redirection 3xx.  */
#define HTTP_STATUS_MULTIPLE_CHOICES      300
#define HTTP_STATUS_MOVED_PERMANENTLY     301
#define HTTP_STATUS_MOVED_TEMPORARILY     302
#define HTTP_STATUS_SEE_OTHER             303 /* HTTP/1.1 */
#define HTTP_STATUS_NOT_MODIFIED          304
#define HTTP_STATUS_TEMPORARY_REDIRECT    307 /* HTTP/1.1 */
/* Client error 4xx.  */
#define HTTP_STATUS_BAD_REQUEST           400
#define HTTP_STATUS_UNAUTHORIZED          401
#define HTTP_STATUS_FORBIDDEN             403
#define HTTP_STATUS_NOT_FOUND             404
#define HTTP_STATUS_RANGE_NOT_SATISFIABLE 416
/* Server errors 5xx.  */
#define HTTP_STATUS_INTERNAL              500
#define HTTP_STATUS_NOT_IMPLEMENTED       501
#define HTTP_STATUS_BAD_GATEWAY           502
#define HTTP_STATUS_UNAVAILABLE           503

/* PROTOTYPES ***************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

void base64_encode(const char *s, char *p);

int wait_for_crlf(int fd, unsigned char *buffer, size_t buf_size);
int wait_for_2crlf(int fd, unsigned char *buffer, size_t buf_size);
int parse_HTTP_return_code(unsigned char *buffer, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* _HTTP_H */
