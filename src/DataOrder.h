/*
 * DataOrder.h
 *
 *  Created on: 16 Apr 2018
 *      Author: ban115
 */

#ifndef DATAORDER_H_
#define DATAORDER_H_

// Dimension labels: time (T), frequency (F), beam (B), and polarization (P).
enum DataOrder { DataOrder_TFBP = 0, DataOrder_FTBP = 1, DataOrder_BPFT = 2, DataOrder_BPTF = 3 };

#endif /* DATAORDER_H_ */
