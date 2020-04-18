#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#define BUFFER_SIZE 1024
#define ICMP_HEADER_LENGTH 8
#define DATA_LENGTH (64 - ICMP_HEADER_LENGTH)

int msg_received_count;
int seq = 0;
pid_t pid;
int sock;
struct addrinfo *host;

//will return the canonical name of the node corresponding to the addrinfo structure value passed back.
int get_addrinfo(const char *host, struct addrinfo **result) {
    struct addrinfo hints; //hints argument points to the addrinfo for the selected socket specified by host
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; /* Allow ADDITIONAL SUPPORT for IPv4 or IPv6 */
    hints.ai_flags = AI_CANONNAME;
    
    
    return getaddrinfo(host, NULL, &hints, result); //getaddrinfo() returns a list of address structures
}

const char *get_sockaddr_text(
                              const struct sockaddr *address,
                              char *text, socklen_t text_length
                              ) {
    return inet_ntop( //convert IPv4 and IPv6 addresses from binary to text form
                     address->sa_family,
                     &(((struct sockaddr_in *)address)->sin_addr),
                     text,
                     text_length
                     );
}

double timeval_to_ms(const struct timeval *time) {
    return (time->tv_sec * 1000.0) + (time->tv_usec / 1000.0);
}
/*
 * Checksum routine for Internet Protocol
 */


u_short checksum(u_short *data, int length) {
    register int data_left = length;
    register u_short *p = data;
    register int sum = 0;
    u_short answer = 0;
    
    /*
     *  This is a simple program using a 32 bit accumulator (sum),
     *  the checksum function add sequential 16 bit words to it, and at the end, fold
     *  back all the carry bits from the top 16 bits into the lower 16 bits.
     */
    
    while (data_left > 1) {
        sum += *p;
        p++;
        data_left -= 2;
    }
    // mop up an odd byte, if necessary
    if (data_left == 1) {
        *(u_char *)(&answer) = *(u_char *)p;
        sum += answer;
    }
    // add back carry outs from top 16 bits to low 16 bits
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    
    return answer;   // Returns checksum
}
// Taking appropriate actions for signal delivery
void alarm_handler(int signal_number) { // set an alarm clock to track delivery of the signals transmitted and received
    int icmp_packet_length = DATA_LENGTH + ICMP_HEADER_LENGTH; //define bits length of sent packets
    
    char send_buffer[BUFFER_SIZE];
    memset(send_buffer + ICMP_HEADER_LENGTH, 0, DATA_LENGTH); //memset fills the first n bytes of DATA_LENGTH of the memory area. //
    
    
    /*
     * here the icmp packet is created
     * also the ip checksum is generated
     */
    
    struct icmp *icmp_packet = (struct icmp *)send_buffer;
    
    icmp_packet->icmp_type = ICMP_ECHO;
    icmp_packet->icmp_code = 0;
    icmp_packet->icmp_id = pid;
    icmp_packet->icmp_seq = seq++;
    gettimeofday((struct timeval *)icmp_packet->icmp_data, NULL);
    icmp_packet->icmp_cksum = 0;
    icmp_packet->icmp_cksum = checksum((u_short *)icmp_packet, icmp_packet_length); /* Calculate ICMP checksum here */
   
    sendto(sock, send_buffer, icmp_packet_length, 0, host->ai_addr, host->ai_addrlen);
    /*
     * now the packet is sent
     */
    
    alarm(1);
    // count messages sent in order to calculate the loss percentage :)
}

int main(int argc, char **argv) {
    
    if (argc != 2) { // command contain adress only.
        printf("usage: %s host\n", argv[0]);
        exit(EXIT_FAILURE); // notify the parent node of exit status and the child dies immediately
    }
    
    pid = getpid() & 0xffff; //returns the process ID  of the calling process -->icmp_id
    
    int status = get_addrinfo(argv[1], &host); // return argument host and approprite pid
    if (status != 0) {
        printf("error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }
    
    sock = socket(host->ai_family, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    setuid(getuid()); //sets the effective ID of the calling proces to the user ID returned  by getuid()
    
    struct sigaction action;  // Install our alarm handler
    action.sa_handler = alarm_handler;
    if (sigaction(SIGALRM, &action, NULL) < 0) {
        // Mask out the alarm signal during normal operation to avoid races
        // and having to handle EINTR everywhere
        perror("signal");
        exit(EXIT_FAILURE);
    }
    
    char send_ip[INET_ADDRSTRLEN];
    get_sockaddr_text(host->ai_addr, send_ip, sizeof(send_ip));
    
    printf(
           "PING %s (%s): %d data bytes\n",
           host->ai_canonname,
           send_ip,
           DATA_LENGTH
           );
    
    alarm_handler(SIGALRM);
    
    /* The buffer incoming data accumulates in.  */
    char receive_buffer[BUFFER_SIZE];
    struct sockaddr receive_address;
    char control_buffer[BUFFER_SIZE];
    
    /*
     *  now we listen for responses
     */
    struct iovec iov;
    iov.iov_base = receive_buffer;
    iov.iov_len = sizeof(receive_buffer);
    
    
    // Set up iov and msgheader
    struct msghdr msg;
    msg.msg_name = &receive_address;
    msg.msg_namelen = sizeof(receive_address);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);
    
    struct timeval receive_time;
    char receive_ip[INET_ADDRSTRLEN];
    
    // Here goes our main loop
    for ( ; ; ) {
        ssize_t n = recvmsg(sock, &msg, 0);
        
        if (n > 0) {
            /*
             * The input packet contains the ip header in front of the icmp packet.
             */
            struct ip *ip_packet = (struct ip *)receive_buffer;
            if (ip_packet->ip_p == IPPROTO_ICMP) {
                
                int ip_header_length = ip_packet->ip_hl << 2;
                int icmp_packet_length = n - ip_header_length;
                if (icmp_packet_length >= 16) {
                    // allocate all necessary memory
                    struct icmp *icmp_packet = (struct icmp *)(receive_buffer + ip_header_length); /* move to the icmp packet */
                    /*
                     * Check whether we got the expected reply type.
                     */
                    if (
                        icmp_packet->icmp_type == ICMP_ECHOREPLY &&
                        icmp_packet->icmp_id == pid
                        )
                        msg_received_count++; //initilize messages received to compute for loss
                    /*
                     * Compute round-trip time.
                     */
                    {
                        gettimeofday(&receive_time, NULL);
                        struct timeval *send_time = (struct timeval *)icmp_packet->icmp_data;
                        
                        printf(
                               "%d bytes from %s: icmp_seq=%u ttl=%d time=%.3f ms\n",
                               icmp_packet_length,
                               get_sockaddr_text(&receive_address, receive_ip, sizeof(receive_ip)),
                               icmp_packet->icmp_seq,
                               ip_packet->ip_ttl,
                               timeval_to_ms(&receive_time) - timeval_to_ms(send_time) );
                        
                        /*
                         * Compute lost packets; if any.
                         */
                        printf("\n%d packets sent, %d packets received, %f percent packet loss. \n\n",
                               seq, msg_received_count, ((seq - msg_received_count)/seq) * 100.0);
                    }
                    
                }
            }
        }
    }
    
    return EXIT_SUCCESS;
}
