CC = g++
CCFLAGS = -Wall -Wextra -std=c++17 -O0 -lm

build: server subscriber

# Compileaza server.cpp
server: server.cpp common.cpp
		$(CC) $(CCFLAGS) common.cpp server.cpp -o server

# Compileaza subscriber.cpp
subscriber: subscriber.cpp common.cpp
	$(CC) $(CCFLAGS) common.cpp subscriber.cpp -o subscriber

.PHONY: build clean

clean:
	rm -rf server subscriber *.o *.dSYM
