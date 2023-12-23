#include <sync.h>
#include <dirent.h>
#include <string.h>
#include <processes.h>
#include <utility.h>
#include <messages.h>
#include <file-properties.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <sys/msg.h>
#include <utime.h>
#include <stdio.h>

/*!
 * @brief synchronize is the main function for synchronization
 * It will build the lists (source and destination), then make a third list with differences, and apply differences to the destination
 * It must adapt to the parallel or not operation of the program.
 * @param the_config is a pointer to the configuration
 * @param p_context is a pointer to the processes context
 */
void synchronize(configuration_t *the_config, process_context_t *p_context) {
    if (the_config == NULL || p_context == NULL) {
        printf("Pointeur de configuration ou de contexte de processus non valide.\n");
        return;
    }
    files_list_t source;
    files_list_t dest;
    files_list_t diff;
    if (the_config->is_parallel) {
        make_files_lists_parallel(&source, &dest, the_config, p_context->message_queue_id);
    } else {
        make_files_list(&source, the_config->source);
        make_files_list(&dest, the_config->destination);
    }
    files_list_entry_t *src_entry = source.head;
    files_list_entry_t *dest_entry = dest.head;
        
    while (src_entry != NULL && dest_entry != NULL) {
        bool different = mismatch(src_entry, dest_entry, the_config->uses_md5);

        if (different) {
            add_entry_to_tail(&diff, src_entry);
        }

        src_entry = src_entry->next;
        dst_entry = dst_entry->next;
    }


}

/*!
 * @brief mismatch tests if two files with the same name (one in source, one in destination) are equal
 * @param lhd a files list entry from the source
 * @param rhd a files list entry from the destination
 * @has_md5 a value to enable or disable MD5 sum check
 * @return true if both files are not equal, false else
 */
bool mismatch(files_list_entry_t *lhd, files_list_entry_t *rhd, bool has_md5) {
    if (strcmp(lhd->path_and_name, rhd->path_and_name) != 0) {
        return true;
    }
    if (has_md5) {
        if (memcmp(lhd->md5sum, rhd->md5sum, MD5_BIGEST_LENGTH) != 0) {
            return true;
        }
    }
    if (lhd->size != rhd->size || lhd->mtime.tv_nsec != rhd->mtime.tv_nsec) {
        return true;
    }
    return false;
}


/*!
 * @brief make_files_list buils a files list in no parallel mode
 * @param list is a pointer to the list that will be built
 * @param target_path is the path whose files to list
 */
void make_files_list(files_list_t *list, char *target_path) {
    if (list == NULL || target_path == NULL) {
        return;
    }

    make_list(list, target_path);
    
    files_list_entry_t *p_entry = list->head;
    while (p_entry != NULL) {
        get_file_stats(p_entry);
        p_entry = p_entry->next;
    }
}

/*!
 * @brief make_files_lists_parallel makes both (src and dest) files list with parallel processing
 * @param src_list is a pointer to the source list to build
 * @param dst_list is a pointer to the destination list to build
 * @param the_config is a pointer to the program configuration
 * @param msg_queue is the id of the MQ used for communication
 */
void make_files_lists_parallel(files_list_t *src_list, files_list_t *dst_list, configuration_t *the_config, int msg_queue) {
    send_analyze_dir_command(msg_queue, MSG_TYPE_TO_SOURCE_LISTER, the_config->source);
    send_analyze_dir_command(msg_queue, MSG_TYPE_TO_DESTINATION_LISTER, the_config->destination);

    any_message_t message;

    char src_code;
    char dst_code;

    do {
        msgrcv(msg_queue, &message, sizeof(any_message_t) - sizeof(long), MSG_TYPE_TO_MAIN, 0);
        if (message.list_entry.op_code == COMMAND_CODE_FILE_ENTRY) {
            if (message.list_entry.reply_to == SOURCE) {
                add_file_entry(src_list, message.list_entry.payload.path_and_name);
            }
            else if (message.list_entry.reply_to == DESTINATION) {
                add_file_entry(dst_list, message.list_entry.payload.path_and_name);
            }
        }
        else if (message.list_entry.op_code == COMMAND_CODE_LIST_COMPLETE) {
            if (message.list_entry.reply_to == SOURCE) {
                src_code = COMMAND_CODE_LIST_COMPLETE;
            }
            else if (message.list_entry.op_code == DESTINATION) {
                dst_code = COMMAND_CODE_LIST_COMPLETE;
            }
        }
    }
    while (src_code != COMMAND_CODE_LIST_COMPLETE || dst_code != COMMAND_CODE_ANALYZE_DIR);    
}

/*!
 * @brief copy_entry_to_destination copies a file from the source to the destination
 * It keeps access modes and mtime (@see utimensat)
 * Pay attention to the path so that the prefixes are not repeated from the source to the destination
 * Use sendfile to copy the file, mkdir to create the directory
 */
void copy_entry_to_destination(files_list_entry_t *source_entry, configuration_t *the_config) {
 
    char source_path[PATH_SIZE];  
    char destination_path[PATH_SIZE]="chemin de la destination";  

    //construit le chemin en concaténant le chemin source et le nom du fichier, stocké dans source_path
    concat_path(source_path, the_config->source, source_entry->path_and_name);
   
    char *file_name = strrchr(source_entry->path_and_name, '/');
    if (file_name == NULL) {
        file_name = source_entry->path_and_name;
    } else {
        file_name++; 
    }
    //construit le chemin en concaténant le chemin du fichier de destination et le nom du fichier, stocké dans destination_path
    concat_path(destination_path, the_config->destination, file_name);
    
    // open the source file for reading
    int source_file = open(source_path, O_RDONLY);
    if (source_file == -1) {
        printf("Error opening source file");
        return;
    }

    char destination_directory[100000];
    strncpy(destination_directory, destination_path, sizeof(destination_directory));
    char *last_slash = strrchr(destination_directory, '/');
    if (last_slash != NULL) {
        *last_slash = '\0'; 
    }

    // create the destination directory
    if (mkdir(destination_directory, S_IRWXU) == -1) {
    // S_IRWXU: propriétaire a des permissions de lecture, écriture et exécution
        printf("Error creating destination directory");
        close(source_file);
        return;
    }

    // open the destination file
    int destination_file = open(destination_path, O_WRONLY | O_CREAT | O_TRUNC, source_entry->mode);
    // O_WRONLY: fichier doit être ouvert en mode écriture seulement 
    // O_CREAT: crée le fichier s'il n'existe pas
    // O_TRUNC: tronque le fichier à zéro s'il existe
    if (destination_file == -1) {
        printf("Error opening destination file");
        close(source_file);
        return;
    }

    // copie des infos du fichier
    off_t offset = 0;
    ssize_t bytes_copied = sendfile(destination_file, source_file, &offset, source_entry->size);

    if (bytes_copied == -1) {
        printf("Error copying file");
    } else {
        
        struct utimbuf times = {source_entry->mtime.tv_sec, source_entry->mtime.tv_nsec / 1000};
        utime(destination_path, &times);
    }

    close(source_file);
    close(destination_file);
}


/*!
 * @brief make_list lists files in a location (it recurses in directories)
 * It doesn't get files properties, only a list of paths
 * This function is used by make_files_list and make_files_list_parallel
 * @param list is a pointer to the list that will be built
 * @param target is the target dir whose content must be listed
 */
void make_list(files_list_t *list, char *target) {
    if (list == NULL || target == NULL) {
        return;
    }

    DIR *dir = opendir(target);
    
    if (!dir) {
        fprintf(stderr, "Error opening directory\n");
        return;
    }

    struct dirent *dp;

    while ((dp = readdir(dir)) != NULL) {
        if (dp->d_type == DT_REG) {
            add_file_entry(list, concat_path(NULL, target, dp->d_name));
        } else if (dp->d_type == DT_DIR) {
            make_list(list, concat_path(NULL, target, dp->d_name));
        }
    }

    closedir(dir);
}


/*!
 * @brief open_dir opens a dir
 * @param path is the path to the dir
 * @return a pointer to a dir, NULL if it cannot be opened
 */
DIR *open_dir(char *path) {
}

/*!
 * @brief get_next_entry returns the next entry in an already opened dir
 * @param dir is a pointer to the dir (as a result of opendir, @see open_dir)
 * @return a struct dirent pointer to the next relevant entry, NULL if none found (use it to stop iterating)
 * Relevant entries are all regular files and dir, except . and ..
 */
struct dirent *get_next_entry(DIR *dir) {
    if (dir == NULL) {
        return NULL;
    } else {
        return readdir(dir);
    }
}
