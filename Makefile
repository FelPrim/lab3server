CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
SRCDIR = server
SOURCES = $(SRCDIR)/main.cpp \
          $(SRCDIR)/Server.cpp \
          $(SRCDIR)/TcpConnection.cpp \
          $(SRCDIR)/UdpSocket.cpp \
          $(SRCDIR)/Conference.cpp \
          $(SRCDIR)/Message.cpp \
          $(SRCDIR)/ClientSession.cpp
OBJS = $(SOURCES:.cpp=.o)
TARGET = video_server

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
