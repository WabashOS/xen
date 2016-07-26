#
# tools/gang-sched-ctrl/Makefile
#

XEN_ROOT=$(CURDIR)/../..
include $(XEN_ROOT)/tools/Rules.mk

CFLAGS  += -Werror
CFLAGS  += $(CFLAGS_libxenctrl) 
CXXFLAGS += $(CFLAGS_libxenctrl) -std=c++11 -Wall 
LDLIBS  += $(LDLIBS_libxenctrl) -lbitmask

TEST_DIR=$(CURDIR)/test

.PHONY: all
all: build

.PHONY: build
build: gsc

.PHONY: clean
clean:
	$(RM) -f *.o $(DEPS)
	$(RM) -f gsc
	$(RM) -f sched_test $(TEST_DIR)/*.o


# Double pattern substition to get the object file right.
GSC_OBJS = $(patsubst %.cc, %.o, $(wildcard *.cc))
GSC_OBJS += $(patsubst %.c, %.o, $(wildcard *.c))

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_$*.o) -c -o $@ $< $(APPEND_CFLAGS)

%.o: %.cc
	$(CC) $(CPPFLAGS) $(CXXFLAGS) $(CXXFLAGS_$*.o) -c -o $@ $< $(APPEND_CFLAGS)

gsc: $(GSC_OBJS) 
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS) $(APPEND_LDFLAGS)

################################################################################
# Unit tests for schedulability analysis.
################################################################################

SCHED_TEST_OBJ = $(patsubst $(TEST_DIR)/%.c,$(TEST_DIR)/%.o,$(wildcard $(TEST_DIR)/*.c)) 

sched_test: $(SCHED_TEST_OBJ) $(GSC_OBJS) 
	$(CXX) $(LDFLAGS) $(filter-out gsc.o,$^) -o $@ $(LDLIBS) $(APPEND_LDFLAGS)


-include $(DEPS)
