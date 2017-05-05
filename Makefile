W        = -Wall -Wextra -Wno-strict-aliasing
OPT      = -O2 -g
STD      = -std=c++11
INC      = -Iinc -Ihoytech-cpp -Iinc/websocketpp -Iinc/variant/include -Iinc/libev/
CXXFLAGS = $(STD) $(OPT) $(W) $(INC) -fPIC $(XCXXFLAGS)
CCFLAGS  = $(OPT) $(W) $(INC) -fPIC $(XCCFLAGS)
LDFLAGS  = $(XLDFLAGS)

PROGOBJS    = main.o websocket.o util.o config.o signalwatcher.o preloadwatcher.o event.o hoytech-cpp/timer.o cmd/base.o cmd/run.o cmd/ps.o cmd/ping.o cmd/get.o cmd/cat.o cmd/config.o


ifeq ($(wildcard hoytech-cpp/README.md),)
  $(info submodules not checked out. Run the following and then try make again:)
  $(info )
  $(info git submodule update --init)
  $(info )
  $(error git submodules not checked out)
endif


.PHONY: all clean realclean test
all: logp logp_preload.so

clean:
	rm -f *.o cmd/*.o hoytech-cpp/*.o *.so logp _buildinfo.h

realclean: clean
	rm -rf dist

_buildinfo.h:
	perl -e '$$v = `git describe --tags --match "logp-*"`; $$v =~ s/^logp-|\s*$$//g; print qq{#define LOGP_VERSION "$$v"\n}' > _buildinfo.h

logp: $(PROGOBJS) ev.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) ev.o $(PROGOBJS) -lz -lssl -lcrypto -lpthread -o $@

%.o: %.cpp inc/logp/*.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

cmd/*.o: inc/logp/cmd/*.h

main.o: _buildinfo.h inc/logp/cmd/*.h

logp_preload.so: logp_preload.c
	$(CC) $(CCFLAGS) -shared -fvisibility=hidden logp_preload.c -o $@

ev.o: ev.cpp inc/libev/*.c inc/libev/*.h
	$(CXX) -std=c++11 -w $(OPT) -Iinc/libev/ -fPIC -c $< -o $@
