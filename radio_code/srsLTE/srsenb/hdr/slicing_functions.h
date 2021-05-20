#ifndef SRSLTE_SLICING_FUNCTIONS_H
#define SRSLTE_SLICING_FUNCTIONS_H

#endif //SRSLTE_SLICING_FUNCTIONS_H

#include <string>
#include <inttypes.h>
#include <srsenb/hdr/stack/mac/scheduler_metric.h>
#include "global_variables.h"

void read_slice_allocation_mask(int slice_idx, std::string config_dir_path, std::string file_name, uint8_t slice_mask[],
                                int mask_length, int line_to_read);
void get_slicing_allocation_mask(int slice_idx, Slice_Tenants* slicing_struct, int line_to_read);
float read_config_parameter(std::string config_dir_path, std::string file_name, std::string param_name);

// get slicing structure from slice number
Slice_Tenants* get_slicing_structure(int slice_id);
