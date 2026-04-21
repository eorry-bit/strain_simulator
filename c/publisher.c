/* C 模拟设备 publisher：用 nanopb 编码 StrainTelemetry，写到 stdout。
 * 配合 send.sh 通过 mosquitto_pub -s 发送到 broker。
 *
 * 用法:
 *   ./publisher [--ch-no N] [--interval-ms M] [--count K] [--no-temp] [--no-battery]
 *   默认: ch_no=4, interval_ms=10, values=32, 包含所有可选字段
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "pb_encode.h"
#include "strain.pb.h"

static uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static float randf(float lo, float hi) {
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

int main(int argc, char **argv) {
    int      ch_no             = 4;
    int      interval_ms       = 10;
    int      n_values          = 32;
    int      include_temp      = 1;
    int      include_battery   = 1;
    int      include_mode      = 1;
    int      include_status    = 1;
    int      skip_wrapper      = 0;   /* 故意漏外层信封  */
    int      skip_meta         = 0;   /* 不填 ch_no / sample_interval_ms */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ch-no") && i+1 < argc)       ch_no = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interval-ms") && i+1 < argc) interval_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--count") && i+1 < argc)  n_values = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-temp"))              include_temp = 0;
        else if (!strcmp(argv[i], "--no-battery"))           include_battery = 0;
        else if (!strcmp(argv[i], "--no-mode"))              include_mode = 0;
        else if (!strcmp(argv[i], "--no-status"))            include_status = 0;
        else if (!strcmp(argv[i], "--skip-wrapper"))         skip_wrapper = 1;
        else if (!strcmp(argv[i], "--skip-meta"))            skip_meta = 1;
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 2;
        }
    }
    if (n_values < 1 || n_values > 64) {
        fprintf(stderr, "n_values out of range (1..64): %d\n", n_values);
        return 2;
    }

    srand((unsigned)time(NULL));

    /* ---- 关键：正确初始化 + 必须设置 has_strainBatch = true ---- */
    shm_strain_v1_StrainTelemetry telem = shm_strain_v1_StrainTelemetry_init_zero;
    telem.has_strainBatch = 1;                                  /* 厂家最常见漏掉的一行 */

    shm_strain_v1_StrainTelemetry_StrainBatch *b = &telem.strainBatch;
    b->timestamp           = now_ms();
    if (!skip_meta) {
        b->ch_no              = (uint32_t)ch_no;
        b->sample_interval_ms = (uint32_t)interval_ms;
    }
    /* 若 skip_meta: ch_no/sample_interval_ms 保持 0，
     * proto3 非 optional 的 0 不会上线，接收端解出来就是"默认 0"——
     * 这正是厂家消息的表现。*/

    b->values_count = (pb_size_t)n_values;
    float base = randf(-50.0f, 50.0f);
    for (int i = 0; i < n_values; i++) {
        b->values[i] = base + 5.0f * sinf((float)i / 8.0f) + randf(-0.5f, 0.5f);
    }

    if (include_temp)    { b->has_temperature = 1; b->temperature = randf(20.0f, 35.0f); }
    if (include_battery) { b->has_battery     = 1; b->battery     = 60 + rand() % 41; }
    if (include_mode)    { b->has_mode        = 1; b->mode        = 1; }
    if (include_status)  { b->has_status      = 1; b->status      = 0; }

    uint8_t  buf[512];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    bool ok;
    if (skip_wrapper) {
        /* 模拟厂家错误：直接序列化 StrainBatch，不套外层 */
        ok = pb_encode(&stream, shm_strain_v1_StrainTelemetry_StrainBatch_fields, b);
    } else {
        ok = pb_encode(&stream, shm_strain_v1_StrainTelemetry_fields, &telem);
    }

    if (!ok) {
        fprintf(stderr, "encode failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
    }

    /* stderr 打印诊断，stdout 写 payload 字节 */
    fprintf(stderr, "[c-pub] %zu bytes  first=0x%02x  ts=%llu  ch=%u  n=%d%s\n",
            stream.bytes_written, buf[0],
            (unsigned long long)b->timestamp, b->ch_no, n_values,
            skip_wrapper ? "  (NO WRAPPER)" : "");

    if (fwrite(buf, 1, stream.bytes_written, stdout) != stream.bytes_written) {
        perror("fwrite");
        return 1;
    }
    return 0;
}
