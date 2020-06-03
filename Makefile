include Makefile.include

.DEFAULT_GOAL := all

# TODO remove this line, add the commented out one below and delete need for
# myheader.h to have the src directory prefix
INCLUDE_DIRS	+= -I$(ROOT_DIR)
INCLUDE_DIRS	+= -I$(ROOT_DIR)/src

CXXFLAGS		+= $(INCLUDE_DIRS)

# Link third party libraries
LIB_BOOST_LOG        := -lboost_log
LIB_BOOST_FILESYSTEM := -lboost_filesystem
LIB_BOOST_SYSTEM     := -lboost_system
LIB_BOOST_THREAD     := -lboost_thread
LIB_BOOST_UNIT_TEST  := -lboost_unit_test_framework

# SPMC library target
LIB_FILE_NAME = libspmc.a

LIB_SRC_CPP_FILES += src/CpuBind.cpp
LIB_SRC_CPP_FILES += src/Latency.cpp
LIB_SRC_CPP_FILES += src/LatencyStats.cpp
LIB_SRC_CPP_FILES += src/Logger.cpp
LIB_SRC_CPP_FILES += src/PerformanceStats.cpp
LIB_SRC_CPP_FILES += src/SignalCatcher.cpp
LIB_SRC_CPP_FILES += src/SPMCSink.cpp
LIB_SRC_CPP_FILES += src/Time.cpp
LIB_SRC_CPP_FILES += src/Timer.cpp
LIB_SRC_CPP_FILES += src/TimeDuration.cpp
LIB_SRC_CPP_FILES += src/Throughput.cpp
LIB_SRC_CPP_FILES += src/ThroughputStats.cpp
LIB_SRC_CPP_FILES += src/detail/SPMCQueue.cpp
LIB_SRC_CPP_FILES += src/detail/SharedMemoryCounter.cpp

LIB_SRC_INL_FILES += Buffer.h     Buffer.inl
LIB_SRC_INL_FILES += SPMCQueue.h  SPMCQueue.inl
LIB_SRC_INL_FILES += SPMCStream.h SPMCStream.inl
LIB_SRC_INL_FILES += SPSCSink.h   SPSCSink.inl

LIB_FILE_PATH := $(LIB_DIR)/$(LIB_FILE_NAME)

# Generate target object file names from source files
LIB_OBJ_FILES = $(patsubst %.cpp,$(LIB_DIR)/.obj/%.o,$(LIB_SRC_CPP_FILES))

# Use "order-only prerequisites" syntax, indicated by '|' symbol, to create the
# library and executable directories if they are not already present
$(LIB_OBJ_FILES): | $(LIB_DIR)/.obj/src/detail
$(LIB_DIR)/.obj/src/detail:
	mkdir -p $(LIB_DIR)/.obj/src/detail

# Create bin directory if required using "order-only prerequisites" syntax
bin_dir: | $(BIN_DIR)
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build library object files
$(LIB_DIR)/.obj/%.o: %.cpp
	$(COMPILER) $(CXXFLAGS) $(CXXFLAGS_LIB)  -c $< -o $@ $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM) $(LIB_BOOST_THREAD)

# Create an archive file from the source files
$(LIB_FILE_PATH): $(LIB_OBJ_FILES)
	@ar -r -o $(LIB_FILE_PATH) $(LIB_OBJ_FILES)

# Build tools
$(BIN_DIR)/spmc_client: $(BIN_DIR) Makefile tools/spmc_client/spmc_client.cpp $(LIB_FILE_PATH)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/spmc_client tools/spmc_client/spmc_client.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM)

$(BIN_DIR)/spmc_server: $(BIN_DIR) Makefile tools/spmc_server/spmc_server.cpp $(LIB_FILE_PATH)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/spmc_server tools/spmc_server/spmc_server.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM)

# Requires a patch for boost spsc_queue (or used to..)
# $(BIN_DIR)/spsc_client: $(BIN_DIR) Makefile tools/spsc_client/spsc_client.cpp $(LIB_FILE_PATH)
# 	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/spsc_client tools/spsc_client/spsc_client.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM)

# $(BIN_DIR)/spsc_server: $(BIN_DIR) Makefile tools/spsc_server/spsc_server.cpp $(LIB_FILE_PATH)
# 	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/spsc_server tools/spsc_server/spsc_server.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM)

$(BIN_DIR)/remove_shared_memory: $(BIN_DIR) Makefile tools/remove_shared_memory/remove_shared_memory.cpp $(LIB_FILE_PATH)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/remove_shared_memory tools/remove_shared_memory/remove_shared_memory.cpp $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM)

# Build tests
$(BIN_DIR)/test_performance: $(BIN_DIR) Makefile tests/test_performance/test_performance.cpp $(LIB_FILE_PATH)
	$(COMPILER) $(CXXFLAGS) -L$(LIB_DIR) -L$(BOOST_LIB_DIR) -o $(BIN_DIR)/test_performance tests/test_performance/test_performance.cpp -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_THREAD) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM)

$(BIN_DIR)/test_spmcqueue: $(BIN_DIR) Makefile tests/test_spmcqueue/test_spmcqueue.cpp $(LIB_FILE_PATH)
	$(COMPILER) $(CXXFLAGS) -L$(LIB_DIR) -L$(BOOST_LIB_DIR) -o $(BIN_DIR)/test_spmcqueue tests/test_spmcqueue/test_spmcqueue.cpp -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_THREAD) $(LIB_BOOST_FILESYSTEM)

$(BIN_DIR)/test_stats: $(BIN_DIR) Makefile tests/test_stats/test_stats.cpp $(LIB_FILE_PATH)
	$(COMPILER) $(CXXFLAGS) -L$(LIB_DIR) -L$(BOOST_LIB_DIR) -o $(BIN_DIR)/test_stats tests/test_stats/test_stats.cpp -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_THREAD) $(LIB_BOOST_FILESYSTEM)

# Build all binaries
all:	$(BIN_DIR)/spmc_client \
		$(BIN_DIR)/spmc_server \
		$(BIN_DIR)/remove_shared_memory \
		$(BIN_DIR)/test_performance \
		$(BIN_DIR)/test_spmcqueue
		$(BIN_DIR)/test_stats \
    #  $(BIN_DIR)/test_allocator \
    #  $(BIN_DIR)/spmc_server \
    #  $(BIN_DIR)/spsc_client \
    #  $(BIN_DIR)/spsc_server \

clean:
	rm -rf build/$(PROCESSOR)$(BUILD_SUFFIX)

test:
	$(BIN_DIR)/test_performance --log_level=INFO
	$(BIN_DIR)/test_spmcqueue

.PHONY: all