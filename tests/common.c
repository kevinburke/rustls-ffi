#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>    /* gai_strerror() */
  #include <io.h>          /* write() */
  #include <fcntl.h>       /* O_BINARY */
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "crustls.h"
#include "common.h"

void
print_error(char *prefix, rustls_result result)
{
  char buf[256];
  size_t n;
  rustls_error(result, buf, sizeof(buf), &n);
  fprintf(stderr, "%s: %.*s\n", prefix, (int)n, buf);
}

#ifdef _WIN32
const char *ws_strerror (int err)
{
  static char ws_err[50];

  if (err >= WSABASEERR) {
    snprintf(ws_err, sizeof(ws_err), "Winsock err: %d", err);
    return ws_err;
  }
  /* Assume a CRT error */
  return (strerror)(err);
}
#endif

/*
 * Write n bytes from buf to the provided fd, retrying short writes until
 * we finish or hit an error. Assumes fd is blocking and therefore doesn't
 * handle EAGAIN. Returns 0 for success or 1 for error.
 *
 * For Winsock we cannot use a socket-fd in write().
 * Call send() if fd > STDOUT_FILENO.
 */
int
write_all(int fd, const char *buf, int n)
{
  int m = 0;

  while(n > 0) {
    m = write(fd, buf, n);
    if(m < 0) {
      perror("writing to stdout");
      return 1;
    }
    if(m == 0) {
      fprintf(stderr, "early EOF when writing to stdout\n");
      return 1;
    }
    n -= m;
  }
  return 0;
}

/*
 * Set a socket to be nonblocking.
 *
 * Returns CRUSTLS_DEMO_OK on success, CRUSTLS_DEMO_ERROR on error.
 */
enum crustls_demo_result
nonblock(int sockfd)
{
#ifdef _WIN32
  u_long nonblock = 1UL;

  if (ioctlsocket(sockfd, FIONBIO, &nonblock) != 0) {
    perror("Error setting socket nonblocking");
    return CRUSTLS_DEMO_ERROR;
  }
#else
  int flags;
  flags = fcntl(sockfd, F_GETFL, 0);
  if(flags < 0) {
    perror("getting socket flags");
    return CRUSTLS_DEMO_ERROR;
  }
  flags = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  if(flags < 0) {
    perror("setting socket nonblocking");
    return CRUSTLS_DEMO_ERROR;
  }
#endif
  return CRUSTLS_DEMO_OK;
}

int
read_cb(void *userdata, uint8_t *buf, uintptr_t len, uintptr_t *out_n)
{
  ssize_t n = 0;
  struct conndata_t *conn = (struct conndata_t*)userdata;
  n = recv(conn->fd, buf, len, 0);
  if(n < 0) {
    return errno;
  }
  if (out_n != NULL) {
    *out_n = n;
  }
  return 0;
}

int
write_cb(void *userdata, const uint8_t *buf, uintptr_t len, uintptr_t *out_n)
{
  ssize_t n = 0;
  struct conndata_t *conn = (struct conndata_t*)userdata;

  n = send(conn->fd, buf, len, 0);
  if(n < 0) {
    return errno;
  }
  *out_n = n;
  return 0;
}
