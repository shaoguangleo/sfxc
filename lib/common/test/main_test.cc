#include <iostream>

#include "Test_unit.h"
#include "backtrace.h"
#include "demangler.h"
#include "monitor.h"

int main(int argc, char** argv) {
  std::cout << "Starting tests" << std::endl;

#ifdef ENABLE_TEST_UNIT
  Test_manager manager;
  //manager.add_test( new Backtrace::Test() );
  manager.add_test( new QOS_MonitorSpeed::Test() );
  manager.do_test();
#endif //

}
