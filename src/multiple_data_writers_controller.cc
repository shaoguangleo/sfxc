/* Copyright (c) 2007 Joint Institute for VLBI in Europe (Netherlands)
 * All rights reserved.
 *
 * Author(s): Nico Kruithof <Kruithof@JIVE.nl>, 2007
 *
 * $Id$
 *
 */

#include "multiple_data_writers_controller.h"
#include "sfxc_mpi.h"
#include "data_writer_file.h"
#include "data_writer_tcp.h"
#include "data_writer_void.h"
#include "tcp_connection.h"

Multiple_data_writers_controller::
Multiple_data_writers_controller(Node &node, int max_connections)
    : Controller(node) {
  int port = SFXC_PORT;
  while (!tcp_connection.open_port(port, max_connections)) {
    port++;
  }
}

Multiple_data_writers_controller::
~Multiple_data_writers_controller() {}


boost::shared_ptr<Data_writer>
Multiple_data_writers_controller::get_data_writer(size_t i) {
  SFXC_ASSERT(i < data_writers.size());
  return data_writers[i];
}

Multiple_data_writers_controller::Process_event_status
Multiple_data_writers_controller::process_event(MPI_Status &status) {
  switch (status.MPI_TAG) {
  case MPI_TAG_ADD_TCP: {
      get_log_writer()(3) << print_MPI_TAG(status.MPI_TAG) << std::endl;

      MPI_Status status2;

      /* - int32_t: stream number for the data writer
       * - int32_t: stream number for the data reader
       * - int32_t: rank of the data_reader
       */
      int32_t ranks[3];
      MPI_Recv(ranks, 3, MPI_INT32, status.MPI_SOURCE,
               status.MPI_TAG, MPI_COMM_WORLD, &status2);

      Data_writer_tcp *data_writer = new Data_writer_tcp();

      if (tcp_connection.get_port() < 0) {
        tcp_connection.open_port(SFXC_PORT, MAX_TCP_CONNECTIONS);
      }
      SFXC_ASSERT(tcp_connection.get_port() > 0);

      std::vector<uint64_t>  ip_addresses;
      // Add number of the data stream:
      ip_addresses.push_back(ranks[2]);
      // Add the ip addresses
      tcp_connection.get_ip_addresses(ip_addresses);

      // Add port
      ip_addresses.push_back(tcp_connection.get_port());

      MPI_Send(&ip_addresses[0], ip_addresses.size(), MPI_INT64,
               ranks[1], MPI_TAG_ADD_DATA_READER_TCP2, MPI_COMM_WORLD);

      data_writer->open_connection(tcp_connection);

      boost::shared_ptr<Data_writer> writer(data_writer);
      add_data_writer(ranks[0], writer);

      int32_t return_msg = 0;
      MPI_Recv(&return_msg, 1, MPI_INT32, ranks[1],
               MPI_TAG_CONNECTION_ESTABLISHED, MPI_COMM_WORLD, &status2);

      MPI_Send(&ranks[0], 1, MPI_INT32,
               status.MPI_SOURCE, MPI_TAG_CONNECTION_ESTABLISHED,
               MPI_COMM_WORLD);

      return PROCESS_EVENT_STATUS_SUCCEEDED;
    }
  case MPI_TAG_ADD_DATA_WRITER_FILE2: {
      get_log_writer()(3) << print_MPI_TAG(status.MPI_TAG) << std::endl;

      MPI_Status status2;
      int size;
      MPI_Get_elements(&status, MPI_CHAR, &size);
      SFXC_ASSERT(size > 0);
      char msg[size];
      MPI_Recv(&msg, size, MPI_CHAR, status.MPI_SOURCE,
               status.MPI_TAG, MPI_COMM_WORLD, &status2);
      int stream_nr;
      memcpy(&stream_nr, msg, sizeof(int32_t));
      char *filename = msg+sizeof(int32_t);

      SFXC_ASSERT(status.MPI_SOURCE == status2.MPI_SOURCE);
      SFXC_ASSERT(status.MPI_TAG == status2.MPI_TAG);

      boost::shared_ptr<Data_writer>
      data_writer(new Data_writer_file(filename));

      add_data_writer(stream_nr, data_writer);

      MPI_Send(&stream_nr, 1, MPI_INT32,
               status.MPI_SOURCE, MPI_TAG_CONNECTION_ESTABLISHED,
               MPI_COMM_WORLD);

      return PROCESS_EVENT_STATUS_SUCCEEDED;
    }
  case MPI_TAG_ADD_DATA_WRITER_VOID2: {
      get_log_writer()(3) << print_MPI_TAG(status.MPI_TAG) << std::endl;

      MPI_Status status2;
      int32_t msg;
      MPI_Recv(&msg, 1, MPI_INT32, status.MPI_SOURCE,
               status.MPI_TAG, MPI_COMM_WORLD, &status2);

      SFXC_ASSERT(status.MPI_SOURCE == status2.MPI_SOURCE);
      SFXC_ASSERT(status.MPI_TAG == status2.MPI_TAG);

      boost::shared_ptr<Data_writer> data_writer(new Data_writer_void());

      add_data_writer(msg, data_writer);

      MPI_Send(&msg, 1, MPI_INT32,
               status.MPI_SOURCE, MPI_TAG_CONNECTION_ESTABLISHED,
               MPI_COMM_WORLD);

      return PROCESS_EVENT_STATUS_SUCCEEDED;
    }
  }
  return PROCESS_EVENT_STATUS_UNKNOWN;
}

bool
Multiple_data_writers_controller::ready() {
  return true;
}

void
Multiple_data_writers_controller::
add_data_writer(unsigned int i, boost::shared_ptr<Data_writer> writer) {
  if (data_writers.size() <= i) {
    data_writers.resize(i+1);
  }
  SFXC_ASSERT(i < data_writers.size());

  data_writers[i] = writer;

  node.hook_added_data_writer(i);
}

