#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_MSG_SIZE 256

// Structure du message
struct msg_buffer {
    long msg_type;
    char msg_text[MAX_MSG_SIZE];
};

int main() {
    key_t key = ftok("/tmp", 'B'); // Clé unique pour la file de messages

    // Création ou accès à la file de messages
    int msg_id = msgget(key, 0666 | IPC_CREAT);
    if (msg_id == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }

    struct msg_buffer message;
    message.msg_type = 1; // Type de message (peut être utilisé pour filtrer les messages)

    // Envoi d'un message
    printf("Saisir un message à envoyer au consommateur: ");
    fgets(message.msg_text, MAX_MSG_SIZE, stdin);

    // Envoi du message à la file de messages
    if (msgsnd(msg_id, &message, sizeof(message), 0) == -1) {
        perror("msgsnd");
        exit(EXIT_FAILURE);
    }

    printf("Message envoyé au consommateur\n");

    return 0;
}
