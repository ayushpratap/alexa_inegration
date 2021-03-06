#include <stdio.h>
#include <string.h>
#include "curl.h"
#include "cJSON.h"
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define HANDLECOUNT  3
#define DOWN_HANDLE  0
#define PING_HANDLE  1
#define EVENT_HANDLE 2

#define UPLOAD_FILE_NAME     "./16k.raw"
#define HEAD_FILE_NAME       "./HeadFile"
#define BODY_FILE_NAME       "./BodyFile"
#define DEL_HTTPHEAD_EXPECT  "Expect:"
#define DEL_HTTPHEAD_ACCEPT  "Accept:"

struct MemoryStruct {
	char *memory;
	size_t size;
};

typedef enum NET_STATE_T{
	NET_STATE_IDLE,
	NET_STATE_PING,
	NET_STATE_SEND_EVENT,
	NET_STATE_SEND_STATE,
}net_state_t;

char *atoken = "Atza|IwEBIC8buqMgu68FRve-HPjlLifjHsNUwBHISLuZPrxC9H2jHNqJxIyWMWrMv7RLAIuNwle4gJWuVKuRLk08ZezdMYoi1oHSGuw-gWp3f8jXtjqLepbBLNB_FbPlMr2axZ3CEzF4DzuMmrwRWJ2Uk4abOXoucirw1ZnAyD7qxkHq1rF-D0k8sHwXSTkFY-OXc11lW_zgDiDXVJFddYH3FwXLeFhEf7Svo-WNAtGTEqniABtkinStETVIo9SsJQR4EKkkTLiXXGCS1qoMyjNNrpvgrtAs7d22LwWd7YtaW5l1LW-OFbtLIme5urD0lcpY4-aNRuiUf6ZPqOrQkqUMPepXLbJbhGkmhaRmIg9vlxalEyxwDZ1-o14I1hgmoTntcHHaIvX1psKKJdHW1mdux5TlHRNK0Ae9CGk73L6pchzILHfws7B3pgRSPLH2SJJAMGLIxktlNclNkvyhV5ww2hg32-I6c_437wsnbGXmbN-VN7NspLzd__UCscaVvU4aAqzq9lYCo0ny8GqKFUChN1z4lZxgUB2uv7AzLHFXsuBUyl7JWw";

static struct MemoryStruct chunk;
static unsigned int data = 100;
static FILE *saveHeadFile = NULL;
static FILE *saveBodyFile = NULL;
static FILE *uploadFile = NULL;
static net_state_t netState;
static unsigned char count = 0;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{  
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;
	
	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if(mem->memory == NULL) {
		/* out of memory! */
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}
	
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

//	printf("%s : size %d \n %s \n", __FUNCTION__, realsize, (char *)contents);
	return realsize;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)   
{
	printf("write data begin...\n");
    int written = fwrite(ptr, size, nmemb, (FILE *)stream);
    fflush((FILE *)stream);
    return written;
}
size_t readFileFunc(char *buffer, size_t size, size_t nitems, void *instream)
{
	printf("begin freadfunc ... \n");
	int readSize = fread( buffer, size, nitems, (FILE *)instream );

	return readSize;
}

static void curl_ping_cfg(CURL *curl, struct curl_slist *head, char *auth)
{
	CURLcode res;

	/* set the opt */
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0 );
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_CAINFO, "/tmp/cacert.pem");

	/* set the head */
	head = curl_slist_append(head , DEL_HTTPHEAD_ACCEPT);
	head = curl_slist_append(head , "Path: /ping");
	head = curl_slist_append(head , auth);
	head = curl_slist_append(head , "Host: avs-alexa-na.amazon.com");
	res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, head);
	if (res != CURLE_OK){
		printf("%s: curl_easy_setopt failed: %s\n", __FUNCTION__, curl_easy_strerror(res));
	}

	/* set the url */
	curl_easy_setopt(curl, CURLOPT_URL, "https://avs-alexa-na.amazon.com/ping");

	/* set the GET */
//	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

}

static void curl_sync_state(CURL *curl, char *strJSONout,
								  struct curl_httppost *postFirst,
								  struct curl_httppost *postLast)
{
	char mid[50] = {0};
	cJSON *root, *context, *event, *header, *payload, *nonname, *tmpJson;

	uploadFile   = fopen( UPLOAD_FILE_NAME, "rb" );
	saveHeadFile = fopen( HEAD_FILE_NAME, "w+" );
	saveBodyFile = fopen( BODY_FILE_NAME,  "w+" );
	/* set the json */
	root = cJSON_CreateObject();

	/*        context        */
	cJSON_AddItemToObject(root, "context", context = cJSON_CreateArray());
	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "AudioPlayer");
	cJSON_AddStringToObject(header, "name", "PlaybackState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddStringToObject(payload, "token", "");
	cJSON_AddNumberToObject(payload, "offsetInMilliseconds", 0);
	cJSON_AddStringToObject(payload, "playerActivity", "IDLE");

	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "Alerts");
	cJSON_AddStringToObject(header, "name", "AlertsState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddItemToObject(payload, "allAlerts", tmpJson = cJSON_CreateArray());
	cJSON_AddItemToObject(payload, "activeAlerts", tmpJson = cJSON_CreateArray());

	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "Speaker");
	cJSON_AddStringToObject(header, "name", "VolumeState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddNumberToObject(payload, "volume", 25);
	cJSON_AddFalseToObject(payload , "muted");

	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "SpeechSynthesizer");
	cJSON_AddStringToObject(header, "name", "SpeechState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddStringToObject(payload, "token", "");
	cJSON_AddNumberToObject(payload, "offsetInMilliseconds", 0);
	cJSON_AddStringToObject(payload, "playerActivity", "FINISHED");

	cJSON_AddItemToObject(root, "event", event = cJSON_CreateObject());
	cJSON_AddItemToObject(event, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "System");
	cJSON_AddStringToObject(header, "name", "SynchronizeState");
	snprintf(mid, 50, "messageId-%d", data);
	data++;
	cJSON_AddStringToObject(header, "messageId", mid);
	cJSON_AddItemToObject(event, "payload", payload = cJSON_CreateObject());

	strJSONout = cJSON_Print(root); 
	cJSON_Delete(root);

	printf("%s\n%ld\n", strJSONout, strlen(strJSONout));

	/*        curl set the formadd      */
	/*         JSON          */
	curl_formadd(&postFirst, &postLast,
				CURLFORM_COPYNAME, "metadata", /* CURLFORM_PTRCONTENTS, pAlexaJSON,  */
				CURLFORM_COPYCONTENTS, strJSONout,
				CURLFORM_CONTENTTYPE, "application/json; charset=UTF-8",
				CURLFORM_END);

	curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFileFunc);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_data);  
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, saveHeadFile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);  
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, saveBodyFile);
	
	/* set http post */
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, postFirst);

}

static void curl_send_audio_content(CURL *curl, char *strJSONout,
								struct curl_httppost *postFirst,
								struct curl_httppost *postLast)
{
//	CURLcode res;
	
	cJSON *root, *context, *event, *header, *payload, *nonname, *tmpJson;
	char mid[50] = {0};
	char did[50] = {0};

	uploadFile   = fopen( UPLOAD_FILE_NAME, "rb" );
	saveHeadFile = fopen( HEAD_FILE_NAME, "w+" );
	saveBodyFile = fopen( BODY_FILE_NAME,  "w+" );
	
	fseek( uploadFile, 0, SEEK_END);
	long uploadFileSize = ftell( uploadFile );
	fseek( uploadFile, 0, SEEK_SET);
	printf("uploadFileSize = %ld\n", uploadFileSize);
	/* set the json */
	root=cJSON_CreateObject();
				//=======context===========//
	cJSON_AddItemToObject(root, "context", context = cJSON_CreateArray() );
	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "AudioPlayer");
	cJSON_AddStringToObject(header, "name", "PlaybackState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddStringToObject(payload, "token", "");
	cJSON_AddNumberToObject(payload, "offsetInMilliseconds", 0);
	cJSON_AddStringToObject(payload, "playerActivity", "IDLE");
	
	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "Alerts");
	cJSON_AddStringToObject(header, "name", "AlertsState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddItemToObject(payload, "allAlerts", tmpJson = cJSON_CreateArray());
	cJSON_AddItemToObject(payload, "activeAlerts", tmpJson = cJSON_CreateArray());
	
	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "Speaker");
	cJSON_AddStringToObject(header, "name", "VolumeState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddNumberToObject(payload, "volume", 25);
	cJSON_AddFalseToObject(payload , "muted");
	
	cJSON_AddItemToArray(context , nonname = cJSON_CreateObject());
	cJSON_AddItemToObject(nonname, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "SpeechSynthesizer");
	cJSON_AddStringToObject(header, "name", "SpeechState");
	cJSON_AddItemToObject(nonname, "payload", payload = cJSON_CreateObject());
	cJSON_AddStringToObject(payload, "token", "");
	cJSON_AddNumberToObject(payload, "offsetInMilliseconds", 0);
	cJSON_AddStringToObject(payload, "playerActivity", "FINISHED");

				//========Events===========//
	cJSON_AddItemToObject(root, "event", event = cJSON_CreateObject());
	cJSON_AddItemToObject(event, "header", header = cJSON_CreateObject());
	cJSON_AddStringToObject(header, "namespace", "SpeechRecognizer");
	cJSON_AddStringToObject(header, "name", "Recognize");
	snprintf(mid, 50, "messageId-%d", data);
	cJSON_AddStringToObject(header, "messageId", mid );//"messageId-123"
	snprintf(did, 50, "dialogRequestId-%d", data);
	data++;
	cJSON_AddStringToObject(header, "dialogRequestId", did );//"dialogRequestId-123"
	cJSON_AddItemToObject(event, "payload", payload = cJSON_CreateObject());
	cJSON_AddStringToObject(payload, "profile", "CLOSE_TALK");
	cJSON_AddStringToObject(payload, "format", "AUDIO_L16_RATE_16000_CHANNELS_1");

				//===========Tail===============//
	strJSONout = cJSON_Print(root); 
	cJSON_Delete(root);
	
	printf("%s\n%ld\n", strJSONout, strlen(strJSONout));

	/* formadd */

			//============josn================//
	curl_formadd(&postFirst, &postLast,
				CURLFORM_COPYNAME, "metadata", /* CURLFORM_PTRCONTENTS, pAlexaJSON,  */
				CURLFORM_COPYCONTENTS, strJSONout,
				CURLFORM_CONTENTTYPE, "application/json; charset=UTF-8",
				CURLFORM_END);

			//=============Audio=================//
	curl_formadd(&postFirst, &postLast,
				CURLFORM_COPYNAME, "audio",
				CURLFORM_STREAM, uploadFile,
				CURLFORM_CONTENTSLENGTH, uploadFileSize,
				CURLFORM_CONTENTTYPE, "application/octet-stream", //"audio/L16; rate=16000; channels=1",
				CURLFORM_END);

	curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFileFunc);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_data);  
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, saveHeadFile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);  
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, saveBodyFile);

	/* set http post */
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, postFirst);

}


static void curl_send_audio_cfg(CURL *curl, struct curl_slist *head, char *auth)
{
	/* set the opt */
	curl_easy_setopt(curl , CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl , CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0 );
	curl_easy_setopt(curl , CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl , CURLOPT_CAINFO, "/tmp/cacert.pem");

	/* set the head */
	head = curl_slist_append(head , DEL_HTTPHEAD_ACCEPT);
	head = curl_slist_append(head , DEL_HTTPHEAD_EXPECT);
	head = curl_slist_append(head , "Path: /v20160207/events");
	head = curl_slist_append(head , auth);
	head = curl_slist_append(head , "Content-type: multipart/form-data" );
	head = curl_slist_append(head , "Transfer-Encoding: chunked");
	head = curl_slist_append(head , "Host: avs-alexa-na.amazon.com");

	/* set the url */
	curl_easy_setopt(curl, CURLOPT_URL, "https://avs-alexa-na.amazon.com/v20160207/events");//v20160207/events
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, head);

}

static void curl_downchannel_cfg(CURL *curl, struct curl_slist *head, char *auth)
{
	CURLcode res;

	/* set the opt */
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_CAINFO, "/tmp/cacert.pem");

	/* set the head */
	head = curl_slist_append(head , DEL_HTTPHEAD_ACCEPT);
	head = curl_slist_append(head , "Path: /v20160207/directives");
	head = curl_slist_append(head , auth);
	head = curl_slist_append(head , "Host: avs-alexa-na.amazon.com");
	res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, head);
	if (res != CURLE_OK){
		printf("%s: curl_easy_setopt failed: %s\n", __FUNCTION__,curl_easy_strerror(res));
	}

	/* set the url */
	curl_easy_setopt(curl, CURLOPT_URL, "https://avs-alexa-na.amazon.com/v20160207/directives");

	/* send all data to this function  */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

	/* we pass our 'chunk' struct to the callback function */
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
}

#define HTTP2 "HTTP/2"
#define DATA_SIZE  100
static int is_rcv_ok(void)
{
	int ret = 0;
	FILE *hfd = NULL;
	char str[DATA_SIZE] = {0};

	hfd = fopen(HEAD_FILE_NAME , "rb");
	fgets(str, DATA_SIZE, hfd);
	fclose(hfd);
	sscanf(str, "HTTP/2 %d ", &ret);
	if (ret == 200){
		printf("%s ok...\n", __FUNCTION__);
		return 1;
	} else {
		printf("%s %d ...\n", __FUNCTION__, ret);
		return 0;
	}
}

int main(int argc, char **argv)
{
	int ret = 0;

	/* the curl variable */
	CURL *handles[HANDLECOUNT];
	CURLM *multi_handle;
	int i;
	int still_running; /* keep number of running handles */
	CURLMsg *msg; /* for picking up messages with the transfer status */
	int msgs_left; /* how many messages are left */
	
	char Authorization[1024] = "Authorization:Bearer ";
	
	/* for the event variable */
	char *strJSONout = NULL;
	struct curl_httppost *postFirst = NULL, *postLast = NULL;
	struct curl_slist *downHead = NULL;
	struct curl_slist *pingHead = NULL;
	struct curl_slist *eventHead = NULL;

	printf("1 curl_ver: %s\n", curl_version());
	/* start */
	strcat( Authorization, atoken );
	printf( "%s\nstarting ...\n", Authorization );


	/* initialize curl */
	/* init a multi stack */
	multi_handle = curl_multi_init();
	/* Allocate one CURL handle per transfer */
	for(i=0; i<HANDLECOUNT; i++){
		handles[i] = curl_easy_init();
	}

	/*******************set the eventhandle opt******************/
	curl_send_audio_cfg(handles[EVENT_HANDLE], eventHead, Authorization);

	/*******************set the downchannel opt*******************/
	chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
	chunk.size = 0;	  /* no data at this point */
	curl_downchannel_cfg(handles[DOWN_HANDLE], downHead, Authorization);
	curl_multi_add_handle(multi_handle, handles[DOWN_HANDLE]);

	/******************set the ping opt***************************/
	curl_ping_cfg(handles[PING_HANDLE], pingHead, Authorization);
	curl_multi_add_handle(multi_handle, handles[PING_HANDLE]);

	/********************run***************************************/
	curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
	/* We do HTTP/2 so let's stick to one connection per host */
	curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, 1L);
	/* we start some action by calling perform right away */
	curl_multi_perform(multi_handle, &still_running);

	do 
	{
		struct timeval timeout;
		int rc;/* select() return code */
		CURLMcode mc; /* curl_multi_fdset() return code */

		fd_set fdread;
		fd_set fdwrite;
		fd_set fdexcep;
		int maxfd = -1;

		long curl_timeo = -1;

		switch (netState)
		{
			case NET_STATE_PING:
			{

			}
				break;
			case NET_STATE_SEND_STATE:
			{
				curl_sync_state(handles[EVENT_HANDLE], strJSONout, postFirst, postLast);
				curl_multi_add_handle(multi_handle, handles[EVENT_HANDLE]);
				printf("start send the state ~~~~~~\n");
				netState = NET_STATE_IDLE;
			}
				break;
			case NET_STATE_SEND_EVENT:
			{

				curl_send_audio_content(handles[EVENT_HANDLE], strJSONout, postFirst, postLast);
				curl_multi_add_handle(multi_handle, handles[EVENT_HANDLE]);	
				printf("start event ~~~~~~~~~~~~~\n");
				netState = NET_STATE_IDLE;
				
			}
				break;
			case NET_STATE_IDLE:
			default:
			{

			}
				break;
		}

		/* set the multi_curl_handle */
		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);
		FD_ZERO(&fdexcep);

		/* set a suitable timeout to play around with */
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		curl_multi_timeout(multi_handle, &curl_timeo);
		if(curl_timeo >= 0) {
			timeout.tv_sec = curl_timeo / 1000;
			if(timeout.tv_sec > 1)
				timeout.tv_sec = 1;
			else
				timeout.tv_usec = (curl_timeo % 1000) * 1000;
		}

		/* get file descriptors from the transfers */
		mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

		if(mc != CURLM_OK) {
			fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
			break;
		}
		
		/* On success the value of maxfd is guaranteed to be >= -1. We call
		select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
		no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
		to sleep 100ms, which is the minimum suggested value in the
		curl_multi_fdset() doc. */
		if(maxfd == -1) {
			/* Portable sleep for platforms other than Windows. */
			struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
			rc = select(0, NULL, NULL, NULL, &wait);
		}
		else {
		/* Note that on some platforms 'timeout' may be modified by select().
		If you need access to the original value save a copy beforehand. */
			rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);
		}
		
		switch(rc) {
			case -1:
				/* select error */
				printf("~~~~~this is error~~~\n");
				break;
			case 0:
			default:
				/* timeout or readable/writable sockets */
				curl_multi_perform(multi_handle, &still_running);
				break;
			}

		/* See how the transfers went */
		while((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
			if(msg->msg == CURLMSG_DONE) {
				int idx, found = 0;

				/* Find out which handle this message is about */
				for(idx=0; idx<HANDLECOUNT; idx++) {
					found = (msg->easy_handle == handles[idx]);
					if(found)
						break;
				}

				switch(idx) {
					case DOWN_HANDLE:
						printf("Downstream completed with status %d\n", msg->data.result);
						curl_multi_add_handle(multi_handle, handles[DOWN_HANDLE]);
						break;
					case PING_HANDLE:
						printf("ping completed with status %d\n", msg->data.result);
						/* after send the ping, then remove the handle */
						curl_multi_remove_handle(multi_handle , handles[PING_HANDLE]);

						/* just change this  */
//						netState = NET_STATE_SEND_STATE;  // you can get the 204 first.
						netState = NET_STATE_SEND_EVENT;  // this is for 200 loop.
						break;
					case EVENT_HANDLE:
						printf("event completed with status %d\n", msg->data.result);
						curl_multi_remove_handle(multi_handle , handles[EVENT_HANDLE]);
						fclose(uploadFile);
						fclose(saveHeadFile);
						fclose(saveBodyFile);
						if (!is_rcv_ok()){
							printf("delete the mp3Ring...\n");
						}
						free(strJSONout);
						strJSONout = NULL;
						curl_formfree( postFirst);
						printf("-----finished count %d\n", count++);
						printf("-----------end-----------\n");
						sleep(5);
						netState = NET_STATE_SEND_EVENT; // go to send event again.
						break;
					default:
						break;
				}
			}
		}

	} while(1);

	/* clean up */
	curl_multi_cleanup(multi_handle);
	/* Free the CURL handles */
	for(i=0; i<HANDLECOUNT; i++)
		curl_easy_cleanup(handles[i]);

	/* free the custom headers */
	curl_slist_free_all(downHead);
	curl_slist_free_all(pingHead);
	curl_slist_free_all(eventHead);

	return ret;
}