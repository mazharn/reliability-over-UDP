#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <cstring>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

using namespace std;

struct sockaddr_in senderAddress, receiverAddress;
struct timeval timeout, tvalBefore, tvalAfter;
socklen_t cl;
int connSockfd=0, seqNoAssigned=0;
const int NOWAIT = 0;
const int SEC = 1;
const int MSEC = 5;
#define MSS 1000


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

packet* make_pkt(int, char*, int);
void udt_send(packet *);
void rdt_send(char*);		//compute checksum in this function
void open_udt_connection();
void close_udt_connection();
void wait_for_ACK(packet*);	//argument is sndpkt whose sequenceNumber is expected sequence number
void calcUtilization(double, double);
int rdt_rcv(ack*,bool);		//second argument tells if we want to discard data or not, if true set timeout = few ms
bool corrupt(ack*);
bool isAck(ack*,int);
unsigned long calculateChecksum(char*,int);
void corruptData(char*);
int artificial_Ack_Drop();
int bytesReadFromFile=0;	//this is equal to number of bytes in array used in checksum
void fixData(char*);
int artificialCorrupt=0;
char original;

static double delayTrans = 0.00008176;	// L/R where L=1022*8 bits, R=10^8
double sumRTT,avgRTT = 0;
double numPackets = 0;
double cpuTime = 0;
int packetNumber=0;

ofstream fileWrite("RDT_Log.txt");		//store each RTT of file being sent


int main()
{
	gettimeofday(&tvalBefore, NULL);

	char *dataRead = NULL;
	ifstream fread("1mb.jpg", ios::in|ios::binary|ios::ate);
	int read_size;
	open_udt_connection();
	int size=0;
	if (fread)
	{
		size = (int) fread.tellg();
		dataRead = new char [MSS+10];
		char temp[MSS+10] = {NULL};
		fread.seekg (0, ios::beg);
		while (!fread.eof())
		{
			fread.read(temp, MSS);
			bytesReadFromFile=(int)fread.gcount();
			rdt_send(temp);
			temp = {NULL};       
		}

		calcUtilization(sumRTT, numPackets);
		fileWrite.close();

		rdt_send("exit");
		fread.close();
	}

	close_udt_connection();	
	exit(0);
}

void rdt_send(char *data)
{
	if(strcmp(data,"exit")==0)
	{
		struct packet *sndpkt;
		sndpkt = make_pkt(seqNoAssigned,data,0);
		udt_send(sndpkt);
		cout<<"Exiting"<<endl;	
		exit(0);
	}
	packetNumber++;
	fileWrite<<"---------------------------\nPacket number: "<<packetNumber<<"\nApplication Data received."<<endl;
	ack *pkt = new ack;	/* used to discard packet */
	while(rdt_rcv(pkt,true) > 0)
	{
		/* do nothing. It is actually discarding the packet */
	}
	struct packet *sndpkt;
	unsigned long chksm = calculateChecksum(data,bytesReadFromFile);
	corruptData(data);
	sndpkt = make_pkt(seqNoAssigned,data,chksm);
	if(seqNoAssigned==0)
	{
		seqNoAssigned=1;
	}
	else
	{
		seqNoAssigned=0;
	}
	udt_send(sndpkt);
	wait_for_ACK(sndpkt);

}

int z=0;
void udt_send(packet *sndpkt)
{
	int bytesSent=0;
	bytesSent = sendto(connSockfd,(struct packet*)sndpkt,sizeof(struct packet),0,(struct sockaddr *)&receiverAddress,sizeof(receiverAddress));
	fileWrite<<"Sequence number "<<sndpkt->sequenceNumber<<" assigned and sent."<<endl;
}

packet* make_pkt(int seqNo, char *data, int chksm)
{
	packet *pkt = new packet;
	pkt->sequenceNumber = seqNo;
	memcpy(pkt->appData,data,bytesReadFromFile+1);
	pkt->sizeOfappData = bytesReadFromFile;
	pkt->checksum=chksm;
	return pkt;
}

void open_udt_connection()
{
	int conn_sock1=0,n=0,err=0;
	struct sockaddr_in server_addr;
	connSockfd = socket(AF_INET,SOCK_DGRAM,0);
	if(connSockfd == -1)
	{

		cout<<"Error No: "<<errno<<endl;
		exit(0);
	}
	receiverAddress.sin_family = AF_INET;
	receiverAddress.sin_port = htons(1234);
	receiverAddress.sin_addr.s_addr = inet_addr("172.16.12.100");//192.168.29.138

	int clientLength = sizeof(server_addr);
	cl = clientLength;

	timeout.tv_sec = SEC;
	timeout.tv_usec = MSEC;
	if(setsockopt (connSockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(timeout)) < 0)
	{
		cout<<"Error while setting timeout value. Quitting program."<<endl;
		exit(0);
	}
}

void close_udt_connection()
{
	close(connSockfd);
}

void wait_for_ACK(packet *sndpkt)
{
	bool corruptReceived = false;
	while(1)	//waits forever until conditions are met
	{
		ack *rcvpkt = new ack;
		if( rdt_rcv(rcvpkt,false) > 0)
		{
			if(artificial_Ack_Drop()==0)
			{

				if( !corrupt(rcvpkt) && isAck(rcvpkt,sndpkt->sequenceNumber) )
				{
					gettimeofday(&tvalAfter, NULL);
					cpuTime=((tvalAfter.tv_sec - tvalBefore.tv_sec)*1000000L+tvalAfter.tv_usec) - tvalBefore.tv_usec;
		
					sumRTT= sumRTT+cpuTime;		//total RTT time for a file
					numPackets++;				//number of packets sent for a file transfer
					fileWrite<<"Ack with sequence number: "<<rcvpkt->sequenceNumber<<" received succesfully at: "<<cpuTime<<endl;	
					return;
				}
				if( corrupt(rcvpkt) || !isAck(rcvpkt,sndpkt->sequenceNumber) )
				{
					cout<<"duplicate/corrupted ack received."<<endl;
					fileWrite<<"duplicate/corrupted ack received."<<endl;
					//cout<<"duplicate/corrupted ack received"<<endl;
					corruptReceived=true;
					//do nothing. Discard packet
				}
			}
		}
		else	//timeout
		{
			cout<<"Timeout occured."<<endl;
			fileWrite<<"timeout occurred"<<endl;
			//cout<<"timeout occurred"<<endl;
			if(artificialCorrupt==1)
			{
				fileWrite<<"Correct packet sent."<<endl;
				cout<<"Correct packet sent."<<endl;
				//cout<<"Fixed packet sent"<<endl;
				fixData(sndpkt->appData);
				udt_send(sndpkt);
			}
			else
			{
				udt_send(sndpkt);
			}
			//start timer;
		}
	}
	
}

int rdt_rcv(ack *ackpkt, bool discard)
{
	if(discard==true)
	{
		timeout.tv_sec = NOWAIT;
		timeout.tv_usec = MSEC;
		if(setsockopt (connSockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(timeout)) < 0)
		{
			cout<<"Error while setting timeout value. Quitting program."<<endl;

		}
	}
	else
	{
		timeout.tv_sec = SEC;
		timeout.tv_usec = MSEC;
		if(setsockopt (connSockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(timeout)) < 0)
		{
			cout<<"Error while setting timeout value. Quitting program."<<endl;
		//	exit(0);
		}
	}
	int bytesReceived=0;
	bytesReceived = recvfrom(connSockfd,(struct ack *)ackpkt,sizeof(struct ack),0,(struct sockaddr *)&receiverAddress,&cl);
	return bytesReceived;
}

bool corrupt(ack *pkt)
{
	unsigned long chksm = calculateChecksum(pkt->isAck,strlen(pkt->isAck));
	if(chksm == pkt->checksum)
	{
		//cout<<"Not corrupt"<<endl;
		return false;
	}
	return true;
}

bool isAck(ack *pkt, int seq)
{
	if(pkt->sequenceNumber == seq)
	{
		return true;
	}
	return false;
}

unsigned long calculateChecksum(char *buffer,int sizeOfData)
{
	unsigned int c=0;
	int size=sizeOfData;
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
	return ~sum;
}

void corruptData(char *buffer)
{
	artificialCorrupt=0;
	srand(time(NULL));
	int num = rand() % 100;
	if(num <= 10)
	{
		artificialCorrupt = 1;
		cout<<"Packet Corrupted."<<endl;
		fileWrite<<"Packet Corrupted."<<endl;
		int size=bytesReadFromFile;
		original = buffer[size-15];	
		buffer[size-15]='9';
	}
}

void fixData(char *buffer)
{
	buffer[bytesReadFromFile-15] = original;
}

int artificial_Ack_Drop()
{
	srand(time(NULL));
	int num = rand() % 100;
	if(num <= 10)
	{
		cout<<"Ack dropped artificially."<<endl;
		fileWrite<<"Ack dropped artificially."<<endl;
		return 1;
	}
	return 0;
}

void calcUtilization(double sum, double numPkt)
{
	cout<<"sum: "<<sum<<endl;
	cout<<"numpkt: "<<numPkt<<endl;
	double util;
	sum=sum/1000000;
	avgRTT=sum/numPackets;	
	cout<<"avgRTT: "<<avgRTT<<endl;
	util = (delayTrans/(delayTrans+avgRTT));
	cout<<"utilization of File: "<<util<<endl;
	fileWrite<<"utilization of File: "<<util<<endl;

}
