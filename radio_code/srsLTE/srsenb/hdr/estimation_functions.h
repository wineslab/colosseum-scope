#ifndef SRSLTE_ESTIMATION_FUNCTIONS_H
#define SRSLTE_ESTIMATION_FUNCTIONS_H

#include <stdint.h>

double compute_ue_dl_channel_coefficient(uint16_t rnti, double tx_data_power_avg);

#endif //SRSLTE_ESTIMATION_FUNCTIONS_H
