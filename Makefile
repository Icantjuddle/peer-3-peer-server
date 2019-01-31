CC = clang
CXX = clang++
CXXFLAGS = -g -Wall
CFLAGS = -g -Wall

default: echo

echo: echoserver.cc register.o
	$(CXX) $(CXXFLAGS)	$^ -o $@ -lbluetooth -lpthread

clean:
	$(RM) *.o echo
