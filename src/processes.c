#include "processes.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <stdio.h>
#include <messages.h>
#include <file-properties.h>
#include <sync.h>
#include <string.h>
#include <errno.h>

/*!
 * @brief prepare prepares (only when parallel is enabled) the processes used for the synchronization.
 * @param the_config is a pointer to the program configuration
 * @param p_context is a pointer to the program processes context
 * @return 0 if all went good, -1 else
 */
int prepare(configuration_t *the_config, process_context_t *p_context) {
    if (the_config != NULL && the_config->is_parallel == true) {
        p_context->shared_key = ftok("LP25_sync", 25);
        if (p_context->shared_key == -1) {
            fprintf(stderr, "Error with mqkey\n");
            return -1;
        }
        p_context->message_queue_id = msgget(p_context->shared_key, 0666 | IPC_CREAT);
        if (p_context->message_queue_id == -1) {
            fprintf(stderr, "Error while creating msgqueue\n");
            return -1;
        }
        p_context->processes_count = 0;
        p_context->main_process_pid = getpid();

        lister_configuration_t src_lister_parameters;
        src_lister_parameters.analyzers_count = (the_config->processes_count-2)/2;
        src_lister_parameters.my_recipient_id = MSG_TYPE_TO_SOURCE_ANALYZERS;
        src_lister_parameters.my_receiver_id = MSG_TYPE_TO_SOURCE_LISTER;
        src_lister_parameters.mq_key = p_context->shared_key;
        p_context->source_lister_pid = make_process(p_context, lister_process_loop, &src_lister_parameters);

        lister_configuration_t dst_lister_parameters;
        dst_lister_parameters.analyzers_count = (the_config->processes_count-2)/2;
        dst_lister_parameters.my_recipient_id = MSG_TYPE_TO_DESTINATION_ANALYZERS;
        dst_lister_parameters.my_receiver_id = MSG_TYPE_TO_DESTINATION_LISTER;
        dst_lister_parameters.mq_key = p_context->shared_key;
        p_context->destination_lister_pid = make_process(p_context, lister_process_loop, &dst_lister_parameters);

        analyzer_configuration_t src_analyser_parameters;
        src_analyser_parameters.my_recipient_id = MSG_TYPE_TO_SOURCE_LISTER;
        src_analyser_parameters.my_receiver_id = MSG_TYPE_TO_SOURCE_ANALYZERS;
        src_analyser_parameters.mq_key = p_context->shared_key;
        src_analyser_parameters.use_md5 = the_config->uses_md5;   
        p_context->source_analyzers_pids = (pid_t*) malloc(sizeof(pid_t)*(the_config->processes_count-2)/2);
        for (int i=0; i<(the_config->processes_count-2)/2; i++) {
            p_context->source_analyzers_pids[i] = make_process(p_context, analyzer_process_loop, &src_analyser_parameters);
        }

        analyzer_configuration_t dst_analyser_parameters;
        dst_analyser_parameters.my_recipient_id = MSG_TYPE_TO_DESTINATION_LISTER;
        dst_analyser_parameters.my_receiver_id = MSG_TYPE_TO_DESTINATION_ANALYZERS;
        dst_analyser_parameters.mq_key = p_context->shared_key;
        dst_analyser_parameters.use_md5 = the_config->uses_md5;   
        p_context->destination_analyzers_pids = (pid_t*) malloc(sizeof(pid_t)*(the_config->processes_count-2)/2);
        for (int i=0; i<(the_config->processes_count-2)/2; i++) {
            p_context->destination_analyzers_pids[i] = make_process(p_context, analyzer_process_loop, &dst_analyser_parameters);
        }
    }

    return 0;
}

/*!
 * @brief make_process creates a process and returns its PID to the parent
 * @param p_context is a pointer to the processes context
 * @param func is the function executed by the new process
 * @param parameters is a pointer to the parameters of func
 * @return the PID of the child process (it never returns in the child process)
 */
int make_process(process_context_t *p_context, process_loop_t func, void *parameters) {
    pid_t pid = fork();

    if (pid == 0) {
        func(parameters);
    }
    else {
        p_context->processes_count++;
        return pid;
    }
}

/*!
 * @brief lister_process_loop is the lister process function (@see make_process)
 * @param parameters is a pointer to its parameters, to be cast to a lister_configuration_t
 */
void lister_process_loop(void *parameters) {
    lister_configuration_t* config = (lister_configuration_t*) parameters;
    any_message_t message;

    files_list_t list;
    list.head = NULL;
    list.tail = NULL;

    files_list_entry_t *p_entry;
    files_list_entry_t *p_entry_analysed;
    int working_analyser = 0;

    int mq_id = msgget(config->mq_key, 0666);

    do {
        if (msgrcv(mq_id, &message, sizeof(any_message_t) - sizeof(long), config->my_receiver_id, 0) != -1) {
            if (message.analyze_file_command.op_code == COMMAND_CODE_ANALYZE_DIR) {
                //list file of the target directory
                make_list(&list, message.analyze_dir_command.target);
                
                // analyse each file
                p_entry = list.head;
                p_entry_analysed = list.head;
                while (p_entry != NULL) {
                    while (p_entry != NULL && working_analyser < config->analyzers_count) {
                        send_analyze_file_command(mq_id, config->my_recipient_id, p_entry);
                        p_entry = p_entry->next;
                        working_analyser++;
                    }
                    while (working_analyser > 0) {
                        msgrcv(mq_id, &message, sizeof(any_message_t) - sizeof(long), config->my_receiver_id, 0);
                        memcpy(p_entry_analysed, &message.analyze_file_command.payload, sizeof(files_list_entry_t));
                        p_entry_analysed = p_entry_analysed->next;
                        working_analyser--;
                    }
                }

                // send each entry to main
                p_entry = list.head;
                while (p_entry != NULL) {
                    if (config->my_receiver_id == MSG_TYPE_TO_SOURCE_LISTER) {
                        send_files_source_list_element(mq_id, MSG_TYPE_TO_MAIN, p_entry);
                    } else {
                        send_files_destination_list_element(mq_id, MSG_TYPE_TO_MAIN, p_entry);
                    }
                    
                    p_entry = p_entry->next;
                }
                if (config->my_receiver_id == MSG_TYPE_TO_SOURCE_LISTER) {
                    send_source_list_end(mq_id, MSG_TYPE_TO_MAIN);
                } else {
                    send_destination_list_end(mq_id, MSG_TYPE_TO_MAIN);
                }
            }
        }
    }
    while (message.simple_command.message != COMMAND_CODE_TERMINATE);

    send_terminate_confirm(mq_id, MSG_TYPE_TO_MAIN);
}

/*!
 * @brief analyzer_process_loop is the analyzer process function
 * @param parameters is a pointer to its parameters, to be cast to an analyzer_configuration_t
 */
void analyzer_process_loop(void *parameters) {
    analyzer_configuration_t* config = (analyzer_configuration_t*) parameters;
    any_message_t message;

    int mq_id = msgget(config->mq_key, 0666);

    do {
        if(msgrcv(mq_id, &message, sizeof(any_message_t) - sizeof(long), config->my_receiver_id, 0) != -1) {
            if (message.analyze_file_command.op_code == COMMAND_CODE_ANALYZE_FILE) {
                get_file_stats(&message.analyze_file_command.payload);
                send_analyze_file_response(mq_id, config->my_recipient_id, &message.analyze_file_command.payload);
            }
        }
    }
    while (message.simple_command.message != COMMAND_CODE_TERMINATE);

    send_terminate_confirm(mq_id, MSG_TYPE_TO_MAIN);
}

/*!
 * @brief clean_processes cleans the processes by sending them a terminate command and waiting to the confirmation
 * @param the_config is a pointer to the program configuration
 * @param p_context is a pointer to the processes context
 */
void clean_processes(configuration_t *the_config, process_context_t *p_context) {
    if (the_config == NULL || p_context == NULL) {
        fprintf(stderr, "Invalid pointers provided to clean_processes function.\n");
        return;
    }

    if (the_config->is_parallel == false) {
        return;
    }

    send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_SOURCE_LISTER);
    send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_DESTINATION_LISTER);

    for (int i = 0; i < (p_context->processes_count-2)/2; i++) {
        send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_SOURCE_ANALYZERS);
        send_terminate_command(p_context->message_queue_id, MSG_TYPE_TO_DESTINATION_ANALYZERS);
    }

    any_message_t message;
    for (int i = 0; i < p_context->processes_count; i++) {
        msgrcv(p_context->message_queue_id, &message, sizeof(any_message_t) - sizeof(long), MSG_TYPE_TO_MAIN, 0);
    }
    
    free(p_context->source_analyzers_pids);
    free(p_context->destination_analyzers_pids);

    if (msgctl(p_context->message_queue_id, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error in removing message queue\n");
    }

    // Do nothing if not parallel
    // Send terminate
    // Wait for responses
    // Free allocated memory
    // Free the MQ
}