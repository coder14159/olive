include Makefile.include

.DEFAULT_GOAL := all

INCLUDE_DIRS	+= -I$(ROOT_DIR)

CXXFLAGS		+= $(INCLUDE_DIRS)

# Link third party libraries
LIB_BOOST_LOG        := -lboost_log
LIB_BOOST_FILESYSTEM := -lboost_filesystem
LIB_BOOST_SYSTEM     := -lboost_system
LIB_BOOST_THREAD     := -lboost_thread
LIB_BOOST_UNIT_TEST  := -lboost_unit_test_framework

# SPMC library target
LIB_FILE_NAME = libspmc.a

LIB_SRC_FILES += src/CpuBind.cpp
LIB_SRC_FILES += src/Latency.cpp
LIB_SRC_FILES += src/LatencyStats.cpp
LIB_SRC_FILES += src/Logger.cpp
LIB_SRC_FILES += src/PerformanceStats.cpp
LIB_SRC_FILES += src/SPMCSink.cpp
LIB_SRC_FILES += src/Time.cpp
# LIB_SRC_FILES += src/TimeDuration.cpp
LIB_SRC_FILES += src/Throughput.cpp
LIB_SRC_FILES += src/ThroughputStats.cpp
LIB_SRC_FILES += src/detail/SPMCQueue.cpp

# Generate target object file names from source files
LIB_OBJ_FILES = $(patsubst src/%.cpp,$(LIB_DIR)/.obj/%.o,$(LIB_SRC_FILES))

LIB_PATH := $(LIB_DIR)/$(LIB_FILE_NAME)

# Build library object files
$(LIB_DIR)/.obj/%.o: src/%.cpp
	$(COMPILER) $(CXXFLAGS) $(CXXFLAGS_LIB)  -c $< -o $@ $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM) $(LIB_BOOST_THREAD)

# Create an archive file from the source files
$(LIB_PATH): $(LIB_OBJ_FILES)
	ar -r -o $(LIB_PATH) $(LIB_OBJ_FILES)

# Use the order-only prerequisites '|' to create library directory if not already present
$(LIB_OBJ_FILES): | $(LIB_DIR)/.obj/detail
$(LIB_DIR)/.obj/detail:
	mkdir -p $(LIB_DIR)/.obj/detail

$(BIN_DIR)/spmc_client: Makefile tools/spmc_client/spmc_client.cpp $(LIB_DIR)/$(LIB_FILE_NAME)
	mkdir -p $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/spmc_client tools/spmc_client/spmc_client.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM)

$(BIN_DIR)/remove_shared_memory: Makefile tools/remove_shared_memory/remove_shared_memory.cpp $(LIB_DIR)/$(LIB_FILE_NAME)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/remove_shared_memory tools/remove_shared_memory/remove_shared_memory.cpp $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM)

$(BIN_DIR)/test_performance: Makefile tests/test_performance/test_performance.cpp $(LIB_DIR)/$(LIB_FILE_NAME)
	mkdir -p $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_HEADER_DIR) -o $(BIN_DIR)/test_performance tests/test_performance/test_performance.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG)

$(BIN_DIR)/test_spmcqueue: Makefile tests/test_spmcqueue/test_spmcqueue.cpp $(LIB_DIR)/$(LIB_FILE_NAME)
	mkdir -p $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -DBOOST_TEST_DYN_LINK -DSPMC_DEBUG_ASSERT -I$(CXXOPTS_DIR) $(LIBRARY_DIRS) -lpthread -lrt -o $(BIN_DIR)/test_spmcqueue tests/test_spmcqueue/test_spmcqueue.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_THREAD) $(LIB_BOOST_FILESYSTEM)

all : $(BIN_DIR)/spmc_client \
      $(BIN_DIR)/remove_shared_memory \
      $(BIN_DIR)/test_performance \
      $(BIN_DIR)/test_spmcqueue \

clean:
	rm -rf build/$(PROCESSOR)$(BUILD_SUFFIX)
