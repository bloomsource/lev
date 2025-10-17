TEST_COVERAGE=0

CPP_STD=c++11
OPTIMIZE_FLAG=-O2
COMPILER_FLAGS=-Wall -g -D_GNU_SOURCE
OBJS= util.o lev.o lev_con.o mempool.o dynbuf.o
LIBS=

ifeq ("$(TEST_COVERAGE)","1")
	COMPILER_FLAGS+=-coverage
	OPTIMIZE_FLAG=-O0
	COV_LIB=-lgcov
endif

LIBS+=$(COV_LIB)
COMPILER_FLAGS+=$(OPTIMIZE_FLAG)
CFLAGS=$(COMPILER_FLAGS)
CXXFLAGS=$(COMPILER_FLAGS) -std=$(CPP_STD)

echo_server: main.o client.o server.o $(OBJS)
	$(CXX) -o $@ $^ $(LIBS)

clean:
	rm -f *.o *.gc* *.log core.* report.zip

c:clean


