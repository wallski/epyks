CXX = g++
CXXFLAGS = -std=c++17 -Wall -I../epyks.Core/src -I.
LDFLAGS = -lsqlite3 -lpthread

SRCS = main.cpp Database.cpp ../epyks.Core/src/Protocol/Packet.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = epyks-server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
