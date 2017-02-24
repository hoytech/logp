W        = -Wall -Wextra
OPT      = -O2 -g
STD      = -std=c++11
INC      = -Iinc -Iinc/websocketpp -Ilib/yaml-cpp/include
CXXFLAGS = $(STD) $(OPT) $(W) $(INC) -fPIC $(XCXXFLAGS)
LDFLAGS  =

BUNDLED_LIBS = lib/yaml-cpp/build/libyaml-cpp.a
PROGOBJS    = main.o websocket.o util.o config.o signalwatcher.o cmd/base.o cmd/run.o cmd/ps.o


ifeq ($(wildcard inc/protected_queue/protected_queue.h),)
  $(info submodules not checked out. Run the following and then try make again:)
  $(info )
  $(info git submodule update --init)
  $(info )
  $(error git submodules not checked out)
endif


## For distribution: -static-libgcc -static-libstdc++


.PHONY: all clean realclean test
all: logp

clean:
	rm -f *.o cmd/*.o *.so logp _buildinfo.h

realclean: clean
	rm -rf lib/yaml-cpp/build/

_buildinfo.h:
	perl -e '$$v = `git describe --tags --match "logp-*"`; $$v =~ s/^logp-|\s*$$//g; print qq{#define LOGP_VERSION "$$v"\n}' > _buildinfo.h

logp: $(PROGOBJS) $(BUNDLED_LIBS)
	$(CXX) $(CXXFLAGS) -L. $(LDFLAGS) $(PROGOBJS) -lssl -lcrypto -lpthread -o $@ $(BUNDLED_LIBS)

%.o: %.cpp inc/logp/*.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

cmd/*.o: inc/logp/cmd/*.h

main.o: _buildinfo.h inc/logp/cmd/*.h

## 3rd party deps

lib/yaml-cpp/build/libyaml-cpp.a:
	mkdir -p lib/yaml-cpp/build/
	cd lib/yaml-cpp/build/ && cmake ..
	cd lib/yaml-cpp/build/ && $(MAKE) yaml-cpp
