PRGNAME		= vvvvvv
CC			= $(CROSS_COMPILE)gcc
CXX			= $(CROSS_COMPILE)g++
STRIP		= $(CROSS_COMPILE)strip
SYSROOT		= /opt/miyoomini-toolchain/usr/arm-linux-gnueabihf/sysroot
SDL_CFLAGS	= $(shell $(SYSROOT)/usr/bin/sdl-config --cflags)

SRCDIR		= ./src/ ./lodepng ./physfs ./tinyxml
VPATH		= $(SRCDIR)
SRC_C		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_CP		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.cpp))
OBJ_C		= $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_CP		= $(notdir $(patsubst %.cpp, %.o, $(SRC_CP)))
OBJS		= $(OBJ_C) $(OBJ_CP)

LTO			= -flto

CFLAGS		= -Ofast -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -march=armv7ve+simd -Wall
CFLAGS		+= -DPHYSFS_SUPPORTS_DEFAULT=0 -DPHYSFS_SUPPORTS_ZIP=1 -DGAME_BITDEPTH=32
CFLAGS		+= -I./src/ -I./lodepng -I./physfs -I./tinyxml $(SDL_CFLAGS) $(LTO)
CFLAGS		+= -fno-PIC -fomit-frame-pointer
CFLAGS		+= -fdata-sections -ffunction-sections
CXXFLAGS	= $(CFLAGS) -std=gnu++11
LDFLAGS     	= -nodefaultlibs -lc -lstdc++ -lgcc -lgcc_s -lm -lSDL -lSDL_image -lSDL_mixer -lz # -lmad
LDFLAGS     	+= -lmi_sys -lmi_gfx -lpthread
#LDFLAGS	+= -Wl,-Bstatic -lSDL_mixer -lvorbisfile -lvorbis -logg -Wl,-Bdynamic
LDFLAGS		+= -no-pie -Wl,--as-needed -Wl,--gc-sections -s

# Rules to make executable
$(PRGNAME): $(OBJS)  
	$(CC) $(CFLAGS) -o $(PRGNAME) $^ $(LDFLAGS)
	$(STRIP) $(PRGNAME)

$(OBJ_C) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_CP) : %.o : %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
#	$(CXX) $(CXXFLAGS) -c -S -fverbose-asm $<

clean:
	rm -f $(PRGNAME) *.o
