#include <arpa/inet.h>
#include <signal.h> 
#include <stdio.h>
#include <wait.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <syslog.h>

#define MAX_TAM 1024*1024	//Maximo tamaño transferido
#define MAX_REQ 2048		//Maximo largo del request
#define NOT_FOUND "HTTP/1.0 404 Not Found\r\nContent-type: text/html\r\n\r\n<HTML><TITLE>Not Found</TITLE><BODY><P>El servidor NO encontro la pagina Solicitada.</BODY></HTML>\r\n"
#define NOT_IMP "HTTP/1.0 501 Method Not Implemented\r\n Content-type: text/html\r\n\r\n<HTML><TITLE>Method Not Implemented</TITLE><BODY><P>El metodo solicitado es NO ha sido implementado por el server.</BODY></HTML>\r\n"
#define BAD_REQ "HTTP/1.0 400 Bad Request\r\n Content-type: text/html\r\n\r\n<HTML><TITLE>Bad Request</TITLE><BODY><P>El metodo solicitado es Incorrecto.</BODY></HTML>\r\n"
#define Largo_NF 160
#define Largo_NI 200
#define Largo_BR 150

typedef struct sockaddr_in tSocket; 

//Metodos Usados:
FILE* defaultsHTML(FILE * archivo,char* filename);	//Intenta archivos por defectos cuando estos no etan especificados en el mensaje
void consultarHTML(char* filename,int socket);	//Recibe el nombre del archivo html y el socket y envia su contenido
void consultarMIME(char* filename,char* ext,int socket);	//Recibe el nombre del archivo MIME y envia su contenido
void ejecutarPHP(char* filename,int socket);	//Ejecuta el .php y envia su resultado
void consultarDefaults(int socket);				//Intenta consultar los archivos por defecto cuando no se especifican
void salirError(char* error);	//Realiza un EXIT_FAILURE pero primero muestra a error por Pantalla
char* getExt(char* filename);	//Recibe un filename y retorna su extension, si este no tubiera, retorna NULL
void setearSignals();			//
void imprimirAyuda();			//
int getSize(char* filename);
void atender(int socket);		//Atiendo el request de un Cliente en el socket TCP socket
int ipValida(char *ipAddress);

int main(int argc, char *argv[]) {
	char *server="127.0.0.1";	//Ip del que escucha el server
	int port=80;				//Puerto ligado al server 
	
	//Asumo orden en el pasaje de parametro
	//De no  tener orden solo encapsularia el if en un for con i del 1 argc-1
	// e indexaria a argv con i en vez de con argc-1
	// servidorHTTP [IP] [:Puerto] [-h]
	if(argc>1 &&strcmp("-h", argv[argc-1]) == 0)	//Si Necesita ayuda, imprimo la misma y salgo
		imprimirAyuda();

	setearSignals();		//Seteo los handlers de las distintas Signals
	openlog("servidorHTTP",LOG_PID,LOG_LOCAL0);	//abro el syslog
	
	int i=fork();
	if(i==0){					//Realizamos un fork para trabajar en Background
		//Si tiene Puerto
		if(argc>1)
		{
			if(strncmp(":",argv[1],1)==0) 	// tiene puerto pero no IP
				port=atoi((argv[1]+1));		//guardo el puerto
			else	//tiene ip
			{
				server=argv[1];							//guardo el ip
				if(argc>2 && strncmp(":",argv[2],1)==0)	//reviso si tiene puerto
					port=atoi((argv[2]+1));				//guardo puerto
			}
		}
	
		//Ya tengo los valores del ip y puerto asignados
		syslog(LOG_INFO,"El ip es: %s y el puerto %d\n",server,port);
		
		if(!ipValida(server))
			salirError("ERROR: Direccion IP NO VALIDA\nh");
		int clilen, pid;			//
		tSocket server_addr, client_addr;
		int socket_B,socketTcp;		//Socket_B es el socket de Bienvenida, SocketTcp el de comunicacion

		socket_B=socket(AF_INET, SOCK_STREAM, 0);	//Socket TCP
		if(socket_B==-1)
			salirError("No se pudo crear el socket TCP de Bienvenida\n");
	
		bzero((char *) &server_addr, sizeof(server_addr)); 	//like memset a 0  en todo ese espacio
		server_addr.sin_family = AF_INET;					//Tipo net
		server_addr.sin_addr.s_addr = inet_addr(server);  	//direccion del ip
		server_addr.sin_port = htons(port);					//Puerto para el socket	
	
		if (bind(socket_B, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)  //realizamos el bind del socket	{
			salirError("Error al ejecutar el bind\n");

		listen(socket_B,5); 			//cola de 5 pedidos
		clilen = sizeof(client_addr);
	
		while(1){
			//Creamos un socket TCP y se lo asignamos a la conexion
			socketTcp= accept(socket_B, (struct sockaddr *) &client_addr, &clilen);
			if(socket_B==-1)
				salirError("No se pudo crear el socket TCP\n");
			pid=fork();
			if(pid<0)
				salirError("No se pudo crear fork\n");
			
			if (pid == 0)  { 			//Hijo
				close(socket_B);		//cerramos la conexion con el socket padre
				atender(socketTcp); 	//Atencion del pedido
				exit(0);
			}
			else close(socketTcp);		//cerramos conexion con socket hijo
			waitpid(-1,NULL, WNOHANG);
		}
	}	
	return 0;
}


//Atiendo el request de un Cliente en el socket TCP socket
void atender(int socket)
{
	char linea[MAX_REQ],msj[MAX_REQ],filename[100]=".";
	char respNF[MAX_TAM]=NOT_FOUND,respNI[MAX_TAM]=NOT_IMP, respBR[MAX_TAM]=BAD_REQ;

	read(socket,msj,MAX_REQ);	 			//Leo un mensaje de  a lo sumo 2048 bytes del socket
	while(strstr(msj,"\r\n\r\n")==NULL){	//Mientras el mensaje no esta completo
		read(socket,linea,MAX_REQ);
		strcat(msj,linea);	
	}
	
	char* ext;
	char* pedido= strtok(msj," ");	 	//Consumo metodo
	char* file=strtok(NULL," ");		//Guardo el nombre del archivo(sin espacios)
	char* protocolo=strtok(NULL,"\n");	//Almaceno el Protocolo utilizado


	if((strcmp(pedido,"POST")==0)||(strcmp(pedido,"HEAD")==0)) //Reviso consultas correctas no implementadas
		send(socket,respNI,strlen(respNI),0);	//Envio NOT_IMPLEMENTED	
	else if(pedido==NULL ||file==NULL ||protocolo==NULL || (strcmp(pedido,"GET")!=0)|| strncmp(protocolo,"HTTP/1.",5)!=0|| strncmp(file,"/",1)!=0) //Si no cumple el formato (GET /archivo HTTP/x.y) devolvemos un bad request
		send(socket,respBR,strlen(respBR),0);	//Envio BAD_REQUEST
	else
	{
		//ya tengo guardado el filename
	
		strcat(filename,file);			//Agrego el '.' adelante de /archivo
		ext=getExt(filename);

		if(strcmp(ext,".html")==0 ||strcmp(ext,".htm")==0)
			consultarHTML(filename,socket);
		else if (strcmp(ext,".png")==0 ||strcmp(ext,".gif")==0 ||strcmp(ext,".jpg")==0)	
				consultarMIME(filename,ext,socket);
			else if(strncmp(ext,".php",4)==0)
					ejecutarPHP(filename,socket);
				else if(strcmp(ext,"./")==0)
						consultarDefaults(socket);
					else
						send(socket,respNF,strlen(respNF),0);	//Envio NOT_IMPLEMENTED
	}
}

//Recibe el nombre del archivo html y devuelve el mensaje a enviar
void consultarHTML(char* filename,int socket)
{
	int filesize=getSize(filename);
	char *txt=malloc(filesize+Largo_NF);
	char linea[filesize];
	FILE * archivo	= fopen(filename,"r");
	
	strcpy(txt,NOT_FOUND);	
	if(archivo!=NULL)			//Si no ecuentra archivo, 404 NOT_FOUND
	{
		//Ya tenemos el archivo abierto
		strcpy(txt, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");
		
		while(!feof(archivo))
		{
			fgets(linea,filesize,archivo);
			strcat(txt,linea);
		}
		fclose(archivo);
	}
	send(socket,txt,strlen(txt),0);
	free(txt);
}

//Recibe el nombre del archivo MIME
void consultarMIME(char* filename,char* ext, int socket)
{
	
	char txt[MAX_TAM]=NOT_FOUND,linea[4096];
	FILE * archivo	=fopen(filename,"rb");
	if(archivo==NULL)
		send(socket,txt,strlen(txt),0);
	else
	{
		if(strcmp(ext,".jpg")==0)strcpy(ext,".jpeg");
		//Ya tenemos el archivo abierto
		strcpy(txt,  "HTTP/1.0 200 OK\r\nMIME-Version: 1.0\r\nContent-Type:  image/");
		strcat(txt,ext+1);	//copio extension
		strcat(txt,"\r\n\r\n");
		send(socket,txt,strlen(txt),0); //Envio Encabezado

		int num;
		char LINEA[100];
		
		while(!feof(archivo))
		{
			num=fread(linea,1,100,archivo);
			send(socket,linea,num,0);
		}
	}
}

//Ejecuta el .php
void ejecutarPHP(char* filename,int socket)	
{	
	char txt[MAX_TAM]="HTTP/1.0 200 OK\r\n";
	char* name=strtok(filename,"?");
	char* param=strtok(NULL,"\n");
	char* parametro= malloc(strlen(param)+13);
	char* archivo= malloc(strlen(param)+13);
	char buffer[MAX_TAM];
	int *status,num,pipefd[2];

	strcpy(parametro,"QUERY_STRING=");
	if(param!=NULL)
		strcat(parametro,param);
	strcpy(archivo,"SCRIPT_FILENAME=");
	strcat(archivo,name);
	
	
	pipe(pipefd);
	if (fork() == 0){
	    close(pipefd[0]);    // close reading end in the child
	    dup2(pipefd[1], 1);  // send stdout to the pipe
	    dup2(pipefd[1], 2);  // send stderr to the pipe
	    close(pipefd[1]);    // this descriptor is no longer needed

		putenv(parametro);
		putenv(archivo);
		execlp("php-cgi","php-cgi",NULL);
	    
	}else{    // parent
	    close(pipefd[1]);  // close the write end of the pipe in the parent
		wait(NULL);
		read(pipefd[0], buffer, sizeof(buffer));
	}
	if(strncmp(buffer,"Status: 404",11)==0)
			strcpy(txt,NOT_FOUND);
	else
		strcat(txt,buffer);
	send(socket,txt,strlen(txt),0); //Envio Mensaje
}

//Intenta consultar los archivos por defecto cuando no se especifican
void consultarDefaults(int socket)
{
	char filename[100];
	FILE* archivo=defaultsHTML(archivo,filename);
	if(archivo!=NULL)
	{
		
		int filesize=getSize(filename);
		char *txt=malloc(filesize+Largo_NF);
		char *linea=malloc(filesize);
		
		strcpy(txt,  "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");
		while(!feof(archivo))
		{
			fgets(linea,filesize,archivo);
			strcat(txt,linea);
		}

		send(socket,txt,strlen(txt),0); //Envio Mensaje
		fclose(archivo);
		free(txt);
	}
	else
		ejecutarPHP("./index.php",socket);
}

//Intenta abrir archivos por defectos cuando estos no estan especificados en el mensaje
FILE* defaultsHTML(FILE * archivo,char* filename)
{
	strcpy(filename,"./index.html");
	archivo	= fopen(filename,"r");
	if(archivo==NULL){
		strcpy(filename,"./index.htm");
		archivo	= fopen(filename,"r");
	}
	return archivo;
}
//Recibe un filename y retorna su extension, si este no tubiera, retorna NULL
char* getExt(char* filename)
{
	char *aux,*ext=strstr(filename,".");
	aux=ext;
	while(ext!=NULL )
	{
		aux=ext;
	   	ext = strstr(ext+1, ".");
	}
	return aux;
}

void * terminar(int myint)
{
	syslog(LOG_INFO,"\nSe recepciono la señal, el programa finalizará.\n\n");
	closelog();
	exit(EXIT_SUCCESS);}
void setearSignals()
{
	signal(2,SIG_IGN);
	signal(3,SIG_IGN);
	signal(15,SIG_IGN);
	signal(SIGUSR2,SIG_IGN);
	signal(SIGUSR1, (void *)  terminar );
		
}
void imprimirAyuda()
{
	//codigo de ayuda
	printf("\n\n    Usage: servidorHTTP [IP] [:Puerto] [-h]\n");
	printf("* IP        Nro de IP donde el programa escucha los requerimientos, por defecto  es localhost\n");
	printf("* Puerto  	  Nro de puerto en el cual se deben decepcionar las peticiones. Por defecto debera ser el puerto 80\n");
	printf("* -h        Mensaje de Ayuda\n\n");
	exit(EXIT_SUCCESS);
}
void salirError(char* error)
{
	syslog(LOG_ERR,"Error: %s",error);
	exit(EXIT_FAILURE);
}

int getSize(char* filename)
{ 
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}
int ipValida(char *ipAddress)
{
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
    return result != 0;
}
