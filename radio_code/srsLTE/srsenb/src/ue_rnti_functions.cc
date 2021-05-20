// Functions related to user RNTI used by SCOPE

#include "srsenb/hdr/ue_rnti_functions.h"
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

#include <srslte/phy/common/phy_common.h>
#include <sstream>

// Write user rnti and default parameter values on file - use decimal value of actual hex rnti
// write RNTI and Slice_ID in permanent file that is NOT removed
// when user disconnects (useful for offline data processing)
void write_user_parameters_on_file(int ue_rnti, int ue_slice)
{
    // file containing the default initial power for the users
    FILE *default_power_file;

    // SCOPE: add user slice to file name
    std::string config_dir_path_power_multiplier;
    std::ostringstream oss;
    oss << SCOPE_CONFIG_DIR;
    oss << "/config/ue_config_power_multiplier_slice_" << ue_slice << ".txt";
    config_dir_path_power_multiplier = oss.str();
    oss.clear();

    std::string config_dir_path_scheduling = SCOPE_CONFIG_DIR;
    config_dir_path_scheduling += "config/ue_config_scheduling.txt";

    // define filename of char array
    char file_name_default_power[100];

    float ue_power_multiplier_default = 1.0f;   // default value for ue_power multiplier
    float ue_default_scheduler = -1;            // default value for ue_scheduler

    // write user rnti and power multiplier on file
    write_rnti_and_value(ue_rnti, ue_power_multiplier_default, config_dir_path_power_multiplier);

    // write user rnti and scheduling value on file
    write_rnti_and_value(ue_rnti, ue_default_scheduler, config_dir_path_scheduling);
}

// Write user rnti and value on file in the form: rnti::value
void write_rnti_and_value(int ue_rnti, float ue_value_default, std::string config_dir_path){

    // file to read ue-specific power allocation parameter from
    FILE *ue_config_file;

    // define filename char arrays
    std::string file_name;

    float read_ue_value;
    std::string in_line_delimiter = "::";

    // return variable for flock
    int ret_fl;

    // form absolute path
    file_name = config_dir_path;

    // read user parameter value from file and check if it is admissible, i.e., in [0, 10000].
    // if yes, then the user rnti is already present in the file, otherwise add it
    read_ue_value = read_ue_value_from_file(ue_rnti, file_name);

    // write user and power offset on file
    if (read_ue_value > 10000) {
        // open configuration file
        ue_config_file = fopen(file_name.c_str(), "a");

        // check if file exists
        if (ue_config_file == NULL) {
            printf("Configuration file path does not exist in pdcp.c/write_rnti_and_value, exiting execution\n");
            exit(0);
        }

        // lock file and check return value
        ret_fl = flock(fileno(ue_config_file), LOCK_EX);
        if (ret_fl == -1) {
            fclose(ue_config_file);
            printf("write_rnti_and_value: flock return value is -1\n");
            exit(0);
        }

        // write actual data on file
        fprintf(ue_config_file, "%d%s%.2f\n", ue_rnti, in_line_delimiter.c_str(), ue_value_default);

        fclose(ue_config_file);
        flock(fileno(ue_config_file), LOCK_UN);
    }
}

// Read user-specific parameter from configuration file
// returns user-specific parameter value if user is in the list, a value > 10000 if not
float read_ue_value_from_file(int ue_rnti, std::string file_name) {

    // File to read user-specific parameter value from
    FILE *ue_config_file;

    // Initialize value parameters to non-admissible value
    float ue_value = 20000;
    int read_ue_rnti = -1;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;
    std::string in_line_delimiter = "::";

    // Other variables
    unsigned int i, j;

    // return variable for flock
    int ret_fl;

    /*
     * Begin read of user-specific value from file
     */

    if (access(file_name.c_str(), F_OK ) != 0) {
        // file does not exist, create it with the right permissions
        int fid = creat(file_name.c_str(), 0666);
        fchmod(fid, 0666);
    }
    else {

        // open configuration file
        ue_config_file = fopen(file_name.c_str(), "r");

        // lock file and check return value
        ret_fl = flock(fileno(ue_config_file), LOCK_EX);
        if (ret_fl == -1) {
            fclose(ue_config_file);
            printf("read_ue_value_from_file: flock return value is -1\n");
            exit(0);
        }

        while ((num_read_elem = getline(&line, &len, ue_config_file)) != -1) {
            // skip lines starting with # as they are considered comments
            if (line[0] == '#')
                continue;

            // copy line to find user id
            char line_copy[num_read_elem + 1];
            strncpy(line_copy, line, num_read_elem);
            line_copy[num_read_elem] = '\0';

            // get user id and value
            char *delimiter_ptr = strtok(line_copy, in_line_delimiter.c_str());
            read_ue_rnti = strtol(line, &delimiter_ptr, 10);

            if (read_ue_rnti == ue_rnti) {
                // copy line to find user id
                char line_copy_val[num_read_elem + 1];
                strncpy(line_copy_val, line, num_read_elem);
                line_copy_val[num_read_elem] = '\0';

                // shift string to remove initial part (user id and delimiter)
                j = strlen(line_copy) + in_line_delimiter.length();
                for (i = j; i < strlen(line_copy_val); i++)
                    line_copy_val[i - j] = line_copy_val[i];

                // cut end of string
                for (i = strlen(line_copy_val) - j; line_copy_val[i] != '\0'; i++)
                    line_copy_val[i] = '\0';

                // convert string to int and then to uint16_t
                sscanf(line_copy_val, "%f", &ue_value);

                break;
            }
        }

        if (line)
            free(line);

        fclose(ue_config_file);
        flock(fileno(ue_config_file), LOCK_UN);
    }

    /*
     * End read of user-specific value from file
     */

    return ue_value;
}

// remove disconnected user from list
void remove_ue_from_list(int ue_rnti, std::string config_dir_path, std::string config_file_name) {

    FILE* file;
    FILE* temp_file;

    std::string file_name;
    std::string temp_file_name;

    std::string in_line_delimiter = "::";

    // read user rnti
    int read_ue_rnti = -1;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    // return variable for flock
    int ret_fl1;
    int ret_fl2;

    // get path of environment variable $HOME and get absolute path
    file_name = config_dir_path + config_file_name;
    temp_file_name = config_dir_path + "temp.txt";

    // open metric file and temporary file
    file = fopen(file_name.c_str(), "r");

    // create temp_file with the right permissions
    if (access(temp_file_name.c_str(), F_OK ) != 0) {
        int fid = creat(temp_file_name.c_str(), 0666);
        fchmod(fid, 0666);
    }

    temp_file = fopen(temp_file_name.c_str(), "w");

    // check if file exists
    if (file == NULL) {
        return;
    }

    // Lock files and check return values
    ret_fl1 = flock(fileno(file), LOCK_EX);
    ret_fl2 = flock(fileno(temp_file), LOCK_EX);

    if (ret_fl1 == -1 || ret_fl2 == -1) {
        fclose(file);
        fclose(temp_file);
        remove(temp_file_name.c_str());
        printf("remove_ue_from_list: flock return value is -1\n");
        return;
    }

    while ((num_read_elem = getline(&line, &len, file)) != -1) {
        // copy line to find user id
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // get user id and value
        char *delimiter_ptr = strtok(line_copy, in_line_delimiter.c_str());
        read_ue_rnti = strtol(line, &delimiter_ptr, 10);

        if (read_ue_rnti != ue_rnti) {
            // write line on temp_file
            fputs(line, temp_file);
        }
    }

    if (line)
        free(line);

    fclose(file);
    fclose(temp_file);

    flock(fileno(file), LOCK_UN);
    flock(fileno(temp_file), LOCK_UN);

    // remove metric_file and rename temp_file
    remove(file_name.c_str());
    rename(temp_file_name.c_str(), file_name.c_str());
}

// get user slice from user structure instead of reading it from config file
int get_slice_from_rnti(int ue_rnti) {

    int ue_array_idx = get_ue_idx_from_rnti(ue_rnti);
    return ue_resources[ue_array_idx].slice_id;
}

// get scheduling policy for current slice
int get_scheduling_policy_from_slice(int slice_id) {

    // slicing database filename
    std::string slicing_file_name = SCOPE_CONFIG_DIR;
    slicing_file_name += "slicing/slice_scheduling_policy.txt";

    int sched_policy = (int) read_ue_value_from_file(slice_id, slicing_file_name);

    // default to 0 if value not found in file
    if (sched_policy >= 20000) {
        sched_policy = 0;
        printf("get_scheduling_policy_from_slice: scheduling policy for slice %d not found in config file!\n", slice_id);
    }

    return sched_policy;
}

// get user index in array structure
int get_ue_idx_from_rnti(int rnti) {
    return rnti - FIRST_VALID_USER_RNTI;
}

// check if rnti is user
bool is_user(int rnti) {

  if (rnti == SRSLTE_SIRNTI || rnti == SRSLTE_PRNTI || rnti == SRSLTE_MRNTI ||
      rnti < FIRST_VALID_USER_RNTI || rnti > MAX_USER_RNTI) {
    return false;
  }

  return true;
}
