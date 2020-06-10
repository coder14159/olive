#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include <boost/container/static_vector.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <iostream>


#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE AllocatorTests

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE (SPSCQueueSharedMemory)
{
  /*
   * https://svn.boost.org/trac/boost/attachment/ticket/11490/spsc_queue_interprocess_test.cpp
   * https://svn.boost.org/trac/boost/attachment/ticket/11490/boost_1_58_0.patch
   *
   * boost spsc_queue contained a bug which prevents compiling run-time sizing.
   * Now fixed.
   */
  using namespace boost;
  using namespace boost::interprocess;

  using Allocator = allocator<int, managed_shared_memory::segment_manager>;
  using Queue     = lockfree::spsc_queue<int, lockfree::allocator<Allocator>>;

  struct shm_remove
  {
    shm_remove() {
      shared_memory_object::remove ("spsc_queue_shm");
    }
    ~shm_remove(){
      shared_memory_object::remove ("spsc_queue_shm");
    }
  } remover;

  managed_shared_memory segment (create_only, "spsc_queue_shm", 1000);

  Allocator allocator (segment.get_segment_manager ());

  Queue* queue = segment.construct<Queue> ("queue")(3, allocator);

  BOOST_CHECK_EQUAL (queue->push (1), true);
  BOOST_CHECK_EQUAL (queue->push (2), true);
  BOOST_CHECK_EQUAL (queue->push (3), true);
  BOOST_CHECK_EQUAL (queue->push (4), false);

  int value;
  BOOST_CHECK_EQUAL(queue->pop (value), true);
  BOOST_CHECK_EQUAL(value, 1);

  BOOST_CHECK_EQUAL(queue->pop (value), true);
  BOOST_CHECK_EQUAL(value, 2);

  BOOST_CHECK_EQUAL(queue->pop (value), true);
  BOOST_CHECK_EQUAL(value, 3);

  BOOST_CHECK_EQUAL(queue->pop (value), false);

  segment.destroy<Queue> ("queue");
}


BOOST_AUTO_TEST_CASE (SPMCQueueSharedMemory)
{
  using namespace boost;
  using namespace boost::interprocess;

  using Allocator = allocator<int, managed_shared_memory::segment_manager>;
  using Queue     = lockfree::spsc_queue<int, lockfree::allocator<Allocator>>;

  struct shm_remove
  {
    shm_remove() {
      shared_memory_object::remove ("spmc_queue_shm");
    }
    ~shm_remove(){
      shared_memory_object::remove ("spmc_queue_shm");
    }
  } remover;

  managed_shared_memory segment (create_only, "spmc_queue_shm", 1000);

  Allocator allocator (segment.get_segment_manager ());

  Queue* queue = segment.construct<Queue> ("queue")(3, allocator);

  BOOST_CHECK_EQUAL (queue->push (1), true);
  BOOST_CHECK_EQUAL (queue->push (2), true);
  BOOST_CHECK_EQUAL (queue->push (3), true);
  BOOST_CHECK_EQUAL (queue->push (4), false);

  int value;
  BOOST_CHECK_EQUAL(queue->pop (value), true);
  BOOST_CHECK_EQUAL(value, 1);

  BOOST_CHECK_EQUAL(queue->pop (value), true);
  BOOST_CHECK_EQUAL(value, 2);

  BOOST_CHECK_EQUAL(queue->pop (value), true);
  BOOST_CHECK_EQUAL(value, 3);

  BOOST_CHECK_EQUAL(queue->pop (value), false);

  segment.destroy<Queue> ("queue");
}
