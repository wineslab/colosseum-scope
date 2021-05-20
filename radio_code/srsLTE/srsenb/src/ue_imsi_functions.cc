// Functions related to user IMSI used by SCOPE

#include "srsenb/hdr/ue_imsi_functions.h"

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
#include <srsenb/hdr/ue_rnti_functions.h>
#include <iostream>


// write IMSI::RNTI on configuration file
// also save IMSI and RNTI in permanent file that is NOT removed
// when user disconnects (useful for offline data processing)
void write_imsi_rnti(long long unsigned int ue_imsi, int ue_rnti, std::string config_dir_path,
                     std::string config_dir_path_permanent) {

    // file to read ue-specific values from
    FILE* config_file;
    FILE* temp_file;
    FILE* permanent_config_file;

    // define filename
    std::string config_file_name;
    std::string temp_file_name;
    std::string permanent_file_name;

    float read_ue_value;
    std::string in_line_delimiter = "::";

    // variable indicating if the file existed before
    int file_existed_before;

    // variable to check if user is already present on file
    int rnti_already_present = 0;

    // read user rnti
    long long unsigned int read_ue_imsi = 0;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    // return variable for flock
    int ret_fl1;
    int ret_fl2;
    int ret_fl3;

    // concatenate strings to form absolute path
    config_file_name = config_dir_path;
    config_file_name += "ue_imsi_rnti.txt";

    temp_file_name = config_dir_path;
    temp_file_name += "temp_imsi.txt";

    permanent_file_name = config_dir_path_permanent;
    permanent_file_name += "ue_imsi_rnti_permanent.txt";

    // open metric files and temporary file
    config_file = fopen(config_file_name.c_str(), "r");
    temp_file = fopen(temp_file_name.c_str(), "w");
    permanent_config_file = fopen(permanent_file_name.c_str(), "a");

    // check if file exists
    if (config_file == NULL) {
        // create empty configuration file
        config_file = fopen(config_file_name.c_str(), "w");
        fclose(config_file);
        config_file = fopen(config_file_name.c_str(), "r");

        file_existed_before = 0;
    } else {
        file_existed_before = 1;
    }

    // lock files and check return value
    ret_fl1 = flock(fileno(config_file), LOCK_EX);
    ret_fl2 = flock(fileno(temp_file), LOCK_EX);
    ret_fl3 = flock(fileno(permanent_config_file), LOCK_EX);

    if (ret_fl1 == -1 || ret_fl2 == -1 || ret_fl3 == -1) {
        fclose(config_file);
        fclose(temp_file);
        fclose(permanent_config_file);
        remove(temp_file_name.c_str());
        printf("write_imsi_rnti: flock return value is -1\n");
        return;
    }

    while ((num_read_elem = getline(&line, &len, config_file)) != -1) {
        // copy line to find user id
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // get user id and value
        char *delimiter_ptr = strtok(line_copy, in_line_delimiter.c_str());
        read_ue_imsi = strtoull(line, &delimiter_ptr, 10);

        if (read_ue_imsi == ue_imsi) {
            // write new_line on temp_file
            fprintf(temp_file, "%llu%s%d\n", ue_imsi, in_line_delimiter.c_str(), ue_rnti);

            rnti_already_present = 1;
        } else {
            // copy old line on file
            fprintf(temp_file, "%s", line);
        }
    }

    // write new_line on file if file did not exist before or if user rnti was not already present
    if (!file_existed_before || !rnti_already_present) {
        fprintf(temp_file, "%llu%s%d\n", ue_imsi, in_line_delimiter.c_str(), ue_rnti);
    }

    // write IMSI and RNTI on permanent config file
    fprintf(permanent_config_file, "%llu%s%d\n", ue_imsi, in_line_delimiter.c_str(), ue_rnti);

    if (line)
        free(line);

    fclose(config_file);
    fclose(temp_file);
    fclose(permanent_config_file);

    flock(fileno(config_file), LOCK_UN);
    flock(fileno(temp_file), LOCK_UN);
    flock(fileno(permanent_config_file), LOCK_UN);

    // remove metric_file and rename temp_file
    remove(config_file_name.c_str());
    rename(temp_file_name.c_str(), config_file_name.c_str());

    chmod(config_file_name.c_str(), 0666);
}

// remove IMSI::RNTI from configuration file
void remove_imsi_rnti(long long unsigned int ue_imsi, std::string config_dir_path) {

    // file variables
    FILE* config_file;
    FILE* temp_file;

    std::string config_file_name;
    std::string temp_file_name;

    std::string in_line_delimiter = "::";

    long long unsigned int read_ue_imsi = 0;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    // return variable for flock
    int ret_fl1;
    int ret_fl2;

    // build absolute paths
    config_file_name = config_dir_path;
    config_file_name += "ue_imsi_rnti.txt";

    temp_file_name = config_dir_path;
    temp_file_name += "temp_imsi_rm.txt";

    // open configuration file and temporary file
    config_file = fopen(config_file_name.c_str(), "r");

    // create temp_file and set permissions
    temp_file = fopen(temp_file_name.c_str(), "w");

    // check if file exists. If it does not exist just remove temp_file and exit
    if (config_file == NULL) {
        remove(temp_file_name.c_str());
        return;
    }

    // lock files and check return value
    ret_fl1 = flock(fileno(config_file), LOCK_EX);
    ret_fl2 = flock(fileno(temp_file), LOCK_EX);

    if (ret_fl1 == -1 || ret_fl2 == -1) {
        fclose(config_file);
        fclose(temp_file);
        remove(temp_file_name.c_str());
        printf("remove_imsi_rnti: flock return value is -1\n");
        return;
    }

    while ((num_read_elem = getline(&line, &len, config_file)) != -1) {
        // copy line to find user id
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // get user imsi and rnti
        char* ue_imsi_ptr =  strtok(line_copy, in_line_delimiter.c_str());

        read_ue_imsi = (long long unsigned int) strtoull(ue_imsi_ptr, (char **)NULL, 10);

        if (read_ue_imsi != ue_imsi) {
            // write line on temp_file
            fputs(line, temp_file);
        }
    }

    if (line)
        free(line);

    fclose(config_file);
    fclose(temp_file);

    flock(fileno(config_file), LOCK_UN);
    flock(fileno(temp_file), LOCK_UN);

    // remove metric_file and rename temp_file
    remove(config_file_name.c_str());
    rename(temp_file_name.c_str(), config_file_name.c_str());

    chmod(config_file_name.c_str(), 0666);
}

// read value associated with user IMSI from configuration file
// NOTE: function does not return error if IMSI is not found. It returns -1 to be handled at the caller function
float get_value_from_imsi(long long unsigned int ue_imsi, std::string config_dir_path, std::string file_name) {

    // file variables
    FILE* config_file;

    std::string config_file_name;

    std::string in_line_delimiter = "::";

    // read user rnti
    long long unsigned int read_ue_imsi = 0;

    // read user value
    float read_ue_value = -1.0;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    // return variable for flock
    int ret_fl;

    // to save read variables
    char *ue_imsi_ptr;
    char *ue_value;

    // build absolute path
    config_file_name = config_dir_path;
    config_file_name += file_name;

    // open configuration file and temporary file
    config_file = fopen(config_file_name.c_str(), "r");

    if (config_file == NULL) {
        printf("read_imsi_value: configuration file not found! Returning -1.0\n");
        return -1.0;
    }

    // Lock file and check return value
    ret_fl = flock(fileno(config_file), LOCK_EX);

    if (ret_fl == -1) {
        fclose(config_file);
        printf("read_imsi_value: flock return value is -1\n");
        return -1;
    }

    while ((num_read_elem = getline(&line, &len, config_file)) != -1) {

        // skip lines starting with # as they are considered comments
        if (line[0] == '#')
          continue;

        // copy line to find user id
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // get user imsi and user value
        ue_imsi_ptr = strtok(line_copy, in_line_delimiter.c_str());
        ue_value = strtok(NULL, in_line_delimiter.c_str());

        read_ue_imsi = strtoull(line, &ue_imsi_ptr, 10);

        // convert to float
        if (read_ue_imsi == ue_imsi) {
            read_ue_value = strtof(ue_value, (char **)NULL);
            break;
        }
    }

    if (line)
        free(line);

    fclose(config_file);
    flock(fileno(config_file), LOCK_UN);

    // check if slice for imsi was found
    if (read_ue_value == -1.0) {
        return -1.0;
    }

    return read_ue_value;
}

// read user RNTI from IMSI from configuration file
long long unsigned int get_imsi_from_rnti(uint16_t ue_rnti, std::string config_dir_path, std::string file_name) {

    // file variables
    FILE* config_file;
    std::string config_file_name;
    std::string in_line_delimiter = "::";

    // read user rnti
    long long unsigned int read_ue_imsi = 0;

    // read user value
    uint16_t read_ue_rnti = 0;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    // return variable for flock
    int ret_fl;

    // to save read variables
    char *ue_imsi_ptr;
    char *ue_rnti_ptr;

    // build absolute path
    config_file_name = config_dir_path;
    config_file_name += file_name;

    // open configuration file and temporary file
    config_file = fopen(config_file_name.c_str(), "r");

    if (config_file == NULL) {
        return read_ue_imsi;
    }

    // lock file and check return value
    ret_fl = flock(fileno(config_file), LOCK_EX);
    if (ret_fl == -1) {
        fclose(config_file);
        printf("get_imsi_from_rnti: flock return value is -1\n");
        return read_ue_imsi;
    }

    while ((num_read_elem = getline(&line, &len, config_file)) != -1) {
        // copy line to find user id
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // get user imsi and user value
        ue_imsi_ptr = strtok(line_copy, in_line_delimiter.c_str());
        ue_rnti_ptr = strtok(NULL, in_line_delimiter.c_str());

        read_ue_rnti = (uint16_t) strtoimax(ue_rnti_ptr, (char **)NULL, 10);

        // convert to float
        if (read_ue_rnti == ue_rnti) {
            read_ue_imsi = strtoull(line, &ue_imsi_ptr, 10);
            break;
        }
    }

    // Free memory occupied by line
    if (line)
        free(line);

    fclose(config_file);
    flock(fileno(config_file), LOCK_UN);

    return read_ue_imsi;
}
