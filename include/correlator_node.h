/* Copyright (c) 2007 Joint Institute for VLBI in Europe (Netherlands)
 * All rights reserved.
 *
 * Author(s): Nico Kruithof <Kruithof@JIVE.nl>, 2007
 *
 * $Id$
 *
 */

#ifndef CORRELATOR_NODE_H
#define CORRELATOR_NODE_H
#include <queue>
#include "node.h"
#include "multiple_data_readers_controller.h"
#include "single_data_writer_controller.h"
#include "control_parameters.h"
#include "correlator_node_data_reader_tasklet.h"
#include "log_writer_mpi.h"
#include "correlation_core.h"
#include "delay_correction.h"
#include <tasklet/tasklet_manager.h>
#include "timer.h"

#include "monitor.h"

// Declare the correlator controller:
class Correlator_node;

/**
 * Correlator_node_controller processes specific signals for the Correlator node.
 **/
class Correlator_node_controller : public Controller {
public:
  Correlator_node_controller(Correlator_node &node);
  ~Correlator_node_controller();

  Process_event_status process_event(MPI_Status &status);

private:
  Correlator_node &node;
};

/**
 * A correlate node will initialize the correlation process and connect
 * to the output node. It can receive messages from a data node asking to
 * open an input connection and from the controller node to process a
 * time slice. After the slice is processed the node will send a message
 * to the controller node saying it is available for a next job.
 *
 * \ingroup Node
 **/
class Correlator_node : public Node {
public:
  typedef Correlator_node                              Self;
  typedef Memory_pool_vector_element<char>             output_value_type;

	/// The states of the correlator_node.
  enum Status {
    // Initialise the Correlate node
    STOPPED=0,
    // The node is correlating
    CORRELATING,
    END_CORRELATING
  };

  Correlator_node(int rank, int nr_corr_node);
  ~Correlator_node();

	/// The the main_loop of the correlator node.
  void start();

  /// Terminate the main_loop.
  void terminate();

	/// Callback function for adding a data_reader:
  void hook_added_data_reader(size_t reader);

  /// Callback function for adding a data_writer:
  void hook_added_data_writer(size_t writer);

  void add_delay_table(int sn, Delay_table_akima &table);

  void output_node_set_timeslice(int slice_nr, int slice_offset, int n_slices,
                                 int stream_nr, int bytes);

  void receive_parameters(const Correlation_parameters &parameters);

  void set_parameters();

  int get_correlate_node_number();
private:

	/// Main "usefull" function in which the real correlation computation is
	/// done.
  void correlate();

  typedef boost::shared_ptr<Correlator_node_data_reader_tasklet>
                                                Bit_sample_reader_ptr;

  typedef boost::shared_ptr<Delay_correction>         Delay_correction_ptr;

  Correlator_node_controller       correlator_node_ctrl;

  /// The correlator node is connected to each of the input nodes.
  Multiple_data_readers_controller data_readers_ctrl;

  Single_data_writer_controller    data_writer_ctrl;

  /// State variables:
  Status status;

  /// Number of the correlator node
  int nr_corr_node;

  std::vector< Bit_sample_reader_ptr >        bit_sample_readers;
  std::vector< Delay_correction_ptr >         delay_modules;
  Correlation_core                            correlation_core;

  int n_integration_slice_in_time_slice;

  std::queue<Correlation_parameters>          integration_slices_queue;

	Timer bit_sample_reader_timer_, bits_to_float_timer_, delay_timer_, correlation_timer_;

#ifdef RUNTIME_STATISTIC
  QOS_MonitorSpeed reader_state_;
  QOS_MonitorSpeed delaycorrection_state_;
  QOS_MonitorSpeed correlation_state_;
  QOS_MonitorSpeed dotask_state_;
#endif //RUNTIME_STATISTIC
};

#endif // CORRELATOR_NODE_H
