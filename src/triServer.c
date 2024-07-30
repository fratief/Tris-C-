/************************************
VR486336,VR487324,VR486827
FRANCESCO TIEFENTHALER, DENIS GALLO, RAKIB HAQUE
17/06/2024
*************************************/

#include <stdio.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "../inc/errExit.h"
#define KEYSHM 180
#define KEYMSG 300
#define KEYSEM 250
#define NSEM 3

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

// dichiarazione variabili globali
int vincitore = -1; // 1 se ha vinto processo utente1, 2 se ha vinto utente 2, -1 matrice piena partita in pari
int fterminaPartitaAnt = -1; // variabile flag 
int cCtrl = 0; // contatore Ctrl+C
int giocoAutomatico = -1; // se diventa uno allora il server e fa exec di triClient in modo automatico! 
pid_t arrayPID[3]; // array pid giocatori + server
int idSem; // identificatore semaforo
int idShm; // identificatore memoria condivisa 
int idMsg; // identificatore coda di messaggi
    

struct msg_buffer message;
memoriaCondivisa *memory; // memoria condivisa, contiene le informazioni necessarie per la partita (sia lato server sia lato client)

// dichiarazione funzioni
void P(int semnum); // funzione P (semaforo)
void V(int semnum); // funzione V (semaforo)
void inizializzaMemoria(char sim1, char sim2, int alarmSec); // inizializza la memoria ()
int checkMossaPartita(char sim); // controlla le mosse dei giocatori ogni volta che hanno "consumato" la mano di gioco
void copiaArrayPid(pid_t *arrayMemCond); // fase inizializzazione, (copia i pid in un apposito vettore)

void eliminaMemoria(); // si preoccupa di fare il detatch e la rimozione della memoria condivisa ---> anche i semafori

// handler
void handle_signal_SIGUSR1(){
    
}

void handle_signal_SIGUSR2(){ // (ricezione SIGURS2 utilizzato per impostare la modalità semi-automatica)
    printf("\nGioco automatico!\n");
    giocoAutomatico = 1;
}


void handler_sigAlarm(int signal){ //ricezione (SIGALRM) --- > vuol dire che la partita è finita anticipatamente --- > un client non ha inserito in tempo i dati oppure sono stati cliccati due volte i ctrl + c
    fterminaPartitaAnt = 1;
}


void handle_CTRL(int signal){ // ricezione (SIGINT) 
    if(cCtrl == 1){
        eliminaMemoria(); // elimina la memoria e i semafori nel caso in cui sia stato cliccato 2 volte ctrl+c sul terminale del server 
        kill(arrayPID[1],SIGUSR2); // invia sia al giocatore 1 sia al giocatore 2 che la partita è terminata --- > (la ricezione SIGURS2 nei client viene interpretata da loro come partita terminata )
        kill(arrayPID[2],SIGUSR2);
        printf("\nGIOCO INTERROTTO ESTERNAMENTE!\n");
        exit(1); // gioco finito esternamente
    } else if(cCtrl < 1){
        printf("\nPer uscire cliccare un altra volta CTRL+C!\n");
        cCtrl++;
    }
}

int main(int argc, char *argv[]){
    // Dichiarazione Variabili
    // simbli utenti
    char simUsr1;
    char simUsr2;
    // set segnali
    sigset_t set;

    pid_t pid;
    int mossa;
    int alarmSec;

     //controllo solo se ci sono i parametri necessari (NB: 1 sono nomeseguibile e argc)
    if(argc != 4 ){
        errExit("Errore!");
    }
    //Creazione semaforo (IPC_CREAT + IPC_EXCL impediscono la creazione di ulteriori semafori ---> se esiste vuol dire che esiste già un server e quindi anche una partita)
    idSem = semget(KEYSEM,NSEM,IPC_CREAT | IPC_EXCL | 0640);
    if(idSem < 0){
        errExit("Server già attivo! Aspettare terminazione!\n");
    }
    //inizializzazione set segnali 
    if(sigfillset(&set) == -1){
        errExit("errore inizializzazione set Segnali");
    }
    if(sigdelset(&set,SIGUSR1) == -1 || sigdelset(&set,SIGALRM) == -1 || sigdelset(&set,SIGUSR2) == -1 || sigdelset(&set,SIGINT) == -1){
        errExit("errore");
    }
    if(sigprocmask(SIG_SETMASK,&set,NULL) == -1){
        errExit("errore rimozione segnali");
    }

    // imposto gli handler  (SIG_IGN --- > permette di ignorare la ricezione di un determinato segnale)
    if (signal(SIGUSR1,handle_signal_SIGUSR1) == SIG_ERR|| signal(SIGALRM,handler_sigAlarm) == SIG_ERR || signal(SIGINT,SIG_IGN) == SIG_ERR || signal(SIGUSR2,handle_signal_SIGUSR2) == SIG_ERR) {
        errExit("ERRORE HANDLER");
    }

    //mi salvo i valori dei simboli in due variabili
    simUsr1 = *argv[2];
    simUsr2 = *argv[3];
    alarmSec = atoi(argv[1]);
    //Inizializzo il semaforo
    union semun arg;
    //inizializzo i 3 semafori nel seguente modo per garantire la mutua esclusione..... l'idea è che ogni processo ha un semaforo a cui si fa riferimento
    unsigned short semInitValue[] = {1,0,0};
    arg.array = semInitValue;
    if(semctl(idSem,0,SETALL,arg) < 0 ){
        errExit("Errore nell'inizializzazione del semaforo!");
    }

    //creo una coda di messaggi per comunicare ai due processi giocatore a quale semaforo si devono riferire (NB: la coda è FIFO (FIRST IN FIRST OUT), in poche parole il primo che si arruffa il primo messaggio ha id_semaforo/giocatore = 1, garantendo ordine di entrata)
    idMsg = msgget(KEYMSG, IPC_CREAT | 0640 | IPC_NOWAIT);

    // primo mess di tipo 2 (scelta arbritraria del numero tipo) ---- > questo serve (lato client) per garantire che non ci possano essere più di due giocatori (Persona vs Persona, Persona vs Bot)
    message.msg_type = 2;
    message.id_semaforo = -1;  // anche l'id è stato messo casualmente, l'importante è che il primo giocatore consumi questo messaggio per far si che ulteriori istanze di triClient verifichino l'esistenza o meno di questo messaggio
    //inserisce il messaggio nella coda
    if(msgsnd(idMsg,&message,sizeof(message),0) == -1){ // inserisce messagio nella coda
        errExit("Errore nell'invio dell'id semaforo!");
    }
    // primo mess di tipo 1 (deve essere uguale al tipo messaggio 1) che a come "contenuto" l'id_semaforo = 1
    message.msg_type = 1;
    message.id_semaforo = 1;
    if(msgsnd(idMsg,&message,sizeof(message),0) == -1){
        errExit("Errore nell'invio dell'id semaforo!");
    }
    // secondo mess di tipo 1 (deve essere uguale al tipo messaggio 1) che a come "contenuto" l'id_semaforo = 2
    message.msg_type = 1;
    message.id_semaforo = 2;
    if(msgsnd(idMsg,&message,sizeof(message),0) == -1){
        errExit("Errore nell'invio dell'id semaforo!");
    }
    //id memoria condivisa
    idShm = shmget(KEYSHM, sizeof(memoriaCondivisa), 0640 | IPC_CREAT);
    if(idShm < 0){
        errExit("Errore nella creazione della memoria condivisa!");
    }
    //sezione critica 
    //faccio la P sul semaforo 0
    P(0);
    memory = (void *)shmat(idShm,NULL,0); //attach memoria condivisa
    inizializzaMemoria(simUsr1,simUsr2,alarmSec); //  inizializzazione memoria
    V(1); //faccio partire il client 1, in attesa si essere
    pause(); // si attende l'arrivo del segnale SIGUSR1 da parte del client 2 (non 1)
    printf("Utente 1 è presente!\n");
    if(giocoAutomatico == 1){
        printf("STO CREANDO IL BOT!\n");
        pid = fork(); // creo un processo figlio
        if(pid == 0){
            message.msg_type=2; // rimetto nella coda un messaggio di tipo 2 così da poter eseguire un nuovo processo triClient 
            message.id_semaforo = -1;
            if(msgsnd(idMsg,&message,sizeof(message),0) == -1){
                exit(1);
            }
            if(execlp("./triClient","./triClient","COMPUTER","*",NULL) == -1){ //sotituisco tale processo con un nuovo processo di tipo triClient in modalità BOT!
                errExit("ERRORE\n");
            };
        } else {
            close(STDOUT_FILENO);
        }

    }
    pause();
    printf("Utente 2 è presente!\n");
    // mi copio i pid di tutti i processi e i Simboli
    P(0);
    copiaArrayPid((pid_t*)&memory->pid);
    V(1);
    printf("Il gioco inizia!\n");
    //inizia il gioco

    //Imposto l'handler CTRL al segnale Ctrl+C
    signal(SIGINT,handle_CTRL);

    //INVIO SIGUSR2 AI CLIENT IMPLICA SIA PARTITA TERMINATA PER ABBANDONO SIA CHE LA PARTITA E' FINITA NORMALMENTE, INVIO SIGUSR1 SERVE PER IL PASSAGGIO DEL TURNO (CLIENT - SERVER - CLIENT)
    while(1){
        //manda il segnale al processo Giocatore 1 che è il suo turno

        if(kill(arrayPID[1],SIGUSR1)){
            errExit("Errore!");
        }
        //aspetta il segnale da User1 
        pause();
        if (fterminaPartitaAnt > 0) {
            vincitore = 2;
            kill(arrayPID[2],SIGUSR2);
            break;
        } 
        // sezione critica
        P(0);
        mossa = checkMossaPartita(simUsr1); //check mossa con simbolo giocatore 1
        if(mossa > 0){
            vincitore = 1;
            kill(arrayPID[1],SIGUSR2);
            kill(arrayPID[2],SIGUSR2);
            break;
        } else if(mossa == 0){
            kill(arrayPID[1],SIGUSR2);
            kill(arrayPID[2],SIGUSR2);
            break;                    //matrice piena esci
        };
        V(2);
        //manda il segnale al processo Giocatore 2 che è il suo turno
        if(kill(arrayPID[2],SIGUSR1)){
            errExit("Errore!");
        }
        //aspetta segnale Utente 2
        pause();
        if (fterminaPartitaAnt > 0) {
            vincitore = 1;
            kill(arrayPID[1],SIGUSR2);
            break;
        }
        //sezione critica
        P(0);
        mossa = checkMossaPartita(simUsr2);

        if(mossa > 0){
            vincitore = 2;
            kill(arrayPID[1],SIGUSR2);
            kill(arrayPID[2],SIGUSR2);
            break;
        } else if(mossa == 0){
            kill(arrayPID[1],SIGUSR2);
            kill(arrayPID[2],SIGUSR2);
            break;                    //matrice piena esci
        };
        V(1);

    }
    //ignoto 
    signal(SIGINT,SIG_IGN);
    unsigned short semValue[] = {1,0,0};
    arg.array = semValue;
    if(semctl(idSem,0,SETALL,arg) < 0 ){
        errExit("Errore nell'inizializzazione del semaforo!");
    }


    printf("partita terminata!\n");
    if(vincitore == 1){
        printf("HA VINTO GIOCATORE1\n");
    } else if(vincitore == -1) {
        printf("PARI\n");
    } else if(vincitore == 2){
        printf("HA VINTO GIOCATORE 2\n");
    }
    P(0);
    memory->vincitore=vincitore; // vedere legenda sopra indicata
    if(fterminaPartitaAnt > 0){
        memory->partitaFant = 1; // comunico ai giocatori che la partita è finita anticipatamente!
    }
    if(fterminaPartitaAnt > 0 && vincitore == 2){
        V(2);
    } else {
        V(1);
    }    
    pause(); // aspetta che l'utente finale mandi segnale per completare l'eliminazione della memoria 
    printf("\nAbbiamo finito!\n");
    eliminaMemoria();

    return 0;

}





void P(int semnum) {
    struct sembuf sop;
    sop.sem_num = semnum;
    sop.sem_op = -1;  // decrementa il semaforo
    sop.sem_flg = 0;
    if (semop(idSem, &sop, 1) == -1) {
        perror("semop P failed");
        exit(1);
    }
}

void V(int semnum) {
    struct sembuf sop;
    sop.sem_num = semnum;
    sop.sem_op = 1;  // incrementa il semaforo
    sop.sem_flg = 0;
    if (semop(idSem, &sop, 1) == -1) {
        perror("semop V failed");
        exit(1);
    }
}


void copiaArrayPid(pid_t *arrayMemCond){
    int i = 0;
    for(i = 0; i < 3; i++){
        arrayPID[i] = arrayMemCond[i];
    }
}


void inizializzaMemoria(char sim1, char sim2, int alarmSec){
    int i;
    int j;
    for(i=0;i<3;i++){
        for(j=0;j<3;j++){
            memory->matrice[i][j] = '\0';
            printf("%c",memory->matrice[i][j]);
        }
    }
    
    memory->simboli[0] = sim1;
    memory->simboli[1] = sim2;
    memory->alarmSec = alarmSec;
    memory->pid[0] = getpid();
    memory->vincitore = -1;
    memory->partitaFant = -1;
}

int checkMossaPartita(char sim){
    int i;
    int j;
    int cUsr;
    int simTot = 0;

    //NB: in futuro facciamo sottofunzioni che verificano ogni caso così da rendere più leggibile il codice
    //caso 1 verifico le righe
    for(i = 0;i<3;i++){
        for(j=0,cUsr = 0;j<3;j++){
            if(memory->matrice[i][j] == sim && cUsr == j){
                cUsr++;
            } else {
                break;
            }
        }
        if(cUsr == 3){
            return 1; // vittoria User
        }
    }
    //caso 2 verifico le colonne
    for(j = 0;j<3;j++){
        for(i=0,cUsr=0;i<3;i++){
            if(memory->matrice[i][j] == sim && cUsr == i){
                cUsr++;
            } else {
                break;
            }

        }
        if(cUsr == 3){
            return 1; // vittoria User
        }
    }

    //caso 3 verifico la diagonale principale partendo da sinistra
    for(i = 0,cUsr=0;i<3;i++){
        if(memory->matrice[i][i] == sim && cUsr == i){
            cUsr++;
        }else {
            break;
        }

    }

    if(cUsr == 3){
        return 1; // vittoria User
    }

    
    // caso 4 verifico la diagonale principale partendo da destra
    for(i = 0,j=2,cUsr=0;i<3;i++){
        if(memory->matrice[i][j-i] == sim && cUsr == i){
            cUsr++;
        } else {
            break;
        }

    }
    if(cUsr == 3){
        return 1; // vittoria User
    }


// matrice piena e pareggio
  for(i = 0;i<3;i++){
        for(j=0;j<3;j++){
            if(memory->matrice[i][j] != '\0'){
                simTot++;
            }
        }
    }
    if(simTot == 9){
        return 0; // non ci sono più caselle!!!!
    }


    return -1;

}

void eliminaMemoria(){
    if(shmdt(memory) < 0 ){ //deattach memoria
        errExit("Errore!!");
    }
    // rimozioze della memoria condivisa, dei semafori e della coda di messaggi!
    if(shmctl(idShm,IPC_RMID,NULL) < 0 || semctl(idSem,0,IPC_RMID) < 0 || msgctl(idMsg,IPC_RMID,NULL) < 0){
        errExit("Errore nella distruzione sem oppure mem cond oppure coda");
    }    

}