/* Copyright (c) 2007 Joint Institute for VLBI in Europe (Netherlands)
 * All rights reserved.
 *
 * Author(s): Ruud Oerlemans  <Oerlemans@JIVE.nl>, 2007
 *            Nico Kruithof   <Kruithof@JIVE.nl>, 2007
 *            Huseyin Ozdemir <Ozdemir@JIVE.nl>, 2007
 *
 * $Id$
 *
 * Class function definitions for station specific data
 */

//standard c includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

//includes for system calls
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

//c++ includes
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>

#include "delay_table_akima.h"
#include "utils.h"

//*****************************************************************************
//function definitions
//*****************************************************************************

// Default constructor
Delay_table_akima::Delay_table_akima()
    : begin_scan(0), end_scan(0), acc(NULL), splineakima(NULL) {}

// Copy constructor
Delay_table_akima::Delay_table_akima(const Delay_table_akima &other)
    : begin_scan(0), end_scan(0), acc(NULL), splineakima(NULL) {
  Delay_table_akima();
  assert(splineakima == NULL);
  times = other.times;
  delays = other.delays;
}

// Destructor
Delay_table_akima::~Delay_table_akima() {}

void Delay_table_akima::operator=(const Delay_table_akima &other) {
  Delay_table_akima();
  assert(splineakima == NULL);
  times = other.times;
  delays = other.delays;
  initialise_next_scan();
}

bool Delay_table_akima::operator==(const Delay_table_akima &other) const {
  if (times != other.times) return false;
  if (delays != other.delays) return false;
  return true;
}

//read the delay table, do some checks and
//calculate coefficients for parabolic interpolation
int Delay_table_akima::open(const char *delayTableName) {
  std::ifstream in(delayTableName);
  assert(in.is_open());
  int32_t header_size;

  // Read the header
  in.read(reinterpret_cast < char * > (&header_size), sizeof(int32_t));
  char header[header_size];
  in.read(reinterpret_cast < char * > (header), header_size*sizeof(char));

  // Read the data
  double line[5];
  while (in.read(reinterpret_cast < char * > (line), 5*sizeof(double))) {
    assert(line[4] <= 0);
    // The time read from file is in seconds, whereas the software correlator
    // works with times in microseconds
    times.push_back(line[0]*1000000);
    delays.push_back(line[4]);
  }

  // Initialise
  begin_scan = 0;
  end_scan   = 0;
  bool result = initialise_next_scan();
  if (!result) {
    DEBUG_MSG("Could not read delay table " << delayTableName);
  }

  return 0;
}

bool Delay_table_akima::initialise_next_scan() {
  if (times.empty()) return false;
  // make end_scan point to the start of the next scan
  if (end_scan != 0) end_scan += 2;
  begin_scan = end_scan;

  if (end_scan >= times.size()) return false;

  // next_end_scan is the past-the-end iterator of the next scan
  while ((end_scan < times.size()) && (times[end_scan] != 0)) {
    assert(delays[end_scan] <= 0);
    end_scan ++;
  }
  if (end_scan >= times.size()) return false;

  if (splineakima != NULL) {
    assert(acc != NULL);
    gsl_spline_free(splineakima);
    gsl_interp_accel_free(acc);
    splineakima = NULL;
  }

  // Initialise the Akima spline
  acc = gsl_interp_accel_alloc();
  int n_pts = end_scan - begin_scan - 1;
  // at least 4 sample points for a spline
  assert(n_pts > 4);

  // End scan now points to the beginning of the next scan and
  // the next scan has n_pts data points
  splineakima = gsl_spline_alloc(gsl_interp_akima, n_pts);

  assert(delays[begin_scan] != 0);
  assert(delays[begin_scan+n_pts] != 0);
  gsl_spline_init(splineakima,
                  &times[begin_scan],
                  &delays[begin_scan],
                  n_pts);

  return true;
}


//calculates the delay for the delayType at time in microseconds
//get the next line from the delay table file
double Delay_table_akima::delay(int64_t time) {
  if (times.empty()) {
    DEBUG_MSG("times.empty()");
    assert(!times.empty());
  }
  while (times[end_scan-1] < time) {
    bool result = initialise_next_scan();
    assert(result);
  }
  assert(splineakima != NULL);
  double result = gsl_spline_eval (splineakima, time, acc);
  assert(result < 0);
  return result;
}

int64_t Delay_table_akima::start_time_scan() {
  assert(begin_scan<times.size());
  return (int64_t)times[begin_scan];
}
int64_t Delay_table_akima::stop_time_scan() {
  assert(end_scan<times.size());
  return (int64_t)times[end_scan-1];
}

std::ostream &
operator<<(std::ostream &out, const Delay_table_akima &delay_table) {
  assert(delay_table.times.size() == delay_table.delays.size());
  for (size_t i=0; i<delay_table.times.size(); i++) {
    out << delay_table.times[i] << " \t" << delay_table.delays[i] << "\n";
  }
  return out;
}
