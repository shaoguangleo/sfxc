AM_CXXFLAGS=-I../testunit/src -I../common/src  $(SFXC_CXXFLAGS) 

noinst_LIBRARIES = libcontainers.a
libcontainers_a_SOURCES = \
  src/default_allocator.cc \
  src/memory_pool.cc \
  src/pc_buffer.cc \
  src/pc_queue.cc \
  src/threadsafe_queue.cc 
pkginclude_HEADERS = src/*.h
