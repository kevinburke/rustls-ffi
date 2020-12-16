#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

/* lib.h is autogenerated in the Makefile using cbindgen. */
#include "lib.h"

/*
 * Write n bytes from buf to the provided fd, retrying short writes until
 * we finish or hit an error. Assumes fd is blocking and therefore doesn't
 * handle EAGAIN. Returns 0 for success or 1 for error.
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
 * Connect to the given hostname on port 443 and return the file descriptor of
 * the socket. On error, print the error and return 1. Caller is responsible
 * for closing socket.
 */
int
make_conn(const char *hostname)
{
  struct addrinfo *getaddrinfo_output = NULL;
  int getaddrinfo_result =
    getaddrinfo(hostname, "443", NULL, &getaddrinfo_output);
  if(getaddrinfo_result != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_result));
    goto cleanup;
  }

  int sockfd = socket(getaddrinfo_output->ai_family,
                      getaddrinfo_output->ai_socktype,
                      getaddrinfo_output->ai_protocol);
  if(sockfd < 0) {
    perror("making socket");
    goto cleanup;
  }

  int connect_result = connect(
    sockfd, getaddrinfo_output->ai_addr, getaddrinfo_output->ai_addrlen);
  if(connect_result < 0) {
    perror("connecting");
    goto cleanup;
  }

  return sockfd;

cleanup:
  if(getaddrinfo_output != NULL) {
    freeaddrinfo(getaddrinfo_output);
  }
  if(sockfd > 0) {
    close(sockfd);
  }
  return -1;
}

/*
 * Given an established TCP connection, and a rustls client_session, send an
 * HTTP request and read the response. On success, return 0. On error, print
 * the message and return 1.
 */
int
send_request_and_read_response(int sockfd,
                               struct rustls_client_session *client_session,
                               const char *hostname, const char *path)
{
  int ret = 1;
  int result = 1;
  char buf[2048];

  bzero(buf, sizeof(buf));
  snprintf(buf,
           sizeof(buf),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: crustls-demo\r\n"
           "Accept: carcinization/inevitable, text/html\r\n"
           "Connection: close\r\n"
           "\r\n",
           path,
           hostname);
  int n =
    rustls_client_session_write(client_session, (uint8_t *)buf, strlen(buf));
  if(n < 0) {
    fprintf(stderr, "error writing plaintext bytes to ClientSession\n");
    goto cleanup;
  }

#define MAX_EVENTS 1
  struct epoll_event ev, events[MAX_EVENTS];
  int nfds = 0;
  int epollfd = epoll_create1(0);
  if(epollfd == -1) {
    perror("epoll_create1");
    goto cleanup;
  }

  ev.events = EPOLLIN | EPOLLOUT;
  ev.data.fd = sockfd;
  if(epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
    perror("epoll_ctl: listen_sock");
    goto cleanup;
  }

  for(;;) {
    nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    if(nfds == -1) {
      perror("epoll_wait");
      goto cleanup;
    }

    if(rustls_client_session_wants_read(client_session) &&
       (events[0].events & EPOLLIN) > 0) {
      fprintf(stderr,
              "ClientSession wants us to read_tls. First we need to pull some "
              "bytes from the socket\n");

      bzero(buf, sizeof(buf));
      n = read(sockfd, buf, sizeof(buf));
      if(n == 0) {
        fprintf(stderr, "EOF reading from socket\n");
        break;
      }
      else if(n < 0) {
        perror("reading from socket");
        goto cleanup;
      }
      fprintf(stderr, "read %d bytes from socket\n", n);

      /*
       * Now pull those bytes from the buffer into ClientSession.
       * Note that we pass buf, n; not buf, sizeof(buf). We don't
       * want to pull in unitialized memory that we didn't just
       * read from the socket.
       */
      n = rustls_client_session_read_tls(client_session, (uint8_t *)buf, n);
      if(n == 0) {
        fprintf(stderr, "EOF from ClientSession::read_tls\n");
        // TODO: What to do here?
        break;
      }
      else if(n < 0) {
        fprintf(stderr, "Error in ClientSession::read_tls\n");
        goto cleanup;
      }

      result = rustls_client_session_process_new_packets(client_session);
      if(result != RUSTLS_RESULT_OK) {
        fprintf(stderr, "Error in process_new_packets");
        goto cleanup;
      }

      /* Read all available bytes from the client_session until EOF.
       * Note that EOF here indicates "no more bytes until
       * process_new_packets", not "stream is closed".
       */
      for(;;) {
        bzero(buf, sizeof(buf));
        n = rustls_client_session_read(
          client_session, (uint8_t *)buf, sizeof(buf));
        if(n == 0) {
          fprintf(stderr, "EOF from ClientSession::read (this is expected)\n");
          break;
        }
        else if(n < 0) {
          fprintf(stderr, "Error in ClientSession::read\n");
          goto cleanup;
        }

        result = write_all(STDOUT_FILENO, buf, n);
        if(result != 0) {
          goto cleanup;
        }
      }
    }
    if(rustls_client_session_wants_write(client_session) &&
       (events[0].events & EPOLLOUT) > 0) {
      fprintf(stderr, "ClientSession wants us to write_tls.\n");
      bzero(buf, sizeof(buf));
      n = rustls_client_session_write_tls(
        client_session, (uint8_t *)buf, sizeof(buf));
      if(n == 0) {
        fprintf(stderr, "EOF from ClientSession::write_tls\n");
        goto cleanup;
      }
      else if(n < 0) {
        fprintf(stderr, "Error in ClientSession::write_tls\n");
        goto cleanup;
      }

      result = write_all(sockfd, buf, n);
      if(result != 0) {
        goto cleanup;
      }
    }
  }

  ret = 0;
cleanup:
  if(epollfd > 0) {
    close(epollfd);
  }
  return ret;
}

int
do_request(const struct rustls_client_config *client_config,
           const char *hostname, const char *path)
{
  int ret = 1;
  int sockfd = make_conn(hostname);
  if(sockfd < 0) {
    // No perror because make_conn printed error already.
    goto cleanup;
  }

  struct rustls_client_session *client_session = NULL;
  rustls_result result =
    rustls_client_session_new(client_config, hostname, &client_session);
  if(result != RUSTLS_RESULT_OK) {
    goto cleanup;
  }

  ret = send_request_and_read_response(sockfd, client_session, hostname, path);
  if(ret != RUSTLS_RESULT_OK) {
    goto cleanup;
  }

  ret = 0;

cleanup:
  rustls_client_session_free(client_session);
  if(sockfd > 0) {
    close(sockfd);
  }
  return ret;
}

int
main(int argc, const char **argv)
{
  int ret = 1;
  int result = 1;
  if(argc <= 2) {
    fprintf(stderr,
            "usage: %s hostname path\n\n"
            "Connect to a host via HTTPS on port 443, make a request for the\n"
            "given path, and emit response to stdout.\n",
            argv[0]);
    return 1;
  }
  const char *hostname = argv[1];
  const char *path = argv[2];

  const struct rustls_client_config *client_config =
    rustls_client_config_new();

  int i;
  for(i = 0; i < 3; i++) {
    result = do_request(client_config, hostname, path);
    if(result != 0) {
      goto cleanup;
    }
  }

  // Success!
  ret = 0;

cleanup:
  rustls_client_config_free(client_config);
  return ret;
}