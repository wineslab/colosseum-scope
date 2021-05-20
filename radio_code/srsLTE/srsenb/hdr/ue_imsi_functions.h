#ifndef SRSLTE_UE_IMSI_FUNCTIONS_H
#define SRSLTE_UE_IMSI_FUNCTIONS_H

#endif //SRSLTE_UE_IMSI_FUNCTIONS_H

#include <string>
#include <inttypes.h>

void write_imsi_rnti(long long unsigned int ue_imsi, int ue_rnti, std::string config_dir_path,
                     std::string config_dir_path_permanent);
void remove_imsi_rnti(long long unsigned int ue_imsi, std::string config_dir_path);
float get_value_from_imsi(long long unsigned int ue_imsi, std::string config_dir_path, std::string file_name);
long long unsigned int get_imsi_from_rnti(uint16_t ue_rnti, std::string config_dir_path, std::string file_name);
