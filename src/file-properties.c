#include <file-properties.h>

#include <sys/stat.h>
#include <dirent.h>
#include <openssl/evp.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <defines.h>
#include <fcntl.h>
#include <stdio.h>
#include <utility.h>
#include <openssl/md5.h>

/*!
 * @brief get_file_stats gets all of the required information for a file (inc. directories)
 * @param the files list entry
 * You must get:
 * - for files:
 *   - mode (permissions)
 *   - mtime (in nanoseconds)
 *   - size
 *   - entry type (FICHIER)
 *   - MD5 sum
 * - for directories:
 *   - mode
 *   - entry type (DOSSIER)
 * @return -1 in case of error, 0 else
 */
int get_file_stats(files_list_entry_t *entry) {

    struct stat file_stats;

    if(stat(entry->path_and_name, &file_stats)==-1){
        return -1;
    }


    if(S_ISREG(file_stats.st_mode)){
        entry->entry_type = FICHIER;
        entry->mode = file_stats.st_mode;
        entry->mtime.tv_nsec = file_stats.st_mtim.tv_nsec;
	entry->mtime.tv_sec = file_stats.st_mtim.tv_sec;
        entry->size = file_stats.st_size;
        
        if (compute_file_md5(entry) != 0) {
            fprintf(stderr, "Error computing MD5: %s\n", entry->path_and_name);
            return -1;
            }
            
    }else if(S_ISDIR(file_stats.st_mode)){
        entry->entry_type = DOSSIER;
        entry->mode = file_stats.st_mode;
        
    }else{
        printf("Pas un DOSSIER ni un FICHIER");
        return -1;
    }

    return 0;


}

/*!
 * @brief compute_file_md5 computes a file's MD5 sum
 * @param the pointer to the files list entry
 * @return -1 in case of error, 0 else
 * Use libcrypto functions from openssl/evp.h
 */
int compute_file_md5(files_list_entry_t *entry) {
    FILE *file = fopen(entry->path_and_name, "rb");
    if (!file) {
        perror("Error opening file for MD5 computation");
        return -1;
    }

    MD5_CTX md5Context;
    MD5_Init(&md5Context);

    const size_t bufferSize = 4096;
    unsigned char buffer[bufferSize];
    size_t bytesRead;

    while ((bytesRead = fread(buffer, 1, bufferSize, file)) != 0) {
        MD5_Update(&md5Context, buffer, bytesRead);
    }

    if (ferror(file) !=0){
	    perror("Error reading file for MD5 computation");
	    fclose(file);
	    return -1;
    }

    MD5_Final(entry->md5sum, &md5Context);

    fclose(file);

    return 0;
}


/*!
 * @brief directory_exists tests the existence of a directory
 * @path_to_dir a string with the path to the directory
 * @return true if directory exists, false else
 */
bool directory_exists(char *path_to_dir) {
	if (path_to_dir==NULL){
		return false;
    	}

    	DIR *dir = opendir(path_to_dir);
    	if (dir) {
        	closedir(dir);
        	return true;
    	}
    	return false;
}


/*!
 * @brief is_directory_writable tests if a directory is writable
 * @param path_to_dir the path to the directory to test
 * @return true if dir is writable, false else
 * Hint: try to open a file in write mode in the target directory.
 */
bool is_directory_writable(char *path_to_dir) {
    if (access(path_to_dir, W_OK) != -1) {
        return true; // Directory is writable
    } else {
        return false;
    }
}

