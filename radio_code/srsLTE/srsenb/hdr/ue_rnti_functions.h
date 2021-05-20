#ifndef SRSLTE_UE_RNTI_FUNCTIONS_H
#define SRSLTE_UE_RNTI_FUNCTIONS_H

#include <string>

void write_user_parameters_on_file(int ue_rnti, int ue_slice);
void write_rnti_and_value(int ue_rnti, float ue_value_default, std::string config_dir_path);
float read_ue_value_from_file(int ue_rnti, std::string file_name);
void remove_ue_from_list(int ue_rnti, std::string config_dir_path, std::string config_file_name);
int get_scheduling_policy_from_slice(int slice_id);
int get_slice_from_rnti(int ue_rnti);
int get_ue_idx_from_rnti(int rnti);
bool is_user(int rnti);

#endif //SRSLTE_UE_RNTI_FUNCTIONS_H
