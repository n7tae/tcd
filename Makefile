#Copyright (C) 2021 by Thomas A. Early, N7TAE

include configure.mk

# If you are going to change this path, you will
# need to update the systemd service script
BINDIR = /usr/local/bin

GCC = g++

ifeq ($(debug), true)
CFLAGS = -ggdb3 -W -Werror -Icodec2 -MMD -MD -std=c++11
else
CFLAGS = -W -Werror -Icodec2 -MMD -MD -std=c++11
endif

LDFLAGS = -lftd2xx -lmd380_vocoder -pthread

SRCS = $(wildcard *.cpp) $(wildcard codec2/*.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)
EXE = tcd

$(EXE) : $(OBJS)
	$(GCC) $(OBJS) $(LDFLAGS) -o $@ -Xlinker --section-start=.firmware=0x0800C000 -Xlinker  --section-start=.sram=0x20000000

%.o : %.cpp
	$(GCC) $(CFLAGS) -c $< -o $@

clean :
	$(RM) $(EXE) $(OBJS) $(DEPS)

-include $(DEPS)

# The install and uninstall targets need to be run by root
install : $(EXE)
	cp $(EXE) $(BINDIR)
	cp systemd/$(EXE).service /etc/systemd/system/
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
