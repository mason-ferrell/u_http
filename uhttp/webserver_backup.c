#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>

#define BUFSIZE 1024

void error(char *msg) {
	printf("Error %s\n", msg);
	exit(-1);
}

//function to ensure entire response is written to client
void socket_write(int client_sock, char *message, int stream_size) {
	int bytes_written, unsent_bytes, new_bytes_written;
	
	bytes_written = write(client_sock, message, stream_size);
	if(bytes_written < 0) error("writing to socket");
	unsent_bytes = stream_size - bytes_written;
	
	while(unsent_bytes > 0) {
		new_bytes_written = write(client_sock, message+bytes_written, unsent_bytes);
		if(new_bytes_written < 0) error("writing to socket");
		
		bytes_written += new_bytes_written;
		unsent_bytes = stream_size - bytes_written;
	}
}

int parse_get_request(char*, char**, char**, char**, char**);
int get_content_length(FILE*);
void get_content_type(char *, char *);
void aggregate_response(FILE *, int, char *, int, char *, char *);
void send_error_message(int, int, char *);
void *http(void *);


int main(int argc, char *argv[]) {
	int sockfd, client_sock;
	int *sock_ptr;
	struct sockaddr_in server, client;
	int clientlen;
	
	if(argc != 2) {
		printf("Usage %s <port #>\n", argv[0]);
		exit(-1);
	}
	
	//set up pthread attributes
	pthread_attr_t attr;
    	pthread_attr_init(&attr);
	
	//create/open server socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0)
		error("opening socket");
		
	int optval = 1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int)) < 0)
		error("setting reuseaddr");
	
	//populate server info
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(atoi(argv[1]));
	
	//bind server, set listen queue to 3
	if(bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
		error("binding socket");

	listen(sockfd, 3);
	
	clientlen = sizeof(client);
	
	while(1) {
	
		client_sock = accept(sockfd, (struct sockaddr *)&client,(socklen_t *)&clientlen);
		if(client_sock < 0)
			error("accepting connection");
			
		/*struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
	
		if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO,&timeout,sizeof(timeout)) < 0)
			error("setting timeout on socket");*/
		
		pthread_t runner;
		
		//use sock_ptr to avoid race condition when starting thread, don't forget to free at end of http function
		sock_ptr = malloc(sizeof(int));
		*sock_ptr = client_sock;		
		
		pthread_create(&runner, &attr, http, (void *) sock_ptr);
		
	}
}

//this function parses get requests and puts each individual chunk into a string passed into the function by reference
//if there is an error in the request, return the appropriate error number
int parse_get_request(char *buffer, char **command, char **uri, char **version, char **ext){
	char *dot;
	
	if(buffer[BUFSIZE - 1] != 0) return 400;
	
	*command = strtok(buffer, " \t\n\r");
	if(*command==NULL) return 400;
	if(strcmp(*command, "HEAD")==0 || strcmp(*command, "POST")==0) return 405;
	if(strcmp(*command, "GET")!=0) return 400;
	
	*uri = strtok(NULL, " \t\n\r");
	if(*uri==NULL) return 400;
	
	*version = strtok(NULL, " \t\n\r");
	if(*version==NULL) return 400;
	if(strcmp(*version, "HTTP/1.0")!=0 && strcmp(*version, "HTTP/1.1")!=0) return 505;
	
	dot = strrchr(*uri, '.');
	if(dot==NULL) *ext = NULL;
	else *ext = dot + 1;
	
	return 0;
}

int get_content_length(FILE *fp) {
	int size;
	
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	return size;
}

//if content type not known, we will send file contents as plaintext
void get_content_type(char *content_type, char *ext) {
	if(ext==NULL) strcpy(content_type, "text/plain");	
	
	else if(strcmp(ext, "html") == 0 || strcmp(ext,"htm")==0) {
		strcpy(content_type, "text/html");
	}
	else if(strcmp(ext, "txt") == 0) strcpy(content_type, "text/plain");
	else if(strcmp(ext, "png") == 0 || strcmp(ext, "gif")==0 || strcmp(ext,"jpg")==0) {
		strcpy(content_type, "image/");
		strcat(content_type, ext);
	}
	else if(strcmp(ext, "css")==0) strcpy(content_type, "text/css");
	else if(strcmp(ext, "js")==0) strcpy(content_type, "application/javascript");
	else strcpy(content_type, "text/plain");
}

//this function bundles together the full response and sends it
void aggregate_response(FILE *fp, int client_sock, char *size_c, int size, char *content_type, char *version) {
	char buf[size + 250];
	char contents[size];
	int header_size;
	int stream_size;
	int bytes_read, unread_bytes, new_bytes_read;
	
	bytes_read = fread(contents, 1, size, fp);
	if(bytes_read < 0) error("reading file");
	unread_bytes = size - bytes_read;
	
	while(unread_bytes > 0) {
		new_bytes_read = fread(contents, 1, unread_bytes, fp);
		if(new_bytes_read < 0) error("writing to socket");
		
		bytes_read += new_bytes_read;
		unread_bytes = size - bytes_read;
	}
	
	strcpy(buf, version);
	strcat(buf, " 200 OK\r\n");
	strcat(buf, "Content-Type: ");
	strcat(buf, content_type);
	strcat(buf, "\r\n");
	strcat(buf, "Content-Length: ");
	strcat(buf, size_c);
	strcat(buf, "\r\n\r\n");
	printf("%s\n", buf);
	
	header_size = strlen(buf);
	stream_size = size + header_size;
	memcpy(buf+header_size, contents, size);
	
	socket_write(client_sock, buf, stream_size);
}

void send_error_message(int client_sock, int err, char *version) {
	char message[100];
	int stream_size;
	strcpy(message, version);

	if(err==400) strcat(message, " 400 Bad Request\r\n\r\n");
	else if(err==403) strcat(message, " 403 Forbidden\r\n\r\n");
	else if(err==404) strcat(message, " 404 Not Found\r\n\r\n");
	else if(err==405) strcat(message, " 405 Method Not Allowed\r\n\r\n");
	else if(err==505) strcat(message, " 505 HTTP Version Not Supported\r\n\r\n");
	else error("programmer messed up error codes, :(");
	
	stream_size = strlen(message);
	socket_write(client_sock, message, stream_size);
}

void* http(void *cs) {
	int client_sock = *(int *)cs;
	free(cs);
	
	char buffer[BUFSIZE]; //error check overflows to this
	char *command, *uri, *version, *ext, *uri_index;
	FILE *fp;
	int file_size;
	char size_c[sizeof(long long int) + 1];
	char content_type[32];
	int err, index_flag = 0;
	
	//sometimes an empty message is received, ignore these and erroneous calls
	bzero(buffer, BUFSIZE);
	if(recv(client_sock, buffer, BUFSIZE, 0) <= 0) {
		if(close(client_sock) < 0) error("closing socket");
		return NULL;
	}
	
	err = parse_get_request(buffer, &command, &uri, &version, &ext);
	
	if(err==0) {
		//uri_index holds value of uri we will open
		//we use this in case we access a directory
		//which should return the directory name + 'index.html'
		
		uri_index = (char *)malloc(strlen(uri) + 10);
		strcpy(uri_index, uri);
		if(uri[strlen(uri) - 1] == '/') {
			strcat(uri_index, "index.htm");
			index_flag = 1;
		}
		
		fp = fopen(uri_index+1, "r"); //add 1 so file locations are relative to pwd instead of /
		if(fp == NULL && index_flag==0) {
			if(errno==EACCES) err = 403;
			else err = 404;
		}
		
		//if index_flag is set and index.htm not found, search for index.html
		if(fp==NULL && index_flag == 1) {
			strcat(uri_index, "l");
			fp = fopen(uri_index+1, "r");
			if(fp==NULL) {
				if(errno==EACCES) err = 403;
				else err = 404;
			}
		}
		free(uri_index);
	}
	
	if(err!=0) {
		send_error_message(client_sock, err, version);
		if(close(client_sock) < 0) error("closing socket");
		return NULL;
	}
	
	file_size = get_content_length(fp);
	bzero(size_c, sizeof(long long int) + 1);
	sprintf(size_c, "%d", file_size);
	
	get_content_type(content_type, ext);

	//since ext is NULL, content type is automatically set as text/plain, but for requests to a directory
	//we must send index.html as text/html if it exists
	if(!index_flag) aggregate_response(fp, client_sock, size_c, file_size, content_type, version);
	else aggregate_response(fp, client_sock, size_c, file_size, "text/html", version);
	
	fclose(fp);
	if(close(client_sock) < 0) error("closing socket");
	
	return NULL;
}
