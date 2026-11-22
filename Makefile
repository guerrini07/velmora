TARGET = velmora
OBJS = main_psp.o game.o render_gu.o

INCDIR =
CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
LIBDIR =
LDFLAGS =
LIBS = -lpspgum -lpspgu -lpspaudio -lpspctrl -lpspdisplay -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Cenere di Velmora

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
