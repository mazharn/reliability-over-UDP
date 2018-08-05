#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <cstring>
using namespace std;

#define MSS 1000
ofstream outt("out.jpg", ios::out | ios::binary);

struct packet
{
	char appData[MSS+10];			//10 to be on the safe side

	int sequenceNumber;
	unsigned long checksum;		//used to check if corrupt
	int sizeOfappData;
	packet()
	{
		sizeOfappData=0;
		sequenceNumber=9;
		checksum=0;
	}
};

struct ack
{
	char isAck[6];
	int sequenceNumber;
	unsigned int checksum;	
	ack()
	{
		sequenceNumber = 9;
		checksum = 0;
	}	
};

int connSock=0;
int lastCorrectlyReceived = 9;	//sequence number of last correctly received packet
struct sockaddr_in client_addr;
socklen_t cl;
int artificialAckCorrupt = 0;

void open_udt_connection();
void close_udt_connection();
void wait_for_0_from_below(ack*);
void wait_for_1_from_below(ack*);
void rdt_rcv(packet*);
ack* make_pkt(char*,int,int);		//takes ack number / sequence number and checksum as arguments
bool corrupt(packet*);		//returns true if corrupt
int has_seq(packet*);		//returns sequence number
void extract(packet*,char[],int);
void deliver_data(char[],int);	//send received data to application
void udt_send(ack*);
unsigned long calculateChecksum(char[],int);
int artficial_packet_drop();
void corrupt_Ack_Packet(char*);
void fixAck(ack*);



int main()
{
	open_udt_connection();
	
	wait_for_0_from_below(NULL);

	close_udt_connection();
}

void open_udt_connection()
{
	int n=0,err=0;
	struct sockaddr_in server_addr, sendclient_addr;
	connSock = socket(AF_INET,SOCK_DGRAM,0);
	if(connSock == -1)
	{
		cout<<"Error No: "<<errno<<endl;
		exit(0);
	}
	bzero((char *) &server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(1234);
	server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");//192.168.29.138

	err = bind(connSock,(struct sockaddr *)&server_addr,sizeof(server_addr));
	if(err == -1)
	{
		cout<<"Error No: "<<errno<<endl;
		exit(0);
	}
	
	int clientLength = sizeof(client_addr);
	cl = clientLength;
}

void close_udt_connection()
{
	close(connSock);
}

int z=0;
void rdt_rcv(packet *pkt)
{
	int bytesReceived = 0;
	bytesReceived = recvfrom(connSock,(struct packet *)pkt,sizeof(struct packet),0,(struct sockaddr *)&client_addr,&cl);
}

void wait_for_0_from_below(ack *prev_sndpkt)
{
	while(1)
	{
		int checksum=0;
		packet *rcvpkt = new packet;
		ack *sndpkt = new ack;
		rdt_rcv(rcvpkt);
		if(strcmp(rcvpkt->appData,"exit")==0)
		{
			outt.close();
			exit(0);
		}
		if(artficial_packet_drop() == 0)
		{
			char *data = new char[MSS+10];
			int dataSize =rcvpkt->sizeOfappData;
			if(!corrupt(rcvpkt) && has_seq(rcvpkt)==0)
			{
				extract(rcvpkt,data,dataSize);
				deliver_data(data,dataSize);
				lastCorrectlyReceived = rcvpkt->sequenceNumber;
				char ackData[6] = "ACK";
				checksum = calculateChecksum(ackData,strlen(ackData));
				corrupt_Ack_Packet(ackData);
				sndpkt = make_pkt(ackData,lastCorrectlyReceived,checksum);
				udt_send(sndpkt);
				wait_for_1_from_below(sndpkt);
			}	
			if(corrupt(rcvpkt) || has_seq(rcvpkt)==1)
			{
				cout<<"Corrupt packet received."<<endl;
				if(prev_sndpkt != NULL)
				{
					if(artificialAckCorrupt==1)
					{
						fixAck(prev_sndpkt);
					}
					udt_send(prev_sndpkt);	//it retransmits packet which was made previously
				}
			}
		}
	}
}

bool corrupt(packet *pkt)
{
	unsigned long chksm = calculateChecksum(pkt->appData,pkt->sizeOfappData);
	if(chksm == pkt->checksum)
	{
		//cout<<"Not corrupt"<<endl;
		return false;
	}
	return true;
}

int has_seq(packet *pkt)
{
	return pkt->sequenceNumber;
}

void extract(packet *pkt, char *data, int size)
{
	memcpy(data,pkt->appData,size+1);
}


void deliver_data(char *buffer, int size)
{
	outt.write(buffer, size);
}

void wait_for_1_from_below(ack *prev_sndpkt)
{
	while(1)
	{
		int checksum=0;
		char *data = new char[MSS+10];
		packet *rcvpkt = new packet;
		ack *sndpkt = new ack;
		rdt_rcv(rcvpkt);
		if(strcmp(rcvpkt->appData,"exit")==0)
		{
			outt.close();
			exit(0);
		}
		if(artficial_packet_drop() == 0)
		{
			int dataSize = rcvpkt->sizeOfappData;
			if(!corrupt(rcvpkt) && has_seq(rcvpkt)==1)
			{
				extract(rcvpkt,data,dataSize);
				deliver_data(data,dataSize);
				lastCorrectlyReceived = rcvpkt->sequenceNumber;
				char ackData[6] = "ACK";
				checksum = calculateChecksum(ackData,strlen(ackData));
				corrupt_Ack_Packet(ackData);
				sndpkt = make_pkt(ackData,lastCorrectlyReceived,checksum);
				udt_send(sndpkt);
				wait_for_0_from_below(sndpkt);
			}	
			if(corrupt(rcvpkt) || has_seq(rcvpkt)==0)
			{
				if(artificialAckCorrupt==1)
				{
					fixAck(prev_sndpkt);
				}
				udt_send(prev_sndpkt);	//it retransmits packet which was made previously
			}
		}
	}
}

ack* make_pkt(char *ackData,int ackNumber,int checksum)
{
	ack *pkt = new ack;
	strcpy(pkt->isAck,ackData);
	pkt->sequenceNumber = ackNumber;
	pkt->checksum = checksum;
	return pkt;
}

static int ch=0;
void udt_send(ack *pkt)
{
	int bytesSent=0;

	bytesSent = sendto(connSock,(struct ack *)pkt,sizeof(struct ack),0,(struct sockaddr *)&client_addr,sizeof(client_addr));
	ch++;
}

unsigned long calculateChecksum(char *buffer,int sizeOfappData)
{
	unsigned int c=0;
	int size=sizeOfappData;
	unsigned short word;
	
	if (size%2 == 1){
		strcat(buffer,"0");
		size++;
	}
	
	unsigned long sum=0;
	
	for (int i=0;i<size;i=i+2){
		word=((buffer[i]<<8)&0xFF00)+((buffer[i+1])&0x00FF);
		sum=sum+(unsigned long)word;
	}

	while (sum>>16){
		sum=(sum & 0x0000FFFF)+(sum>>16);
	}
	if(strcmp(buffer,"ACK0")==0)
	{
		buffer[sizeOfappData]='\0';
	}
	return ~sum;
}

int artficial_packet_drop()
{
	srand(time(NULL));
	int num = rand() % 100;
	if(num <= 10)
	{
		cout<<"Packet dropped artificially."<<endl;
		return 1;
	}
	return 0;
}

void corrupt_Ack_Packet(char *buffer)
{
	srand(time(NULL));
	int num = rand() % 100;
	if(num <= 10)
	{
		buffer[1]='b';
		cout<<"Ack corrupted artificially."<<endl;
		artificialAckCorrupt = 1;
	}
}

void fixAck(ack *pkt)
{
	pkt->isAck[1] = 'C';
}
