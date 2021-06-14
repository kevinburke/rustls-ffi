enum crustls_demo_result
{
  CRUSTLS_DEMO_OK,
  CRUSTLS_DEMO_ERROR,
  CRUSTLS_DEMO_AGAIN,
  CRUSTLS_DEMO_EOF,
  CRUSTLS_DEMO_CLOSE_NOTIFY,
};

typedef struct conndata_t {
  int fd;
  const char *verify_arg;
  struct rustls_connection *rconn;
  char *data_from_client;
  size_t data_len;
  size_t data_capacity;
} conndata_t;

void
print_error(char *prefix, rustls_result result);

int
write_all(int fd, const char *buf, int n);

enum crustls_demo_result
nonblock(int sockfd);

int
read_cb(void *userdata, uint8_t *buf, uintptr_t len, uintptr_t *out_n);

int
write_cb(void *userdata, const uint8_t *buf, uintptr_t len, uintptr_t *out_n);
