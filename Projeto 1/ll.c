#include "ll.h"

char buf[255];
char received[255];
int res, fd;

int readResponseSET() {
    res = read(fd, received, 5);
    printf("Received SET. Checking values...\n");

    if (received[0] != FLAG || received[4] != FLAG){
        printf("FLAG error\n");
        return 1;
    }
    else if (received[1] != A_UA){
        printf("A_UA error\n");
        return 1;
    }
    else if (received[2] != C_UA){
        printf("C_UA error\n");
        return 1;
    }
    else if (received[3] != BCC2){
        printf("BCC2 error\n");
        return 1;
    }
    else{
        printf("SET is valid\n");
    }

    return 0;
}

int readResponseDISC() {
    res = read(fd, received, 5);
    printf("Received DISC. Checking values...\n");

    if (received[0] != FLAG || received[4] != FLAG){
        printf("FLAG error\n");
        return 1;
    }
    else if (received[1] != A_SET){
        printf("A_SET error\n");
        return 1;
    }
    else if (received[2] != C_DISC){
        printf("C_DISC error\n");
        return 1;
    }
    else if (received[3] != BCC_DISC){
        printf("BCC_DISC error\n");
        return 1;
    }
    else{
        printf("DISC is valid\n");
    }
    return 0;
}

int stopConnection() {
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &app.oldtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }
    close(fd);
    printf("\nENDING DATA TRANSFER\n");
    return 0;
}

int sendFrame(int fd, char* packet, int size){
    unsigned int maxSize = 6 + 2*size; // worst case scenario, every byte has to be stuffed (size duplicates) except for the control bytes (6) 
    char frame[maxSize];
    int index = 0;

    // Control front bytes
    frame[0] = FLAG;
    frame[1] = A_SET;
    if (app.ns == 0)
        frame[2] = BCC_NS0;
    else frame[2] = BCC_NS1;
    frame[3] = (A_SET ^ frame[2]);

    char bcc2 = 0x00;

	for (int i = 0; i < size; i++)
		bcc2 ^= packet[i];

    for (int x = 0; x < size; x++){
        if (packet[x] == FLAG || packet[x] == ESC){
            frame[index + 4] = ESC;
            frame[index + 5] = packet[x] ^ STUFFING;
            index++;
        }
        else frame[index + 4] = packet[x];

        index++;
    }

    frame[4 + index] = bcc2;
    index++;
    frame[4 + index] = FLAG;

    int totalSize = index + 5;


    // Control back bytes
    /*if (bcc2 == FLAG || bcc2 == ESC){
        frame[4+index] = ESC;
        frame[4+index+1] = bcc2 ^ STUFFING;
        index++;
    }
    else frame[index] = bcc2;
    index++;

    frame[index] = FLAG;
    */

    //frame[index + 4] = FLAG;;

    write(fd, frame, totalSize);
    printf("Size sent: %d\n", totalSize);

    return totalSize;  
}

int checkSucess(int fd, char* packet){
    char response[256];
    char temp[4];

    temp[0] = 0x01;
    temp[1] = 0x81;
    temp[2] = 0x05;
    temp[3] = 0x85;

    read(fd, response, 5);

    if (response[0] != FLAG || response[4] != FLAG){
        printf("FLAG error\n");
        return 1;
    }
    else if (response[1] != A_SET){
        printf("A_SET error\n");
        return 1;
    }
    else if (response[3] != BCC1){
        printf("BCC1 error\n");
        return 1;
    }
    
    if (response[2] == temp[0])
        return 1;
    if (response[2] == temp[1])
        return 1;
    if (response[2] == temp[2]){
        return 0;
    }
    if (response[2] == temp[3])
        return 0;
        

    return 1;
}

int llwrite(int fd, char* packet, int size){
    int frameSize, res;
    
    do{
        frameSize = sendFrame(fd, packet, size);
        startAlarm();
        app.alarmFlag = 1;
        res = checkSucess(fd, packet);
        if (!checkSucess(fd, packet)){
            stopAlarm();
            app.alarmFlag = 0;
        }
    } while (app.numTries < MAX_TRIES && app.alarmFlag);

    stopAlarm();

    return frameSize;
}

void checkFrameState(State *state, char byte){
    switch(*state){
        case START:
            if (byte == FLAG) *state = FLAG_RCVD;
            break;
  
        case FLAG_RCVD:
            if (byte == A_SET) *state = A_RCVD;
            else if (byte == FLAG){
                *state = FLAG_RCVD;
            }
            break;
        
        case A_RCVD:
            if (byte == BCC_NS0 || byte == BCC_NS1){
                *state = C_RCVD;
            }
            else if (byte == FLAG){
                *state = FLAG_RCVD;
            }
            break;

        case C_RCVD:
            if (byte == (A_SET ^ BCC_NS1) || byte == (A_SET ^ BCC_NS0)){
                *state = BCC1_RCVD;
            }
            else if (byte == FLAG){
                *state = FLAG_RCVD;
            }
            break;

        case BCC1_RCVD:
            if (byte != FLAG) *state = DATA_RCVD;
            else if (byte == FLAG){
                *state = FLAG_RCVD;
            }
            break;
        
        case DATA_RCVD:
            if (byte == FLAG){
                *state = END;
            }
            break;

        default:
            break;
    }
}

int readFrame(int fd, char* packet){
    int len = 0, msgIndex = 0;
    char byte, bcc2 = 0x00;
    State state = START; 
    char msg[1024];
    //int messagelen = 0;

    while (1){
        read(fd, &byte, 1);
        checkFrameState(&state, byte);
        packet[len] = byte;
        len++;
        if (state == END){
            for (int x = 4; x < len - 2; x++){
                msg[msgIndex] = packet[x];
                msgIndex++;
            }
            
            for (int i = 0; i < msgIndex; i++)
		        bcc2 ^= msg[i];

            if (packet[len - 2] == bcc2) break;
            else return 0;

        }
    }
    
    return len;
}

int destuff(char* packet, char* destuffed, int size){
    for (int x = 0; x < 5; x++){
        destuffed[x] = packet[x];
    }

    int destuffedIndex = 0;

    for (int i = 0; i < size - 2; i++){
        if (packet[i] == ESC && packet[i] == (FLAG ^ STUFFING)){
            destuffed[destuffedIndex] == FLAG;
            i++;
        }
        else if (packet[i] == ESC && packet[i] == (ESC ^ STUFFING)){
            destuffed[destuffedIndex] == ESC;
            i++;
        }
        else destuffed[destuffedIndex] = packet[i];

        destuffedIndex++;
    }

    destuffed[destuffedIndex] = FLAG;

    return destuffedIndex;
}

int verifyPacket (char* destuffedFrame, int size){
    if (destuffedFrame[0] != FLAG){
        printf("FLAG error\n");
        return 1;
    }
    else if (destuffedFrame[1] != A_SET){
        printf("A_SET error\n");
        return 1;
    }
    else if (app.ns == 0 && destuffedFrame[2] != BCC_NS0){
        printf("BCC_NS0 error\n");
        return 1;
    }
    else if (app.ns == 1 && destuffedFrame[2] != BCC_NS1){
        printf("BCC_NS1 error\n");
        return 1;
    }
    else if (destuffedFrame[3] != (A_SET ^ BCC_NS1) && destuffedFrame[3] != (A_SET ^ BCC_NS0)){
        printf("BCC1 error\n");
        return 1;
    }

    return 0;
}

int buildResponse(char* response, int type){
    response[0] = FLAG;
    response[1] = A_SET;
    response[3] = BCC1;
    response[4] = FLAG;

    if (type == 1){
        response[2] = 0x81;
    }
    else if (type == 2){
        response[2] = 0x01;
    }
    else if (type == 3){
        response[2] = 0x85;
    }
    else if (type == 4){
        response[2] = 0x05;
    }

    return 0;
}

int llread(int fd, char* packet){
    char destuffedFrame[1024];
    char message[256];
    char response[256];
    char* answer;
    int size = 0;

    if ((size = readFrame(fd, packet)) > 0){
        destuff(packet, destuffedFrame, size);

        for (int x = 4; x < size - 2; x++){
            if (destuffedFrame[x] != FLAG){
                message[x-4] = destuffedFrame[x];
            }
        }

        //code smell magic numbers mas caguei, strings sao cenas que ao C nao assistem
        if (verifyPacket(destuffedFrame, size)){
            if (destuffedFrame[2] == BCC_NS0){
                buildResponse(response, 1);
                write(fd, response, 5);
                printf("REJ sent: 1\n");
            }
            else if (destuffedFrame[2] == BCC_NS1){
                buildResponse(response, 2);
                write(fd, response, 5);
                printf("REJ sent: 0\n");
            }

            return 0; 
        }

        else{
            if (destuffedFrame[2] == BCC_NS0){
                buildResponse(response, 3);
                write(fd, response, 5);
                printf("RR sent: 1\n");
            }
            else if (destuffedFrame[2] == BCC_NS1){
                buildResponse(response, 4);
                write(fd, response, 5);
                printf("RR sent: 0\n");
            }
        }

    }

    write(STDOUT_FILENO, message, strlen(message));
    return 0;
}

int setStruct(const char* serialPort, int status){
    fd = open(serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(serialPort); 
        exit(-1); 
    }

    if (tcgetattr(fd, &app.oldtio) == -1) {
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&app.newtio, sizeof(app.newtio));
    app.newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    app.newtio.c_iflag = IGNPAR;
    app.newtio.c_oflag = 0;

    app.newtio.c_lflag = 0;

    app.newtio.c_cc[VTIME] = 0;
    app.newtio.c_cc[VMIN] = 5;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &app.newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    printf("New termios structure set\n");

    strcpy(app.serialPort, serialPort);
    app.status = status;
    app.ns = 0;
    app.timeouts = MAX_TIMEOUTS;
    app.numTries = 0;
    app.alarmFlag = 1;

    return fd;
}

int llopen(const char* serialPort, int status){
    int fd = setStruct(serialPort, status);
    
    switch(status){
        case TRANSMITTER: //TRANSMITTER

            buf[0] = FLAG;
            buf[1] = A_SET;
            buf[2] = C_SET;
            buf[3] = BCC1;
            buf[4] = FLAG;  

            do {
                printf("Writing SET\n");
                res = write(fd, buf, 5);
                printf("SET sent.\n");
                startAlarm();
                app.alarmFlag = 0;

                printf("Waiting for UA...\n");
                int error = readResponseSET();

            } while (app.numTries < MAX_TRIES && app.alarmFlag);

            stopAlarm();

            if (app.numTries >= MAX_TRIES) {
                printf("max number of tries achieved\n");
                return -1;
            }
            app.numTries = 0;
            break;


        case RECEIVER: //RECEIVER
            printf("Waiting for SET...\n");
            res = read(fd, received, 5);
            printf("Received SET. Checking values...\n");

            if (received[0] != FLAG || received[4] != FLAG){
                printf("FLAG error\n");
                return 1;
            }
            else if (received[1] != A_SET){
                printf("A_SET error\n");
                return 1;
            }
            else if (received[2] != C_SET){
                printf("C_SET error\n");
                return 1;
            }
            else if (received[3] != BCC1){
                printf("BCC1 error\n");
                return 1;
            }
            else{
                printf("SET is valid\n");
            }

            buf[0] = FLAG;
            buf[1] = A_UA;
            buf[2] = C_UA;
            buf[3] = BCC2;
            buf[4] = FLAG;
            res = write(fd, buf, 5);  
            printf("UA Sent.\n");

            break;
        default:
            printf("Status error!\n");
            return -1;
    }
    
    return fd;
}

int llclose(int fd, int status){
    switch(status){
        case TRANSMITTER:
            buf[0] = FLAG;
            buf[1] = A_SET;
            buf[2] = C_DISC;
            buf[3] = BCC_DISC;
            buf[4] = FLAG;  

            do {
                res = write(fd, buf, 5);
                printf("DISC sent.\n");
                startAlarm();
                app.alarmFlag = 0;
                //reading DISC frame
                printf("Waiting for DISC...\n");
                int error = readResponseDISC();

            } while (app.numTries < MAX_TRIES && app.alarmFlag);

            stopAlarm();

            if (app.numTries >= MAX_TRIES) {
                printf("max number of tries achieved\n");
                return -1;
            }
            else {
                printf("Writing UA\n");
                buf[0] = FLAG;
                buf[1] = A_UA;
                buf[2] = C_UA;
                buf[3] = BCC2;
                buf[4] = FLAG;
                res = write(fd, buf, 5); 
                printf("UA Sent.\n"); 
                sleep(1);
            }
            break;


        case RECEIVER:
            printf("Waiting for DISC...\n");
            res = read(fd, received, 5);
            printf("Received DISC. Checking values...\n");

            if (received[0] != FLAG || received[4] != FLAG){
                printf("FLAG error\n");
                return 1;
            }
            else if (received[1] != A_SET){
                printf("A_SET error\n");
                return 1;
            }
            else if (received[2] != C_DISC){
                printf("C_DISC error\n");
                return 1;
            }
            else if (received[3] != BCC_DISC){
                printf("BCC_DISC error\n");
                return 1;
            }
            else{
                printf("DISC is valid\n");
            }

            buf[0] = FLAG;
            buf[1] = A_SET;
            buf[2] = C_DISC;
            buf[3] = BCC_DISC;
            buf[4] = FLAG;  
            res = write(fd, buf, 5);
            printf("DISC sent.\n");  
            
            // UA Handling
            printf("Waiting for UA...\n");

            res = read(fd, received, 5);
            printf("Received UA. Checking values...\n");

            if (received[0] != FLAG || received[4] != FLAG){
                printf("FLAG error\n");
                return 1;
            }
            else if (received[1] != A_UA){
                printf("A_UA error\n");
                return 1;
            }
            else if (received[2] != C_UA){
                printf("C_UA error\n");
                return 1;
            }
            else if (received[3] != BCC2){
                printf("BCC2 error\n");
                return 1;
            }
            else {
                printf("UA is valid\n");
            }
            break;
        default:
            printf("Status error!\n");
            return -1;
    }

    stopConnection();
    return fd;
}