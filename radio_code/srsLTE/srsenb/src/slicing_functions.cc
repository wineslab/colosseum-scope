// Slicing functions used by SCOPE

#include "srsenb/hdr/slicing_functions.h"
#include "srsenb/hdr/global_variables.h"

// for flock
#include <sys/file.h>

// for fileno
#include <stdio.h>
#include <cstdlib>
#include <cstring>

// for access and fchmod
#include <unistd.h>
#include <sys/stat.h>

#include <string>
#include <sstream>
#include <inttypes.h>
#include <sys/timeb.h>

#include <sstream>
#include <srsenb/hdr/metrics_functions.h>


// read slice allocation mask from configuration file
// since mask is very long return it as an array instead of a single int variable
// NOTE: function returns without any error if slicing configuration file is not found
void read_slice_allocation_mask(int slice_idx, std::string config_dir_path, std::string file_name, uint8_t slice_mask[], int mask_length, int line_to_read) {

    // file variables
    FILE* config_file;

    std::string config_file_name;

    // initialize array
    for (int i = 0; i < mask_length; ++i)
        slice_mask[i] = -1;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    // return variable for flock
    int ret_fl;

    // line number variable
    int line_no = 0;

    // form absolute path
    config_file_name = config_dir_path;
    config_file_name += file_name;

    // open configuration file and temporary file
    config_file = fopen(config_file_name.c_str(), "r");

    if (config_file == NULL) {
        if (slice_idx == 0)
            printf("read_slice_allocation_mask: configuration file not found (%s)!\n", config_file_name.c_str());
        return;
    }

    // lock file and check flock return value
    ret_fl = flock(fileno(config_file), LOCK_EX);
    if (ret_fl == -1) {
        fclose(config_file);
        printf("read_slice_allocation_mask: flock return value is -1\n");
        return;
    }

    while ((num_read_elem = getline(&line, &len, config_file)) != -1) {
        // copy line to find user id
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // save slice mask one digit at a time into array
        if (line_no == line_to_read) {
            for (int i = 0; i < mask_length; ++i) {
                // convert char to uint8_t and save it into array
                slice_mask[i] = (uint8_t) (line_copy[i] - '0');
            }

            break;
        }

        line_no++;
    }

    if (line)
        free(line);

    fclose(config_file);
    flock(fileno(config_file), LOCK_UN);

    // read from beginning if exceeded number of lines in file
    // num_read_elem equals to 1 if reading a blank line '\n'
    if (line_no < line_to_read || num_read_elem <= 1)
        read_slice_allocation_mask(slice_idx, config_dir_path, file_name, slice_mask, mask_length, 0);
}

// get slicing allocation mask from configuration file
void get_slicing_allocation_mask(int slice_idx, Slice_Tenants* slicing_struct, int line_to_read) {

    int rbg;

    // total number of PRBs assigned to this slicing mask
    int prb_mask_tot = 0;
    int rbg_mask_last_bit = 0;

    // initialize array to return
    uint8_t slice_mask[MAX_MASK_LENGTH];
    uint8_t ul_slice_mask[MAX_MASK_LENGTH];

    // Initialize slice_alloc_mask
    for (rbg = 0; rbg < MAX_MASK_LENGTH; ++rbg) {
        slice_mask[rbg] = 0;

        slicing_struct->slicing_mask[rbg] = 0;
    }

    // form filename
    std::string slicing_filename;
    std::string ul_slicing_filename;
    std::ostringstream oss;
    oss << "slice_allocation_mask_tenant_" << slice_idx << ".txt";
    slicing_filename = oss.str();
    ul_slicing_filename = "ul_" + oss.str();
    oss.clear();

    // read slicing allocation mask
    // NOTE: function returns without any error if slicing configuration file is not found
    std::string slicing_dir_path;
    slicing_dir_path = SCOPE_CONFIG_DIR;
    slicing_dir_path += "slicing/";
    read_slice_allocation_mask(slice_idx, slicing_dir_path, slicing_filename, slice_mask, MAX_MASK_LENGTH, line_to_read);
    read_slice_allocation_mask(slice_idx, slicing_dir_path, ul_slicing_filename, ul_slice_mask, MAX_MASK_LENGTH, line_to_read);

    // only save active RBGs and set to 0 the rest
    for (rbg = 0; rbg < cell_rbgs; ++rbg) {
        slicing_struct->slicing_mask[rbg] = slice_mask[rbg];
        slicing_struct->ul_slicing_mask[rbg] = ul_slice_mask[rbg];

        // add total number of RBGs
        prb_mask_tot += slice_mask[rbg];

        if (rbg == cell_rbgs - 1)
            rbg_mask_last_bit = slice_mask[rbg];
    }

    // convert RBGs to PRBs
    prb_mask_tot *= get_prbs_per_rbg();

    // adjust number of PRBs if the last mask bit has been allocated (set to 1)
    if (rbg_mask_last_bit == 1) {
        if (cell_prbs_global == 15 || cell_prbs_global == 25 || cell_prbs_global == 50 || cell_prbs_global == 75)
            prb_mask_tot -= 1;
    }

    // save information into structure
    slicing_struct->slice_id = slice_idx;

    if (prb_mask_tot <= cell_prbs_global)
        slicing_struct->slice_prbs = prb_mask_tot;
    else
        slicing_struct->slice_prbs = 0;     // slice is not active
}

// read parameter from configuration file
float read_config_parameter(std::string config_dir_path, std::string file_name, std::string param_name){

    // configuration file
    FILE *config_file;

    // define filename and build absolute path
    std::string config_file_name;
    config_file_name = config_dir_path;
    config_file_name += file_name;

    int i, j;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    std::string in_line_delimiter = "::";

    char* param_read;
    char* value_read;

    // default gain values
    float output_value = -1.0;

    // open configuration file
    config_file = fopen(config_file_name.c_str(), "r");

    while ((num_read_elem = getline(&line, &len, config_file)) != -1) {

        // copy line to find gain type
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // get gain type and value
        param_read = strtok(line_copy, in_line_delimiter.c_str());
        value_read = strtok(NULL, in_line_delimiter.c_str());

        if (strcmp(param_read, param_name.c_str()) == 0) {
            // convert string to float
            output_value = strtof(value_read, (char **) NULL);
        }
    }

    if (line)
        free(line);

    fclose(config_file);

    return output_value;
}

// get slicing structure from slice number
Slice_Tenants* get_slicing_structure(int slice_id) {

    for (int s_idx = 0; s_idx < MAX_SLICING_TENANTS; ++s_idx) {
        if (slicing_structure[s_idx].slice_id == slice_id)
            return &slicing_structure[s_idx];
    }

    return nullptr;
}
