#ifndef NETWORK_HH
#define NETWORK_HH

#include <vector>
#include <string>

#include "tcp_connection.h"
#include "common.h"

#include "interface.h"
#include "connexion.h"

#ifdef ENABLE_TEST_UNIT
#include "Test_unit.h"
#endif // ENABLE_TEST_UNIT

class Network {
public:
  static void get_interfaces(std::vector<InterfaceIP*>& interfaces);
  static bool match_interface(in_addr_t dest);

  // Return a list of interfaces, ordered by the given list of interface names.
  static void get_interfaces_ordered_by_name(const Vector_string& names,
      std::vector<InterfaceIP*>& interfaces);

  static pInterfaceIP get_interface_by_name(const String& name);
  static pInterfaceIP get_any_interface();

  static pConnexion connect_to(const std::string& ipaddress, unsigned short port, int type = SOCK_STREAM);
  static pConnexion connect_to(uint64_t ip, short port, int type = SOCK_STREAM);

  static EndpointIP* create_endpoint(unsigned short port = 0);

private:
  static pInterfaceIP interface_any_;
};

#endif // NETWORK_HH
