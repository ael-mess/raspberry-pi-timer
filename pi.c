//compile with -lwiringPi -lpthread
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <wiringPi.h>
#include <sr595.h>

#define BASEPIN 100
#define NBPINS 32
#define DATAPIN 0
#define CLOCKPIN 1
#define LATCHPIN 2

#define MAX_UDP 1500
#define BUFF 1024
#define DATA_SIZE 2

const uint8_t zero[8] = {1, 1, 1, 1, 1, 0, 1, 0};
const uint8_t one[8] = {0, 0, 1, 1, 0, 0, 0, 0};
const uint8_t two[8] = {1, 1, 0, 1, 1, 0, 0, 1};
const uint8_t three[8] = {0, 1, 1, 1, 1, 0, 0, 1};
const uint8_t four[8] = {0, 0, 1, 1, 0, 0, 1, 1};
const uint8_t five[8] = {0, 1, 1, 0, 1, 0, 1, 1};
const uint8_t six[8] = {1, 1, 1, 1, 1, 0, 0, 1};
const uint8_t seven[8] = {1, 1, 1, 1, 1, 0, 0, 1};
const uint8_t eight[8] = {1, 1, 1, 1, 1, 0, 1, 1};
const uint8_t nine[8] = {0, 0, 1, 1, 1, 0, 1, 1};

int fdp[2];
int doing = 1;

void writeDigit(uint8_t range, const uint8_t* number) {
    int i;
    for(i=0; i<8; i++) digitalWrite(BASEPIN + range, number[i]);
    for(i=0; i<8; i++) digitalWrite(BASEPIN + range + 2, number[i]);
}

int8_t parseData(uint8_t range, uint16_t data) {
    int8_t res = -1;
    if(data > 99) {
        if(range == 0) res = (int8_t) ((data/60)-(int16_t)((data/600)*10));
        else if(range == 1) res = (int8_t) (data/600);
    }
    else if(data <= 99) {
        if(range == 0) res = (int8_t) (data - (int16_t)((data/10)*10));
        else if(range == 1) res = (int8_t) (data/10);
    }
    return res;
}

void* print(void* none) {
    writeDigit(0, zero);
    writeDigit(1, zero);
    int i;
    int8_t res, pdata[2];
    uint16_t data;
    while(doing) {
        res = read(fdp[0], &data, 2);
        if(res != 2) perror("print.read");
        printf("print.read (read) : %d\n", data);
        pdata[0] = parseData(0, data);
        pdata[1] = parseData(1, data);
        printf("print.parseData (units) : %d (tens) : %d\n");
        for(i=0; i<2; i++) {
            if(pdata[i] == 0) writeDigit(i, zero);
            else if(pdata[i] == 1) writeDigit(i, one);
            else if(pdata[i] == 2) writeDigit(i, two);
            else if(pdata[i] == 3) writeDigit(i, three);
            else if(pdata[i] == 4) writeDigit(i, four);
            else if(pdata[i] == 5) writeDigit(i, five);
            else if(pdata[i] == 6) writeDigit(i, six);
            else if(pdata[i] == 7) writeDigit(i, seven);
            else if(pdata[i] == 8) writeDigit(i, eight);
            else if(pdata[i] == 9) writeDigit(i, nine);
            else perror("print.parseData");
        }
    }
    return NULL;
}

void* timer(void* arg) {
    uint16_t init = atoi((const char *) arg);
    printf("timer.atoi (init value) : %d\n", init); //test reception
    struct timespec now;
    int res = clock_gettime(CLOCK_REALTIME, &now);
    if(res != 0) perror("timer.clock_gettime");
    time_t end = now.tv_sec, start = now.tv_sec;
    uint16_t data;
    while(doing) {
        clock_gettime(CLOCK_REALTIME, &now);
        data = init - (end - start);
        if(end != now.tv_sec) {
            res = write(fdp[1], &data, 2);
            printf("timer.write (send) : %d\n", data); //test data sent
            if(res != 2) perror("timer.write");
        }
        if((uint16_t)(end - start) >= init) doing = 0;
        end = now.tv_sec;
    }
    return NULL;
}

int UDPserver(char *service) {
    struct addrinfo precision, *result, *origin;
    int res;
        
    /* build address structure */
    memset(&precision, 0, sizeof precision);
    precision.ai_family = AF_UNSPEC;
    precision.ai_socktype = SOCK_DGRAM;
    precision.ai_flags = AI_PASSIVE;
    res = getaddrinfo(NULL, service, &precision, &origin);
    if(res < 0) { perror("UDPserver.getaddrinfo"); return -1; }
    struct addrinfo *p;
    for(p=origin, result=origin; p!=NULL; p=p->ai_next)
        if(p->ai_family == AF_INET6) { result = p; break; }
        
    /* create socket */
    int soc = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if(soc < 0) { perror("UDPserver.socket"); return -1; }
        
    /* options  */
    int vrai=1;
    if(setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &vrai,sizeof(vrai))<0) { perror("UDPserver.setsockopt (REUSEADDR)
"); return -1; }

    /* bind the address */
    res = bind(soc, result->ai_addr, result->ai_addrlen);
    if(res < 0) { perror("UDPserver.bind"); return -1; }

    /* free info structure */
    freeaddrinfo(origin);
        
    return soc;
}

int onReceived(int soc, uint8_t *data) {
    struct sockaddr_storage address;
    socklen_t len = sizeof(address);
    //unsigned char data[MAX_UDP];
    int res = recvfrom(soc, data, DATA_SIZE*sizeof(*data), 0, (struct sockaddr *) &address, &len);
    if(res<0) { perror("onReceived.recvfrom"); return -1; }

    char host[NI_MAXHOST], service[NI_MAXSERV];
    int resI = getnameinfo((struct sockaddr *) &address, len, host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSER
V);

    if(resI == 0) {
        printf("Received %d bytes from %s:%s\n (udp) : %s\n", res, host, service, data); //test onReceived
        char acn[32] = "re�çu fr�ère\n";
        if(sizeof(acn) != sendto(soc, acn, sizeof(acn), 0, (struct sockaddr *) &address, len)) perror("onReceived.
sendto");
    }
    else { perror("onReceived.getnameinfo"); return -1; }

    return 0;
}

int main (void) {
    int socket = UDPserver("5555");
    if(socket == -1) perror("main.socket");
    pthread_t tid1, tid2;

    printf("main.socket : %d\n", socket); //test socket

    wiringPiSetup() ;
    sr595Setup(BASEPIN, NBPINS, DATAPIN, CLOCKPIN, LATCHPIN);

    int res;
    res = pipe(fdp);
    if(res != 0) perror("main.pipe");
    
    uint8_t data[DATA_SIZE];
    memset(&data, 0, sizeof(data));
    while(1) {
        doing = 1;
        
        res = onReceived(socket, data);
        if(res != 0) perror("main.onReceived");
        int i;
        printf("main.onReceived :");
        for(i=0; i<DATA_SIZE; i++) printf(" %d", data[i]); //test udp server
        printf("\n");

        res = pthread_create(&tid1,NULL,print,NULL);
        if(res != 0) perror("main.pthread_create");
        res = pthread_create(&tid2,NULL,timer,(void *)(data));
        if(res != 0) perror("main.pthread_create");
        
        pthread_join(tid1,NULL);
        pthread_join(tid2,NULL);

        printf("main.doing %d\n", doing);
    }
    
    return 0 ;
}
