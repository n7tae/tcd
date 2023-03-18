#Copyright (C) 2021 by Thomas A. Early, N7TAE

include tcd.mk

GCC = g++

ifeq ($(debug), true)
CFLAGS = -ggdb3 -W -Werror -Icodec2 -MMD -MD -std=c++11
else
CFLAGS = -W -Werror -Icodec2 -MMD -MD -std=c++11
endif

ifeq ($(swambe2), true)
CFLAGS+= -DUSE_SW_AMBE2
endif

LDFLAGS = -lftd2xx -limbe_vocoder -pthread

ifeq ($(swambe2), true)
LDFLAGS += -lmd380_vocoder
endif

SRCS = $(wildcard *.cpp) $(wildcard codec2/*.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)
EXE = tcd

$(EXE) : $(OBJS)
ifeq ($(swambe2), true)
	$(GCC) $(OBJS) $(LDFLAGS) -o $@ -Xlinker --section-start=.firmware=0x0800C000 -Xlinker  --section-start=.sram=0x20000000
else
	$(GCC) $(OBJS) $(LDFLAGS) -o $@
endif

%.o : %.cpp
	$(GCC) $(CFLAGS) -c $< -o $@

clean :
	$(RM) $(EXE) $(OBJS) $(DEPS)

-include $(DEPS)

# The install and uninstall targets need to be run by root
install : $(EXE)
	cp $(EXE) $(BINDIR)
	cp $(EXE).service /etc/systemd/system/
	systemctl enable $(EXE)
	systemctl daemon-reload
	systemctl start $(EXE)

uninstall :
	systemctl stop $(EXE)
	systemctl disable $(EXE)
	systemctl daemon-reload
	rm -f /etc/systemd/system/$(EXE).service
	rm -f $(BINDIR)/$(EXE)
	systemctl daemon-reload
