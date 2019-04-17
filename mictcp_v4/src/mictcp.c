#include <mictcp.h>
#include <api/mictcp_core.h>
#include <time.h>
#include <string.h>
 
#define MAX_RESEND 50
#define TIMEOUT 200 //en ms
#define TIMEOUTACCEPT 1000000000 //en ms, doit être suffisamment long pour lancer la source
#define TAILLE_FENETRE_GLISSANTE 10
#define BUFFER_SIZE 1024

int derniers_messages[TAILLE_FENETRE_GLISSANTE]; //1 = message acquitté, 0 = message perdu
float taux_perte_acceptable = 0.50;
 
/* Définition des variables gloables */
mic_tcp_sock sock;
int nextld = 0; // id du socket
int sn = 0; // numero de sequence du message à envoyer
int last_sn = 0; // n de sequence du dernier message recu
 
/* Permet de remplir les champs d'un pdu */
void fill_pdu(mic_tcp_pdu * pdu,unsigned short source_port, unsigned short dest_port, unsigned int seq_num, unsigned int ack_num, unsigned char syn, unsigned char ack, unsigned char fin, char * data, int size){
    pdu->header.source_port = source_port;
    pdu->header.dest_port = dest_port;
    pdu->header.seq_num = seq_num;
    pdu->header.ack_num = ack_num;
    pdu->header.syn = syn;
    pdu->header.ack = ack;
    pdu->header.fin = fin;
    pdu->payload.data = data;
    pdu->payload.size = size;
}
 
/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   
    sock.fd = nextld++;
    sock.state = IDLE;
 
    if(initialize_components(sm) == -1){
        printf("Erreur dans la creation du socket\n"); /* Appel obligatoire */
        return -1;
    }
 
    return sock.fd;
}
 
/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    sock.fd = socket;
    sock.addr = addr;
    return 0;
}
 
/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    sock.state = WAIT_FOR_SYN;
 
    /* attend le SYN */
    while(sock.state == WAIT_FOR_SYN);
 
    /* on choisit le taux de perte */
    int taux_demande = 20;
    char * payload = taux_demande;
    int taille_payload = strlen(payload) * sizeof(char);

    /* on envoie le SYNACK */
    mic_tcp_pdu synack;
    fill_pdu(&synack,sock.addr.port,addr->port,0,0,1,1,0,payload,taille_payload);
    if(IP_send(synack, *addr) == -1)
        return -1;
 
    /* attend le ACK */
    while(sock.state == WAIT_FOR_ACK);
 
    printf("[MIC-TCP] Connected :)\n");
 
    return 0;
}
 
/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(sock.state == IDLE){

        int taux_seuil = 30;
 
        printf("addr : %s %d %d\n",addr.ip_addr, addr.ip_addr_size, addr.port);
 
        //construction du pdu syn
        mic_tcp_pdu syn;
        fill_pdu(&syn, sock.addr.port, addr.port,0,0,1,0,0,"", 0); //initialisation du syn
 
        //tant que l'on a pas recu de synack
        mic_tcp_pdu synack;
        synack.payload.size = BUFFER_SIZE;
        synack.payload.data = malloc(synack.payload.size);
       
        do{
            if(IP_send(syn, addr) == -1) // envoie de syn
                return -1; // erreur ipsend
        }while(IP_recv(&synack, &sock.addr, TIMEOUT) == -1);

        printf("J'ai envie de mourir\n");
 
        if(synack.header.syn == 1 && synack.header.ack == 1){
            int taux_demande = atoi(synack.payload.data);

            /* Si me taux demandé est inférieur au seuil, on le choisit sinon on garde notre seuil */
            if(taux_demande > taux_seuil) taux_demande = taux_seuil;

            /* On applique le taux de perte */
            set_loss_rate(taux_demande);

            printf("SYNACK recu, taux de perte accorde : %s\n", taux_demande);
 
            /* envoie de ACK */
            mic_tcp_pdu ack;
            fill_pdu(&ack,sock.addr.port, addr.port,1,0,0,1,0,"",0);
            if(IP_send(ack, addr) == -1)
                return -1;
 
            printf("ack sent\n");
 
            sock.state = CONNECTED;
        }else{
            return -1; //ce n'est pas un synack, connexion echouée
        }
 
    }else{
        return -1; //on n'est pas en IDLE
    }
   
    return 0; // la connexion est etablie
}
 
/*
* initialise la fenetre glissante
*/
void init_fen(int * init){
   
    for(int i = 0; i < TAILLE_FENETRE_GLISSANTE; i++)
        derniers_messages[i] = 1; //on suppose 0% de perte initialement
    *init = 1;
}
 
float taux_perte(){
    int nb_mess_acquitte = 0;
    for(int i = 0; i < TAILLE_FENETRE_GLISSANTE; i++)
        nb_mess_acquitte += derniers_messages[i];
    return 1 - (nb_mess_acquitte * 1.0 / TAILLE_FENETRE_GLISSANTE);
}
 
/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    if(sock.state != CONNECTED)
        return -1; // pas en etat connecté, on n'envoie pas
 
    static int init= 0;
    if(init == 0)   init_fen(&init);
   
    static int n_message = 0;
 
    if(sock.fd == mic_sock){
 
        //construction du pdu
        mic_tcp_pdu pdu;
        fill_pdu(&pdu,0,sock.addr.port,0,0,0,0,0,mesg,mesg_size);
 
        pdu.header.seq_num = sn++;
        int size = IP_send(pdu, sock.addr);
 
        int nb_resend = 0;
        mic_tcp_pdu ack;
 
        while(nb_resend < MAX_RESEND){
            if(IP_recv(&ack, & sock.addr, TIMEOUT) == -1){ // si le timer expire
                if(taux_perte() < taux_perte_acceptable){ //si on peut perdre le message, on ne renvoie rien
                    derniers_messages[n_message] = 0;
                    printf("ACK DROPPED, taux de pertes actuel : %f\n", taux_perte());
                    break;
                }
 
                //renvoi du message, mécanisme du stop and wait à reprise des pertes totales
                nb_resend++;
                printf("Timeout ack, resending (%d left)... \n", MAX_RESEND - nb_resend);
                size = IP_send(pdu, sock.addr);
            } else { // on recoit le ack on quitte la boucle while
                printf("Reception du ack n %d \n", ack.header.ack_num);
                derniers_messages[n_message] = 1;
                break;
 
                /*
                verifie le numero d'acquittement (inutile pour le moment)
                rcv_ack = ack.header.ack_num == pdu.header.seq_num
                */
            }
        }
 
        n_message = (n_message + 1) % TAILLE_FENETRE_GLISSANTE;
        return size;
    } else {
        return -1;
    }
}
 
/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
 
    if(sock.state != CONNECTED){
        perror("[MIC-TCP] Erreur, etat non connecte\n");
        return -1; // pas en etat connecté, on ne peut pas recevoir
    }
 
    mic_tcp_payload mesg_recu;
    int nb_octets_lus;
 
    mesg_recu.size = max_mesg_size;
    mesg_recu.data = mesg;
 
    if(sock.fd == socket){
        nb_octets_lus = app_buffer_get(mesg_recu);
        return nb_octets_lus;
    }
 
    return -1;
}
 
/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
    return 0;
}
 
/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
 
    mic_tcp_pdu ack;
    switch (sock.state)
    {
        case WAIT_FOR_SYN:
            if(pdu.header.syn == 1)
                sock.state = WAIT_FOR_ACK;
            break;
 
        case WAIT_FOR_ACK:
            if(pdu.header.ack == 1)
                sock.state = CONNECTED;
            break;
       
        case CONNECTED:
            /* envoie l'acquitement */
            fill_pdu(&ack,0,0,0,pdu.header.seq_num,0,1,0,"",0);
            IP_send(ack, sock.addr);
 
            /* passage du payload a l'application */
            app_buffer_put(pdu.payload);
            break;
       
        default:
            break;
    }
}