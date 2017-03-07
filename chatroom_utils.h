#ifndef CHATROOM_UTILS_H_INCLUDED
#define CHATROOM_UTILS_H_INCLUDED

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#define MSGSZ     150
#define MAX_CLIENTS 4

/* enum of different messages possible */
typedef enum {
  CONNECT = 1,
  DISCONNECT,
  GET_USERS,
  SET_USERNAME,
  PUBLIC_MESSAGE,
  PRIVATE_MESSAGE,
  TOO_FULL,
  USERNAME_ERROR,
  SUCCESS,
  ERROR,
  ALIVE,
  NONE
} MessageType;


/* message structure */
typedef struct message {
    MessageType   mtype;
    bool is_taken;
    char username[20];
    char    message[MSGSZ];
} MessageBuf;

int CreateSegments(void);
int AttachSegments(void);
int OpenSemaphores(void);
int InitializeClient(void);
void TrimNewlineChar(char *text);
int SetUserName(void);
void *CheckServerStatus(void *args);
void *SetClientStatus(void *args);
int GetUserList(void);
int ConnectServer(void);
void *HandleIncomingMsg(void *args);
bool IsPrivateMsg(int msg_len, char *usrmsg);
bool IsHelpMsg(char *usrmsg);
bool IsDisconnectMsg(char *usrmsg);
void DisplayHelp(void);
void *HandleUserInput(void *args);

#endif
