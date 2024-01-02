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
#include <stdlib.h>

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

    files_list_t source_list = {NULL, NULL};
    files_list_t dest_list = {NULL, NULL};
    files_list_t diff_list = {NULL, NULL};

    if (the_config->is_parallel == true) {
        make_files_lists_parallel(&source_list, &dest_list, the_config, p_context->message_queue_id);
    } else {
        make_files_list(&source_list, the_config->source);
        make_files_list(&dest_list, the_config->destination);
    }

    files_list_entry_t *src_entry = source_list.head;
    files_list_entry_t *dest_entry = NULL;
    files_list_entry_t new_entry;
        
    while (src_entry != NULL) {
        dest_entry = find_entry_by_name(&dest_list, src_entry->path_and_name, strlen(the_config->source), strlen(the_config->destination));
        if (dest_entry == NULL || mismatch(src_entry, dest_entry, the_config->uses_md5) == true) {
            memcpy(&new_entry, src_entry, sizeof(files_list_entry_t));
            add_entry_to_tail(&diff_list, &new_entry);
        }
        src_entry = src_entry->next;
    }

    display_files_list(&source_list);
    //display_files_list(&dest_list);
    display_files_list(&diff_list);

    files_list_entry_t *p_diff = diff_list.head;
    while (p_diff != NULL) {
        copy_entry_to_destination(p_diff, the_config);
        p_diff = p_diff->next;
    }

    //free all list
    files_list_entry_t *p_entry = NULL;
    files_list_entry_t *tmp_entry = NULL;

    p_entry = source_list.head;
    while (p_entry != NULL) {
        tmp_entry = p_entry->next;
        free(p_entry);
        p_entry = tmp_entry;
    }

    p_entry = dest_list.head;
    while (p_entry != NULL) {
        tmp_entry = p_entry->next;
        free(p_entry);
        p_entry = tmp_entry;
    }

    p_entry = diff_list.head;
    while (p_entry != NULL) {
        tmp_entry = p_entry->next;
        free(p_entry);
        p_entry = tmp_entry;
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
    if (has_md5 == true) {
        for (int i = 0; i < 16; i++) {
            if (lhd->md5sum[i] != rhd->md5sum[i]) {
                return true;
            }
        }
    }

    if (lhd->size != rhd->size || lhd->mtime.tv_nsec != rhd->mtime.tv_nsec || lhd->mtime.tv_sec != rhd->mtime.tv_sec || lhd->mode != rhd->mode) {
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

    bool src_complete = false;
    bool dst_complete = false;

    files_list_entry_t *new_entry = NULL;

    do {
        msgrcv(msg_queue, &message, sizeof(any_message_t) - sizeof(long), MSG_TYPE_TO_MAIN, 0);
        switch (message.list_entry.op_code) {
            case COMMAND_CODE_SOURCE_FILE_ENTRY:
                new_entry = (files_list_entry_t*) malloc(sizeof(files_list_entry_t));
                memcpy(new_entry, &message.list_entry.payload, sizeof(files_list_entry_t));
                add_entry_to_tail(src_list, new_entry);
                break;
            case COMMAND_CODE_DESTINATION_FILE_ENTRY:
                new_entry = (files_list_entry_t*) malloc(sizeof(files_list_entry_t));
                memcpy(new_entry, &message.list_entry.payload, sizeof(files_list_entry_t));
                add_entry_to_tail(dst_list, new_entry);
                break;
            case COMMAND_CODE_SOURCE_LIST_COMPLETE:
                src_complete = true;
                break;
            case COMMAND_CODE_DESTINATION_LIST_COMPLETE:
                dst_complete = true;
                break;
            
            default:
                break;
        }
    }
    while (src_complete == false || dst_complete == false);
}

/*!
 * @brief copy_entry_to_destination copies a file from the source to the destination
 * It keeps access modes and mtime (@see utimensat)
 * Pay attention to the path so that the prefixes are not repeated from the source to the destination
 * Use sendfile to copy the file, mkdir to create the directory
 */
void copy_entry_to_destination(files_list_entry_t *source_entry, configuration_t *the_config) {
    
    // open the source file for reading
    int source_file = open(source_entry->path_and_name, O_RDONLY);
    if (source_file == -1) {
        printf("Error opening source file");
        return;
    }

    char dest_entry_path[PATH_SIZE]  = "";
    concat_path(dest_entry_path, the_config->destination, source_entry->path_and_name + strlen(the_config->source));

    // open the destination file
    int destination_file = open(dest_entry_path, O_WRONLY | O_CREAT | O_TRUNC, source_entry->mode);
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
        struct timespec new_time[2];
        new_time[0].tv_nsec = UTIME_NOW;
        new_time[0].tv_sec = UTIME_NOW;
        new_time[1].tv_nsec = source_entry->mtime.tv_nsec;
        new_time[1].tv_sec = source_entry->mtime.tv_sec;
        if (utimensat(AT_FDCWD, dest_entry_path, new_time, 0) != 0) {
            fprintf(stderr, "Erreur lors de la modification de l'heure de modification");
        }

        chmod(dest_entry_path, source_entry->mode);
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

    DIR *dir = open_dir(target);
    
    if (!dir) {
        fprintf(stderr, "Error opening directory\n");
        return;
    }

    struct dirent *dp;
    char path[PATH_SIZE] = "";

    while ((dp = readdir(dir)) != NULL) {
        if (dp->d_type == DT_REG) {
            if (concat_path(path, target, dp->d_name) != NULL) {
                add_file_entry(list, path);
            }
        } else if (dp->d_type == DT_DIR && strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            if (concat_path(path, target, dp->d_name) != NULL) {
                make_list(list, path);
            }
        }
        strcpy(path, "");
    }

    closedir(dir);
}


/*!
 * @brief open_dir opens a dir
 * @param path is the path to the dir
 * @return a pointer to a dir, NULL if it cannot be opened
 */
DIR *open_dir(char *path) {
    if(path==NULL){
        return NULL;
    }
    return opendir(path);
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
        struct dirent *next_entry = readdir(dir);
        while (next_entry != NULL && (strcmp(next_entry->d_name, ".") == 0 || strcmp(next_entry->d_name, "..") == 0)) {
            next_entry = readdir(dir);
        }
        return readdir(dir);
    }
}
