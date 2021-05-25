PRGNAME		= vvvvvv
CC		= /opt/trimui-toolchain/bin/arm-linux-gcc
CXX		= /opt/trimui-toolchain/bin/arm-linux-g++
STRIP		= /opt/trimui-toolchain/bin/arm-linux-strip
SYSROOT		= /opt/trimui-toolchain/arm-buildroot-linux-gnueabi/sysroot
SDL_CFLAGS	= $(shell $(SYSROOT)/usr/bin/sdl-config --cflags)

SRCDIR		= ./src/ ./lodepng ./physfs ./tinyxml
VPATH		= $(SRCDIR)
SRC_C		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_CP		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.cpp))
OBJ_C		= $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_CP		= $(notdir $(patsubst %.cpp, %.o, $(SRC_CP)))
OBJS		= $(OBJ_C) $(OBJ_CP)

LTO		= -flto

CFLAGS		= -O2 -DPHYSFS_SUPPORTS_DEFAULT=0 -DPHYSFS_SUPPORTS_ZIP=1 -DGAME_BITDEPTH=16
CFLAGS		+= -I./src/ -I./lodepng -I./physfs -I./tinyxml $(SDL_CFLAGS) $(LTO)
CFLAGS		+= -fdata-sections -ffunction-sections -fno-PIC -fomit-frame-pointer
CXXFLAGS	= $(CFLAGS) -std=gnu++11
LDFLAGS     = -nodefaultlibs -lc -lstdc++ -lgcc -lgcc_s -lm -lSDL -lSDL_image
LDFLAGS		+= -lSDL_ttf -ldl
LDFLAGS		+= -lz -lmad
LDFLAGS		+= -Wl,-Bstatic -lSDL_mixer -lvorbisidec -lvorbisfile -lvorbis -logg -Wl,-Bdynamic
LDFLAGS		+= -no-pie -Wl,--as-needed -Wl,--gc-sections

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
