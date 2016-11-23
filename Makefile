W        = -Wall -Wextra
OPT      = -O2 -g
STD      = -std=c++11
CXXFLAGS = $(STD) $(OPT) $(W) -fPIC -Iinc $(XCXXFLAGS)
LDFLAGS  =

PROGOBJS    = main.o websocket.o


ifeq ($(wildcard inc/protected_queue/protected_queue.h),)
  $(info submodules not checked out. Run the following and then try make again:)
  $(info )
  $(info git submodule update --init)
  $(info )
  $(error git submodules not checked out)
endif


.PHONY: all clean test
all: logp

clean:
	rm -f *.o *.so logp

logp: $(PROGOBJS) Makefile
	$(CXX) $(CXXFLAGS) -L. $(LDFLAGS) $(PROGOBJS) -lpthread -o $@ 

%.o: %.cpp inc/logp/*.h Makefile
	$(CXX) $(CXXFLAGS) -c $<
