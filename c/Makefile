CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS = -I. -Inanopb_runtime -DPB_ENABLE_MALLOC=0

# Windows (MinGW) 需要链接 ws2_32; POSIX 下是空
ifeq ($(OS),Windows_NT)
  LDLIBS_MQTT = -lws2_32
  EXE = .exe
else
  LDLIBS_MQTT =
  EXE =
endif

NANOPB_SRC = nanopb_runtime/pb_common.c nanopb_runtime/pb_encode.c nanopb_runtime/pb_decode.c

# publisher       : 只输出 protobuf 字节到 stdout（配合 send.sh 用，需要 mosquitto_pub）
# publisher_mqtt  : 自带 MQTT 客户端，单体可执行（推荐给厂家，Windows 友好）
all: publisher$(EXE) publisher_mqtt$(EXE)

publisher$(EXE): publisher.c strain.pb.c $(NANOPB_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -lm -o $@

publisher_mqtt$(EXE): publisher_mqtt.c mqtt_mini.c strain.pb.c $(NANOPB_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -lm $(LDLIBS_MQTT) -o $@

clean:
	rm -f publisher publisher.exe publisher_mqtt publisher_mqtt.exe

.PHONY: all clean
