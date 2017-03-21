W        = -Wall -Wextra
OPT      = -O2 -g
STD      = -std=c++11
INC      = -Iinc -Ihoytech-cpp -Iinc/websocketpp -Iinc/variant/include
CXXFLAGS = $(STD) $(OPT) $(W) $(INC) -fPIC $(XCXXFLAGS)
LDFLAGS  = $(XLDFLAGS)

PROGOBJS    = main.o websocket.o util.o config.o signalwatcher.o event.o hoytech-cpp/timer.o cmd/base.o cmd/run.o cmd/ps.o cmd/ping.o


ifeq ($(wildcard hoytech-cpp/README.md),)
  $(info submodules not checked out. Run the following and then try make again:)
  $(info )
  $(info git submodule update --init)
  $(info )
  $(error git submodules not checked out)
endif


.PHONY: all clean realclean test
all: logp

clean:
	rm -f *.o cmd/*.o hoytech-cpp/*.o *.so logp _buildinfo.h

realclean: clean
	rm -rf dist

_buildinfo.h:
	perl -e '$$v = `git describe --tags --match "logp-*"`; $$v =~ s/^logp-|\s*$$//g; print qq{#define LOGP_VERSION "$$v"\n}' > _buildinfo.h

logp: $(PROGOBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PROGOBJS) -lz -lssl -lcrypto -lpthread -o $@

%.o: %.cpp inc/logp/*.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

cmd/*.o: inc/logp/cmd/*.h

main.o: _buildinfo.h inc/logp/cmd/*.h
