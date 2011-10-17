/* Copyright (c) 2007 Joint Institute for VLBI in Europe (Netherlands)
 * All rights reserved.
 *
 * Author(s): Nico Kruithof <Kruithof@JIVE.nl>, 2007
 *
 * $Id$
 *
 */

#include "manager_node.h"
#include "sfxc_mpi.h"
#include "utils.h"
#include "mpi_transfer.h"
#include "log_writer_cout.h"
#include "uvw_model.h"
#include "svn_version.h"

#include <iostream>
#include <iomanip>
#include <fftw3.h>
#include <stdlib.h>
#include <cstring>
#include <set>

Manager_node::
Manager_node(int rank, int numtasks,
             Log_writer *log_writer,
             const Control_parameters &control_parameters)
    : Abstract_manager_node(rank, numtasks,
                            log_writer,
                            control_parameters),
    manager_controller(*this),
    integration_slice_nr(0),
    current_scan(0)
/**/ {
  SFXC_ASSERT(rank == RANK_MANAGER_NODE);

  add_controller(&manager_controller);

  get_log_writer()(1) << "Starting nodes" << std::endl;

  // initialise the log node
  //start_log_node(RANK_LOG_NODE, "file://./output.txt");
  start_log_node(RANK_LOG_NODE);

  // initialise the output node
  start_output_node(RANK_OUTPUT_NODE);

  // Input nodes:
  int n_stations = get_control_parameters().number_stations();
  for (int input_node=0; input_node<n_stations; input_node++) {
    SFXC_ASSERT(input_node+3 != RANK_MANAGER_NODE);
    SFXC_ASSERT(input_node+3 != RANK_LOG_NODE);
    SFXC_ASSERT(input_node+3 != RANK_OUTPUT_NODE);
    SFXC_ASSERT(input_node+3 < numtasks);

    start_input_node(/*rank_nr*/ input_node+3,
                                 get_control_parameters().station(input_node));
  }
  SFXC_ASSERT(n_stations > 0);

  // correlator nodes:
  if (numtasks-(n_stations+3) - control_parameters.number_correlation_cores_per_timeslice(get_current_mode()) < 0) {
    std::cout << "#correlator nodes < #freq. channels, use at least "
    << n_stations+3+control_parameters.number_correlation_cores_per_timeslice(get_current_mode())
    << " nodes." << std::endl
    << "Exiting now." << std::endl;
    get_log_writer()(1)
    << "#correlator nodes < #freq. channels, use at least "
    << n_stations+3+control_parameters.number_correlation_cores_per_timeslice(get_current_mode())
    << " nodes." << std::endl
    << "Exiting now." << std::endl;
    exit(1);
  }
  n_corr_nodes = numtasks-(n_stations+3);
  std::vector<MPI_Request> pending_requests;
  int numrequest;
	if( control_parameters.cross_polarize() ){
			numrequest = (n_stations*2+1) * n_corr_nodes;
	}else{
			numrequest = (n_stations+1) * n_corr_nodes;
	}
	pending_requests.resize(numrequest);
	int currreq=0;
  for (int correlator_nr = 0;
       correlator_nr < n_corr_nodes;
       correlator_nr++) {
    int correlator_rank = correlator_nr + n_stations+3;
    SFXC_ASSERT(correlator_rank != RANK_MANAGER_NODE);
    SFXC_ASSERT(correlator_rank != RANK_LOG_NODE);
    SFXC_ASSERT(correlator_rank != RANK_OUTPUT_NODE);

    start_correlator_node(/*rank_nr*/ correlator_rank);

    // Set up the connection to the input nodes:
    for (int station_nr=0; station_nr<n_stations; station_nr++) {
    	connect_to(input_rank(get_control_parameters().station(station_nr)),
								 correlator_nr,
								 correlator_rank, station_nr,
								 input_node_cnx_params_[station_nr]->ip_port_, correlator_rank, &pending_requests[currreq++] );
    }

    if (control_parameters.cross_polarize()) {
      // duplicate all stations:

      for (int station_nr=0; station_nr<n_stations; station_nr++) {
        connect_to(input_rank(get_control_parameters().station(station_nr)),
									 correlator_nr+n_corr_nodes,
									 correlator_rank,
									 station_nr+n_stations,
									 input_node_cnx_params_[station_nr]->ip_port_,
									 correlator_rank, &pending_requests[currreq++] );
      }
    }

		//currreq++;
    // Set up the connection to the output node:

    connect_writer_to(
									 correlator_rank, 0,
									 RANK_OUTPUT_NODE, correlator_nr,
									 output_node_cnx_params_[0]->ip_port_,
									 correlator_rank, &pending_requests[currreq++] );
	}

  // We simply sum all of the number of connexion eshtablished
  std::vector<MPI_Status> pending_status;
  pending_status.resize( currreq );

  MPI_Waitall( currreq, &pending_requests[0], &pending_status[0]);
	std::cout << "All the connexion are established ! " << std::endl;
}

Manager_node::~Manager_node() {
  for (int rank=0; rank < numtasks; rank++) {
    if ((rank != RANK_MANAGER_NODE) &&
        (rank != RANK_LOG_NODE)) {
      end_node(rank);
    }
  }
}

void Manager_node::start() {
  get_log_writer()(1) << "Manager_node::start()" << std::endl;

  PROGRESS_MSG("start correlating");
  initialise();
  current_correlator_node = 0;
  status = START_NEW_SCAN;
  while (status != END_NODE) {
    process_all_waiting_messages();

    switch (status) {
      case START_NEW_SCAN: {
        // set track information
        initialise_scan(control_parameters.scan(current_scan));

        // Set the input nodes to the proper start time
        for (size_t station=0; station < control_parameters.number_stations();
             station++) {
          Time station_time =
            input_node_get_current_time(control_parameters.station(station));
          if (station_time >
              start_time + integration_time() * integration_slice_nr) {
            integration_slice_nr = (int) ((station_time - start_time) / integration_time());
          }
        }

        // Check whether the new start time is before the stop time
        get_log_writer() << "START_TIME: " << start_time << std::endl;
        if (stop_time <= start_time) {
          status = STOP_CORRELATING;
          break;
        }
        for (size_t station=0; station < control_parameters.number_stations();
             station++) {
          input_node_set_time(control_parameters.station(station),
                              start_time + integration_time()*integration_slice_nr,
                              stop_time_scan);
        }
        status = START_CORRELATION_TIME_SLICE;
        break;
      }
      case START_CORRELATION_TIME_SLICE: {
        current_channel = 0;
        status = START_CORRELATOR_NODES_FOR_TIME_SLICE;
        break;
      }
      case START_CORRELATOR_NODES_FOR_TIME_SLICE: {
        bool added_correlator_node = false;
#ifdef SFXC_DETERMINISTIC

        if (correlator_node_ready[current_correlator_node]) {
          set_correlator_node_ready(current_correlator_node, false);
          start_next_timeslice_on_node(current_correlator_node);

          added_correlator_node = true;
        }
#else
        if (!ready_correlator_nodes.empty()) {
          start_next_timeslice_on_node(ready_correlator_nodes.front());
          ready_correlator_nodes.pop();
          added_correlator_node = true;
        }
#endif

        if (added_correlator_node) {
          if (current_channel == control_parameters.number_frequency_channels()) {
            status = GOTO_NEXT_TIMESLICE;
          }
        } else {
          // No correlator node added, wait for the next message
          check_and_process_message();
        }

        break;
      }
      case GOTO_NEXT_TIMESLICE: {
        integration_slice_nr += 1;
        PROGRESS_MSG("starting timeslice " << start_time+integration_time()*integration_slice_nr);
        // Check whether the integration slice continues past the stop time
        if (start_time + integration_time() * (integration_slice_nr + 1) >
            stop_time) {
          status = STOP_CORRELATING;

          // Check whether the integration slice continues past the scan
        } else if (start_time + integration_time() * (integration_slice_nr + 1) >
                   stop_time_scan) {
          // We can stop if we finished the last scan
          if (current_scan == control_parameters.number_scans()) {
            status = STOP_CORRELATING;
          } else {
            current_scan++;
            status = START_NEW_SCAN;
          }
        } else {
          // Just process the next time slice
          status = START_CORRELATION_TIME_SLICE;
        }
        break;
      }
      case STOP_CORRELATING: {
        // The status is set to END_NODE as soon as the output_node is ready
        MPI_Send(&output_slice_nr, 1, MPI_INT32,
                 RANK_OUTPUT_NODE, MPI_TAG_OUTPUT_NODE_CORRELATION_READY,
                 MPI_COMM_WORLD);

        status = WAIT_FOR_OUTPUT_NODE;
        break;
      }
      case WAIT_FOR_OUTPUT_NODE: {
        // The status is set to END_NODE as soon as the output_node is ready
        check_and_process_message();
        break;
      }
      case END_NODE: {
        break;
      }
    }
  }
  PROGRESS_MSG("terminating nodes");

  get_log_writer()(1) << "Terminating nodes" << std::endl;
}

void Manager_node::terminate()
{
	  PROGRESS_MSG("MESSAGE TERMINATE !");
		status = END_NODE;
}

void Manager_node::start_next_timeslice_on_node(int corr_node_nr) {
  int cross_channel = -1;
  if (control_parameters.cross_polarize()) {
    cross_channel = control_parameters.cross_channel(current_channel,
                    get_current_mode());
    SFXC_ASSERT((cross_channel == -1) || (cross_channel > (int)current_channel));
  }

  // Initialise the correlator node
  if (cross_channel == -1) {
    Time time = start_time + integration_time() * integration_slice_nr;
    get_log_writer()(1)
    << "start "
    << time.date_string()
    << ", channel " << current_channel << " to correlation node "
    << corr_node_nr << std::endl;
    PROGRESS_MSG("start "
                 << time.date_string()
                 << ", channel " << current_channel << " to correlation node "
                 << corr_node_nr);
  } else {
    Time time = start_time + integration_time()*integration_slice_nr;
    get_log_writer()(1)
    << "start "
    << time.date_string()
    << ", channel "
    << current_channel << ","
    << cross_channel << " to correlation node "
    << corr_node_nr << std::endl;
    PROGRESS_MSG("start "
                 << time.date_string()
                 << ", channel "
                 << current_channel << ","
                 << cross_channel << " to correlation node "
                 << corr_node_nr);
  }

  std::string channel_name =
    control_parameters.frequency_channel(current_channel);
  std::vector<std::string> station_name;
  Correlation_parameters correlation_parameters;
  int nr_stations = control_parameters.number_stations();
  for (int i=0; i<nr_stations; i++) {
    station_name.push_back(get_control_parameters().station(i));
  }
  std::string scan_name = control_parameters.scan(current_scan);
  correlation_parameters =
    control_parameters.
    get_correlation_parameters(scan_name,
                               channel_name,
                               station_name,
                               get_input_node_map());
  correlation_parameters.start_time =
    start_time + integration_time() * integration_slice_nr;
  correlation_parameters.stop_time  =
    start_time + integration_time() * (integration_slice_nr+1);
  correlation_parameters.integration_nr = integration_slice_nr;
  correlation_parameters.slice_nr = output_slice_nr;
  strncpy(correlation_parameters.source, control_parameters.scan_source(scan_name).c_str(), 11);
  correlation_parameters.pulsar_binning = control_parameters.pulsar_binning();
  correlation_parameters.n_phase_centers = n_sources_in_current_scan;
  correlation_parameters.cross_polarize = (cross_channel != -1);

  // Check the cross polarisation
  if (cross_channel != -1) {
    int n_stations = control_parameters.number_stations();
    int n_streams = correlation_parameters.station_streams.size();
    SFXC_ASSERT(n_stations == n_streams);
    // Add the cross polarisations
    for (int i=0; i<n_stations; i++) {
      Correlation_parameters::Station_parameters stream =
        correlation_parameters.station_streams[i];
      stream.station_stream += n_stations;
      correlation_parameters.station_streams.push_back(stream);
    }
  }

  correlator_node_set(correlation_parameters, corr_node_nr);

  // set the input streams
  size_t nStations = control_parameters.number_stations();
  for (size_t station_nr=0;
       station_nr< nStations;
       station_nr++) {
    input_node_set_time_slice(control_parameters.station(station_nr),
                              current_channel,
                              /*stream*/corr_node_nr,
                              correlation_parameters.start_time,
                              correlation_parameters.stop_time);

    if (cross_channel != -1) {
      // Add the cross polarisation channel
      input_node_set_time_slice(control_parameters.station(station_nr),
                                cross_channel,
                                /*stream*/corr_node_nr+n_corr_nodes,
                                correlation_parameters.start_time,
                                correlation_parameters.stop_time);
    }
  }

  current_channel ++;
  if (control_parameters.cross_polarize()) {
    // Go to the next channel.
    cross_channel =
      control_parameters.cross_channel(current_channel,
                                       get_current_mode());
    while ((current_channel <
            control_parameters.number_frequency_channels()) &&
           (cross_channel >= 0) && (cross_channel < (int)current_channel)) {
      current_channel ++;
      cross_channel =
        control_parameters.cross_channel(current_channel,
                                         control_parameters.get_mode(correlation_parameters.start_time));
    }
  }
#ifdef SFXC_DETERMINISTIC
  current_correlator_node = (current_correlator_node+1)%correlator_node_ready.size();
#endif
  output_slice_nr++;
}

void
Manager_node::initialise() {
  get_log_writer()(1) << "Initialising the Input_nodes" << std::endl;
  for (size_t station=0;
       station<control_parameters.number_stations(); station++) {
    // setting the first data-source of the first station
    const std::string &station_name = control_parameters.station(station);
    std::string filename = control_parameters.data_sources(station_name)[0];
    set_data_reader(input_rank(station_name), 0, filename);
  }
  // Get start of first scan and end of last scan in correlation
  const Vex vex = control_parameters.get_vex();
  Time t_begin, t_end;
  {
  Vex::Date start_time(control_parameters.get_start_time().date_string());
  Vex::Date stop_time(control_parameters.get_stop_time().date_string());
  // Find the name of the last scan
  std::string last_scan_name = vex.get_scan_name(start_time);
  Vex::Node::const_iterator it = vex.get_root_node()["SCHED"][last_scan_name];
  while (++it != vex.get_root_node()["SCHED"]->end()){
    Vex::Date start_scan = vex.start_of_scan(it.key());
    if(start_scan >= stop_time)
      break;

    last_scan_name = vex.get_scan_name(start_scan);
  }

  t_begin = Time(vex.get_start_time_of_scan(vex.get_scan_name(start_time)));
  t_end = Time(vex.get_stop_time_of_scan(last_scan_name));
  }

  // Send the delay tables:
  get_log_writer() << "Set delay_table" << std::endl;
  for (size_t station=0;
       station < control_parameters.number_stations(); station++) {
    Delay_table_akima delay_table;
    const std::string &station_name = control_parameters.station(station);
    const std::string &delay_file =
      control_parameters.get_delay_table_name(station_name); // also generates delay file if it doesn't exist
    delay_table.open(delay_file.c_str(), t_begin, t_end);
    if (!delay_table.initialised()) {
      DEBUG_MSG("Delay table could not be read");
      control_parameters.generate_delay_table(station_name, delay_file);
      delay_table.open(delay_file.c_str(), t_begin, t_end);
      if(!delay_table.initialised()){
        std::string msg = std::string("Couldn't generate delay table, please remove '") +
                          delay_file + std::string("' and restart the correlator");
        sfxc_abort(msg.c_str());
      }
    }
    // Get clock offset
    Vex::Node root = vex.get_root_node();
    std::string site_clock = root["STATION"][station_name]["CLOCK"]->to_string();
    double offset = root["CLOCK"][site_clock]["clock_early"][1]->to_double() / 1e6;
    double rate = root["CLOCK"][site_clock]["clock_early"][3]->to_double() / 1e6;
    std::string str_epoch = root["CLOCK"][site_clock]["clock_early"][2]->to_string();
    Time epoch(str_epoch);
    delay_table.set_clock_offset(offset, rate, epoch);
    send(delay_table, /* station_nr */ 0, input_rank(station));
    correlator_node_set_all(delay_table, station_name);
  }

  // Send the UVW tables:
  get_log_writer() << "Set uvw_table" << std::endl;
  for (size_t station=0;
       station < control_parameters.number_stations(); station++) {
    Uvw_model uvw_table;
    const std::string &station_name = control_parameters.station(station);
    const std::string &delay_file =
      control_parameters.get_delay_table_name(station_name);
    uvw_table.open(delay_file.c_str(), t_begin, t_end);

    correlator_node_set_all(uvw_table, station_name);
  }

  start_time = control_parameters.get_start_time();
  stop_time = control_parameters.get_stop_time();
  // Get a list of all scan names
  current_scan = control_parameters.scan(start_time.date_string());
  SFXC_ASSERT(current_scan >= 0);
  SFXC_ASSERT((size_t)current_scan < control_parameters.number_scans());

  if(control_parameters.pulsar_binning()){
    // If pulsar binning is enabled : get all pulsar parameters (polyco files, etc.)
    if (!control_parameters.get_pulsar_parameters(pulsar_parameters))
      sfxc_abort("Error parsing pulsar information from control file\n");
    correlator_node_set_all(pulsar_parameters);
    // Set the output files (minimum of two bins for off-pulse data)
    int max_nbins=2;
    std::map<std::string, Pulsar_parameters::Pulsar>::iterator it;
    for ( it=pulsar_parameters.pulsars.begin() ; it != pulsar_parameters.pulsars.end(); it++ ){
      max_nbins = std::max(it->second.nbins, max_nbins);
    }
    std::string base_filename = control_parameters.get_output_file();
    // Open one output file per pulsar bin
    for(int bin=0;bin<max_nbins;bin++){
      std::ostringstream outfile;
      outfile << base_filename << ".bin" << bin;
      set_data_writer(RANK_OUTPUT_NODE, bin, outfile.str());
    }
  }else if(control_parameters.multi_phase_center()){
    SFXC_ASSERT(!control_parameters.pulsar_binning());
    // build a list of all source in the current job
    const Vex vex = control_parameters.get_vex();

    Vex::Date start_time(control_parameters.get_start_time().date_string());
    Vex::Date stop_time(control_parameters.get_stop_time().date_string());
    std::string first_scan_name = vex.get_scan_name(start_time);
    Vex::Node::const_iterator it = vex.get_root_node()["SCHED"][first_scan_name];
    std::set<std::string> sources;
    std::cout << "Getting sources starting scan " << first_scan_name << " ; " << it.key()
              << ", stop_time = " << stop_time.to_string()
              << ", tstart = " << vex.start_of_scan(it.key()).to_string() << "\n";
    while(it != vex.get_root_node()["SCHED"]->end() && vex.start_of_scan(it.key()) < stop_time){
      Vex::Node::const_iterator sources_it = it->begin("source");
      std::cout << "scan " << it.key() << "\n";
      while(sources_it != it->end("source")){
        sources.insert(sources_it->to_string());
        std::cout << "found source " << sources_it->to_string() << " in scan " << it.key() << "\n";
        sources_it++;
      }
      it++;
    }
    correlator_node_set_all(sources);

    // open one output file per source
    std::string base_filename = control_parameters.get_output_file();
    std::set<std::string>::iterator sources_it = sources.begin();
    int source_nr=0;
    while(sources_it != sources.end()){
      set_data_writer(RANK_OUTPUT_NODE, source_nr, base_filename + "_" + *sources_it);
      sources_it++;
      source_nr++;
    }
  }else
    set_data_writer(RANK_OUTPUT_NODE, 0, control_parameters.get_output_file());

  // Write the global header in the outpul file
  send_global_header();

  output_slice_nr = 0;

  PROGRESS_MSG("start_time: " << start_time.date_string());
  PROGRESS_MSG("stop_time: " << stop_time.date_string());

  get_log_writer()(1) << "Starting correlation" << std::endl;
}

void Manager_node::initialise_scan(const std::string &scan) {
  Vex::Date start_of_scan =
    control_parameters.get_vex().start_of_scan(scan);
  Vex::Date stop_of_scan =
    control_parameters.get_vex().stop_of_scan(scan);

  int start_mjd = mjd(1, 1, start_of_scan.year) + start_of_scan.day - 1;
  Time scan_start(start_mjd, start_of_scan.to_miliseconds() / 1000.);

  // set the start time to the beginning of the scan
  if (start_time + integration_time() * integration_slice_nr < scan_start) {
    Time start_interval = scan_start - start_time;
    integration_slice_nr = (int) (start_interval / integration_time());
    if ((start_interval % integration_time()) != Time()) {
      integration_slice_nr ++;
    }
  }

  int stop_mjd = mjd(1, 1, stop_of_scan.year) + stop_of_scan.day - 1;
  stop_time_scan = Time(stop_mjd, stop_of_scan.to_miliseconds() / 1000.);

  if (stop_time < stop_time_scan)
    stop_time_scan = stop_time;
  // Align the stop time with the time slices
  SFXC_ASSERT(((stop_time_scan-start_time)%integration_time()) >= Time());
  stop_time_scan -= (stop_time_scan-start_time)%integration_time();
  SFXC_ASSERT(((stop_time_scan-start_time)%integration_time()) == Time());


  // Send the track parameters to the input nodes
  const std::string &mode_name =
    control_parameters.get_vex().get_mode(scan);
  for (size_t station=0;
       station<control_parameters.number_stations(); station++) {
    const std::string &station_name =
      control_parameters.station(station);

    Input_node_parameters input_node_param =
      control_parameters.get_input_node_parameters(mode_name, station_name);
    input_node_set(station_name, input_node_param);
  }
  n_sources_in_current_scan = control_parameters.get_vex().n_sources(scan);
}

void Manager_node::end_correlation() {
  SFXC_ASSERT(status == WAIT_FOR_OUTPUT_NODE);
  status = END_NODE;
}

std::string Manager_node::get_current_mode() const {
  std::string scan_name = control_parameters.scan(current_scan);
  return control_parameters.get_vex().get_mode(scan_name);
}

void Manager_node::send_global_header(){ 
    // Send the global header
    Output_header_global output_header;
    memset(&output_header, 0, sizeof(Output_header_global));
    output_header.header_size = sizeof(Output_header_global);

    strcpy(output_header.experiment,control_parameters.experiment().c_str());      // Name of the experiment
    Time start = control_parameters.get_start_time();
    int start_year, start_day;
    // Start year and day (day of year) of the experiment
    start.get_date(start_year, start_day);
    output_header.start_year = start_year;      // Start year of the experiment
    output_header.start_day = start_day;        // Start day of the experiment (day of year)
    output_header.start_time = (int)start.get_time();
    // Start time of the correlation in seconds since
    // midnight
    output_header.number_channels = control_parameters.number_channels();  // Number of frequency channels
    Time int_time = control_parameters.integration_time();// Integration time: microseconds
    output_header.integration_time = (int)int_time.get_time_usec();
    output_header.output_format_version = OUTPUT_FORMAT_VERSION;
    output_header.correlator_version = atoi(SVN_VERSION);

    output_header.polarisation_type =
      control_parameters.polarisation_type_for_global_output_header();
    // 3 bytes left:
    output_header.empty[0] = 0;
    output_header.empty[1] = 0;
    output_header.empty[2] = 0;

    output_node_set_global_header((char *)&output_header,
                                  sizeof(Output_header_global));
  }
