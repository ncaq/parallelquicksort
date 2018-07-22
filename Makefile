PROG  := parallelquicksort
PROG2 := makequicksortdata

SRCS  := parallelquicksort.cpp
SRCS2 := makequicksortdata.cpp

OBJS  = parallelquicksort.o
OBJS2 = makequicksortdata.o

DEPS  = parallelquicksort.d
DEPS2 = makequicksortdata.d

VPATH  = src/parallelquicksort src/makequicksortdata
CXX = clang++
CXXFLAGS = -Wextra -O3 -mtune=native -march=native -pipe -std=c++17 -fopenmp -I/home/ncaq/Downloads/parallelstl/include
LDFLAGS = -ltbb -lboost_filesystem -lboost_system -lboost_thread

all: $(PROG) $(PROG2) ;
#rm -f $(OBJS) $(DEPS)

-include $(DEPS)

$(PROG): $(OBJS)
		$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $^

$(PROG2): $(OBJS2)
		$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
		$(CXX) $(CXXFLAGS) -c -MMD -MP -DDEBUG $<

clean:
		rm -f $(PROG) $(OBJS) $(DEPS)
		rm -f $(PROG2) $(OBJS2) $(DEPS2)
