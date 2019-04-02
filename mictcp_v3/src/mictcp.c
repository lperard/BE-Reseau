#include <mictcp.h>
#include <api/mictcp_core.h>
#include <time.h>

#define MAX_RESEND 50
#define RESEND_TIMEOUT 20 //en ms
#define TAILLE_FENETRE_GLISSANTE 10

int derniers_messages[TAILLE_FENETRE_GLISSANTE]; //1 = message acquitté, 0 = message perdu
float taux_perte_acceptable = 0.50;

/* Définition des variables gloables */
mic_tcp_sock sock;
int nextld = 0; // id du socket
int sn = 0; // numero de sequence du message à envoyer
int last_sn = 0; // n de sequence du dernier message recu

/* Remplit le header du pdu */
void fill_pdu_header(mic_tcp_header * header, unsigned int seq_num, unsigned int ack_num, unsigned char syn, unsigned char ack, unsigned char fin){
    header->seq_num = seq_num;
    header->ack_num = ack_num;
    header->syn = syn;
    header->ack = ack;
    header->fin = fin;
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

    set_loss_rate(80);

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
    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    
    return 0;
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
    printf("Calcul du taux de perte\n");
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
    static int init= 0;
    if(init == 0) {
        printf("Initialisation de la fenêtre\n");
        init_fen(&init);
    }
        

    static int n_message = 0;

    if(sock.fd == mic_sock){
        mic_tcp_pdu pdu;

        pdu.payload.data = mesg;
        pdu.payload.size = mesg_size;
        pdu.header.dest_port = sock.addr.port;
        pdu.header.seq_num = sn++;

        int size = IP_send(pdu, sock.addr);

        int nb_resend = 0;
        mic_tcp_pdu ack;

        while(nb_resend < MAX_RESEND){
            if(IP_recv(&ack, & sock.addr, RESEND_TIMEOUT) == -1){ // si le timer expire

                if(taux_perte() < taux_perte_acceptable){ //si on peut perdre le message, on ne renvoie rien
                    derniers_messages[n_message] = 0;
                    printf("ACK DROPPED, taux de pertes actuel : %f\n", taux_perte()); ///Probleme d'arrondi, a voir
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
    ack.header.ack = 1; //flag pour indiquer que le message est un acquitement
    ack.header.ack_num = pdu.header.seq_num;
    ack.payload.size = 0;

    IP_send(ack, sock.addr); //envoie l'acquitement
    
    if(pdu.header.seq_num != last_sn){ // si on ne reçoit pas deux fois le même message, on le met dans le buffer
        app_buffer_put(pdu.payload);
    }else{
        printf("ACK lost \n");
    }

    last_sn = pdu.header.seq_num;
}
