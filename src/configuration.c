#include <configuration.h>
#include <stddef.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

typedef enum {DATE_SIZE_ONLY, NO_PARALLEL} long_opt_values;

/*!
 * @brief function display_help displays a brief manual for the program usage
 * @param my_name is the name of the binary file
 * This function is provided with its code, you don't have to implement nor modify it.
 */
void display_help(char *my_name) {
    printf("%s [options] source_dir destination_dir\n", my_name);
    printf("Options: \t-n <processes count>\tnumber of processes for file calculations\n");
    printf("         \t-h display help (this text)\n");
    printf("         \t--date_size_only disables MD5 calculation for files\n");
    printf("         \t--no-parallel disables parallel computing (cancels values of option -n)\n");
}

/*!
 * @brief init_configuration initializes the configuration with default values
 * @param the_config is a pointer to the configuration to be initialized
 */
void init_configuration(configuration_t *the_config) {
    the_config->is_parallel = false;
    the_config->dry_run = false;
    the_config->processes_count = 0; //valeur à changer car non nulle
    the_config->uses_md5 = true;
    the_config->verbose = false;
    strcpy(the_config->source, "\0");
    strcpy(the_config->destination, "\0");;
}

/*!
 * @brief set_configuration updates a configuration based on options and parameters passed to the program CLI
 * @param the_config is a pointer to the configuration to update
 * @param argc is the number of arguments to be processed
 * @param argv is an array of strings with the program parameters
 * @return -1 if configuration cannot succeed, 0 when ok
 */
int set_configuration(configuration_t *the_config, int argc, char *argv[]) {
    int opt = 0;

	struct option long_opts[] = {
		{.name="date-size-only ",.has_arg=0,.flag=0,.val='o'},
		{.name="no-parallel",.has_arg=0,.flag=0,.val='p'},
		{.name="dry-run",.has_arg=0,.flag=0,.val='r'},
        {.name="source",.has_arg=0,.flag=0,.val='s'},
		{.name="destination",.has_arg=0,.flag=0,.val='d'},
		{.name=0,.has_arg=0,.flag=0,.val=0},
	};
    
	while((opt = getopt_long(argc, argv, "s:d:n:v", long_opts, NULL)) != -1) {
		switch (opt) {
			case 'o':
                the_config->uses_md5 = false;
				break;
				
			case 'p':
                the_config->is_parallel = true;
				break;
				
            case 'r':
                the_config->dry_run = true;
				break;
			
            case 's':
                strcpy(the_config->source, optarg);
				break;
				
			case 'd':
                strcpy(the_config->destination, optarg);
				break;

            case 'n':
                the_config->processes_count = atoi(optarg);
                break;

            case 'v':
                the_config->verbose = true;
                break;
            
            default:
                fprintf(stderr, "Error with argument\n");
                display_help(argv[0]);
                return 1;
		}
	}

    if (strcmp(the_config->source, "\0") == 0 || strcmp(the_config->destination, "\0") == 0) {
        fprint(stderr, "Error source and destination must be specified\n");
        display_help(argv[0]);
        return 1;
    }

	return 0;
}
