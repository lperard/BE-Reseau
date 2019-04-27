#include <mictcp.h>
#include <api/mictcp_core.h>
#include <time.h>
#include <string.h>
 
#define MAX_RESEND 50 // nombre maximal de renvoie du stop and wait
#define TIMEOUT 2000 // en ms
#define TIMEOUTACCEPT 1000000000 // en ms, doit être suffisamment long pour lancer la source
#define TAILLE_FENETRE_GLISSANTE 10
#define BUFFER_SIZE 1024
 
/* Définition des variables gloables */
int derniers_messages[TAILLE_FENETRE_GLISSANTE]; //1 = message acquitté, 0 = message perdu
mic_tcp_sock sock;
mic_tcp_sock_addr addr_dest;
int nextld = 0; // id du socket
int last_sn = 0; // n de sequence du dernier message recu
int taux_perte_acceptable; //en pourcentage
int taux_seuil = 30; // taux de perte seuil pour la negociation
int taux_demande = 25; // taux demande pour la negocation
int ack_num = 0;
int seq_num = 0;

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
   
    /* Pourcentage de perte sur le reseau */
    set_loss_rate(20);

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
    char * payload = (char *) &taux_demande;
    int taille_payload = sizeof(char);

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
    
    if(sock.state != IDLE)
        return -1; //on n'est pas en IDLE

    /* construction du pdu SYN */
    mic_tcp_pdu syn;
    fill_pdu(&syn, sock.addr.port, addr.port,0,0,1,0,0,"", 0); //initialisation du syn

    /* Declaration du PDU que l'on va recevoie */
    mic_tcp_pdu synack;
    synack.payload.size = BUFFER_SIZE;
    synack.payload.data = malloc(synack.payload.size);
    
    /* envoie du SYN tant que l'on a pas recu de synack */
    do{
        if(IP_send(syn, addr) == -1)
            return -1; // erreur ipsend
    }while(IP_recv(&synack, &addr_dest, TIMEOUT) == -1);

    if(!(synack.header.syn == 1 && synack.header.ack == 1))
        return -1; //ce n'est pas un synack, connexion echouée

    /* On lit le taux de perte demande par le serveur */
    int taux_demande = *synack.payload.data;

    /* Negociation du taux de perte, on applique le taux de perte le plus faible */
    if(taux_demande < taux_seuil){
        taux_perte_acceptable = taux_demande;
    } else {
        taux_perte_acceptable = taux_seuil;
    }

    printf("SYNACK recu\n");
    printf("Taux de perte demande : %d, taux seuil : %d, accorde : %d\n", taux_demande, taux_seuil, taux_perte_acceptable);

    /* envoie de ACK */
    mic_tcp_pdu ack;
    fill_pdu(&ack,sock.addr.port, addr.port,1,0,0,1,0,"",0);
    if(IP_send(ack, addr) == -1)
        return -1;

    printf("ACK sent\n");

    /* on passe dans l'etat connecté */
    sock.state = CONNECTED;
   
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
 
int taux_perte(){
    int nb_mess_acquitte = 0;
    for(int i = 0; i < TAILLE_FENETRE_GLISSANTE; i++)
        nb_mess_acquitte += derniers_messages[i];
    return (1 - (nb_mess_acquitte * 1.0 / TAILLE_FENETRE_GLISSANTE))*100;
}
 
/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    /* Si on n'est pas en état connecté, on n'envoie rien */
    if(sock.state != CONNECTED)
        return -1;
 
    /* Si on n'utilise pas le bon socket */
    if(sock.fd != mic_sock)
        return -1;

    /* On initialise la fenêtre si c'est sa première utilisation */
    static int init= 0;
    if(init == 0) init_fen(&init);

    /* utilisé pour se situer dans la fenetre glissante */
    static int n_message = 0;

    /* construction du PDU */
    mic_tcp_pdu pdu;
    fill_pdu(&pdu,sock.addr.port,addr_dest.port,seq_num,0,0,0,0,mesg,mesg_size);

    /* Incrémentation de numero de sequence */
    seq_num = (seq_num + 1) % 2;

    /* envoie du message */
    int size = IP_send(pdu, sock.addr);

    /* initialisation du pdu ack pour la reception */
    mic_tcp_pdu ack;
    ack.payload.size = 2*sizeof(short)+2*sizeof(int)+3*sizeof(char);
    ack.payload.data = malloc(ack.payload.size);

    if(IP_recv(&ack, &addr_dest, TIMEOUT) == -1){   // si on ne recoit pas ack a temps
        if(taux_perte() < taux_perte_acceptable){   // si la perte est acceptable, on ne renvoie rien
            derniers_messages[n_message] = 0;       // maj de la fenetre glissante
            
            /* Incrémentation de numero de sequence */
            seq_num = (seq_num + 1) % 2;

            printf("Perte admissible (%d pct) \n", taux_perte());
        } else {
            //renvoi du message, mécanisme du stop and wait
            int nb_resend = 0;
            do{
                nb_resend++;
                printf("Timeout ack, resending (%d left)... \n", MAX_RESEND - nb_resend);
                size = IP_send(pdu, sock.addr);
            }while(nb_resend < MAX_RESEND && IP_recv(&ack, &addr_dest, TIMEOUT) == -1);

            if(nb_resend == MAX_RESEND){
                printf("Aucune réponse du récepteur\n");
            }else{
                printf("Reception du ack n %d \n", ack.header.ack_num);
                derniers_messages[n_message] = 1; // maj de la fenetre glissante
            }

        }
    } else if(ack.header.ack == 1 && ack.header.ack_num == seq_num){
        printf("Reception du ack n %d \n", ack.header.ack_num);
        derniers_messages[n_message] = 1; // maj de la fenetre glissante
    } else {
        printf("Le message recu n'est pas le ACK attendu (expected %d received %d)\n", seq_num, ack.header.ack_num);
    }

    n_message = (n_message + 1) % TAILLE_FENETRE_GLISSANTE;
    return size;
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
            if(pdu.header.seq_num == ack_num){
                /* passage du payload a l'application */
                app_buffer_put(pdu.payload);

                /* Incrémentation de ack_num */
                ack_num = (ack_num + 1) % 2;
                //Pas d'accord : on envoie un message avec sn comme seq_num, on doit renvoyer
                //un ack qui vaut sn sauf qu'il a ete incrémenté, 
                //donc autant utiliser last_sn, variable inutilisé qu'on a juste a incrémenter et à 
                //mettre en static
            }

            /* envoie l'acquitement */
            fill_pdu(&ack,sock.addr.port,addr.port,0,ack_num,0,1,0,"",0);
            IP_send(ack, addr);

            break;
       
        default:
            break;
    }
    
    //meme fonction que process received pdu mais coté client
void client_process_received_pdu (mic_tcp_pdu pdu, mic_tcp_sock_addr addr) {
    
}
}