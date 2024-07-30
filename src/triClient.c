/************************************
VR486336,VR487324,VR486827
FRANCESCO TIEFENTHALER, DENIS GALLO, RAKIB HAQUE
17/06/2024
*************************************/
#include <stdio.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include "../inc/errExit.h"

#define KEYSHM 180
#define KEYSEM 250
#define KEYMSG 300
#define NSEM 3
#define MAX_BUFFER 100


typedef struct {
    char matrice[3][3];
    char simboli[2]; // simbolo i = 0 triClient "utente 1" i = 1 triClient "utente 2" 
    pid_t pid[3]; // i = 0 pid triServer, i = 1 pid triClient "utente1", i = 2 pid triClient "utente2"
    int alarmSec; // secondi alarm
    int vincitore; // 1 se ha vinto processo utente1, 2 se ha vinto utente 2, -1 matrice piena partita in pari
    int partitaFant; // QUANDO LA PARTITA FINISCE ANTICIPATAMENTE PARTE AVVERSARIO!
} memoriaCondivisa;

// struttura creata ad hoc per i messaggi da mettere nella coda 
struct msg_buffer {
    long msg_type;
    int id_semaforo;
};

union semun {
    int val;                  /* valore per SETVAL */
    struct semid_ds *buf;     /* buffer per IPC_STAT, IPC_SET */
    unsigned short *array;    /* array per GETALL, SETALL */
};


// DICHIARAZIONI VARIABILI GLOBALI
int pidProcessi[3]; 
int flagPTerminata = -1; // se positiva allora la partita è terminata anticipatamente
int cCtrl = 0;
int gioco = 0; //0 normale, 1 automatico
pid_t pidServer;
int idSem;
int idShm;
int idMsg;
memoriaCondivisa *memory;

// DICHIARAZIONI FUNZIONI
void stampaMatrice(memoriaCondivisa *memory); // funzione che stampa la matrice 
void checkCoordinate_Inserimento(memoriaCondivisa *memory, char sim,int alarmSec,int iGiocatore); // funzione che controlla le coordinate inserite dall'utente e se soddisfano determinati vincoli allora verrano inserite nella matrice di gioco
void P(int semid, int semnum);
void V(int semid, int semnum);
int generatoreNumeriCasuali(); // genera numeri casuali in caso di giocatore BOT

//Handler
void handle_signal_SIGUSR1(){
    
}
void handle_signal_SIGUSR2 (int signal) {
    idSem = semget(KEYSEM,3,0); // esiste un semaforo?
    if(idSem<0){ // se no allora vuol dire che il server è stato chiuso e quindi lap artita è terminata per motivi esterni
        printf("\nGioco interrotto esternamente\n");
        exit(1); //non ho trovato il semaforo è stato tutto cancellato!
    }
    flagPTerminata = 1; // se si allora l'avversario ha abbandonato!
}


void handle_alarm(int signal) {
    printf("\nNON HAI INSERITO ENTRO IL TERMINE DI SECONDI! PERDERAI PER ABBANDONO!\n");
    kill(pidServer,SIGALRM); // si manda un segnale al server per dire che la partita è terminata anticipatamente 
    exit(EXIT_FAILURE);
}

void handle_CTRL(int signal){
    if(cCtrl == 1){
        flagPTerminata = 1;
        printf("\nPartita terminata per abbandono\n");
        kill(pidServer,SIGALRM); // si manda un segnale al server per dirgli che la partita è terminata anticipatamente
        exit(EXIT_FAILURE);
    } else if(cCtrl < 1){
        printf("\nPer uscire cliccare un altra volta CTRL+C!\n");
        cCtrl++;
    }
}



int main(int argc, char *argv[]){
    int iGiocatore; // indica a quale giocatore fa riferimento
    int alarmSec;
    char sim; // simbolo giocatore
    int turno = 0; //turno di gioco
    sigset_t set; //set di segnali
    // controllo parametri
    if(argc < 2){
        errExit("PARAMETRI SBAGLIATI: ./triClient <nomeutente> oppure ./triClient <nomeutente> \\*");
        printf("1");
    } else if(argc == 3) {
        if(strcmp(argv[2],"*") != 0){
            errExit("Per giocare il computer devi digitare: ./triClient <nomeutente> \\*");
        } else {
            gioco = 1;
            srand(time(0));
        }
    } else if(argc > 3) {
        printf("%d",argc);
        printf("3");
        errExit("PARAMETRI SBAGLIATI: ./triClient <nomeutente> oppure ./triClient <nomeutente> *");
    }
    //Creazione semaforo (IPC_NOWAIT in caso non esistesse non aspetta e termina il processo)
    idSem = semget(KEYSEM,3,IPC_NOWAIT);
    if(idSem<0){
        errExit("SERVER NON ANCORA ATTIVO!\n");
    }
    idMsg = msgget(KEYMSG,0);
    if(idMsg < 0){
        errExit("PARTITA IN CORSO, RIPROVARE PIù tardi!\n");
    }
    //inizializzazione semaforo 
    if(sigfillset(&set) == -1){
        errExit("errore inizializzazione set Segnali");
    }
    if(sigdelset(&set,SIGUSR1) == -1 || sigdelset(&set,SIGUSR2) == -1 || sigdelset(&set,SIGALRM) == -1 || sigdelset(&set,SIGINT) == -1){
        errExit("errore");
    }

    if(sigprocmask(SIG_SETMASK,&set,NULL) == -1){
        errExit("errore rimozione segnali tranne SIGUSR1");
    }
    if (signal(SIGUSR1,handle_signal_SIGUSR1) == SIG_ERR || signal(SIGUSR2,handle_signal_SIGUSR2) == SIG_ERR || signal(SIGINT,SIG_IGN) == SIG_ERR || signal(SIGALRM,handle_alarm) == SIG_ERR) {
        errExit("ERRORE HANDLER");
    }

    struct msg_buffer message;
    if(gioco == 1){ //il primo giocatore (gioco automatico) si mangia il messaggio di tipo 2 così da impedire ad altri giocatori umani di giocare, altrimenti è in corso un gioco tra solo umani
        if(msgrcv(idMsg,&message,sizeof(message),2,IPC_NOWAIT) < 0){
            errExit("GIOCO GIA IN CORSO. RIPROVARE PIU' TARDI");
        }    
    }

    if(msgrcv(idMsg,&message,sizeof(message),1,IPC_NOWAIT) < 0){ //il processo si prende l'id giocatore 
        errExit("GIOCO GIA IN CORSO. RIPROVARE PIU' TARDI");
    }
    iGiocatore = message.id_semaforo;

    printf("Ho l'id %d\n",iGiocatore);
    if(iGiocatore == 1 && gioco == 0){  //il primo giocatore (gioco umano) si mangia il messaggio di tipo 2 così da impedire ad altri giocatori umani + bot di giocare, altrimenti è in corso una partita semi-automatica
        if(msgrcv(idMsg,&message,sizeof(message),2,IPC_NOWAIT) < 0){
            exit(1);
        }
    };
    // prendo l'id della memoria condivisa
    idShm = shmget(KEYSHM,sizeof(memoriaCondivisa),IPC_NOWAIT);
    if(idShm < 0){
        errExit("Errore accesso memoria");
    }
    //consuma il semaforo del giocatore corrente
    P(idSem,iGiocatore);
    //inizializza tutti i campi necessari della memoria condivisa
    memory = (void *)shmat(idShm,NULL,0);
    sim = memory->simboli[iGiocatore - 1];
    pidServer = memory->pid[0]; // i = 0 è il pid del server!!
    memory->pid[iGiocatore] = getpid();
    alarmSec = memory->alarmSec;


    // manda un segnale in caso di gioco automatico al server così creare il bot
    if(iGiocatore == 1 && gioco == 1){
        kill(pidServer, SIGUSR2);
    }
    
    if(gioco == 0 || (gioco == 1 && iGiocatore == 1)){
        printf("Ecco il mio simbolo client %d %c \n",iGiocatore, sim);
    }

    if(iGiocatore == 1 && gioco == 0){
        printf("In attesa del secondo giocatore\n");
    }
    //sblocca il giocatore successivo oppure il server --- > dipende dal valore di iGiocatore (ovviamente deve essere iGiocatore =  2 per risvegliare il Server)
    V(idSem, (iGiocatore + 1)%3);

    //dice al server che il giocatore (idGiocatore è presente!!)
    if(kill(pidServer,SIGUSR1) == -1){
        errExit("Errore Signal!");
    }

    while(1){
        //rimane in attesa fintantochè non arriva segnale SIGURS 1 O SIGUSR2
        pause();
        tcflush(STDIN_FILENO,TCIFLUSH);
        if(flagPTerminata > 0){ // se positivo vuol dire partitta terminata, esce dal while
            break;
        }
        turno++;
        printf("\nHai la mano di gioco! Siamo al turno ---> %d\n",turno);
        // consuma il semaforo del giocatore corrente
        P(idSem,iGiocatore);

        stampaMatrice(memory);
        checkCoordinate_Inserimento(memory,sim,alarmSec,iGiocatore);
        stampaMatrice(memory);
        if(flagPTerminata < 0)
            printf("Attendi la mano avversaria!\n");
        V(idSem,0); // sblocca il server 
        cCtrl = 0;
        //manda il segnale al server di aver inserito i dati
        if(flagPTerminata > 0){
            break;
        } else {
            if(kill(pidServer,SIGUSR1) == -1){
                errExit("Errore Signal!");
            }
        }
    }
    //attendo che server inserisca l'esito!
    //entro in sezione critica
    // GIOCO 1  == GIOCO SEMI AUTOMATICO, GIOCO  == 0 GIOCO TRA DUE UTENTI NORMALI!
    if(gioco == 1){
            P(idSem,iGiocatore);
            if(memory->vincitore == iGiocatore){
                if(memory->partitaFant == 1 && iGiocatore == 2){ // DA RICORDARSI CHE IL BOT NON PUo terminare anticipatamente in quanto BOT
                    printf("HO VINTO IO (BOT) PER ABBANDONO AVVERSARIO!\n");
                } else {
                    printf("HO VINTO IO: UTENTE: %s\n", argv[1]);
                }
            } else if(memory->vincitore == -1) {
                printf("PARI\n");
            } else {
                printf("HO PERSO!\n");
            };
            V(idSem,(iGiocatore+1)%3);
    } else {
        P(idSem,iGiocatore);
        if(memory->vincitore == iGiocatore){
                if(memory->partitaFant == 1){
                    printf("HO VINTO PER ABBANDONO AVVERSARIO!\n");
                } else {
                    printf("HO VINTO IO %s\n", argv[1]);
                }
        } else if(memory->vincitore == -1) {
                printf("LA PARTITA E' FINITA IN PARI\n");
        } else {
                printf("Ha vinto l'avversario!\n");
        };
            V(idSem,(iGiocatore+1)%3);
    }


    // se la partita è termintata anticipatamente, il client ancora attivo ha il compito di passare il turno al server sbloccando il suo semaforo
    if((iGiocatore == 2 && flagPTerminata > 0) || (iGiocatore == 1  && flagPTerminata > 0)){
        V(idSem,0);
        kill(pidServer,SIGUSR1);
    } else if (iGiocatore == 1 && flagPTerminata < 0) {
        V(idSem,2);
    }
    if(iGiocatore == 2 && flagPTerminata < 0){
        kill(pidServer,SIGUSR1);
    }

    if(shmdt(memory) == -1){
        errExit("errore");
    }

    return 0;

}

void P(int semid, int semnum) {
    struct sembuf sop;
    sop.sem_num = semnum;
    sop.sem_op = -1;  // decrementa il semaforo
    sop.sem_flg = 0;
    if (semop(semid, &sop, 1) == -1) {
        perror("semop P failed");
        exit(1);
    }
}

void V(int semid, int semnum) {
    struct sembuf sop;
    sop.sem_num = semnum;
    sop.sem_op = 1;  // incrementa il semaforo
    sop.sem_flg = 0;
    if (semop(semid, &sop, 1) == -1) {
        perror("semop V failed");
        exit(1);
    }
}


void checkCoordinate_Inserimento(memoriaCondivisa *memory, char sim,int alarmSec, int iGiocatore){
    int x = -1;
    int y = -1;
    char buffer[MAX_BUFFER + 1];

    tcflush(STDIN_FILENO,TCIFLUSH);
    if(gioco == 0 || (gioco == 1 && iGiocatore == 1)){
        printf("SONO QUA!\n");
        if (signal(SIGINT,handle_CTRL) == SIG_ERR) {
            errExit("ERRORE HANDLER");
        };

        alarm(alarmSec);
        printf("Inserire le coordinate! (Viene considerata solo il primo carattere della sequenza!)\n");
        printf("Coordinata 1: \n");
        if(read(STDIN_FILENO,buffer,sizeof(buffer))>=2){
            if(isdigit(buffer[0])){
                buffer[1] = '\0';
                x = atoi(buffer);
            }        
        }



        printf("Coordinata 2: \n");
        if(read(STDIN_FILENO,buffer,sizeof(buffer))>=2){
            if(isdigit(buffer[0])){
                buffer[1] = '\0';
                y = atoi(buffer);
            }
        }
        alarm(0);

        if (signal(SIGINT,SIG_IGN) == SIG_ERR) {
            errExit("ERRORE HANDLER");
        };
    } else {
        x = generatoreNumeriCasuali();
        y = generatoreNumeriCasuali();
        printf("NUMERI CASUALI DEL BOT: %d %d\n", x,y);
    }
 
    printf("\033[H\033[J \n"); //pulisce lo schermo 

    if(x>=0 && x <=2 && y>=0 && y<=2){
        if(memory -> matrice[x][y] == sim){
            printf("Hai già segnato il tuo simbolo in quella posizione!\n");
        }else if(memory -> matrice[x][y] != sim && memory->matrice[x][y] != '\0'){
            printf("Vuoi barare! Salti il turno!\n");
            
        } else {
            memory->matrice[x][y] = sim;
        }
    }else{
        printf("Coordinate errate, salti il turno!\n");
    }
}

void stampaMatrice(memoriaCondivisa *memory){
    int i;
    int j;

    for(i = 0;i < 3;i++){
        printf("%d", i);
        for(j=0;j<3;j++){
            if(memory->matrice[i][j] != '\0'){
                printf("|%c|",memory->matrice[i][j]);
            } else {
                printf("| |");
            }
        }
        printf("\n");
    }
        printf("  0  1  2\n");
}


int generatoreNumeriCasuali(){
    return rand() % 3;
}
