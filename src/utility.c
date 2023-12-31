#include <defines.h>
#include <string.h>

/*
 * @brief concat_path concatenates suffix to prefix into result
 * It checks if prefix ends by / and adds this token if necessary
 * It also checks that result will fit into PATH_SIZE length
 * @param result the result of the concatenation
 * @param prefix the first part of the resulting path
 * @param suffix the second part of the resulting path
 * @return a pointer to the resulting path, NULL when concatenation failed
 */
char *concat_path(char *result, char *prefix, char *suffix) {
        
	if (prefix == NULL || suffix == NULL) {
		return NULL;
	}

	//check if prefix and by /
	if ((strlen(prefix) > 0 && prefix[strlen(prefix) - 1] != '/') && suffix[0] != '/') {
        	strcat(result, prefix);
        	strcat(result, "/");
    	} else {
        	strcpy(result, prefix);
    	}
	
	if (strlen(result) + strlen(suffix) < PATH_SIZE-1) {
		strcat(result, suffix);
	}else{
		return NULL;
	}

	return result;
}
