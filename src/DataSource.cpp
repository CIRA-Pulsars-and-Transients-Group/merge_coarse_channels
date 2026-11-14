/*
 * DataSource.cpp
 *
 *  Created on: 1 Nov 2016
 *      Author: ban115
 */

#include "DataSource.h"


DataSource::DataSource() {
}

DataSource::~DataSource() {
}

double DataSource::current_mjd() {
  // Convert the elapsed time in seconds to days before adding it to the start MJD.
  double mjd = tstart() + current_sample()*tsamp()/86400.0;
  
  return mjd;
}
