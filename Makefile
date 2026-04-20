# Makefile
AMXX_DIR = amxmodx-master
HLSDK_DIR = hlsdk-master
METAMOD_DIR = metamod-hl1-master

INCLUDES = -I. \
           -I$(AMXX_DIR)/public \
           -I$(AMXX_DIR)/public/amxmodx \
           -I$(AMXX_DIR)/public/sdk \
           -I$(AMXX_DIR)/amxmodx \
           -I$(METAMOD_DIR) \
           -I$(HLSDK_DIR)/common \
           -I$(HLSDK_DIR)/engine \
           -I$(HLSDK_DIR)/dlls \
           -I$(HLSDK_DIR)/pm_shared

CFLAGS = -m32 -fPIC -shared -O2 -Wall -Wno-narrowing -Wno-write-strings

SRC = bsp_module.cpp \
      $(AMXX_DIR)/public/sdk/amxxmodule.cpp

all:
	g++ $(CFLAGS) $(INCLUDES) $(SRC) -o BSP-Core_amxx_i386.so
