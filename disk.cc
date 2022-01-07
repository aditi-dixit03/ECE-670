#include<cstdlib>
#include<iostream>
#include"thread.h"
#include<assert.h>
#include<fstream>
#include<cmath>
#include<list>
#include<iterator>

using namespace std;

int diskSize;
int lastTrack;
char** files;
int* servicedThreads;
int numOfProducers;

typedef struct _buffer{
	int requester;
	int track;
} Buffer;

struct Comparator{
	bool operator()(const Buffer *q1, const Buffer *q2){
		return (abs(q1->track - lastTrack) < abs(q2->track - lastTrack));		
	}
};

list<Buffer*> buffer;

bool isBufferFull(){
	return (buffer.size()>=diskSize);
}

int BufferSize(){
	return diskSize;
}

void IterationBuffer(){
	list<Buffer*> :: iterator itr;
	for(itr = buffer.begin(); itr != buffer.end(); itr++){
		cout<<((*itr)->requester)<<"\n";
		cout<<((*itr)->track)<<"\n";
	}
}

void serviceBuffer(){
	Buffer* serviceOut = (Buffer*)malloc(sizeof(Buffer));
	serviceOut = buffer.front();
	buffer.pop_front();
	cout<<"service requester "<<serviceOut->requester<<" track "<<serviceOut->track<<"\n";
	lastTrack = serviceOut->track;
	servicedThreads[serviceOut->requester] = 1;
	serviceOut = NULL;
	free(serviceOut);
}

void sendBuffer(Buffer* request){
	cout<<"requester "<<request->requester<<" track "<<request->track<<"\n";
	buffer.push_back(request);
	buffer.sort(Comparator());
	servicedThreads[request->requester] = 0;
}

void serviceFunc(void* arg){
	int sID;
	sID = (intptr_t)arg;
	thread_lock(1);
	while(BufferSize()){
		while(!isBufferFull()){
			thread_wait(1, 3);
		}
		if(!buffer.empty()){
			serviceBuffer();
		}
		thread_broadcast(1, 2);

	}
	thread_unlock(1);
}

void reqFunc(void* arg){
	int rID;
	rID = (intptr_t)arg;
	int currentTrack = 0;
	Buffer* serviceIn;
	ifstream Track(files[rID]);
	while(1){
		Track >> currentTrack;
		if(Track.eof()){
			break;
		}
		
		serviceIn = (Buffer*)malloc(sizeof(Buffer));
		serviceIn->requester = rID;
		serviceIn->track = currentTrack;
		thread_lock(1);
		while(isBufferFull() || servicedThreads[serviceIn->requester]==0){
			thread_wait(1, 2);
		}
		sendBuffer(serviceIn);
		thread_broadcast(1, 3);
	}
	Track.close();

	while(servicedThreads[rID] == 0){
		thread_wait(1, 2);
	}

	numOfProducers--;
	if(numOfProducers < BufferSize()){
		diskSize--;
		thread_broadcast(1, 3);
	}

	thread_unlock(1);

}

void threadSpawn(void* arg){
	int id, i;
	id = (intptr_t)arg;
	for(i=0; i<id; i++){
		if(i==id-1){
			if(thread_create((thread_startfunc_t)serviceFunc, (void*)(intptr_t)i)){
				cout<<"Thread creation failed\n";
			}
		}
		else{
			if(thread_create((thread_startfunc_t)reqFunc, (void*)(intptr_t)i)){
				cout<<"Thread creation failed\n";
			}
		}

	}

	//Should work without yield in a concurrent program
	// if (thread_yield()) { 
	//     cout << "thread_yield failed\n";
	//     exit(1);
	// }	
}


int main(int argc, char** argv){
	if(argc<=2){
		exit(0);
	}
	try{
		diskSize = stoi(argv[1]);

	}
	catch(std::invalid_argument e){
		exit(0);
	}
	int requestNumber = argc;
	int fileNumber = argc-2;
	int numOfThreads = argc-1;
	files = argv+2;
	lastTrack = 0;
	numOfProducers = numOfThreads - 1;
	int numOfConsumers = numOfThreads - numOfProducers;
	servicedThreads = (int*)malloc((sizeof(int))*(numOfProducers));
	for(int i=0; i<numOfProducers; i++){
		servicedThreads[i] = 1;
	}
	if(thread_libinit((thread_startfunc_t)threadSpawn, (void*)(intptr_t)(numOfThreads))){
		cout<<"Thread initialization failed.\n";
	}

	servicedThreads = NULL;
	free(servicedThreads);

	return 0;
}
