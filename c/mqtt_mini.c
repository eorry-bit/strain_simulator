#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* 打开 getaddrinfo / struct addrinfo */
#endif

#include "mqtt_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  #define CLOSESOCK(s) closesocket(s)
  #define SEND(s, b, n)  send((s), (const char *)(b), (int)(n), 0)
  #define RECV(s, b, n)  recv((s), (char *)(b),       (int)(n), 0)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  #define CLOSESOCK(s) close(s)
  #define SEND(s, b, n)  send((s), (b), (n), 0)
  #define RECV(s, b, n)  recv((s), (b), (n), 0)
#endif

struct mqtt_mini {
    sock_t sock;
};

/* ---- 工具 ---- */

static int send_all(sock_t s, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        int n = (int)SEND(s, p, len);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(sock_t s, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        int n = (int)RECV(s, p, len);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* MQTT 变长剩余长度编码（最多 4 字节），返回写入字节数 */
static int encode_remlen(uint8_t *out, uint32_t v) {
    int i = 0;
    do {
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v) b |= 0x80;
        out[i++] = b;
    } while (v && i < 4);
    return i;
}

static uint8_t *put_str(uint8_t *p, const char *s) {
    uint16_t n = (uint16_t)strlen(s);
    *p++ = (uint8_t)(n >> 8);
    *p++ = (uint8_t)(n & 0xFF);
    memcpy(p, s, n);
    return p + n;
}

#ifdef _WIN32
static int wsa_inited = 0;
static int wsa_init_once(void) {
    if (wsa_inited) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    wsa_inited = 1;
    return 0;
}
#endif

/* ---- 公共 API ---- */

mqtt_mini_t *mqtt_mini_connect(const char *host,
                               uint16_t    port,
                               const char *client_id,
                               const char *username,
                               const char *password)
{
#ifdef _WIN32
    if (wsa_init_once() != 0) return NULL;
#endif

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        fprintf(stderr, "mqtt_mini: getaddrinfo(%s) failed\n", host);
        return NULL;
    }

    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == SOCK_INVALID) { freeaddrinfo(res); return NULL; }

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        fprintf(stderr, "mqtt_mini: connect(%s:%u) failed\n", host, (unsigned)port);
        CLOSESOCK(s); freeaddrinfo(res);
        return NULL;
    }
    freeaddrinfo(res);

    /* ---- 构造 CONNECT 包 ---- */
    uint8_t flags = 0x02;  /* Clean session */
    size_t cid_len = strlen(client_id);
    size_t payload_len = 2 + cid_len;
    if (username) { flags |= 0x80; payload_len += 2 + strlen(username); }
    if (password) { flags |= 0x40; payload_len += 2 + strlen(password); }

    size_t var_header_len = 10;  /* protocol name(2+4) + level(1) + flags(1) + keepalive(2) */
    size_t remaining = var_header_len + payload_len;

    uint8_t packet[512];
    if (remaining + 5 > sizeof(packet)) {
        fprintf(stderr, "mqtt_mini: connect packet too large\n");
        CLOSESOCK(s); return NULL;
    }

    uint8_t *p = packet;
    *p++ = 0x10;                                        /* CONNECT */
    p += encode_remlen(p, (uint32_t)remaining);
    *p++ = 0x00; *p++ = 0x04;                           /* "MQTT" */
    memcpy(p, "MQTT", 4); p += 4;
    *p++ = 0x04;                                        /* MQTT 3.1.1 */
    *p++ = flags;
    *p++ = 0x00; *p++ = 0x3C;                           /* keepalive 60s */
    p = put_str(p, client_id);
    if (username) p = put_str(p, username);
    if (password) p = put_str(p, password);

    if (send_all(s, packet, (size_t)(p - packet)) != 0) {
        fprintf(stderr, "mqtt_mini: send CONNECT failed\n");
        CLOSESOCK(s); return NULL;
    }

    /* ---- CONNACK: 0x20, 0x02, <flags>, <rc> ---- */
    uint8_t ack[4];
    if (recv_all(s, ack, 4) != 0) {
        fprintf(stderr, "mqtt_mini: recv CONNACK failed\n");
        CLOSESOCK(s); return NULL;
    }
    if (ack[0] != 0x20 || ack[3] != 0x00) {
        fprintf(stderr, "mqtt_mini: CONNACK rejected (type=0x%02x rc=%u)\n", ack[0], ack[3]);
        CLOSESOCK(s); return NULL;
    }

    mqtt_mini_t *m = (mqtt_mini_t *)calloc(1, sizeof(*m));
    if (!m) { CLOSESOCK(s); return NULL; }
    m->sock = s;
    return m;
}

int mqtt_mini_publish(mqtt_mini_t   *m,
                      const char    *topic,
                      const uint8_t *payload,
                      size_t         payload_len)
{
    if (!m) return -1;
    size_t topic_len = strlen(topic);
    size_t remaining = 2 + topic_len + payload_len;

    /* fixed header(1) + remlen(max 4) + topic length(2) + topic */
    uint8_t header[512];
    if (topic_len > sizeof(header) - 7) return -1;

    uint8_t *p = header;
    *p++ = 0x30;                                        /* PUBLISH, QoS 0 */
    p += encode_remlen(p, (uint32_t)remaining);
    *p++ = (uint8_t)(topic_len >> 8);
    *p++ = (uint8_t)(topic_len & 0xFF);
    memcpy(p, topic, topic_len); p += topic_len;

    if (send_all(m->sock, header, (size_t)(p - header)) != 0) return -1;
    if (payload_len > 0 && send_all(m->sock, payload, payload_len) != 0) return -1;
    return 0;
}

void mqtt_mini_close(mqtt_mini_t *m)
{
    if (!m) return;
    uint8_t disc[2] = { 0xE0, 0x00 };                   /* DISCONNECT */
    send_all(m->sock, disc, 2);
    CLOSESOCK(m->sock);
    free(m);
}
