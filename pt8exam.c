#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/errno.h>

static int16_t memory[32768];
static uint8_t tape[262144];
static uint32_t tapeCnt;
static char junkbuf[262144];
static char databuf[262144];
static char ruboutbuf[262144];

enum state_e {init, wf_leader, in_leader, in_idle,  wf_addr2, wf_data2};
#define DATA 0
#define ADDR 1
#define HDR  2
#define FLD  3

static int16_t lastModifiedAddress=-1;
static int16_t lastModifiedData=-1;

static uint32_t section=0;
static uint32_t leaderlen=0;
static uint32_t footerlen=0;
static uint32_t addresslen=0;
static uint32_t state=init;
static uint32_t i=0;
static uint32_t origin=0;
static uint32_t address=0;
static uint32_t data=0;
static uint32_t field=0;
static uint32_t inrubout=0;
static uint32_t chksum=0;
static uint32_t gotNewAddress=0;


//
// Set entire memory array to -1
//
void ClearMemory(void) {
    memset(memory,0xFF,sizeof(memory));
}


//
//
//
void ShowDataBuffer(void) {
    int len=strlen(databuf)/5;

    if (len==0 && !gotNewAddress) return;
    if (len<2) printf("  %04o      : ",origin);
    else printf("  %04o-%04o : ",origin,origin+len-1);

    databuf[5*20+0]='.';
    databuf[5*20+1]='.';
    databuf[5*20+2]='.';
    databuf[5*20+3]='\0';
    printf("%s\n",databuf);

    gotNewAddress=0;
    databuf[0]='\0';
}


// wf_leader    hdr->in_leader  rubout
//              section++
//
// in_leader    hdr->in_leader  fld->in_idle    addr->wf_addr2  rubout
//                              ?checksum+=      checksum+=
//                              Set fld         Set addrH
//
// in_idle      hdr->in_leader  fld->in_idle    addr->wf_addr2  data->wf_data2  rubout
//              Store chksum    checksum+=      checksum+=      checksum+=
//              section++       Store memory    Store memory    Store memory
//                              Set fld         Set addrH       Set dataH
//
// wf_addr2     data->in_idle
//              Set addrL
//
// wf_data2     data->in_idle
//              Set dataL
//


//
//
//
int ProcessFile(char *filename) {
    int f=open(filename,O_RDONLY);
    if (f<0) {
        printf("Can't open file '%s'\n",filename);
        return 2;
    }
    tapeCnt=read(f,tape,sizeof(tape));
    close(f);

    // int showjunk=1;
    junkbuf[0]='\0';
    ruboutbuf[0]='\0';
    section=0;

    while (i<tapeCnt) {
        // If we're in rubout/comment mode then ignore everything until another 
        // rubout character appears in the stream
        if (inrubout) {
            if (tape[i]==0xFF) inrubout=0;
            else sprintf(&ruboutbuf[strlen(ruboutbuf)],"%04o ",tape[i]);
            i++;
            continue;
        }

        if (state==init) {
            ClearMemory();
            leaderlen=0;
            footerlen=0;
            addresslen=0;
            address=0;
            data=0;
            chksum=0;
            inrubout=0;
            state=wf_leader;
        }

        // WAIT FOR HEADER
        // Search for RUBOUT or HEADER, else increment JUNK
        if (state==wf_leader) {
            if (tape[i]==0xFF) {
                inrubout=1;
            } else if (tape[i]==0x80) {
                leaderlen++;
                section++;
                state=in_leader;
            } else {
                sprintf(&junkbuf[strlen(junkbuf)],"%04o ",tape[i]);
            }
            i++;
            continue;
        }

        // IN THE HEADER
        // Search for RUBOUT, HEADER, FIELD or ADDRESS
        if (state==in_leader) {
            if (tape[i]==0xFF) {inrubout=1; continue;}
            switch (tape[i]>>6) {
                case HDR:
                    leaderlen++;
                    i++;
                    break;
                case FLD:
                    printf("Leader %d bytes\n",leaderlen);
                    leaderlen=0;
                    field=tape[i]&0x03;
                    printf("Field is set to %d\n",field);
                    state=in_idle;
                    i++;
                    break;
                case ADDR:
                    printf("Leader %d bytes\n",leaderlen);
                    leaderlen=0;
                    chksum+=tape[i];
                    address=(tape[i]&0x3F)<<6;
                    state=wf_addr2;
                    i++;
                    break;
                default:
                    printf("Leader %d bytes\n",leaderlen);
                    leaderlen=0;
                    state=wf_leader;
                    break;
            }
            continue;
        }

        // IN IDLE MODE
        // Search for RUBOUT, HEADER, FIELD, ADDRESS, DATA
        if (state==in_idle) {
            if (tape[i]==0xFF) {inrubout=1; continue;}
            switch (tape[i]>>6) {
                case HDR:
                    // If we get a LEADER in the idle mode then a checksum must have been the previous data
                    // so restore the memory that got overwritten by the checksum and also remove it from
                    // the data buffer
                    if (lastModifiedAddress!=-1) {
                        memory[lastModifiedAddress]=lastModifiedData;
                        if (strlen(databuf)>=5) databuf[strlen(databuf)-5]='\0';
                        lastModifiedAddress=-1;
                        lastModifiedData=-1;
                    }
                    ShowDataBuffer();
                    // The checksum from the file (which still resides in 'data') got included in the 
                    // calculated checksum so we must subtract it to get a correct sum
                    chksum=(chksum - (data&0x3F) - (data>>6)) & 07777;
                    if (data==chksum) printf("Checksum %04o OK\n",chksum);
                    else printf("Checksum fail, file:%04o calculated: %04o\n",data, chksum);
                    chksum=0;
                    state=in_leader;
                    i++;
                    break;
                case FLD:
                    ShowDataBuffer();
                    field=tape[i]&0x03;
                    printf("Field is set to %d\n",field);
                    i++;
                    break;
                case ADDR:
                    ShowDataBuffer();
                    chksum+=tape[i];
                    address=(tape[i]&0x3F)<<6;
                    state=wf_addr2;
                    i++;
                    break;
                case DATA:
                    chksum+=tape[i];
                    data=(tape[i]&0x3F)<<6;
                    state=wf_data2;
                    i++;
                    break;
                default:
                    ShowDataBuffer();
                    printf("Invalid  byte @in_idle %03o\n",tape[i]);
                    return 1;
            }
            continue;
        }

        // WAIT FOR SECOND ADDRESS PART
        // Search for DATA
        if (state==wf_addr2) {
            switch (tape[i]>>6) {
                case DATA:
                    chksum+=tape[i];
                    address=address+(tape[i]&0x3F);
                    origin=address;
                    gotNewAddress=1;
                    state=in_idle;
                    addresslen++;
                    i++;
                    break;
                default:
                    ShowDataBuffer();
                    printf("Invalid  byte @wf_addr2 %03o\n",tape[i]);
                    return 1;
            }
            continue;
        }

        // WAIT FOR SECOND DATA PART
        // Search for DATA
        if (state==wf_data2) {
            switch (tape[i]>>6) {
                case DATA:
                    data=data+(tape[i]&0x3F);
                    sprintf(&databuf[strlen(databuf)],"%04o ",data);
                    lastModifiedAddress=address;
                    lastModifiedData=memory[address];
                    memory[address]=data;
                    address++;
                    chksum+=tape[i];
                    state=in_idle;
                    i++;
                    break;
                default:
                    ShowDataBuffer();
                    printf("Invalid  byte @wf_addr2 %03o\n",tape[i]);
                    return 1;
            }
            continue;
        }

    }

    if (leaderlen>0) printf("Leader %d bytes\n",leaderlen);
    ShowDataBuffer();
    return 0;
}








//
//
//
int main(int argc, char *argv[]) {
    uint8_t isBin = 0, isRim = 0, isWrite = 0, isVerbose = 0;
    int opt;
    while ((opt = getopt(argc, argv, "wrbvMV")) != -1) {
        switch (opt) {
        case 'w':
            isWrite++;
            break;
        case 'r':
            isRim++;
            break;
        case 'b':
            isBin++;
            break;
        case 'v':
            isVerbose++;
            break;
        case 'V':
            printf("%s version %s\n", "pt8exam", "0.00");
            break;
        // default:
        //     usage();
        }
    }
    
    ProcessFile(argv[optind]);
    if (isWrite) {
        char filename1[128];
        strcpy(filename1,argv[optind]);
        strcat(filename1,".core");
        int fd=open(filename1,O_WRONLY|O_CREAT|O_TRUNC,0644);
        if (fd<0) printf("Failed to create %s : %s\n",filename1,strerror(errno));
        for (int i=0; i<32768; i++) {
            uint16_t w=07402; // HLT
            if (memory[i]>=0 && memory[i]<=4095) w=memory[i];
            write(fd,&w,2);
        }
        close(fd);
    }
}
