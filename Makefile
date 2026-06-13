
SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp, ./build/%.o, $(SRCS))
TARGETS := $(patsubst %.cpp, ./build/%, $(SRCS))

.SUFFIXES:
.DEFAULT_GOAL := all
.SECONDARY: $(OBJS)

CXX := g++
CXXFLAGS := -std=c++11 -I./include -g -O0 -fno-omit-frame-pointer -DDEBUG
LDLIBS := -Wl,-rpath=./lib/x64 -pthread -lstdc++ -L./lib/x64 -lSVBCameraSDK -lusb-1.0

SYSTEM_PKG_CONFIG_LIBDIR := /usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig
OPENCV_CFLAGS := $(shell PKG_CONFIG_LIBDIR=$(SYSTEM_PKG_CONFIG_LIBDIR) pkg-config --cflags opencv4)
OPENCV_LIBS   := $(shell PKG_CONFIG_LIBDIR=$(SYSTEM_PKG_CONFIG_LIBDIR) pkg-config --libs opencv4)

all: $(TARGETS)

run: ./build/svbcapture
	LIBGL_ALWAYS_SOFTWARE=1 QT_XCB_GL_INTEGRATION=none ./build/svbcapture

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

.PHONY: all run clean distclean


