#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MESSAGE_SIZE 256

// Identifiants de type de message
#define TYPE_MESSAGE_A 1
#define TYPE_MESSAGE_B 2

// Structure de message générique
struct GenericMessage {
    long messageType;
    char messageText[MESSAGE_SIZE];
};

int main() {
    key_t key;
    int msgQueueId;

    // Générer une clé unique pour la file de messages
    if ((key = ftok("test.txt", 'C')) == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    // Créer ou ouvrir la file de messages
    if ((msgQueueId = msgget(key, 0666 | IPC_CREAT)) == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();

    if (pid == 0) {
        key_t child_key = msgget(ftok("test.txt", 'C'), 0666);
        printf("child id = %d,\n", ftok("test.txt", 'D'));
    }
    else {
        printf("MQ_id = %d, %d\n", msgQueueId, key);

        // Supprimer la file de messages
        if (msgctl(msgQueueId, IPC_RMID, NULL) == -1) {
            perror("msgctl");
            exit(EXIT_FAILURE);
        }
    }

    
    

    return 0;
}
