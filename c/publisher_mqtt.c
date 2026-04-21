/* 单体 C 设备模拟器：nanopb 编码 StrainTelemetry + 通过内置 MQTT 客户端发送。
 *
 * 跨 Windows（Winsock2）/ Linux / macOS，无外部依赖（nanopb + mqtt_mini 自包含）。
 *
 * 用法:
 *   publisher_mqtt [--host H] [--port P] [--token T] [--sn SN] [--topic T]
 *                  [--ch-no N] [--interval-ms M] [--count K]
 *                  [--no-temp] [--no-battery] [--no-mode] [--no-status]
 *                  [--skip-wrapper] [--skip-meta]
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
  #include <windows.h>
  #define SLEEP_MS(ms) Sleep((DWORD)(ms))
#else
  #include <sys/time.h>
  static void SLEEP_MS(long ms) {
      struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
      nanosleep(&ts, NULL);
  }
#endif

#include "pb_encode.h"
#include "strain.pb.h"
#include "mqtt_mini.h"

static uint64_t now_ms(void) {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    /* 100ns intervals since 1601 -> ms since 1970 */
    return (t / 10000ULL) - 11644473600000ULL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
#endif
}

static float randf(float lo, float hi) {
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

/* 构造一帧 telemetry payload，返回写入 buf 的字节数，失败返回 0 */
static size_t build_telemetry(uint8_t *buf, size_t bufsz,
                              int ch_no, int interval_ms, int n_values,
                              int include_temp, int include_battery,
                              int include_mode, int include_status,
                              int skip_wrapper, int skip_meta)
{
    shm_strain_v1_StrainTelemetry telem = shm_strain_v1_StrainTelemetry_init_zero;
    telem.has_strainBatch = 1;

    shm_strain_v1_StrainTelemetry_StrainBatch *b = &telem.strainBatch;
    b->timestamp = now_ms();
    if (!skip_meta) {
        b->ch_no              = (uint32_t)ch_no;
        b->sample_interval_ms = (uint32_t)interval_ms;
    }

    b->values_count = (pb_size_t)n_values;
    float base = randf(-50.0f, 50.0f);
    for (int i = 0; i < n_values; i++) {
        b->values[i] = base + 5.0f * sinf((float)i / 8.0f) + randf(-0.5f, 0.5f);
    }
    if (include_temp)    { b->has_temperature = 1; b->temperature = randf(20.0f, 35.0f); }
    if (include_battery) { b->has_battery     = 1; b->battery     = (uint32_t)(60 + rand() % 41); }
    if (include_mode)    { b->has_mode        = 1; b->mode        = 1; }
    if (include_status)  { b->has_status      = 1; b->status      = 0; }

    pb_ostream_t stream = pb_ostream_from_buffer(buf, bufsz);
    bool ok = skip_wrapper
        ? pb_encode(&stream, shm_strain_v1_StrainTelemetry_StrainBatch_fields, b)
        : pb_encode(&stream, shm_strain_v1_StrainTelemetry_fields, &telem);
    if (!ok) {
        fprintf(stderr, "encode failed: %s\n", PB_GET_ERROR(&stream));
        return 0;
    }
    return stream.bytes_written;
}

int main(int argc, char **argv) {
    const char *host   = "gw.mqtt.inteagle.com";
    int         port   = 1883;
    const char *token  = "2rdavok650n1c9o6xyai";
    const char *sn     = NULL;
    const char *topic  = NULL;                  /* 若 NULL 用 strain/<sn>/telemetry */
    int         ch_no  = 4;
    int         interval_ms = 10;
    int         n_values    = 32;
    int         count   = 1;
    int         interval_ms_between = 1000;
    int         include_temp = 1, include_battery = 1, include_mode = 1, include_status = 1;
    int         skip_wrapper = 0, skip_meta = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--host")        && i+1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port")        && i+1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--token")       && i+1 < argc) token = argv[++i];
        else if (!strcmp(argv[i], "--sn")          && i+1 < argc) sn = argv[++i];
        else if (!strcmp(argv[i], "--topic")       && i+1 < argc) topic = argv[++i];
        else if (!strcmp(argv[i], "--ch-no")       && i+1 < argc) ch_no = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interval-ms") && i+1 < argc) interval_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--values")      && i+1 < argc) n_values = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--count")       && i+1 < argc) count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sleep-ms")    && i+1 < argc) interval_ms_between = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-temp"))    include_temp = 0;
        else if (!strcmp(argv[i], "--no-battery")) include_battery = 0;
        else if (!strcmp(argv[i], "--no-mode"))    include_mode = 0;
        else if (!strcmp(argv[i], "--no-status"))  include_status = 0;
        else if (!strcmp(argv[i], "--skip-wrapper")) skip_wrapper = 1;
        else if (!strcmp(argv[i], "--skip-meta"))    skip_meta = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("see comment at top of publisher_mqtt.c for options\n");
            return 0;
        }
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2;
        }
    }

    if (!sn) sn = token;
    char topic_buf[256];
    if (!topic) {
        snprintf(topic_buf, sizeof(topic_buf), "strain/%s/telemetry", sn);
        topic = topic_buf;
    }

    srand((unsigned)time(NULL));

    /* client id: strain-pub-<pid or tick> */
    char client_id[64];
#ifdef _WIN32
    snprintf(client_id, sizeof(client_id), "strain-pub-%lu", (unsigned long)GetCurrentProcessId());
#else
    snprintf(client_id, sizeof(client_id), "strain-pub-%ld", (long)time(NULL));
#endif

    fprintf(stderr, "[pub-mqtt] connecting %s:%d  client_id=%s  topic=%s\n",
            host, port, client_id, topic);

    mqtt_mini_t *m = mqtt_mini_connect(host, (uint16_t)port, client_id, token, NULL);
    if (!m) {
        fprintf(stderr, "[pub-mqtt] connect failed\n");
        return 1;
    }
    fprintf(stderr, "[pub-mqtt] connected\n");

    uint8_t buf[512];
    int sent = 0;
    for (int i = 0; i < count; i++) {
        size_t n = build_telemetry(buf, sizeof(buf),
                                   ch_no, interval_ms, n_values,
                                   include_temp, include_battery,
                                   include_mode, include_status,
                                   skip_wrapper, skip_meta);
        if (n == 0) { mqtt_mini_close(m); return 1; }

        if (mqtt_mini_publish(m, topic, buf, n) != 0) {
            fprintf(stderr, "[pub-mqtt] publish #%d failed\n", i+1);
            mqtt_mini_close(m);
            return 1;
        }
        fprintf(stderr, "[pub-mqtt] #%d sent %zu bytes  first=0x%02x\n",
                i+1, n, buf[0]);
        sent++;
        if (i+1 < count) SLEEP_MS(interval_ms_between);
    }

    mqtt_mini_close(m);
    fprintf(stderr, "[pub-mqtt] done, %d/%d sent\n", sent, count);
    return 0;
}
