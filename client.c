#define _GNU_SOURCE
#include "chatroom_utils.h"
#include "chatroom_interface.h"

/* global variables to work with threads */
int shmid_clients;
int shmid_server;
int client_index = -1;
int dlgy, dlgx;
bool dead_server = false;
MessageBuf *msgbuf_clients;
MessageBuf *msgbuf_server;
key_t key_out = 5;
key_t key_in = 10;

/* message communication semaphores */
sem_t *msg_mutex_server;
sem_t *msg_mutex_client;
const char MSG_SEM_SERVER[] = "sync_server";
const char MSG_SEM_CLIENTS[] = "sync_clients";

/* status check semaphores */
sem_t *stat_clients[MAX_CLIENTS];
sem_t *stat_server;
const char STAT_CLIENTS[] = "client_status";
const char STAT_SERVER[] = "server_status";

/* create shm segments for message communication and status check */
int CreateSegments(void) {
  int shmflg = IPC_CREAT | 0666;

  if ((shmid_server = shmget(key_out, sizeof(MessageBuf), shmflg)) < 0) {
    perror("shmget");
    exit(1);
  }

  if ((shmid_clients = shmget(key_in, sizeof(MessageBuf) * MAX_CLIENTS, shmflg))
      < 0) {
    perror("shmget");
    return -1;
  }

  return 0;
}

/* attach shared memory segments */
int AttachSegments(void) {
  if ((msgbuf_clients = shmat(shmid_clients, NULL, 0)) == (MessageBuf *) -1) {
    perror("shmat");
    return -1;
  }

  if ((msgbuf_server = shmat(shmid_server, NULL, 0)) == (MessageBuf *) -1) {
    perror("shmat");
    return -1;
  }

  return 0;
}

/* open semaphores for message communication and status check */
int OpenSemaphores(void) {
  int i;
  /* create & initialize semaphore */
  /* open message semaphore */
  msg_mutex_server = sem_open(MSG_SEM_SERVER, 0, 0644, 0);
  if (msg_mutex_server == SEM_FAILED) {
    perror("Unable to create semaphore\n");
    sem_unlink(MSG_SEM_SERVER);
    return -1;
  }

  msg_mutex_client = sem_open(MSG_SEM_CLIENTS, 0, 0644, 0);
  if (msg_mutex_client == SEM_FAILED) {
    perror("Unable to create semaphore\n");
    sem_unlink(MSG_SEM_CLIENTS);
    return -1;
  }

  /* open status check on server semaphore */
  for (i = 0; i < MAX_CLIENTS; i++) {
    stat_clients[i] = sem_open(STAT_CLIENTS, 0, 0644, 0);
    if (stat_clients[i] == SEM_FAILED) {
      perror("Unable to create semaphore\n");
      sem_unlink(STAT_CLIENTS);
      return -1;
    }
  }

  /* open status check on server semaphore */
  stat_server = sem_open(STAT_SERVER, 0, 0644, 0);
  if (stat_server == SEM_FAILED) {
    perror("Unable to create semaphore\n");
    sem_unlink(STAT_SERVER);
    return -1;
  }

  return 0;
}

int InitializeClient(void) {
  if (CreateSegments() < 0)
    return -1;
  else if (AttachSegments() < 0)
    return -1;
  else if (OpenSemaphores() < 0)
    return -1;
  else
    return 0;
}

/* trim newline character */
void TrimNewlineChar(char *text) {
  int len = strlen(text) - 1;
  if (text[len] == '\n') {
    text[len] = '\0';
  }
}

/* input user name */
int SetUserName(void) {
  char usrmsg[20];
  int i;

  wprintw(dlgwin, "Set your username: ");
  wrefresh(dlgwin);
  wgetnstr(dlgwin, usrmsg, 20);
  for (i = 0; i < MAX_CLIENTS; i++) {
    if (strcmp(usrmsg, msgbuf_clients[i].username) == 0)
      return -1;
  }
  strcpy(msgbuf_clients[client_index].username, usrmsg);
  return 0;
}

/* check whether the server is alive every 5 seconds */
void *CheckServerStatus(void *args) {
  int status;
  while (!dead_server) {
    sleep(1);
    status = sem_trywait(stat_server);
    if (status < 0)
      dead_server = true;
  }
  wprintw(dlgwin, "Cannot reach the server\n");
  wrefresh(dlgwin);
  pthread_exit(NULL);
}

/* send message that client is alive every 5 seconds */
void *SetClientStatus(void *args) {
  int sval;

  while (!dead_server) {
    sem_getvalue(stat_clients[client_index], &sval);
    if (sval < 2) {
      sem_post(stat_clients[client_index]);
    }
  }
  pthread_exit(NULL);
}

/* get user list */
int GetUserList(void) {
  int i = 0;
  int j = 0;
  int line = 1;
  DrawBoxWin(usrlist);
  wmove(usrlist, line, 1);
  for (i = 0; i < MAX_CLIENTS; i++) {
    if (msgbuf_clients[i].is_taken) {
      while (msgbuf_clients[i].username[j] != '\0') {
        waddch(usrlist, msgbuf_clients[i].username[j]);
        j++;
      }
      line++;
      j = 0;
      wmove(usrlist, line, 1);
    }
  }
  wmove(msgwin, 1, 1);
  wrefresh(usrlist);
  wrefresh(msgwin);
  return 0;
}

/* register user */
int ConnectServer(void) {
  int i;
  int status = -1;

  /* check clients segment for free segment */
  for (i = 0; i < MAX_CLIENTS; i++) {
    if (!msgbuf_clients[i].is_taken) {
      client_index = i;
      msgbuf_clients[i].mtype = CONNECT;
      msgbuf_clients[i].is_taken = true;
      break;
    }
  }

  /* if there's no free segments left */
  if (client_index < 0) {
    wprintw(dlgwin, "Chatroom is full, try later\n");
    return -1;
  }

  /* set username */
  while (status < 0) {
    status = SetUserName();

    if (status < 0) {
      wprintw(dlgwin, "Given username has been already taken\n");
      wrefresh(dlgwin);
    }
  }
  msgbuf_server->mtype = CONNECT;
  getyx(dlgwin, dlgy, dlgx);
  return 0;
}

/* handle incoming messages from server */
void *HandleIncomingMsg(void *args) {
  wmove(dlgwin, dlgy, dlgx);
  wprintw(dlgwin, "Press /h for help\n");
  wrefresh(dlgwin);
  while (!dead_server) {
    sem_wait(msg_mutex_client);
    switch (msgbuf_clients[client_index].mtype) {
      case PUBLIC_MESSAGE:
        wattron(dlgwin, COLOR_PAIR(2));
        wprintw(dlgwin, "%s\n", msgbuf_clients[client_index].message);
        wattroff(dlgwin, COLOR_PAIR(2));
        wrefresh(dlgwin);
        wrefresh(msgwin);
        break;
      case PRIVATE_MESSAGE:
        wattron(dlgwin, COLOR_PAIR(4));
        wprintw(dlgwin, "%s\n", msgbuf_clients[client_index].message);
        wattroff(dlgwin, COLOR_PAIR(4));
        wrefresh(dlgwin);
        wrefresh(msgwin);
        break;
      case GET_USERS:
        GetUserList();
        break;
      case DISCONNECT:
        dead_server = true;
        break;
      default:
        break;
    }
    msgbuf_clients[client_index].mtype = NONE;
    memset(msgbuf_clients[client_index].message, 0, MSGSZ);
    sem_post(msg_mutex_client);
  }
  pthread_exit(NULL);
}

/* check  whether the message is private */
bool IsPrivateMsg(int msg_len, char *usrmsg) {
  int i;
  bool is_private = false;

  if (usrmsg[0] != '/')
    return false;
  for (i = 1; i < msg_len; i++) {
    if (usrmsg[i] == '/') {
      is_private = true;
      break;
    }
  }
  return is_private;
}

/* check whether the given string is disconnect option */
bool IsHelpMsg(char *usrmsg) {
  if (strcmp(usrmsg, "/h") == 0) {
    return true;
  } else {
    return false;
  }
}

/* check whether the given string is disconnect option */
bool IsDisconnectMsg(char *usrmsg) {
  if (strcmp(usrmsg, "/q") == 0) {
    return true;
  } else {
    return false;
  }
}

void DisplayHelp(void) {
  waddstr(dlgwin, "/q - quit\n");
  waddstr(dlgwin, "/username/ - private message\n");
  waddstr(dlgwin, "/h - help\n");
  wrefresh(dlgwin);
  wrefresh(msgwin);
}

/* user input thread */
void *HandleUserInput(void *args) {
  int msg_len;
  char usrmsg[128];
  while (!dead_server) {
    wmove(msgwin, 1, 1);
    wgetnstr(msgwin, usrmsg, 124);
    TrimNewlineChar(usrmsg);
    msg_len = strlen(usrmsg);
    sem_wait(msg_mutex_server);
    if (IsPrivateMsg(msg_len, usrmsg)) {
      msgbuf_server->mtype = PRIVATE_MESSAGE;
      strcpy(msgbuf_server->message, usrmsg);
      strcpy(msgbuf_server->username, msgbuf_clients[client_index].username);
    } else if (IsHelpMsg(usrmsg)) {
      DisplayHelp();
      memset(usrmsg, 0, 128);
      DrawBoxWin(msgwin);
      continue;
    } else if (IsDisconnectMsg(usrmsg)) {
      msgbuf_server->mtype = DISCONNECT;
      dead_server = true;
    } else {
      msgbuf_server->mtype = PUBLIC_MESSAGE;
      strcpy(msgbuf_server->message, usrmsg);
      strcpy(msgbuf_server->username, msgbuf_clients[client_index].username);
    }
    sem_post(msg_mutex_server);
    memset(usrmsg, 0, 128);
    DrawBoxWin(msgwin);
  }
  pthread_exit(NULL);
}

int main(void) {
  int i;
  pthread_t chat_func[3];
  pthread_attr_t attr;

  initscr();
  InitializeColors();

  /* draw frame */
  frame = newwin(LINES, COLS, 0, 0);
  DrawBoxWin(frame);

  usrlist = newwin(LINES - 4, 25, 0, COLS - 25);
  dlgwin = newwin(LINES - 5, COLS - 26, 1, 1);
  msgwin = newwin(LINES - (LINES - 4), COLS, LINES - 4, 0);
  scrollok(dlgwin, TRUE);

  DrawBoxWin(frame);
  DrawBoxWin(usrlist);
  DrawBoxWin(msgwin);
  wrefresh(dlgwin);

  if (InitializeClient() < 0) {
    return EXIT_FAILURE;
  }

  if (ConnectServer() < 0) {
    return EXIT_FAILURE;
  }

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  /*0: user input thread, 1: server messages thread
   *2: checking whether server is on thread, 3: client alive status thread */
  pthread_create(&chat_func[0], NULL, HandleUserInput, NULL);
  pthread_create(&chat_func[1], NULL, HandleIncomingMsg, NULL);
  pthread_create(&chat_func[2], NULL, CheckServerStatus, NULL);
  pthread_create(&chat_func[3], NULL, SetClientStatus, NULL);

  pthread_attr_destroy(&attr);

  for (i = 0; i < 4; i++) {
    pthread_join(chat_func[i], NULL);
  }

  DeleteWindow(frame);
  DeleteWindow(usrlist);
  DeleteWindow(dlgwin);
  DeleteWindow(msgwin);
  sem_close(msg_mutex_server);

  return EXIT_SUCCESS;
}
