/*
  omnimsg.c
  Omni Messenger (omnimsg) - minimal serverless LAN messenger over UDP broadcast.

  Stage 1:
    - text messages only
    - broadcast to local subnet (no router traversal)
    - Windows/macOS/Linux/X68000 (elf2x68k + libsocket)

  X68000 notes:
    - poll() is planned but not available yet.
    - receive is non-blocking using SO_SOCKLENRECV (getsockopt).
    - stdin input is blocking (baseline). Once poll() exists, multiplex stdin+socket.
    - sleep uses IOCS _ONTIME (1/100 sec) via TRAP #15 (see user's platform.h style).
*/

#include "net_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
  #include <conio.h>
  #include <windows.h>
#else
  #include <unistd.h>
  #if !defined(__human68k__)
    #include <fcntl.h>
  #endif
#endif

#if defined(__human68k__)
  #include <x68k/iocs.h>
#endif

#define DEFAULT_PORT 24250
#define MAX_NICK 32
#define MAX_TEXT 512
#define MAX_PKT  768

/* ---------------- timing / sleep ---------------- */

#if defined(__human68k__)
static inline uint32_t x68k_ontime_cs(void) {
  /* IOCS _ONTIME (D0=0x7F) returns centiseconds (1/100 sec) in D0 */
  uint32_t cs;
  __asm__ volatile(
    "moveq  #0x7F,%%d0 \n\t"
    "trap   #15        \n\t"
    "move.l %%d0,%0    \n\t"
    : "=d"(cs)
    :
    : "d0","d1","a0","cc","memory"
  );
  return cs;
}
#endif

static void tiny_sleep_ms(int ms) {
  if (ms <= 0) {
    return;
  }
#if defined(_WIN32)
  Sleep((DWORD)ms);
#elif defined(__human68k__)
  /* Busy-wait using 1/100 sec tick. (10ms resolution) */
  {
    uint32_t start = x68k_ontime_cs();
    uint32_t ticks = (uint32_t)((ms + 9) / 10); /* round up to centiseconds */
    if (ticks == 0) {
      ticks = 1;
    }
    while ((uint32_t)(x68k_ontime_cs() - start) < ticks) {
      /* busy */
    }
  }
#else
  usleep((useconds_t)(ms * 1000));
#endif
}

/* ---------------- usage ---------------- */


static const char *prog_basename(const char *argv0) {
  /* Extract filename from argv[0] which may be a full path.
     Handles '/', '\', and drive separators like 'A:'. */
  const char *base = argv0 ? argv0 : "omnimsg";
  for (const char *p = base; p && *p; ++p) {
    if (*p == '/' || *p == '\\' || *p == ':') {
      base = p + 1;
    }
  }
  return (base && *base) ? base : "omnimsg";
}

static void usage(const char *prog) {
  const char *app = prog_basename(prog);
  fprintf(stderr,
    "Omni Messenger (omnimsg) - minimal serverless LAN chat\n\n"
    "Usage: %s [options]\n\n"
    "Options:\n"
    "  --nick <name>        nickname (default: anon)\n"
    "  --port <port>        UDP port (default: 24250)\n"
    "  --broadcast <ip>     broadcast IP (default: 255.255.255.255)\n"
    "  --send <text>        send one message and exit\n"
    "  --help               show this help\n",
    app);
}

static int parse_ipv4(const char *s, uint32_t *out_addr_be) {
  if (!s || !out_addr_be) {
    return -1;
  }
  /* inet_addr() returns INADDR_NONE on failure, but 255.255.255.255 is also INADDR_NONE. */
  {
    uint32_t a = (uint32_t)inet_addr(s);
    if (a == (uint32_t)INADDR_NONE && strcmp(s, "255.255.255.255") != 0) {
      return -1;
    }
    *out_addr_be = a;
    return 0;
  }
}

/* ---------------- protocol (Stage 1) ----------------
   Packet: OM1|<nick>|<text>
*/

static void format_packet(char *out, int out_size,
                          const char *nick, const char *text) {
  if (!out || out_size <= 0) {
    return;
  }
  if (!nick) {
    nick = "anon";
  }
  if (!text) {
    text = "";
  }

  {
    char tmp[MAX_TEXT];
    int j = 0;
    for (int i = 0; text[i] && j < (int)sizeof(tmp) - 1; i++) {
      char c = text[i];
      if (c == '\r' || c == '\n') {
        continue;
      }
      tmp[j++] = c;
    }
    tmp[j] = '\0';
    snprintf(out, (size_t)out_size, "OM1|%s|%s", nick, tmp);
  }
}

static int parse_packet(const char *pkt,
                        char *nick_out, int nick_out_size,
                        char *text_out, int text_out_size) {
  if (!pkt || !nick_out || !text_out || nick_out_size <= 0 || text_out_size <= 0) {
    return -1;
  }
  nick_out[0] = '\0';
  text_out[0] = '\0';

  if (strncmp(pkt, "OM1|", 4) != 0) {
    return -1;
  }

  const char *p = pkt + 4;
  const char *bar = strchr(p, '|');
  if (!bar) {
    return -1;
  }

  {
    int nlen = (int)(bar - p);
    if (nlen <= 0) {
      return -1;
    }
    if (nlen >= nick_out_size) {
      nlen = nick_out_size - 1;
    }
    memcpy(nick_out, p, (size_t)nlen);
    nick_out[nlen] = '\0';
  }

  p = bar + 1;
  {
    int tlen = (int)strlen(p);
    if (tlen >= text_out_size) {
      tlen = text_out_size - 1;
    }
    memcpy(text_out, p, (size_t)tlen);
    text_out[tlen] = '\0';
  }

  return 0;
}

/* ---------------- stdin handling (best-effort) ---------------- */

#if !defined(__human68k__)

static int stdin_make_nonblock_posix(int *old_flags) {
#if defined(_WIN32)
  (void)old_flags;
  return 0;
#else
  if (!old_flags) {
    return -1;
  }
  int flags = fcntl(0, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  *old_flags = flags;
  if (fcntl(0, F_SETFL, flags | O_NONBLOCK) < 0) {
    return -1;
  }
  return 0;
#endif
}

static void stdin_restore_flags_posix(int old_flags) {
#if defined(_WIN32)
  (void)old_flags;
#else
  if (old_flags >= 0) {
    fcntl(0, F_SETFL, old_flags);
  }
#endif
}

static int stdin_poll_line(char *out, int out_size) {
#if defined(_WIN32)
  static char buf[MAX_TEXT];
  static int len = 0;

  while (_kbhit()) {
    int c = _getch();

    if (c == 3) { /* Ctrl+C */
      return -2;
    }

    if (c == '\r' || c == '\n') {
      putchar('\n');
      buf[len] = '\0';
      snprintf(out, (size_t)out_size, "%s", buf);
      len = 0;
      return 1;
    }

    if (c == 8) { /* backspace */
      if (len > 0) {
        len--;
        fputs("\b \b", stdout);
      }
      continue;
    }

    if (c >= 32 && c <= 126) {
      if (len < (int)sizeof(buf) - 1) {
        buf[len++] = (char)c;
        putchar((char)c);
      }
    }
  }

  return 0;
#else
  static char buf[MAX_TEXT];
  static int len = 0;

  char tmp[128];
  ssize_t n = read(0, tmp, sizeof(tmp));
  if (n < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      return 0;
    }
    return -1;
  }
  if (n == 0) {
    return 0;
  }

  for (ssize_t i = 0; i < n; i++) {
    char c = tmp[i];
    if (c == '\n') {
      buf[len] = '\0';
      snprintf(out, (size_t)out_size, "%s", buf);
      len = 0;
      return 1;
    }
    if (c == '\r') {
      continue;
    }
    if (len < (int)sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
  return 0;
#endif
}

#endif /* !__human68k__ */

/* ---------------- main ---------------- */

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  const char *nick = "anon";
  int port = DEFAULT_PORT;
  const char *bcast_ip = "255.255.255.255";
  const char *send_once = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    }
    if ((strcmp(argv[i], "--nick") == 0 || strcmp(argv[i], "-n") == 0) && i + 1 < argc) {
      nick = argv[++i];
      continue;
    }
    if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
      port = atoi(argv[++i]);
      continue;
    }
    if ((strcmp(argv[i], "--broadcast") == 0 || strcmp(argv[i], "-b") == 0) && i + 1 < argc) {
      bcast_ip = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--send") == 0 && i + 1 < argc) {
      send_once = argv[++i];
      continue;
    }

    fprintf(stderr, "Unknown/invalid argument: %s\n", argv[i]);
    usage(argv[0]);
    return 1;
  }

  if (net_init() != 0) {
    fprintf(stderr, "net_init() failed\n");
    return 1;
  }

  sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock == INVALID_SOCKET) {
    fprintf(stderr, "socket() failed: %s\n", net_last_error_str());
    net_cleanup();
    return 1;
  }

  /* Best-effort socket options (guarded for X68000/libsocket). */
  {
    int yes = 1;

    #ifdef SO_REUSEADDR
      (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, (socklen_t)sizeof(yes));
    #endif

    #ifdef SO_REUSEPORT
      (void)setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, (socklen_t)sizeof(yes));
    #endif

    #ifdef SO_BROADCAST
      (void)setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, (socklen_t)sizeof(yes));
    #endif
  }

  /* Bind local port */
  {
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons((unsigned short)port);

    if (bind(sock, (struct sockaddr *)&local, (socklen_t)sizeof(local)) == SOCKET_ERROR) {
      fprintf(stderr, "bind() failed: %s\n", net_last_error_str());
      sock_close(sock);
      net_cleanup();
      return 1;
    }
  }

  /* Broadcast destination */
  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons((unsigned short)port);
  {
    uint32_t addr_be = 0;
    if (parse_ipv4(bcast_ip, &addr_be) != 0) {
      fprintf(stderr, "Invalid broadcast IP: %s\n", bcast_ip);
      sock_close(sock);
      net_cleanup();
      return 1;
    }
    dest.sin_addr.s_addr = addr_be;
  }

  (void)sock_set_nonblock(sock); /* X68000 uses SO_SOCKLENRECV polling anyway */

  /* Send-once mode */
  if (send_once) {
    char pkt[MAX_PKT];
    format_packet(pkt, (int)sizeof(pkt), nick, send_once);
    int len = (int)strlen(pkt);
    int sent = (int)sendto(sock, pkt, (size_t)len, 0,
                           (struct sockaddr *)&dest, (socklen_t)sizeof(dest));
    if (sent < 0) {
      fprintf(stderr, "sendto() failed: %s\n", net_last_error_str());
    }
    sock_close(sock);
    net_cleanup();
    return (sent < 0) ? 1 : 0;
  }

  printf("Omni Messenger (omnimsg) - LAN chat (UDP broadcast)\n");
  printf("  nick      : %s\n", nick);
  printf("  port      : %d\n", port);
  printf("  broadcast : %s\n", bcast_ip);
  printf("\nType a message and press Enter to broadcast.\n");
  printf("Commands: /quit, /help\n\n");

#if !defined(__human68k__)
  int old_stdin_flags = -1;
  (void)stdin_make_nonblock_posix(&old_stdin_flags);
#endif

  printf("> ");

  for (;;) {
    /* Drain pending packets */
    for (;;) {
      char buf[MAX_PKT];
      struct sockaddr_in from;
      int from_len = (int)sizeof(from);

      int n = udp_recvfrom_nb(sock, buf, (int)sizeof(buf) - 1, &from, &from_len);
      if (n < 0) {
        fprintf(stderr, "\nrecvfrom() failed: %s\n", net_last_error_str());
        break;
      }
      if (n == 0) {
        break;
      }
      buf[n] = '\0';

      {
        char from_nick[MAX_NICK];
        char from_text[MAX_TEXT];
        if (parse_packet(buf, from_nick, (int)sizeof(from_nick),
                         from_text, (int)sizeof(from_text)) == 0) {
          printf("\n[%s] %s: %s\n> ",
                 inet_ntoa(from.sin_addr), from_nick, from_text);
        } else {
          printf("\n[%s] %s\n> ", inet_ntoa(from.sin_addr), buf);
        }
      }
    }

    /* Read user input */
#if defined(__human68k__)
    /* Baseline: blocking input. */
    {
      char line[MAX_TEXT];
      if (!fgets(line, sizeof(line), stdin)) {
        break;
      }
      /* strip CR/LF */
      {
        size_t L = strlen(line);
        while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r')) {
          line[L - 1] = '\0';
          L--;
        }
      }

      if (strcmp(line, "/quit") == 0) {
        break;
      }
      if (strcmp(line, "/help") == 0) {
        printf("Commands: /quit, /help\n");
        printf("> ");
        continue;
      }
      if (line[0] == '\0') {
        printf("> ");
        continue;
      }

      char pkt[MAX_PKT];
      format_packet(pkt, (int)sizeof(pkt), nick, line);
      int len = (int)strlen(pkt);
      (void)sendto(sock, pkt, (size_t)len, 0,
                   (struct sockaddr *)&dest, (socklen_t)sizeof(dest));
      printf("> ");
    }
#else
    {
      char line[MAX_TEXT];
      int r = stdin_poll_line(line, (int)sizeof(line));
      if (r == -2) { /* Ctrl+C on Windows */
        break;
      }
      if (r < 0) {
        fprintf(stderr, "\nstdin read failed\n");
        break;
      }
      if (r == 1) {
        if (strcmp(line, "/quit") == 0) {
          break;
        }
        if (strcmp(line, "/help") == 0) {
          printf("Commands: /quit, /help\n> ");
          continue;
        }
        if (line[0] != '\0') {
          char pkt[MAX_PKT];
          format_packet(pkt, (int)sizeof(pkt), nick, line);
          int len = (int)strlen(pkt);
          (void)sendto(sock, pkt, (size_t)len, 0,
                       (struct sockaddr *)&dest, (socklen_t)sizeof(dest));
          printf("> ");
        } else {
          printf("> ");
        }
      }
    }
#endif

    tiny_sleep_ms(10);
  }

#if !defined(__human68k__)
  stdin_restore_flags_posix(old_stdin_flags);
#endif

  sock_close(sock);
  net_cleanup();
  printf("\nBye.\n");
  return 0;
}
