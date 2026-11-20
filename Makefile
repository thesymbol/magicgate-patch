TARGET = magicgate-patch
OBJS = main.o
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
PSP_FW_VERSION = 661
CFLAGS = -O2 -Wall -Wextra

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
