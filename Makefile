TARGET: netstore-server netstore-client

CC = g++
CFLAGS = -std=c++17 -Wall -Wextra -O2 -Werror
LFLAGS = -lboost_program_options -lboost_system -lboost_filesystem -lpthread

netstore-server: src/run_server.cpp src/server.cpp src/communication.cpp
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

netstore-client: src/run_client.cpp src/client.cpp src/communication.cpp
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

.PHONY: clean TARGET
clean:
	rm -f netstore-server netstore-client
