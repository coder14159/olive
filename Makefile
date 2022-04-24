include Makefile.include

.PHONY:	all test clean spmc_client spmc_server $(BIN_DIR) $(LIB_DIR)

.DEFAULT_GOAL := all

INCLUDE_DIRS	+= -I$(ROOT_DIR)src

CXXFLAGS		+= $(INCLUDE_DIRS)

# Link boost libraries
LIB_BOOST_LOG        := -lboost_log
LIB_BOOST_FILESYSTEM := -lboost_filesystem
LIB_BOOST_SYSTEM     := -lboost_system
LIB_BOOST_THREAD     := -lboost_thread
LIB_BOOST_UNIT_TEST  := -lboost_unit_test_framework

# SPMC library target
LIB_FILE_NAME = libspmc.a

LIB_SRC_CPP_FILES += src/Assert.h
LIB_SRC_CPP_FILES += src/Buffer.h
LIB_SRC_CPP_FILES += src/Buffer.inl
LIB_SRC_CPP_FILES += src/Chrono.h
LIB_SRC_CPP_FILES += src/Chrono.inl
LIB_SRC_CPP_FILES += src/CpuBind.cpp
LIB_SRC_CPP_FILES += src/CpuBind.h
LIB_SRC_CPP_FILES += src/Latency.cpp
LIB_SRC_CPP_FILES += src/Latency.h
LIB_SRC_CPP_FILES += src/Latency.inl
LIB_SRC_CPP_FILES += src/LatencyStats.cpp
LIB_SRC_CPP_FILES += src/LatencyStats.h
LIB_SRC_CPP_FILES += src/LatencyStats.inl
LIB_SRC_CPP_FILES += src/Logger.cpp
LIB_SRC_CPP_FILES += src/Logger.h
LIB_SRC_CPP_FILES += src/PerformanceStats.cpp
LIB_SRC_CPP_FILES += src/PerformanceStats.h
LIB_SRC_CPP_FILES += src/PerformanceStats.inl
LIB_SRC_CPP_FILES += src/SignalCatcher.cpp
LIB_SRC_CPP_FILES += src/SignalCatcher.h
LIB_SRC_CPP_FILES += src/SPMCQueue.h
LIB_SRC_CPP_FILES += src/SPMCQueue.inl
LIB_SRC_CPP_FILES += src/SPMCSink.h
LIB_SRC_CPP_FILES += src/SPMCSink.inl
LIB_SRC_CPP_FILES += src/SPMCSource.h
LIB_SRC_CPP_FILES += src/SPMCSource.inl
LIB_SRC_CPP_FILES += src/SPSCSink.h
LIB_SRC_CPP_FILES += src/SPSCSink.inl
# LIB_SRC_CPP_FILES += src/SPSCSources.cpp
# LIB_SRC_CPP_FILES += src/SPSCSources.h
LIB_SRC_CPP_FILES += src/SPSCSource.h
LIB_SRC_CPP_FILES += src/SPSCSource.inl
LIB_SRC_CPP_FILES += src/Throttle.h
LIB_SRC_CPP_FILES += src/Throttle.inl
LIB_SRC_CPP_FILES += src/Throughput.cpp
LIB_SRC_CPP_FILES += src/Throughput.h
LIB_SRC_CPP_FILES += src/Throughput.inl
LIB_SRC_CPP_FILES += src/ThroughputStats.cpp
LIB_SRC_CPP_FILES += src/ThroughputStats.h
LIB_SRC_CPP_FILES += src/TimeDuration.cpp
LIB_SRC_CPP_FILES += src/TimeDuration.h
LIB_SRC_CPP_FILES += src/TimeDuration.inl
LIB_SRC_CPP_FILES += src/Timer.cpp
LIB_SRC_CPP_FILES += src/Timer.h
LIB_SRC_CPP_FILES += src/detail/CXXOptsHelper.h
LIB_SRC_CPP_FILES += src/detail/CXXOptsHelper.inl
LIB_SRC_CPP_FILES += src/detail/GetSize.h
LIB_SRC_CPP_FILES += src/detail/GetSize.inl
LIB_SRC_CPP_FILES += src/detail/SharedMemoryCounter.cpp
LIB_SRC_CPP_FILES += src/detail/SharedMemoryCounter.h
LIB_SRC_CPP_FILES += src/detail/SharedMemory.h
LIB_SRC_CPP_FILES += src/detail/SPMCBackPressure.h
LIB_SRC_CPP_FILES += src/detail/SPMCBackPressure.inl
LIB_SRC_CPP_FILES += src/detail/SPMCQueue.h
LIB_SRC_CPP_FILES += src/detail/SPMCQueue.inl
LIB_SRC_CPP_FILES += src/detail/Utils.h

LIB_FILE_PATH := $(LIB_DIR)/$(LIB_FILE_NAME)

# Generate target object file names from source files
LIB_OBJ_FILES = $(patsubst %.cpp,$(LIB_DIR)/.obj/%.o,$(LIB_SRC_CPP_FILES))

$(LIB_OBJ_FILES): Makefile
$(LIB_OBJ_FILES): Makefile.include

# Use "order-only prerequisites" syntax, indicated by '|' symbol, to create the
# library and executable directories if they are not already present
$(LIB_OBJ_FILES): | $(LIB_DIR)/.obj/src/detail
$(LIB_DIR)/.obj/src/detail:
	mkdir -p $(LIB_DIR)/.obj/src/detail

THIRD_PARTY_LIBS += $(LIB_BOOST_LOG)
THIRD_PARTY_LIBS += $(LIB_BOOST_SYSTEM)
THIRD_PARTY_LIBS += $(LIB_BOOST_THREAD)

# Build library object files
$(LIB_DIR)/.obj/%.o: %.h
$(LIB_DIR)/.obj/%.o: %.inl
$(LIB_DIR)/.obj/%.o: %.cpp
	$(COMPILER) $(CXXFLAGS) $(CXXFLAGS_LIB) $(THIRD_PARTY_LIBS) -c $< -o $@

# Create an archive file from the source files
$(LIB_FILE_PATH): $(LIB_OBJ_FILES)
	@ar -r -o $(LIB_FILE_PATH) $(LIB_OBJ_FILES)

# Build all binaries

EXE_FILES = $(BIN_DIR)/spmc_client \
            $(BIN_DIR)/spmc_server \
            $(BIN_DIR)/spsc_server \
            $(BIN_DIR)/spsc_client \
            $(BIN_DIR)/remove_shared_memory \
            $(BIN_DIR)/test_performance \
            $(BIN_DIR)/test_spmcqueue \
            $(BIN_DIR)/test_stats \
            $(BIN_DIR)/test_allocator

# Create bin directory if required
# Invoke using "order-only prerequisites" syntax to prevent directory timestamp
# from updating and causing a rebuild
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Build tools
spmc_client: $(BIN_DIR)/spmc_client
$(BIN_DIR)/spmc_client: Makefile tools/spmc_client/spmc_client.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_DIR) tools/spmc_client/spmc_client.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM) -o $@

spmc_server: $(BIN_DIR)/spmc_server
$(BIN_DIR)/spmc_server: Makefile tools/spmc_server/spmc_server.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_DIR) tools/spmc_server/spmc_server.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) -o $@

spmc: spmc_client spmc_server

spsc_client: $(BIN_DIR)/spsc_client
$(BIN_DIR)/spsc_client: Makefile tools/spsc_client/spsc_client.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_DIR) tools/spsc_client/spsc_client.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM) -o $@

spsc_server: $(BIN_DIR)/spsc_server
$(BIN_DIR)/spsc_server: Makefile tools/spsc_server/spsc_server.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_DIR) tools/spsc_server/spsc_server.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) -o $@

spsc: spsc_client spsc_server

$(BIN_DIR)/remove_shared_memory: Makefile tools/remove_shared_memory/remove_shared_memory.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_DIR) tools/remove_shared_memory/remove_shared_memory.cpp $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) -o $@

remove_shared_memory: $(BIN_DIR)/remove_shared_memory

ping_pong: $(BIN_DIR)/ping_pong
$(BIN_DIR)/ping_pong: Makefile tools/ping_pong/ping_pong.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(BOOST_LIB_DIR) -I$(CXXOPTS_DIR) tools/ping_pong/ping_pong.cpp -L$(LIB_DIR) -lspmc $(LIB_BOOST_LOG) $(LIB_BOOST_FILESYSTEM) -o $@

test_allocator: $(BIN_DIR)/test_allocator
$(BIN_DIR)/test_allocator: Makefile tests/test_allocator/test_allocator.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(LIB_DIR) -L$(BOOST_LIB_DIR) tests/test_allocator/test_allocator.cpp -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_THREAD) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM) -o $@

test_performance: $(BIN_DIR)/test_performance
$(BIN_DIR)/test_performance: Makefile tests/test_performance/test_performance.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(LIB_DIR) -L$(BOOST_LIB_DIR) tests/test_performance/test_performance.cpp -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_THREAD) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_FILESYSTEM) -o $@

test_spmcqueue: $(BIN_DIR)/test_spmcqueue
$(BIN_DIR)/test_spmcqueue: Makefile tests/test_spmcqueue/test_spmcqueue.cpp $(LIB_FILE_PATH) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(LIB_DIR) -L$(BOOST_LIB_DIR) tests/test_spmcqueue/test_spmcqueue.cpp -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_THREAD) $(LIB_BOOST_FILESYSTEM) -o $@

test_stats: $(BIN_DIR)/test_stats
$(BIN_DIR)/test_stats: Makefile tests/test_stats/test_stats.cpp $(LIB_FILE_PATH) $(LIB_SRC_CPP_FILES) | $(BIN_DIR)
	$(COMPILER) $(CXXFLAGS) -L$(LIB_DIR) -L$(BOOST_LIB_DIR) tests/test_stats/test_stats.cpp -lspmc $(LIB_BOOST_UNIT_TEST) $(LIB_BOOST_LOG) $(LIB_BOOST_SYSTEM) $(LIB_BOOST_THREAD) $(LIB_BOOST_FILESYSTEM) -o $@

tests: $(BIN_DIR)/test_allocator \
       $(BIN_DIR)/test_performance \
       $(BIN_DIR)/test_spmcqueue \
       $(BIN_DIR)/test_stats

# Build all binaries
all : $(BIN_DIR)/spmc_client \
      $(BIN_DIR)/spmc_server \
      $(BIN_DIR)/spsc_server \
      $(BIN_DIR)/spsc_client \
      $(BIN_DIR)/remove_shared_memory \
      tests

clean:
	rm -rf build/$(PROCESSOR)$(BUILD_SUFFIX)

define run_test
	@echo "[$1]"
	@$(BIN_DIR)/$1
endef

test:
	$(call run_test,test_allocator)
	$(call run_test,test_performance)
	$(call run_test,test_spmcqueue)
	$(call run_test,test_stats)
