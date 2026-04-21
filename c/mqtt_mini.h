/* 极简 MQTT 3.1.1 客户端：仅支持 CONNECT + PUBLISH(QoS0) + DISCONNECT。
 * 跨平台（POSIX / Winsock2），无外部依赖。
 */
#ifndef MQTT_MINI_H
#define MQTT_MINI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mqtt_mini mqtt_mini_t;

/* 连接并发送 CONNECT，阻塞等待 CONNACK。失败返回 NULL。
 * username / password 可为 NULL。
 */
mqtt_mini_t *mqtt_mini_connect(const char *host,
                               uint16_t    port,
                               const char *client_id,
                               const char *username,
                               const char *password);

/* 发送 PUBLISH QoS 0。成功返回 0。 */
int mqtt_mini_publish(mqtt_mini_t   *m,
                      const char    *topic,
                      const uint8_t *payload,
                      size_t         payload_len);

/* 发送 DISCONNECT 并关闭 socket，free 结构体。 */
void mqtt_mini_close(mqtt_mini_t *m);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MINI_H */
