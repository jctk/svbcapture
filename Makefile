
SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp, ./build/%.o, $(SRCS))
TARGETS := $(patsubst %.cpp, ./build/%, $(SRCS))

.SUFFIXES:
.DEFAULT_GOAL := all
.SECONDARY: $(OBJS)

CXX := g++
CXXFLAGS := -std=c++11 -I./include -g -O0 -fno-omit-frame-pointer -DDEBUG
LDLIBS := -Wl,-rpath=./lib/x64 -pthread -lstdc++ -L./lib/x64 -lSVBCameraSDK -lusb-1.0

OPENCV_CFLAGS := -I/usr/include/opencv4
OPENCV_LIBS   := -L/usr/lib/x86_64-linux-gnu -lopencv_core -lopencv_imgproc -lopencv_highgui

all: $(TARGETS)

./build/svbcapture.o: CXXFLAGS += $(OPENCV_CFLAGS)
./build/svbcapture:   LDLIBS   += $(OPENCV_LIBS)

%: %.o
	$(CXX) $< $(LDLIBS) -o $@

./build/%.o: %.cpp
	@mkdir -p ./build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGETS)

distclean:
	rm -rf *.bin

.PHONY: all clean distclean


