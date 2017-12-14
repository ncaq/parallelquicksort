PROG := parallelquicksort
SRCS :=	parallelquicksort.cpp

OBJS = parallelquicksort.o
DEPS = parallelquicksort.d

VPATH  = src
CXX = icpc
CXXFLAGS = -Wall -Wextra -O3 -xCORE-AVX2 -ipo -pipe -std=c++17 -fopenmp
LDFLAGS = -L/home/dc1394/oss/tbb2018_20171205oss/lib/intel64/gcc4.7 -ltbb \
		  -L/home/dc1394/oss/boost_1_65_1/stage/icc/lib -lboost_system -lboost_thread

all: $(PROG) ;
#rm -f $(OBJS) $(DEPS)

-include $(DEPS)

$(PROG): $(OBJS)
		$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
		$(CXX) $(CXXFLAGS) -c -MMD -MP -DDEBUG $<

clean:
		rm -f $(PROG) $(OBJS) $(DEPS)
