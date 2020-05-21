#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/md5.h>
#include <arpa/inet.h>
#include <libgen.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct _fileNode_{
	char* filename;
	char* filepath;
	long long filelength;
	struct _fileNode_* next;
	struct _fileNode_* prev;
}fileNode;

typedef struct _manifestNodes_{
	char* filepath;
	char* version;
	char* hash;
	struct _manifestNodes_* next;
	struct _manifestNodes_* prev;
}mNode;

// Client Setup Methods
int connectToServer(int socketfd, char** serverinfo);
int createSocket();
int setupConnection();
char** getConfig();

// Cleanup Methods
void freeFileNodes();
void freeMLL(mNode* head);
void freeMNode(mNode* node);

// Manifest Methods
int readManifest(char* projectName, int socketfd, int length, mNode** head);
void compareManifest(mNode* serverManifest, mNode* clientManifest, char* ProjectName, char* serverVersion);
void updateManifestVersion(char* projectName, int socketfd);
char* createManifestLine(char* version, char* filepath, char* hashcode, int local, int mode);
char* getManifestVersion(char* projectName);
void modifyManifest(char* projectName, char* filepath, int mode, char* replace);
mNode* insertMLL(mNode* newNode, mNode* head);
void printMLL(mNode* head);
int searchMLL(mNode* serverManifest, mNode* clientManifest, int updateFilefd);
char* generateManifestPath(char* projectName);

// Helper Methods
int directoryExist(char* directoryPath);
void createDirectories(fileNode* list);
int makeDirectory(char* directoryName);
void makeNestedDirectories(char* path);
void quickSortRecursive(mNode* startNode, mNode* endNode, int (*comparator)(void*, void*));
int quickSort( mNode* head, int (*comparator)(void*, void*));
void* partition(mNode* startNode, mNode* endNode, int (*comparator)(void*, void*));
int strcomp(void* string1, void* string2); 
char* generatePath(char* projectName, char* pathToAppend);
char* doubleStringSize(char* word, int newsize);
char* generateHashCode(char* filepath);
int reserveKeywords(char* word);

// File Sending Methods
void writeToSocketFromFile(int clientfd, char* fileName);
void sendLength(int socketfd, char* token);
void writeToFile(int fd, char* data);
void sendFile(int clientfd, char* filepath);
void sendFileBytes(char* filepath, int socketfd);

// File Recieving Methods
void metadataParser(int clientfd);
void readNbytes(int fd, int length, char* mode, char** placeholder);
void writeToFileFromSocket(int socketfd, fileNode* files);
void fileWriteFromSocket(int socketfd, int filelength, char* filepath);
int bufferFill(int fd, char* buffer, int bytesToRead);
char* readFileTillDelimiter(int fd);

// File Helper Methods
void insertLL(fileNode* node);
void printFiles();
long long calculateFileBytes(char* fileName);
int getLength(int socketfd);

// Commands Methods
void createProject(char* directoryName, int socketfd);
void update(char* ProjectName, int socketfd);
void upgradeProcess(char* projectName, int upgradefd, int socketfd);
void commit(char* ProjectName, int socketfd);
void pushCommit(char* projectName, int socketfd, char* commitfilepath);

// Commands Helper Methods
int getCommit(int commitfd, mNode** head);
void commitManifest(mNode* serverManifest, mNode* clientManifest, char* projectName, int socketfd);
int commitVersionAndHash(mNode* serverNode, mNode* clientNode, int commitfd);
int commitCompare(mNode* serverNode, mNode* clientNode, int commitfd);
int setupPush(char* projectName, char* commitfilepath);
char* generateClientid();
int checkVersionAndHash(mNode* serverNode, mNode* clientNode, char* projectName, int updateFilefd, int conflictFilefd);

fileNode* listOfFiles = NULL;
int numOfFiles = 0;
int version = -1;

int main(int argc, char** argv) {
    if(argc != 4 && argc != 3){
    	printf("Fatal Error: Incorrect number of arguments.\n");    
    }else{
    	if(argc == 3){
    		if(strlen(argv[1]) == 8 && strcmp(argv[1], "checkout") == 0){  //checkout
    			if(directoryExist(argv[2]) == 1){
    				printf("Fatal Error: Project already exist on client side\n");
    				return 0;
    			}
    			int socketfd  = setupConnection();
    			if(socketfd > 0){
    				writeToFile(socketfd, "checkout$");
					sendLength(socketfd, argv[2]);
					char* temp = NULL;
					readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
					if(strcmp(temp, "SUCCESS") == 0){
						//printf("Server successfully located the project, recieving the project\n");
						metadataParser(socketfd);
						createDirectories(listOfFiles);
						writeToFileFromSocket(socketfd, listOfFiles);
					}else if(strcmp(temp, "FAILURE") == 0){
						printf("Error: Server failed to locate the project\n");
					}else{
						printf("Error: Could not interpret the server's response\n");
					}
					free(temp);
					close(socketfd);
					printf("Client: Terminated Server Connection\n");
    			}else{
    			
    			}
    		}else if(strlen(argv[1]) == 6 && strcmp(argv[1], "update") == 0){ //UPDATE
    			if(directoryExist(argv[2]) == 0){
    				printf("Fatal Error: Project does not exist to update\n");
    				return 0;
    			}
    			int socketfd = setupConnection();
    			if(socketfd > 0){
    				writeToFile(socketfd, "update$");
    				sendLength(socketfd, argv[2]);
    				char* temp = NULL;
    				readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
    				if(strcmp(temp, "SUCCESS") == 0){
    					//printf("Server sucessfully located the project's manifest, recieving the server manifest\n");
    					metadataParser(socketfd);
    					update(argv[2], socketfd);
    				}else if(strcmp(temp , "FAILURE") == 0){
    					printf("Error: Server failed to locate the project's manifest\n");
    				}else{
    					printf("Error: Could not interpret the server's response\n");
    				}
    				free(temp);
    				close(socketfd);
    				printf("Client: Terminated Server Connection\n");
    			}else{
    			
    			}
    		}else if(strlen(argv[1]) == 7 && strcmp(argv[1], "upgrade") == 0){ //upgrade
    			if(directoryExist(argv[2]) == 0){
    				printf("Fatal Error: Project does not exist to upgrade\n");
    				return 0;
    			}
    			char* conflictFile = generatePath(argv[2], "/.Conflict"); //Check later

    			int conflictFd = open(conflictFile, O_RDONLY);
    			if(conflictFd != -1){
    				printf("Fatal Error: Please resolve Conflicts before upgrade\n");
    				close(conflictFd);
    				free(conflictFile);
    			}else{
    				free(conflictFile);
    				char* updateFile = generatePath(argv[2], "/.Update");
    				int updateFd = open(updateFile, O_RDONLY);
    				if(updateFd == -1){
    					printf("Fatal Error: Please call upgrade before update\n");
    					free(updateFile);
    					freeFileNodes();
    					return 0;
    				}else{
    					char buffer[5] = {'\0'};
    					int read = bufferFill(updateFd, buffer, 5);
    					close(updateFd);
    					if(read == 0){
    						printf("Up to Date\n");
    						freeFileNodes();
    						close(updateFd);
    						remove(updateFile);
    						free(updateFile);
    						char* removeManifestVersion = generatePath(argv[2], "/.serverManifestVersion"); //CHANGE TO HIDDEN LATER
    						int serverManifestfd = open(removeManifestVersion, O_RDONLY);
    						if(serverManifestfd != -1){
    							char* serverManifestVersion = readFileTillDelimiter(serverManifestfd);
		 						close(serverManifestfd);
								remove(removeManifestVersion);
								char* manifestVersionLine = malloc(sizeof(char) * (strlen(serverManifestVersion) + 2));
								memset(manifestVersionLine, '\0', sizeof(char) * (strlen(serverManifestVersion) + 2));
								memcpy(manifestVersionLine, serverManifestVersion, strlen(serverManifestVersion));
								strcat(manifestVersionLine, "\n");
								modifyManifest(argv[2], NULL, 2, manifestVersionLine);
								free(serverManifestVersion);
    						}
	 						free(removeManifestVersion);
    						return 0;
    					}
    					updateFd = open(updateFile, O_RDONLY);
    				}
    				
    				int socketfd = setupConnection();
    				if(socketfd > 0){
    					upgradeProcess(argv[2], updateFd, socketfd);
    					close(socketfd);
    					char* removeManifestVersion = generatePath(argv[2], "/.serverManifestVersion"); //CHANGE TO HIDDEN LATER
						remove(removeManifestVersion);
						free(removeManifestVersion);
						printf("Client: Terminated Server Connection\n");
    				}else{
    				
    				}
    				close(updateFd);
    				remove(updateFile);
    				free(updateFile);
    			}
    		}else if(strlen(argv[1]) == 6 && strcmp(argv[1], "commit") == 0){ //commit
    			if(directoryExist(argv[2]) == 0){
    				printf("Fatal Error: Project does not exist for commit\n");
    				return 0;
    			}
    			char* conflictFile = generatePath(argv[2], "/.Conflict"); //Check later
    			int conflictFd = open(conflictFile, O_RDONLY);
    			if(conflictFd != -1){
    				printf("Fatal Error: Please resolve Conflicts before commit\n");
    				close(conflictFd);
    				free(conflictFile);
    			}else{
    				int notEmpty = 0;
    				free(conflictFile);
    				char* updateFile = generatePath(argv[2], "/.Update");
    				int updateFd = open(updateFile, O_RDONLY);
    				if(updateFd != -1){
    					int read = 0;
    					char buffer[10] = {'\0'};
    					read = bufferFill(updateFd, buffer, sizeof(buffer)); 
    					if(read != 0){
    						printf("Fatal Error: Update File is not empty for commit\n");
    						notEmpty = 1;
    					}
    					close(updateFd);
    				}
    				if(notEmpty == 0){
    					int socketfd = setupConnection();
    					if(socketfd > 0){
		 					writeToFile(socketfd, "commit$");
							sendLength(socketfd, argv[2]);
		 					char* temp = NULL;
							readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
							if(strcmp(temp, "SUCCESS") == 0){
								//printf("Server successfully found the Project's Manifest\n");
								commit(argv[2], socketfd);
		 					}else if(strcmp(temp , "FAILURE") == 0){
		 						printf("Fatal Error: Server failed to find the project's manifest\n");
		 					}else{
		 						printf("Error: Could not interpret the server's response\n");
		 					}
		 					free(temp);
		 					close(socketfd);
		 					printf("Client: Terminated Server Connection\n");
		 				}
    				}
    				free(updateFile);		
    			}
    		}else if(strlen(argv[1]) == 4 && strcmp(argv[1], "push") == 0){ //push
    		   if(directoryExist(argv[2]) == 0){
    				printf("Fatal Error: Project does not exist for push\n");
    				return 0;
    			}
    			char* commitFilepath = generatePath(argv[2], "/.commit"); //Check later
    			int commitfd = open(commitFilepath, O_RDONLY);
    			if(commitfd == -1){
    				printf("Fatal Error: Commit file does not exist or cannot be opened for the project, please call commit before calling push\n");
    			}else{
    				if(setupPush(argv[2], commitFilepath) == 1){
    					int socketfd = setupConnection();
						if(socketfd > 0){
			 				close(commitfd);
			 				writeToFile(socketfd, "push$");
			 				sendLength(socketfd, argv[2]);
			 				char* serverResponse = NULL;
							readNbytes(socketfd, strlen("FAILURE"), NULL, &serverResponse);
							if(strcmp(serverResponse, "SUCCESS") == 0){
								//printf("Project was found on the server for push, proceeding with push checks\n");
								pushCommit(argv[2], socketfd, commitFilepath);
							}else if(strcmp(serverResponse, "FAILURE") == 0){
								printf("Fatal Error: Project does not exist on the server side\n");
							}else{
								printf("Error: Could not interpret the server's response %s\n", serverResponse);
							}
							free(serverResponse);
			 				remove(commitFilepath);
			 				close(socketfd);
			 				printf("Client: Terminated Server Connection\n");
			 			}
    				}
    			}
    			free(commitFilepath);
    		}else if(strlen(argv[1]) == 6 && strcmp(argv[1], "create") == 0){ //create
    			 if(directoryExist(argv[2]) == 1){
    				printf("Fatal Error: Project already exist on client side, please remove before retrying\n");
    				return 0;
    			}
    		
				int socketfd = setupConnection();
				if(socketfd > 0){
					writeToFile(socketfd, "create$");
					sendLength(socketfd, argv[2]);
					char* temp = NULL;
					readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
					if(strcmp(temp, "SUCCESS") == 0){
						//printf("Server sucessfully created the project, recieving the Manifest\n");
						metadataParser(socketfd);
						createProject(argv[2], socketfd);
					}else if(strcmp(temp , "FAILURE") == 0){
						printf("Error: Server failed to created the project\n");
					}else{
						printf("Error: Could not interpret the server's response\n");
					}
					free(temp);
					close(socketfd);
					printf("Client: Terminated Server Connection\n");
				}else{
				
				}
    		}else if(strlen(argv[1]) == 7 && strcmp(argv[1], "destroy") == 0){ //destroy
    			int socketfd = setupConnection();
				if(socketfd > 0){
					writeToFile(socketfd, "destroy$");
					sendLength(socketfd, argv[2]);
					char* temp = NULL;
					readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
					if(strcmp(temp, "SUCCESS") == 0){
						//printf("Server sucessfully destroyed the project\n");
					}else if(strcmp(temp , "FAILURE") == 0){
						printf("Fatal Error: Server failed to destroy the project, either project did not exist on server side or had no permissions to delete the project\n");
					}else{
						printf("Fatal Error: Could not interpret the server's response\n");
					}
					free(temp);
					close(socketfd);
					printf("Client: Terminated Server Connection\n");
				}else{
				
				}
    		}else if(strlen(argv[1]) == 14 && strcmp(argv[1], "currentversion") == 0){ //currentversion
				int socketfd = setupConnection();
				if(socketfd > 0){
					writeToFile(socketfd, "currentversion$");
					sendLength(socketfd, argv[2]);
 					char* temp = NULL;
					readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
					if(strcmp(temp, "SUCCESS") == 0){
						char* response = NULL;
						//printf("Server Succesfully located the project, recieving the files and versions\n");
						metadataParser(socketfd);
					}else if(strcmp(temp, "FAILURE") == 0){
						printf("Fatal Error: Server could not locate the project\n");
					}else{
						printf("Error: Could not interpret the server's response\n");
					}
					free(temp);
               close(socketfd);
               printf("Client: Terminated Server Connection\n");
				}else{
						
				}
				
    		}else if(strlen(argv[1]) == 7 && strcmp(argv[1], "history") == 0){ //history
    			int socketfd = setupConnection();
    			if(socketfd > 0){
    				writeToFile(socketfd, "history$");
    				sendLength(socketfd, argv[2]);
    				char* temp = NULL;
					readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
					if(strcmp(temp, "SUCCESS") == 0){
						//printf("Server succesfully located the project, recieving the history file\n");
		 				int historylength = getLength(socketfd);
		 				if(historylength == 0){
		 					printf("Warning: History file is empty, nothing to output\n");
		 				}else{
		 					readNbytes(socketfd, historylength, NULL, NULL);
		 				}
		 			}else if(strcmp(temp, "FAILURE") == 0){
		 				printf("Fatal Error: Server could not locate the project and or history file\n");
		 			}else{
		 				printf("Error: Could not interpret the server's response\n");
		 			}
    				free(temp);
    				close(socketfd);
    				printf("Client: Terminated Server Connection\n");
    			}else{
    			
    			}
    		}else{
    			printf("Fatal Error: Invalid operation or Improperly Formatted\n");
    		}
    		
    	}else{
    		if(strlen(argv[1]) == 9 && strcmp(argv[1], "configure") == 0){ //configure
    			int fd = open(".configure", O_WRONLY | O_TRUNC | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
    			if(fd == -1){
    				printf("Fatal Error: Could not create the .configure file\n");
    				return 0;
    			}
    			writeToFile(fd, argv[2]);
    			writeToFile(fd, " ");
    			writeToFile(fd, argv[3]);
    			writeToFile(fd, "\n");
    			close(fd);
    			//printf("Sucessfully created Configure file in current directory\n");
    			
    		}else if(strlen(argv[1]) == 3 && strcmp(argv[1], "add") == 0){ //Add

    			if(directoryExist(argv[2]) == 0){
    				printf("Fatal Error: Project does not exist to add the file\n");
    			}else{
    				if(reserveKeywords(argv[3]) == 1){
    					printf("Fatal Error: The file you wish to add is used as metadata, please rename the file before adding\n");
    				}else{
    					char* modifiedfilepath = NULL;
		 				modifiedfilepath = malloc(sizeof(char) * (strlen(argv[2]) + strlen(argv[3]) + 3));
		 				memset(modifiedfilepath, '\0', sizeof(char) * (strlen(argv[2]) + strlen(argv[3]) + 2));
		 				strcat(modifiedfilepath, argv[2]);
		 				strcat(modifiedfilepath, "/");
		 				strcat(modifiedfilepath, argv[3]);
		 				//printf("Modified filepath: %s\n", modifiedfilepath);
		 				char* hashcode = generateHashCode(modifiedfilepath);
		 				if(hashcode == NULL){
		 					printf("Fatal Error: File does not exist\n");
		 				}else{
			 				char* temp = createManifestLine("0", modifiedfilepath, hashcode, 1, 1); 
			 				modifyManifest(argv[2], modifiedfilepath, 1, temp);    
			 				free(hashcode);
			 				free(temp);
		 				}
		 				free(modifiedfilepath);
    				}  			
    			}
    			
    		}else if(strlen(argv[1]) == 6 && strcmp(argv[1], "remove") == 0){ //Remove
    			if(directoryExist(argv[2]) == 0){
    				printf("Fatal Error: Project does not exist to remove the file\n");
    			}else{
    				char* modifiedfilepath = NULL;
    				modifiedfilepath = malloc(sizeof(char) * (strlen(argv[2]) + strlen(argv[3]) + 3));
    				memset(modifiedfilepath, '\0', sizeof(char) * (strlen(argv[2]) + strlen(argv[3]) + 2));
    				strcat(modifiedfilepath, argv[2]);
    				strcat(modifiedfilepath, "/");
    				strcat(modifiedfilepath, argv[3]);
    				//printf("Modified filepath: %s\n", modifiedfilepath);
    				modifyManifest(argv[2], modifiedfilepath, 0, NULL);
    				free(modifiedfilepath);
    			}
    		}else if(strlen(argv[1]) == 8 && strcmp(argv[1], "rollback") == 0){ //Rollback
    			int socketfd = setupConnection();
    			if(socketfd > 0){
    				writeToFile(socketfd, "rollback$");
    				sendLength(socketfd, argv[2]);
    				sendLength(socketfd, argv[3]);
    				char* serverResponse = NULL;
					readNbytes(socketfd, strlen("FAILURE"), NULL, &serverResponse);
					if(strcmp(serverResponse, "SUCCESS") == 0){
						//printf("Server succesfully located the project, server is now checking the version number\n");
						free(serverResponse);
						serverResponse = NULL;
						readNbytes(socketfd, strlen("FAILURE"), NULL, &serverResponse);
						if(strcmp(serverResponse, "SUCCESS") == 0){
							char* temp = NULL;
							readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
							if(strcmp(temp, "SUCCESS") == 0){
								//printf("Server successfully rollbacked the project\n");
							}else{
								printf("Error: Could not interpret the server's response: %s\n", temp);
							}
							free(temp);
						}else if(strcmp(serverResponse, "FAILURE") == 0){
							printf("Fatal Error: Server could not locate the version\n");
						}else{
							printf("Error: Could not interpret the server's response: %s\n", serverResponse);
						}
						free(serverResponse);
					}else if(strcmp(serverResponse, "FAILURE") == 0){
						printf("Fatal Error: Server could not locate the project\n");
						free(serverResponse);
					}else{
						printf("Error: Could not interpret the server's response: %s\n", serverResponse);
						free(serverResponse);
					}
    				close(socketfd);
    				printf("Client: Terminated Server Connection\n");
    			}else{
    			
    			}
    		}else{
    			printf("Fatal Error: Invalid operation or Improperly Formatted\n");
    		}
    	}
    }
    freeFileNodes();
    return 0;
}

/*
Purpose: Parses the metadata
	$ is the delimiter
	Modes: 
		- output
		Format: output$dataBytes$data
		- sendFile
		Format: sendFile$numOfFiles$file1_name_bytes$file1name sizeoffile1$(...repeat for number of files...) file1contentfile2contentfile3content (...repeat for number of files...)
		*Note the space between filename and sizeoffile is added for readablility, it should not have a space and () indicates the previous pattern repeats for the number of files, everything should be one continous token
*/
void metadataParser(int clientfd){
	char buffer[2] = {'\0'}; 
	int defaultSize = 15;
	char* token = malloc(sizeof(char) * (defaultSize + 1));
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	int read = 0;
	int bufferPos = 0;
	int tokenpos = 0;
	int filesRead = 0;
	int readName = 0; 
	listOfFiles = NULL; 
	fileNode* file = NULL;
	numOfFiles = 0;
	char* mode = NULL;
	do{
		read = bufferFill(clientfd, buffer, 1);
		if(buffer[0] == '$'){
			if(mode == NULL){
				mode = token;
				//printf("%s\n", mode);
			}else if(strcmp(mode, "output") == 0){
				int responseLength = atoi(token);
				free(token);
				if(responseLength == 0){
					printf("Warning: There are no files in the Manifest\n");
				}else{
					readNbytes(clientfd, responseLength, NULL, NULL);
				}
				break;
			}else if(strcmp(mode, "sendFile") == 0){
				if(numOfFiles == 0){
					numOfFiles = atoi(token);
					
					file = (fileNode*) malloc(sizeof(fileNode) * 1);
					file->next = NULL;
					file->prev = NULL;
					
					free(token);
				}else if(readName == 0){
					//printf("The length of the file path is: %s\n", token);
					char* temp = NULL;
					int filepathlength = atoi(token);
					readNbytes(clientfd, filepathlength, NULL, &temp);
					file->filepath = temp;
					
					char* name = (char*) malloc(sizeof(char) * strlen(basename(temp)) + 1);
					memset(name, '\0', sizeof(char) * strlen(basename(temp)) + 1);
					memcpy(name, basename(temp), strlen(basename(temp)));
					file->filename = name;
					
					free(token);
					readName = 1;
				}else{
					file->filelength = (long long) atoi(token);
					insertLL(file);
					//printf("The file is %s with path of %s\n", file->filename, file->filepath);
					file = (fileNode*) malloc(sizeof(fileNode) * 1);
					file->next = NULL;
					file->prev = NULL;
					
					filesRead++;
					free(token);
					readName = 0;
				}
				if(numOfFiles == filesRead){
					free(file);
					fileNode* temp = listOfFiles;
					while(temp != NULL){
						//printf("File: %s\n", temp->filename);
						temp = temp->next;
					}
					break;
				}
			}else{
			
			}
			defaultSize = 10;
			tokenpos = 0;
			token = (char*) malloc(sizeof(char) * (defaultSize + 1));
			memset(token, '\0', sizeof(char) * (defaultSize + 1));
		}else{
			if(tokenpos >= defaultSize){
				defaultSize = defaultSize * 2;
				token = doubleStringSize(token, defaultSize);
			}
			token[tokenpos] = buffer[bufferPos];
			tokenpos++;
		}
	}while(buffer[0] != '\0' && read != 0);
		if(mode != NULL){
			free(mode);
		}
}

/*
	Automatically creates the Manifest file and calls writeToFileFromSocket to read the socket and store the data in the files
	Assumes metadataParser was called before to create the listOfFiles
*/
void createProject(char* directoryName, int socketfd){
	//printf("Attempting to create the directory\n");
	int success = makeDirectory(directoryName);
	if(success){
		char* manifest = generateManifestPath(directoryName);
		int fd = open(manifest, O_WRONLY | O_TRUNC | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
		close(fd);
		free(manifest);
		writeToFileFromSocket(socketfd, listOfFiles);
	}else{
		printf("Error: Directory Failed to Create\n");
	}
}

/*
	Update function: 
		- checks the Manifest versions and if not the same, it will create two linked list of that consist of Server Manifest and Client Manifest and quicksorts them by filepath and then has compareManifest do the line by line comparsion. If successfully, it will generate a file to keep the server's manifest version to compare in upgrade to ensure the server's manifest has not changed since
*/
void update(char* ProjectName, int socketfd){
	char* manifest = generateManifestPath(ProjectName);
	int manifestfd = open(manifest, O_RDONLY);
	free(manifest);
	if(manifestfd == -1){
		printf("Fatal Error: Manifest does not exist or does not have permissions to be opened\n");
		return;
	}
   char buffer[100] = {'\0'};
   int defaultSize = 15;
   int read = 0;
	int length = listOfFiles->filelength;
	int tokenpos = 0;
	
   char* serverVersion = malloc(sizeof(char) * (defaultSize + 1));
   memset(serverVersion, '\0', sizeof(char) * (defaultSize + 1));
   do{
		read = bufferFill(socketfd, buffer, 1);
		length = length - read;
		if(buffer[0] == '\n'){
			break;
		}
   	if(tokenpos >= defaultSize){
			defaultSize = defaultSize * 2;
			serverVersion = doubleStringSize(serverVersion, defaultSize);
		}
		serverVersion[tokenpos] = buffer[0];
		tokenpos++;
   }while(read != 0);
   
   int numOfSpaces = 0;
   int bufferPos = 0;
   defaultSize = 15;
	tokenpos = 0;
	
	mNode* clientHead = NULL;
	int sameVersion = 0;
	int clientVersion = readManifest(ProjectName, manifestfd, -1, &clientHead);
	if(clientVersion == atoi(serverVersion)){
		sameVersion = 1;
	}
	char* token = malloc(sizeof(char) * (defaultSize + 1));
   memset(token, '\0', sizeof(char) * (defaultSize + 1));
	mNode* curLine = malloc(sizeof(mNode) * 1);
	
	close(manifestfd);
	mNode* serverHead = NULL;
	if(sameVersion == 1){
		char* updateFile = generatePath(ProjectName, "/.Update"); //CHANGE TO ./Update for FINAL
		char* conflictFile =  generatePath(ProjectName, "/.Conflict"); //CHANGE TO ./Conflict for FINAL
		int updateFilefd = open(updateFile, O_RDONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		close(updateFilefd);
		remove(conflictFile);
		free(updateFile);
		free(conflictFile);
		free(curLine);
		free(token);
		free(serverVersion);
		freeMLL(clientHead);
		printf("Up To Date\n");
		return;
	}else{
		do{
			if(length == 0){
				break;
			}else{
				if(length > sizeof(buffer)){
					read = bufferFill(socketfd, buffer, sizeof(buffer));
				}else{
					memset(buffer, '\0', sizeof(buffer));
					read = bufferFill(socketfd, buffer, length);
				}
			}
			for(bufferPos = 0; bufferPos < read; ++bufferPos){
				if(buffer[bufferPos] == '\n'){ 
				  	curLine->hash = token;
				  	serverHead = insertMLL(curLine, serverHead);
				  	curLine = (mNode*) malloc(sizeof(mNode) * 1);
				  	defaultSize = 15;
		 			token = (char*) malloc(sizeof(char) * (defaultSize + 1));
					memset(token, '\0', sizeof(char) * (defaultSize + 1));
					tokenpos = 0;
				  	numOfSpaces = 0;
				}else if(buffer[bufferPos] == ' '){
				 	numOfSpaces++;
				 	if(numOfSpaces == 1){
				 		curLine->version = token;
				 	}else if(numOfSpaces == 2){
				 		curLine->filepath = token;
				 	}
				 	defaultSize = 15;
				 	token = (char*) malloc(sizeof(char) * (defaultSize + 1));
					memset(token, '\0', sizeof(char) * (defaultSize + 1));
					tokenpos = 0;
				}else{
				 	if(tokenpos >= defaultSize){
						defaultSize = defaultSize * 2;
						token = doubleStringSize(token, defaultSize);
					}
					token[tokenpos] = buffer[bufferPos];
					tokenpos++;
				}
			}
			length = length - read;
		}while (buffer[0] != '\0' && read != 0 && length != 0);
	}
	//printf("BEFORE: serverManifest:\n");
	//printMLL(serverHead);
	//printf("BEFORE: clientManifest:\n");
	//printMLL(clientHead);
	if(serverHead != NULL){
		quickSort(serverHead, strcomp);
	}
	if(clientHead != NULL){
		quickSort(clientHead, strcomp);
	}
	//printf("ServerHead:\n");
	//printMLL(serverHead);
	//printf("clientHead:\n");
	//printMLL(clientHead);
	free(curLine); 
	free(token);
	compareManifest(serverHead, clientHead, ProjectName, serverVersion);
	free(serverVersion);
	freeMLL(clientHead);
	freeMLL(serverHead);
}

/*
	upgrade performs the necessary checks and then updates the files and updates the client's manifest
*/
void upgradeProcess(char* projectName, int upgradefd, int socketfd){
	char* serverManifestfilepath = generatePath(projectName, "/.serverManifestVersion");
	int serverManifestfd = open(serverManifestfilepath, O_RDONLY);
	char* serverManifestVersion = NULL;
	free(serverManifestfilepath);
	if(serverManifestfd == -1){
		printf("Fatal Error: serverManifest Version could not be obtained to verify no changes have been since the last update\n");
	}else{
	 	serverManifestVersion = readFileTillDelimiter(serverManifestfd);
	 	close(serverManifestfd);
	}
	//printf("The client stored server version is: %s\n", serverManifestVersion);
	writeToFile(socketfd, "upgrade$");
	sendLength(socketfd, projectName);
	
	char* foundManifestVersion = NULL;
	readNbytes(socketfd, strlen("FAILURE"), NULL, &foundManifestVersion);
	if(strcmp(foundManifestVersion, "FAILURE") == 0){
		printf("Fatal Error: Please call update again, the server's manifest for this project has changed since you last updated\n");
		free(foundManifestVersion);
		return;
	}else{
		//printf("The server send back %s\n", foundManifestVersion);
		free(foundManifestVersion);
		int manifestversionlength = getLength(socketfd);
		char* currentServerManifestVersion = NULL;
		readNbytes(socketfd, manifestversionlength, NULL, &currentServerManifestVersion);
		char* temp = malloc(sizeof(manifestversionlength) * sizeof(char));
		memset(temp, '\0', manifestversionlength);
		memcpy(temp, currentServerManifestVersion, manifestversionlength - 1);
		free(currentServerManifestVersion);
		//printf("The server's current manifest version: %s\n", temp);
		if(strcmp(temp, serverManifestVersion) == 0){
			free(serverManifestVersion);
			//printf("Versions are correct, can continue with upgrade, no changes have been made to the server's manifest since last update\n");
			free(temp);
		}else{
			printf("Fatal Error: Please call update again, the server's manifest for this project has changed since you last updated\n");
			free(serverManifestVersion);
			writeToFile(socketfd, "-1$");
			foundManifestVersion = NULL;
			readNbytes(socketfd, strlen("FAILURE"), NULL, &foundManifestVersion);
			if(strcmp(foundManifestVersion, "SUCCESS") == 0){
				//printf("Server Succesfully completed Upgrade\n");
			}
			free(foundManifestVersion);
			free(temp);
			return;
		}
	}
	// Processing the upgrade file
	char buffer[100] = {'\0'};
	int bufferPos = 0;
	
	int defaultSize = 15;
	char* token = (char*) malloc(sizeof(char) * (defaultSize + 1));	
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	int tokenpos = 0;
	int mode = 0;
	int numOfSpace = 0;
	int read = 0;
	mNode* mhead = NULL;
	mNode* curFile = (mNode*) malloc(sizeof(mNode) * 1);
	curFile->next = NULL;
	curFile->prev = NULL;
	int numberOfFiles = 0;
	int empty = 1;
	//upgrade$BProjectName$ProjectNameNumOfFiles$BFile1$File1BFile2$
	//SERVER SENDS: sendFile....THEN SUCCESS OR FAILURE THEN MANIFESTVERSION
	do{
		read = bufferFill(upgradefd, buffer, sizeof(buffer));
		for(bufferPos = 0; bufferPos < read; ++bufferPos){
			if(buffer[bufferPos] == '\n'){
				if(mode == 1){
					modifyManifest(projectName, curFile->filepath, 0, NULL);
					free(curFile->filepath);
					free(curFile);
					free(token);
				}else{
					empty = 0;
					curFile->hash = token;
					mhead = insertMLL(curFile, mhead);
					numberOfFiles = numberOfFiles + 1;
				}
				curFile = (mNode*) malloc(sizeof(mNode) * 1);
				mode = 0;
				defaultSize = 25;
				tokenpos = 0;
				token = malloc(sizeof(char) * (defaultSize + 1));
				memset(token, '\0', sizeof(char) * (defaultSize + 1));
				numOfSpace = 0;
			}else if(buffer[bufferPos] == ' '){
				numOfSpace++;
				if(numOfSpace == 1){
					if(strcmp("D", token) == 0){
						mode = 1;
						free(token);
					}else if(strcmp("A", token) == 0){
						mode = 2;
						curFile->version = token;
					}else if(strcmp("M", token) == 0){
						mode = 3;
						curFile->version = token;
					}else{
						printf("Warning: The upgrade file is not properly formatted, %s", token);
						mode = -1;
					}
					defaultSize = 15;
					tokenpos = 0;
					token = malloc(sizeof(char) * (defaultSize + 1));
					memset(token, '\0', sizeof(char) * (defaultSize + 1));
				}else{
					
					curFile->filepath = token;
					defaultSize = 15;
					tokenpos = 0;
					token = malloc(sizeof(char) * (defaultSize + 1));
					memset(token, '\0', sizeof(char) * (defaultSize + 1));
					
				}
			}else{
				
				if(tokenpos >= defaultSize){
					defaultSize = defaultSize * 2;
					token = doubleStringSize(token, defaultSize);
				}
				token[tokenpos] = buffer[bufferPos];
				tokenpos++;
				
			}		
		}
	}while(buffer[0] != '\0' && read != 0);
	free(curFile);
	if(empty){
		writeToFile(socketfd, "0$");
		char* serverResponse_ = NULL;
		readNbytes(socketfd, strlen("FAILURE"), NULL, &serverResponse_);
		if(strcmp(serverResponse_, "SUCCESS") == 0){
			updateManifestVersion(projectName, socketfd);
		}else{
			printf("Fatal Error: Server has not successfully finished upgrade but client is done, Server's Response: %s\n", serverResponse_);
		}
		free(serverResponse_);
	}else{
		mNode* temp = mhead;
		char str[5] = {'\0'};
		sprintf(str, "%d", numberOfFiles);
		writeToFile(socketfd, str);
		writeToFile(socketfd, "$");
		while(temp != NULL){
			int tempfd = open(temp->filepath, O_RDONLY);
			if(tempfd == -1){
				char* filepathTEMP = strdup(temp->filepath);
				char* subdirectories = dirname(filepathTEMP);
				makeNestedDirectories(subdirectories);
				free(filepathTEMP);
			}else{
				close(tempfd);
				remove(temp->filepath);
			}
			//printf("Sending: %s\n", temp->filepath);
			sendLength(socketfd, temp->filepath);
			temp = temp->next;
		}
		//printf("Awaiting files:\n");
		metadataParser(socketfd);
		//printf("Recieving the Files, Changing the these files:\n");
		//printFiles();
		//printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		writeToFileFromSocket(socketfd, listOfFiles); 
		//printf("Updating Local Manifest to the Server's Manifest\n");
		char* foundServerManifest = NULL;
		readNbytes(socketfd, strlen("FAILURE"), NULL, &foundServerManifest);
		if(strcmp(foundServerManifest, "SUCCESS") == 0){
			int serverManifestLength = getLength(socketfd);
			char* manifest = generateManifestPath(projectName);
			fileWriteFromSocket(socketfd, serverManifestLength, manifest);
			free(manifest);
		}else if(strcmp(foundServerManifest, "FAILURE") == 0){
			printf("Fatal Error: Server could not find the Manifest\n");
		}
		free(foundServerManifest);
	}
	freeMLL(mhead);
	free(token);
	//printf("Done\n");
}

void fileWriteFromSocket(int socketfd, int filelength, char* filepath){
	char buffer[101] = {'\0'};
	int read = 0;
	int filefd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC,  S_IRUSR | S_IWUSR);
	if(filefd == -1){
		printf("Fatal Error: Could not write to File from the Socket because File did not exist or no permissions\n");
	}
	while(filelength != 0){
		if(filelength > (sizeof(buffer) - 1)){
			read = bufferFill(socketfd, buffer, (sizeof(buffer) - 1));
		}else{
			memset(buffer, '\0', (sizeof(buffer) - 1));
			read = bufferFill(socketfd, buffer, filelength);
		}
		//printf("The buffer has %s\n", buffer);
		writeToFile(filefd, buffer);
		filelength = filelength - read;
	}
	//printf("Finished reading %s\n", filepath);
	close(filefd);
}


int getLength(int socketfd){
	char buffer[2] = {'\0'};
	int defaultSize = 15;
	char* token = (char*) malloc(sizeof(char) * (defaultSize + 1));	
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	int tokenpos = 0;
	int bufferPos = 0;
	int length = -1;
	int read = 0;
	do{
		read = bufferFill(socketfd, buffer, 1);
		if(buffer[0] == '$'){
			length = atoi(token);
			free(token);
			break;
		}else{
			if(tokenpos >= defaultSize){
				defaultSize = defaultSize * 2;
				token = doubleStringSize(token, defaultSize);
			}
			token[tokenpos] = buffer[0];
			tokenpos++;
		}
	}while(read != 0 && buffer[0] != '\0');
	return length;
}


int readManifest(char* projectName, int socketfd, int length, mNode** head){
	char buffer[100] = {'\0'};
	int bufferPos = 0;
	int defaultSize = 25;
	char* token = (char*) malloc(sizeof(char) * (defaultSize + 1));	
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	int tokenpos = 0;
	int read = 0;
	int version = -1;
	int numOfSpaces = 0;
	int foundManifestVersion = 0;
	mNode* curEntry = NULL;
	do{
		if(length != -1){
			if(sizeof(buffer) > length){
				memset(buffer, '\0', sizeof(buffer));
				read = bufferFill(socketfd, buffer, length);
			}else{
				read = bufferFill(socketfd, buffer, sizeof(buffer));
			}
			length = length - read;
		}else{
			read = bufferFill(socketfd, buffer, sizeof(buffer));
		}
		for(bufferPos = 0; bufferPos < read; ++bufferPos){
			if(buffer[bufferPos] == '\n'){
				if(foundManifestVersion){
					curEntry->hash = token;
					(*head) = insertMLL(curEntry, (*head));
				}else{
					foundManifestVersion = 1;
					version = atoi(token);
					free(token);
				}
				curEntry = (mNode*) malloc(sizeof(mNode) * 1);
				curEntry->filepath = NULL;
				curEntry->hash = NULL;
				curEntry->version = NULL;
				tokenpos = 0;
				defaultSize = 25;
				numOfSpaces = 0;
				token = (char*) malloc(sizeof(char) * (defaultSize + 1));	
				memset(token, '\0', sizeof(char) * (defaultSize + 1));
			}else if(buffer[bufferPos] == ' '){
				numOfSpaces++;
		    	if(numOfSpaces == 1){
		    		curEntry->version = token;
		    	}else if(numOfSpaces == 2){
		    		curEntry->filepath = token;
		    	}
		    	defaultSize = 25;
		    	token = malloc(sizeof(char) * (defaultSize + 1));
   			memset(token, '\0', sizeof(char) * (defaultSize + 1));
   			tokenpos = 0;
			}else{
				if(tokenpos >= defaultSize){
					defaultSize = defaultSize * 2;
					token = doubleStringSize(token, defaultSize);
				}
				token[tokenpos] = buffer[bufferPos];
				tokenpos++;
			}
		}
	}while(buffer[0] != '\0' && read != 0);
	free(token);
	free(curEntry);
	return version;
}

void commit(char* projectName, int socketfd){
	metadataParser(socketfd);
	mNode* serverManifest = NULL;
	int serverManifestLength = listOfFiles->filelength;
	int serverManifestVersion = readManifest(projectName, socketfd, serverManifestLength, &serverManifest);
	mNode* clientManifest = NULL;
	char* manifest = generatePath(projectName, "/.Manifest"); //CHANGE MANIFEST
	int manifestfd = open(manifest, O_RDONLY);
	if(manifestfd == -1){
		writeToFile(socketfd, "FAILURE");
		printf("Error: Project's Manifest does not exist for commit\n");
		free(manifest);
		freeMLL(serverManifest);
		return;
	}
	int clientManifestVersion = readManifest(projectName, manifestfd, -1, &clientManifest);
	close(manifestfd);
	if(clientManifestVersion == serverManifestVersion){
		writeToFile(socketfd, "SUCCESS");
		//printf("BEFORE serverManifest:\n"); //DEBUGGING
		//printMLL(serverManifest);
		//printf("BEFORE clientManifest:\n");
		//printMLL(clientManifest);
		
		if(serverManifest != NULL){
			quickSort(serverManifest, strcomp);
		}
		if(clientManifest != NULL){
			quickSort(clientManifest, strcomp);
		}
		//printf("serverManifest:\n"); //DEBUGGING
		//printMLL(serverManifest);
		//printf("clientManifest:\n");
		//printMLL(clientManifest);
		
		commitManifest(serverManifest, clientManifest, projectName, socketfd);
	}else{
		writeToFile(socketfd, "FAILURE");
		printf("Fatal Error: Server and Client Manifest Versions are not the same, please update before calling commit\n");
	}
	free(manifest);
	freeMLL(clientManifest);
	freeMLL(serverManifest);
}

void freeMLL(mNode* head){
	mNode* temp = head;
	while(temp != NULL){
		mNode* tobefreed = temp;
		temp = temp->next;
		freeMNode(tobefreed);
	}
}

void freeMNode(mNode* node){
	free(node->filepath);
	free(node->version);
	free(node->hash);
	free(node);
}

void commitManifest(mNode* serverManifest, mNode* clientManifest, char* projectName, int socketfd){
	int outdated = 0;
	mNode* sEntry = serverManifest;
	mNode* cEntry = clientManifest;
	if(sEntry == NULL && cEntry == NULL){
		printf("Up To Date, no changes made\n");
		writeToFile(socketfd, "UPDATED");
		return;
	} 
	char* commitfilepath = generatePath(projectName, "/.commit"); //CHANGE LATER 
	int commitfd = open(commitfilepath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if(commitfd == -1){
		printf("Fatal Error: The .Commit file could be not created\n");
		free(commitfilepath);
		writeToFile(socketfd, "FAILURE");
		return;
	}else{
		while(sEntry != NULL && cEntry != NULL){
			if(strcmp(sEntry->filepath, cEntry->filepath) == 0){
				int success = commitVersionAndHash(sEntry, cEntry, commitfd);
				if(success == -1){
					printf("Error: File did not exist or had no permissions to compute livehash, failure\n");
				}else if(success == 2){
					outdated = 1;
					break;
				}else if(success == -2){
					printf("Error: Please Check Manifest, the version numbers somehow were messed up or the hash\n");
				}
				sEntry = sEntry->next;
				cEntry = cEntry->next;
			}else{
				int success = commitCompare(sEntry, cEntry, commitfd);
				if(success){
					sEntry = sEntry->next;
				}else{
					cEntry = cEntry->next;
				}
			}
		}
		if(outdated != 1){
			if(sEntry != NULL){
				while(sEntry != NULL){
					printf("D %s\n",sEntry->filepath);
					writeToFile(commitfd, "D ");
					writeToFile(commitfd, sEntry->filepath);
					writeToFile(commitfd, " ");
					writeToFile(commitfd, sEntry->hash);
					writeToFile(commitfd, "\n");
					sEntry = sEntry->next;
				}
			}else{
				while(cEntry != NULL){
					char* temp = generatePath("", cEntry->filepath);
					char* livehash = generateHashCode(temp);
					if(livehash == NULL){
						free(temp);
						writeToFile(socketfd, "FAILURE");
						break;
					}
					printf("A %s\n", cEntry->filepath);
					writeToFile(commitfd, "A ");
					writeToFile(commitfd, cEntry->filepath);
					writeToFile(commitfd, " ");
					writeToFile(commitfd, livehash);
					writeToFile(commitfd, "\n");
					free(temp);
					free(livehash);
					cEntry = cEntry->next;
				}
			}
		}
	}
	
	close(commitfd);
	char tempBuffer[10] = {'\0'};
	commitfd = open(commitfilepath, O_RDONLY);
	int notEmpty = bufferFill(commitfd, tempBuffer, sizeof(tempBuffer));
	close(commitfd);
	if(notEmpty == 0){
		writeToFile(socketfd, "UPDATED");
		printf("Up to Date, no changes made\n");
		remove(commitfilepath);
		free(commitfilepath);
		return;
	} 
	
	if(outdated){
		printf("Fatal Error: Outdated Files detected, please update your repository before calling commit\n");
		remove(commitfilepath);
		writeToFile(socketfd, "FAILURE");
	}else{
		char* clientidpath = generatePath("", ".clientid.txt"); //CHANGE LATER to hidden 
		writeToFile(socketfd, "SUCCESS");
		int clientidfd = open(clientidpath, O_RDONLY);
		if(clientidfd != -1){
		 	// Format: clientidtoken$
			//printf("Clientid already exist, reading\n");
		 	char* clientidtoken = readFileTillDelimiter(clientidfd);
		 	//printf("Clientid read: %s\n", clientidtoken);
			//Sending commit as: numOfBytes$clientidTokennumOfBytes$projectNamenumOfBytes$.commit
			sendLength(socketfd, clientidtoken);
			free(clientidtoken);
			sendLength(socketfd, projectName);
			sendFileBytes(commitfilepath, socketfd);
			char* serverResponse = NULL;
			readNbytes(socketfd, strlen("FAILURE"), NULL, &serverResponse);
			if(strcmp(serverResponse, "SUCCESS") == 0){
				//printf("Server recieved the commit and succesfully stored it\n");
			}else{
				printf("Error: The server send: %s, not sure what this means\n", serverResponse);
			}
			free(serverResponse);
		}else{
			clientidfd = open(clientidpath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
			//printf("(DEBUGGING): Clientid did not exist, created one\n");
			
			char* clientidcomplete = generateClientid();
			sendLength(socketfd, clientidcomplete);
			sendLength(socketfd, projectName);
			sendFileBytes(commitfilepath, socketfd);
			writeToFile(clientidfd, clientidcomplete);
			writeToFile(clientidfd, "$");

			char* serverResponse = NULL;
			readNbytes(socketfd, strlen("FAILURE"), NULL, &serverResponse);
			if(strcmp(serverResponse, "SUCCESS") == 0){
				//printf("Server recieved the commit and succesfully stored it\n");
			}else{
				printf("Error: The server send: %s, not sure what this means\n", serverResponse);
			}
			free(serverResponse);
		}
		char* commitVersionfilepath = generatePath(projectName, "/.commitManifestVersion"); //CHANGE TO HIDDEN LATER
		//printf("The commit version filepath is: %s\n", commitVersionfilepath);
		int commitVersionfilefd = open(commitVersionfilepath, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR);
		free(commitVersionfilepath);
		
		char* manifestfilepath = generateManifestPath(projectName);
		char* clientManifestVersion = generateHashCode(manifestfilepath);
		//printf("%s\n", clientManifestVersion);
		free(manifestfilepath);
		writeToFile(commitVersionfilefd, clientManifestVersion);
		free(clientManifestVersion);
		writeToFile(commitVersionfilefd, "$");
		close(commitVersionfilefd);
		close(clientidfd);
		free(clientidpath);
	}
	free(commitfilepath); 
}

/*
	Reads the given file descriptor (usually for sockets), one byte at time and stores it into a char* and returns once it hits '$'
*/
char* readFileTillDelimiter(int fd){
	int defaultSize = 25;
 	char* token = malloc(sizeof(char) * (defaultSize + 1));
 	memset(token, '\0', sizeof(char) * (defaultSize + 1));
 	int read = 0;
 	int tokenpos = 0;
 	char buffer[101] = {'\0'};
 	int bufferPos = 0;
 	do{
 		read = bufferFill(fd, buffer, 100);
		for(bufferPos = 0; bufferPos < read; ++bufferPos){
			if(buffer[bufferPos] == '$'){
				read = 0;
				break;
			}else{
				if(tokenpos >= defaultSize){
					defaultSize = defaultSize * 2;
					token = doubleStringSize(token, defaultSize);
				}
				token[tokenpos] = buffer[bufferPos];
				tokenpos++;
			}
		}
 	}while(read != 0);
	return token;
}

/*
	Generates the clientid for commit 
	- uses the ip of the client and a random number from 0 to 65535
*/
char* generateClientid(){
	char ipbuffer[256] = {'\0'};
	gethostname(ipbuffer, sizeof(ipbuffer));
	struct hostent* localip = gethostbyname(ipbuffer);
	char* ipaddress = inet_ntoa( *((struct in_addr*) localip->h_addr_list[0]) );
	//printf("Local IP: %s\n", ipaddress);
	srand(time(0));
	int port = rand() % 65535;
	char* portholder = malloc(sizeof(char) * 7);
	memset(portholder, '\0', sizeof(char) * 7);
	sprintf(portholder, "%d", port);
	char* clientidcomplete = (char*) malloc(sizeof(char) * (strlen(ipaddress) + strlen(portholder)) + 2);
	memset(clientidcomplete, '\0', sizeof(char) * (strlen(ipaddress) + strlen(portholder)) + 1);
	strcat(clientidcomplete, ipaddress);
	strcat(clientidcomplete, portholder);
	free(portholder);
	return clientidcomplete;
}

/*
	Given the filepath and the socketfd, it sends the bytes of the file and then sends the file's contents
*/
void sendFileBytes(char* filepath, int socketfd){
	long long fileBytes = calculateFileBytes(filepath);
	char filebytes[256] = {'\0'};
	sprintf(filebytes, "%lld", fileBytes);
	writeToFile(socketfd, filebytes);
	writeToFile(socketfd, "$");
	sendFile(socketfd, filepath);
}

/*
	Mode: -2, server and client file entry's either version number or hash are different and the version number of server < version number of client
	Mode: -1 if file does not exist
	Mode: 0 if the file are not changed
	Mode: 1 if files are changed
	Mode: 2 Client has outdated file
*/
int commitVersionAndHash(mNode* serverNode, mNode* clientNode, int commitfd){
	if(strcmp(serverNode->hash, clientNode->hash) == 0){
		char* temp = generatePath("", clientNode->filepath);
		char* livehash = generateHashCode(temp);
		if(livehash == NULL){
			free(temp);
			return -1;
		}
		if(strcmp(livehash, clientNode->hash) == 0){
			free(temp);
			free(livehash);
			return 0;
		}else{
			printf("M %s\n", clientNode->filepath);
			writeToFile(commitfd, "M ");
			writeToFile(commitfd, clientNode->filepath);
			writeToFile(commitfd, " ");
			writeToFile(commitfd, livehash);
			writeToFile(commitfd, "\n");
			free(livehash);
			free(temp);
			return 1;
		}
	}else{
		int clientVersion = 0;
		if(clientNode->version[0] == 'l'){
			//printf("Modified Version: %s \n", clientNode->version);
			char* number = (clientNode->version + 1);
			clientVersion = atoi(number);
			//printf("Number version: %d\n", clientVersion);
		}else{
			clientVersion = atoi(clientNode->version);
		}
		int serverVersion = atoi(serverNode->version);
		if(serverVersion >= clientVersion){
			return 2;
		}else{
			printf("Error: Different hashes and Client's version > Server's version, please call update and upgrade to retrieve the latest version\n");
			return -2;
		}
	}
}

/*
	Returns: -1 if the file does not exist for Add
	Returns: 0 if Server does not have the file
	Returns: 1 if Client does not have the file
*/
int commitCompare(mNode* serverNode, mNode* clientNode, int commitfd){
	if(strcmp(serverNode->filepath, clientNode->filepath) > 0){
		char* temp = generatePath("", clientNode->filepath);
		char* livehash = generateHashCode(temp);
		if(livehash == NULL){
			free(temp);
			return -1;
		}
		printf("A %s\n", clientNode->filepath);
		writeToFile(commitfd, "A ");
		writeToFile(commitfd, clientNode->filepath);
		writeToFile(commitfd, " ");
		writeToFile(commitfd, livehash);
		writeToFile(commitfd, "\n");
		free(temp);
		free(livehash);
		return 0;
	}else{
		printf("D %s\n",serverNode->filepath);
		writeToFile(commitfd, "D ");
		writeToFile(commitfd, serverNode->filepath);
		writeToFile(commitfd, " ");
		writeToFile(commitfd, serverNode->hash);
		writeToFile(commitfd, "\n");
		return 1;
	}
}

void updateManifestVersion(char* projectName, int socketfd){
	char* temp = NULL;
	readNbytes(socketfd, strlen("FAILURE"), NULL, &temp);
	if(strcmp(temp, "SUCCESS") == 0){
		free(temp);
		char buffer[2] = {'\0'};
		int bufferPos = 0;
		int defaultSize = 25;
		char* token = (char*) malloc(sizeof(char) * (defaultSize + 1));	
		memset(token, '\0', sizeof(char) * (defaultSize + 1));
		char* serverManifestVersion = NULL;
		int tokenpos = 0;
		int read = 0;
		do{
			read = bufferFill(socketfd, buffer, 1);
			if(buffer[bufferPos] == '$'){
				int tokenlength = atoi(token);
				free(token);
				readNbytes(socketfd, tokenlength, NULL, &serverManifestVersion);
				break;
			}else{
				if(tokenpos >= defaultSize){
					defaultSize = defaultSize * 2;
					token = doubleStringSize(token, defaultSize);
				}
				token[tokenpos] = buffer[bufferPos];
				tokenpos++;
			}
		}while(buffer[0] != 0 && read != 0);
		modifyManifest(projectName, NULL, 2, serverManifestVersion);
		free(serverManifestVersion);
	}else{
		free(temp);
		printf("Fatal Error: Server could not find the Manifest for this project\n");
	}
}

void compareManifest(mNode* serverManifest, mNode* clientManifest, char* ProjectName, char* serverVersion){
	char* updateFile = generatePath(ProjectName, "/.Update"); //CHANGE TO ./Update for FINAL
	char* conflictFile =  generatePath(ProjectName, "/.Conflict"); //CHANGE TO ./Conflict for FINAL
	//printf("Updatepath: %s\n", updateFile);
	//printf("Conflictpath: %s\n", conflictFile);
	mNode* serverCurNode = serverManifest;
	mNode* clientCurNode = clientManifest;
	int updateFilefd = open(updateFile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if(updateFilefd == -1){
		printf("Fatal Error: Could not create a new update file for upgrade\n");
		return;
	}
	if(serverCurNode == NULL && clientCurNode == NULL){
		printf("Warning: There were no entries in either Server's Manifest or Client's Manifest, succesfully finish with update\n");
		return;
	}
	int conflictFilefd = open(conflictFile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if(conflictFilefd == -1){
		printf("Fatal Error: Could not create a new conflict file for update\n");
		return;
	}
	int conflict = 0;
	int deletedFile = 0;
	while(serverCurNode != NULL && clientCurNode != NULL){
		if(strcmp(serverCurNode->filepath, clientCurNode->filepath) == 0){
			int success = checkVersionAndHash(serverCurNode, clientCurNode, ProjectName, updateFilefd, conflictFilefd);
			if(success == -1){
				printf("Error: File did not exist or had no permissions to compute livehash, continuing to show updates but will not output the update file, please fix the file before calling update\n");
				deletedFile = 1;
				conflict = 1;
			}else if(success == 2){
				conflict = 1;
			}
			serverCurNode = serverCurNode->next;
			clientCurNode = clientCurNode->next;
		}else{
			int success = searchMLL(serverCurNode, clientCurNode, updateFilefd);
			if(success){
				serverCurNode = serverCurNode->next;
			}else{
				clientCurNode = clientCurNode->next;
			}
		}
	}
	
	if(serverCurNode != NULL){
		while(serverCurNode != NULL){
			printf("A %s\n",serverCurNode->filepath);
			writeToFile(updateFilefd, "A ");
			writeToFile(updateFilefd, serverCurNode->filepath);
			writeToFile(updateFilefd, " ");
			writeToFile(updateFilefd, serverCurNode->hash);
			writeToFile(updateFilefd, "\n");
			serverCurNode = serverCurNode->next;
		}
	}else{
		while(clientCurNode != NULL){
			printf("D %s\n", clientCurNode->filepath);
			writeToFile(updateFilefd, "D ");
			writeToFile(updateFilefd, clientCurNode->filepath);
			writeToFile(updateFilefd, " ");
			writeToFile(updateFilefd, clientCurNode->hash);
			writeToFile(updateFilefd, "\n");
			clientCurNode = clientCurNode->next;
		}
	}
	close(conflictFilefd);
	close(updateFilefd);
	if(conflict == 0){
		remove(conflictFile);
		//printf("Storing Server's Manifest Version for upgrade\n");
		char* serverManifestVersionfilepath = generatePath(ProjectName, "/.serverManifestVersion");
		int serverManifestfilefd = open(serverManifestVersionfilepath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		free(serverManifestVersionfilepath);
		writeToFile(serverManifestfilefd, serverVersion);
		writeToFile(serverManifestfilefd, "$");
		close(serverManifestfilefd);
	}else{
		if(deletedFile == 0){
			printf("Warning: Project could not be updated: All the conflicts must be resolved before the project is updated\n");
		}else{
			remove(conflictFile);
		}
		remove(updateFile);
	}
	free(conflictFile);
	free(updateFile);
}

int searchMLL(mNode* serverManifest, mNode* clientManifest, int updatefd){
	mNode* serverCurNode = serverManifest;
	mNode* clientCurNode = clientManifest;
	if(strcmp(serverCurNode->filepath, clientCurNode->filepath) > 0){
		printf("D %s\n", clientCurNode->filepath);
		writeToFile(updatefd, "D ");
		writeToFile(updatefd, clientCurNode->filepath);
		writeToFile(updatefd, " ");
		writeToFile(updatefd, clientCurNode->hash);
		writeToFile(updatefd, "\n");
		return 0;
	}else{
		printf("A %s\n",serverCurNode->filepath);
		writeToFile(updatefd, "A ");
		writeToFile(updatefd, serverCurNode->filepath);
		writeToFile(updatefd, " ");
		writeToFile(updatefd, serverCurNode->hash);
		writeToFile(updatefd, "\n");
		return 1;
	}
}
/*
	Returns:
		-1 -> File did not exist to compute the livehash
		0 -> both are the same 
		1 -> Outputted Modified
		2 -> Outputted Conflict
		
*/
int checkVersionAndHash(mNode* serverNode, mNode* clientNode, char* projectName, int updateFilefd, int conflictFilefd){
	mNode* serverCurNode = serverNode;
	mNode* clientCurNode = clientNode;
	if(strcmp(serverCurNode->version, clientCurNode->version) == 0 && strcmp(serverCurNode->hash, clientCurNode->hash) == 0){
		return 0;
	}else{
		char* temp = generatePath("", clientCurNode->filepath);
		char* livehash = generateHashCode(temp);
		if(livehash == NULL){
			free(temp);
			return -1;
		}
		if(strcmp(livehash, clientCurNode->hash) != 0){
			printf("C %s\n", clientCurNode->filepath);
			writeToFile(conflictFilefd, "C ");
			writeToFile(conflictFilefd, serverCurNode->filepath);
			writeToFile(conflictFilefd, " ");
			writeToFile(conflictFilefd, livehash);
			writeToFile(conflictFilefd, "\n");
			free(temp);
			free(livehash);
			return 2;
		}else{
			printf("M %s\n", serverCurNode->filepath);
			writeToFile(updateFilefd, "M ");
			writeToFile(updateFilefd, serverCurNode->filepath);
			writeToFile(updateFilefd, " ");
			writeToFile(updateFilefd, serverCurNode->hash);
			writeToFile(updateFilefd, "\n");
			free(temp);
			free(livehash);
			return 1;
		}
	}
}
/*
	Searches for:
		- clientid
		- the Manifest Version when the commit was created and verifies it was the client's Manifest was the same
*/
int setupPush(char* projectName, char* commitfilepath){
	char* clientidpath = generatePath("", ".clientid.txt"); //CHANGE LATER to hidden 
	int clientidfd = open(clientidpath, O_RDONLY);
	if(clientidfd == -1){
		printf("Fatal Error: could not locate the clientid file to send alongside the commit to push your commit, please redo commit using the commit keyword then push\n");
		free(clientidpath);
		return -1;
	}
	close(clientidfd);
	char* storedcommitManifestVersion = NULL;
	char* commitManifestVersionfilepath = generatePath(projectName, "/.commitManifestVersion");
	int commitManifestVersionfd = open(commitManifestVersionfilepath, O_RDONLY);
	if(commitManifestVersionfd == -1){
		printf("Fatal Error: Could not locate the Manifest Version when the commit was made to verify no changes have been made to client's Manifest since last commit, please redo commit using the commit keyowrd then push\n");
		free(commitManifestVersionfilepath);
		return -1;
	}else{
		storedcommitManifestVersion = readFileTillDelimiter(commitManifestVersionfd);
		close(commitManifestVersionfd);
		remove(commitManifestVersionfilepath);
		free(commitManifestVersionfilepath);
	}
	char* manifestfilepath = generateManifestPath(projectName);
	char* currentManifestVersion = generateHashCode(manifestfilepath);
	free(manifestfilepath);
	if(strcmp(currentManifestVersion, storedcommitManifestVersion) != 0){
		printf("Fatal Error: There has been changes to the client's manifest since the last commit, please call commit again\n");
		free(currentManifestVersion);
		free(storedcommitManifestVersion);
		return -1;
	}
	free(currentManifestVersion);
	free(storedcommitManifestVersion);
	free(clientidpath);
	return 1; 
}

/*
	Sends the commit over to the server and if it is found, sends the files
*/
void pushCommit(char* projectName, int socketfd, char* commitfilepath){
	char* clientidpath = generatePath("", ".clientid.txt"); //CHANGE LATER to hidden 
	int clientidfd = open(clientidpath, O_RDONLY);
	free(clientidpath);
	//printf("Client is now sending the clientid and commit to server\n");
	char* clientidtoken = readFileTillDelimiter(clientidfd);
	//printf("Clientid: %s\n", clientidtoken);
	close(clientidfd);
	sendLength(socketfd, clientidtoken); //Project$clientid$commit$
	sendFileBytes(commitfilepath,socketfd);
	free(clientidtoken);
	//SEND CLIENTID -> SEND COMMIT -> LOOK THROUGH COMMITS AND SEND THE FILES IN THE COMMITS (FORMAT FILELENGTH$FILE (ALL A's and M's)
	char* commitFound = NULL;
	readNbytes(socketfd, strlen("FAILURE"), NULL, &commitFound);
	if(strcmp(commitFound, "SUCCESS") == 0){
		free(commitFound);
		//printf("(DEBUG) Server successfully found your commit and expired all the other commits.\n");
		commitFound = NULL;
		readNbytes(socketfd, strlen("FAILURE"), NULL, &commitFound);
		if(strcmp(commitFound, "SUCCESS") == 0){
			//printf("Server was able to create the backup and store it in the history folder\n");
			//printf("Server is now waiting for the files to send in the order of the commit and format: bytesOfFile$Filecontent\n");
			mNode* commitHeads = NULL;
			int commitfd = open(commitfilepath, O_RDONLY);
			getCommit(commitfd, &commitHeads);
			close(commitfd);
			//printf("The commit file contains:\n");
			//printMLL(commitHeads);
			mNode* curFile = commitHeads;
			while(curFile != NULL){
				sendFileBytes(curFile->filepath, socketfd);
				curFile = curFile->next;
			}
			freeMLL(commitHeads);
			//printf("(DEBUG) Finished sending all the files to the server\n");
			
			free(commitFound);
			readNbytes(socketfd, strlen("FAILURE"), NULL, &commitFound);
			if(strcmp(commitFound, "SUCCESS") == 0){
				free(commitFound);
				//printf("Server successfully updated everything and is sending the Manifest back\n");
				
				char* manifest = generateManifestPath(projectName);
				fileNode* manifestNode = malloc(sizeof(fileNode) * 1);
				manifestNode->next = NULL;
				manifestNode->prev = NULL;
				manifestNode->filepath = manifest;
				int ManifestLength = getLength(socketfd);
				//printf("The Manifest Length is: %d\n", ManifestLength);
				manifestNode->filelength = ManifestLength;
				writeToFileFromSocket(socketfd, manifestNode);
				free(manifestNode->filepath);
				free(manifestNode);
				//printf("Succesfully updated the client Manifest\n");
				
			}else if(strcmp(commitFound, "FAILURE") == 0){
				printf("Fatal Error: Server failed to update all the files and the push\n");
				free(commitFound);
			}else{
				printf("Error: Not sure what the server send back %s\n", commitFound);
				free(commitFound);
			}
		}else if(strcmp(commitFound, "FAILURE") == 0){
			printf("Fatal Error: Server was not able to create the backup, stopping push\n");
			free(commitFound);
		}else{
			printf("Error: Not sure what the server send back %s\n", commitFound);
			free(commitFound);
		}
		
	}else if(strcmp(commitFound, "FAILURE") == 0){
		printf("Error: Server could not find your commit, please retry commit and push, deleting your commit file\n");
		free(commitFound);
	}else{
		printf("Error: Not sure what the server send back %s\n", commitFound);
		free(commitFound);
	}
	//printf("Finished with push\n");
}

/*
	Searches the commit file and organizes each line into a node and inserts it into a Linked List
*/
int getCommit(int commitfd, mNode** head){
	char buffer[100] = {'\0'};
	int bufferPos = 0;
	
	int defaultSize = 15;
	char* token = (char*) malloc(sizeof(char) * (defaultSize + 1));	
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	int tokenpos = 0;
	int mode = 0;
	int numOfSpace = 0;
	int read = 0;
	mNode* curFile = (mNode*) malloc(sizeof(mNode) * 1);
	curFile->next = NULL;
	curFile->prev = NULL;
	int numberOfFiles = 0;
	do{
		read = bufferFill(commitfd, buffer, sizeof(buffer));
		for(bufferPos = 0; bufferPos < read; ++bufferPos){
			if(buffer[bufferPos] == '\n'){
				if(mode == 1){
					free(curFile->filepath);
					free(curFile);
					free(token);
				}else{
					curFile->hash = token;
					(*head) = insertMLL(curFile, (*head));
					numberOfFiles = numberOfFiles + 1;
				}
				curFile = (mNode*) malloc(sizeof(mNode) * 1);
				mode = 0;
				numOfSpace = 0;
				tokenpos = 0;
				defaultSize = 25;
				token = malloc(sizeof(char) * (defaultSize + 1));
				memset(token, '\0', sizeof(char) * (defaultSize + 1));
			}else if(buffer[bufferPos] == ' '){
				numOfSpace++;
				if(numOfSpace == 1){
					if(strcmp("D", token) == 0){
						mode = 1;
						free(token);
					}else if(strcmp("A", token) == 0){
						mode = 2;
						curFile->version = token;
					}else if(strcmp("M", token) == 0){
						mode = 3;
						curFile->version = token;
					}else{
						//printf("Warning: The commit file is not properly formatted, %s", token);
						mode = -1;
					}
					defaultSize = 15;
					tokenpos = 0;
					token = malloc(sizeof(char) * (defaultSize + 1));
					memset(token, '\0', sizeof(char) * (defaultSize + 1));
				}else{
					curFile->filepath = token;
					defaultSize = 15;
					tokenpos = 0;
					token = malloc(sizeof(char) * (defaultSize + 1));
					memset(token, '\0', sizeof(char) * (defaultSize + 1));
				}
			}else{
				if(tokenpos >= defaultSize){
					defaultSize = defaultSize * 2;
					token = doubleStringSize(token, defaultSize);
				}
				token[tokenpos] = buffer[bufferPos];
				tokenpos++;
			}		
		}
	}while(buffer[0] != '\0' && read != 0);
	free(curFile);
	free(token);
	return numberOfFiles;
}

/*
	Given the projectname, it will search the first line of the manifest (manifest's version) and return the contents
*/
char* getManifestVersionString(char* projectName){
	char* manifest = generateManifestPath(projectName);
	int manifestfd = open(manifest, O_RDONLY);
	free(manifest);
	if(manifestfd == -1){
		printf("Fatal Error: Project's Manifest does not exist\n");
		return NULL;
	}else{
		char buffer[100] = {'\0'};
		int bufferPos = 0;
		int defaultSize = 10;
		int tokenpos = 0;
		char* token = malloc(sizeof(char) * (defaultSize + 1));
		memset(token, '\0', sizeof(char) * (defaultSize + 1));
		int read = 0;
		do{
			read = bufferFill(manifestfd, buffer, sizeof(buffer));
			for(bufferPos = 0; bufferPos < read; ++bufferPos){
				if(buffer[bufferPos] == '\n'){
					close(manifestfd);
					return token;
				}else{
					if(tokenpos >= defaultSize){
						defaultSize = defaultSize * 2;
						token = doubleStringSize(token, defaultSize);
					}
					token[tokenpos] = buffer[bufferPos];
					tokenpos++;
				}
			}
		}while(read != 0 && buffer[0] != '\0');
	}
	close(manifestfd);
}

/*
	Given the projectName and pathToAppend, it will create a char terminated array from the current working directory
	Format: ./projectNamepathToAppend
*/
char* generatePath(char* projectName, char* pathToAppend){
	char* filepath = malloc(sizeof(char) * (strlen(projectName) + strlen(pathToAppend) + 3));
	memset(filepath, '\0', (sizeof(char) * (strlen(projectName) + 3 + strlen(pathToAppend))));
	filepath[0] = '.';
	filepath[1] = '/';
	memcpy(filepath + 2, projectName, strlen(projectName));
	strcat(filepath, pathToAppend);
	return filepath;
}

void printMLL(mNode* head){
	mNode* temp = head;
	while(temp != NULL){
		printf("Version:%s\tFilepath:%s\tHashcode:%s\n", temp->version, temp->filepath, temp->hash);
		temp = temp->next;
	}
}

mNode* insertMLL(mNode* newNode, mNode* head){
	newNode->next = NULL;
	newNode->prev = NULL;
	if(head == NULL){
		
	}else{
		newNode->next = head;
		head->prev = newNode;
	}
	return newNode;
}

/*
	Reads N bytes specified by length
		IF MODE = NULL: creates a token and stores into placeholder
		IF MODE = NULL AND placeholder = NULL, outputs to the terminal
*/
void readNbytes(int fd, int length, char* mode, char** placeholder){
	char buffer[100] = {'\0'};
	int defaultSize = 15;
	char* token = malloc(sizeof(char) * (defaultSize + 1));
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	
	int tokenpos = 0;
	int read = 0;
	int bufferPos = 0;

	do{
		if(length == 0){
			break;
		}else{
			if(length > sizeof(buffer)){
				read = bufferFill(fd, buffer, sizeof(buffer));
			}else{
				memset(buffer, '\0', sizeof(buffer));
				read = bufferFill(fd, buffer, length);
			}
		}
		if(mode == NULL && placeholder != NULL){
			for(bufferPos = 0; bufferPos < read; ++bufferPos){
			if(tokenpos >= defaultSize){
				defaultSize = defaultSize * 2;
				token = doubleStringSize(token, defaultSize);
			}
			token[tokenpos] = buffer[bufferPos];
			tokenpos++;
			}
		}else if(mode == NULL && placeholder == NULL){
			printf("%s", buffer);
		}
		length = length - read;
	}while(buffer[0] != '\0' && read != 0);
	if(mode == NULL && placeholder != NULL){
		*placeholder = token;
	}else if(mode == NULL && placeholder == NULL){
		free(token);
	}
}

/*
Purpose: Reads data from the socket and writes to the files
	Assuming files is an linked list of fileNode* (generated by metadataParser)
*/
void writeToFileFromSocket(int socketfd, fileNode* files){
	fileNode* file = files;
	while(file != NULL){
		char buffer[101] = {'\0'};
		int filelength = file->filelength;
		int read = 0;
		int filefd = open(file->filepath, O_WRONLY | O_CREAT | O_TRUNC,  S_IRUSR | S_IWUSR);
		if(filefd == -1){
			printf("Fatal Error: Could not write to File from the Socket because File did not exist or no permissions\n");
		}
		while(filelength != 0){
			if(filelength > (sizeof(buffer) - 1)){
				read = bufferFill(socketfd, buffer, (sizeof(buffer) - 1));
			}else{
				memset(buffer, '\0', sizeof(buffer));
				read = bufferFill(socketfd, buffer, filelength);
			}
			//printf("The buffer has %s\n", buffer);
			writeToFile(filefd, buffer);
			
			filelength = filelength - read;
		}
		file = file->next;
		close(filefd);
	}
}

/*
Purpose: Reads data from a single file and writes to the socket
*/
void writeToSocketFromFile(int clientfd, char* fileName){
	int filefd = open(fileName, O_RDONLY);
	int read = 0;
	if(filefd == -1){
		printf("Fatal Error: Could not send file because file did not exist or no permissions\n");
		return;
	}
	char buffer[101] = {'\0'}; // Buffer is sized to 101 because we need a null terminated char array for writeToFile method since it performs a strlen
	do{
		read = bufferFill(filefd, buffer, (sizeof(buffer) - 1)); 
		//printf("The buffer has %s\n", buffer);
		writeToFile(clientfd, buffer);
	}while(buffer[0] != '\0' && read != 0);
	//printf("Finished sending file: %s to Server\n", fileName);
	close(filefd);
}

/*
	Given a buffer and bytesToRead, it will attempt to read the fd and fill the buffer
		bytesToRead should be <= sizeof(buffer)
*/
int bufferFill(int fd, char* buffer, int bytesToRead){
    int position = 0;
    int bytesRead = 0;
    int status = 0;
    memset(buffer, '\0', bytesToRead);
    do{
        status = read(fd, (buffer + bytesRead), bytesToRead-bytesRead);
        if(status == 0){
       		break;
        }else if(status == -1){
            printf("Warning: Error when reading the file reading\n");
            return bytesRead;
        }
        bytesRead += status;
    }while(bytesRead < bytesToRead);
    	return bytesRead;
}

/*
	Given a null terminated char array, it will write all bytes from this array to the file
*/
void writeToFile(int fd, char* data){
	int bytesToWrite = strlen(data);
	int bytesWritten = 0;
	int status = 0;
	while(bytesWritten < bytesToWrite){
		status = write(fd, (data + bytesWritten), (bytesToWrite - bytesWritten));
		if(status == -1){
			printf("Warning: write encountered an error\n");
			close(fd);
			return;
		}
		bytesWritten += status;
	}
}

/*
	Given the filepath, will return the size (bytes) of the file
*/
long long calculateFileBytes(char* fileName){
	struct stat fileinfo;
	bzero((char*)&fileinfo, sizeof(struct stat));
	int success = stat(fileName, &fileinfo);
	if(success == -1){
		printf("Error: File not found or lacking permissions to access file to get metadata\n");
	}else{
		return (long long) fileinfo.st_size;
	}
}

/*
	Given the socket and filepath, sends the file to the server 
	(will not generate the metadata for this file, just sends the contents of this file)
*/
void sendFile(int clientfd, char* filepath){
	int filefd = open(filepath, O_RDONLY);
	int read = 0;
	if(filefd == -1){
		printf("Fatal Error: Could not send file because file did not exist\n");
		return;
	}
	char buffer[101] = {'\0'}; 
	do{
		read = bufferFill(filefd, buffer, (sizeof(buffer) - 1)); 
		//printf("The buffer has %s\n", buffer);
		writeToFile(clientfd, buffer);
	}while(buffer[0] != '\0' && read != 0);
	//printf("Finished sending file: %s to client\n", filepath);
	close(filefd);
}

/*
	Mode 0: Remove
	Mode 1: Add (Will not overwrite)
	Mode 2: Replace Version Number
	Mode 3: Replace 
*/
void modifyManifest(char* projectName, char* filepath, int mode, char* replace){
	char* manifest = generateManifestPath(projectName);
	char* manifestTemp = malloc(sizeof(char) * (strlen(manifest) + 11));
	memset(manifestTemp, '\0', sizeof(char) * (strlen(manifest) + 11));
	memcpy(manifestTemp, manifest, strlen(manifest));
	strcat(manifestTemp, "4829484936"); 
	
	int manifestfd = open(manifest, O_RDONLY); 
	
	if(manifestfd == -1){
		printf("Fatal Error: The Manifest does not exist!\n");
		return;
	}
	int tempmanifestfd = open(manifestTemp, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if(tempmanifestfd == -1){
		//printf("Fatal Error: The temporarily Manifest already exist\n");
	}
	int defaultSize = 25;
	char* token = malloc(sizeof(char) * (defaultSize + 1));
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	
	char buffer[125] = {'\0'};
	int tokenpos = 0;
	int bufferPos = 0;
	int read = 0;
	int found = -1;
	int existed = -1;
	int manifestVersion = 0;
	do{
		read = bufferFill(manifestfd, buffer, sizeof(buffer));
		for(bufferPos = 0; bufferPos < sizeof(buffer); ++bufferPos){
			if(tokenpos >= defaultSize){
				defaultSize = defaultSize * 2;
				token = doubleStringSize(token, defaultSize);
			}
			token[tokenpos] = buffer[bufferPos];
			tokenpos++;
			if(buffer[bufferPos] == '\n'){
				if(mode != 2){
					char* temp = malloc(sizeof(char) * strlen(token));
					memset(temp, '\0', strlen(token) * sizeof(char));
					int i = 0;
					int numofspaces = 0;
					int temppos = 0;
					for(i = 0; i < tokenpos; ++i){
						if(token[i] == ' '){
							numofspaces++;
							if(numofspaces == 2){
								found = strcmp(filepath, temp);
								if(found == 0){
									existed = 1;
								}
								break;
							}
						}else if(numofspaces == 1){
							temp[temppos] = token[i];
							temppos++;
						}
					}
					//printf("%s\n", temp);
					free(temp);
				}
				
				if(mode == 2 && manifestVersion == 0){
					writeToFile(tempmanifestfd, replace);
					manifestVersion = 1;
				}else if(mode == 1){
					writeToFile(tempmanifestfd, token);
				}else if(mode == 3 && found == 0){
					writeToFile(tempmanifestfd, replace);
				}else{
					if(found != 0){
						writeToFile(tempmanifestfd, token);
					}	
				}
				found = -1;
				free(token);
				defaultSize = 25;
				token = malloc(sizeof(char) * (defaultSize + 1));
				memset(token, '\0', sizeof(char) * (defaultSize + 1));
				tokenpos = 0;
			}
		}
	}while(buffer[0] != '\0' && read != 0);
	
	close(manifestfd);

	if(existed == -1 && mode != 2){
		if(mode == 1 || mode == 3){
			writeToFile(tempmanifestfd, replace);
			close(tempmanifestfd);
			remove(manifest);
			rename(manifestTemp, manifest);
		}else{
			printf("Warning: The File Entry is not listed in the Manifest to remove\n");
			close(tempmanifestfd);
			remove(manifestTemp);
		}
	}else{
		if(mode == 1 && existed == 1){
			printf("Warning: File entry already existed in the Manifest and therefore could not be added\n");
			close(tempmanifestfd);
			remove(manifestTemp);
		}else{
			close(tempmanifestfd);
			remove(manifest);
			rename(manifestTemp, manifest);
		}
	}
	free(token);
	free(manifest);
	free(manifestTemp);	
}
/*
	Given the project name, it generates the path to Manifest
		Format: ./projectName/Manifest
*/
char* generateManifestPath(char* projectName){
	char* manifest = malloc(sizeof(char) * strlen(projectName) + 3 + strlen("/.Manifest")); //CHANGE LATER
	memset(manifest, '\0', sizeof(char) * strlen(projectName) + 3 + strlen("/.Manifest")); //CHANGE LATER
	manifest[0] = '.';
	manifest[1] = '/';
	memcpy(manifest + 2, projectName, strlen(projectName));
	strcat(manifest, "/.Manifest");
	return manifest;
}

/*
Local 0: Indicates the file is not just local added
Local 1: Indicates the file is locally added
Mode 0: will just create a line of the Manifest given the version/filepath/hashcode
Mode 1: For 'add', will append l before versionNumber to indicate it was locally added
Mode 2: For 'remove', will append rl before versionNumber to indicate it was locally removed
*/
char* createManifestLine(char* version, char* filepath, char* hashcode, int local, int mode){
	char* line = NULL;
	if(local){
		line = (char*) malloc(sizeof(char) * (strlen(version) + strlen(filepath) + strlen(hashcode) + 6)); // 1 for null terminal, 3 for spaces/ 2 to indicate if it was created locally
		memset(line, '\0', sizeof(char) * (strlen(version) + strlen(filepath) + strlen(hashcode) + 6));
	}else{
		line = (char*) malloc(sizeof(char) * (strlen(version) + strlen(filepath) + strlen(hashcode) + 4));
		memset(line, '\0', sizeof(char) * (strlen(version) + strlen(filepath) + strlen(hashcode) + 4));
	}
	if(mode == 1){
		char* temp = (char*) malloc(sizeof(char) * (strlen(version) + 2));
		memset(temp, '\0', sizeof(char) * (strlen(version) + 2));
		temp[0] = 'l';
		strcat(temp, version);
		memcpy(line, temp, strlen(temp));
		line[strlen(temp)] = ' ';
		memcpy(line+strlen(temp) + 1, filepath, strlen(filepath));
		line[strlen(temp) + 1 + strlen(filepath)] = ' ';
		memcpy(line+strlen(temp) + 1 + strlen(filepath) + 1, hashcode, strlen(hashcode));
		free(temp);
	}else if(mode == 2){
		char* temp = (char*) malloc(sizeof(char) * (strlen(version) + 3));
		memset(temp, '\0', sizeof(char) * (strlen(version) + 3));
		temp[0] = 'l';
		temp[1] = 'r';
		strcat(temp, version);
		memcpy(line, temp, strlen(temp));
		line[strlen(temp)] = ' ';
		memcpy(line+strlen(temp) + 1, filepath, strlen(filepath));
		line[strlen(temp) + 1 + strlen(filepath)] = ' ';
		memcpy(line+strlen(temp) + 1 + strlen(filepath) + 1, hashcode, strlen(hashcode));
		free(temp);
	}else{
		memcpy(line, version, strlen(version));
		line[strlen(version)] = ' ';
		memcpy(line+strlen(version) + 1, filepath, strlen(filepath));
		line[strlen(version) + 1 + strlen(filepath)] = ' ';
		memcpy(line+strlen(version) + 1 + strlen(filepath) + 1, hashcode, strlen(hashcode));
	}
	line[strlen(line)] = '\n';
	//printf("Inserting:%s", line);
	return line;
}

/*
	Given the filepath, it will read the file and convert it to md5 hash and translate it to hexadecimal char array
*/
char* generateHashCode(char* filepath){
	int filefd = open(filepath, O_RDONLY);
	if(filefd == -1){
		//printf("Fatal Error: File does not exist to generate a hashcode\n");
		return NULL;
	}else{
		char* hash = (char *) malloc(sizeof(char) * (MD5_DIGEST_LENGTH + 1));	
		memset(hash, '\0', sizeof(char) * (MD5_DIGEST_LENGTH + 1));
		char buffer[1024] = {'\0'};
		int read = 0;
		MD5_CTX mdHash;
		MD5_Init (&mdHash);
		do{
			read = bufferFill(filefd, buffer, sizeof(buffer));
			MD5_Update(&mdHash, buffer, read);
		}while(buffer[0] != '\0' && read != 0);
		MD5_Final (hash, &mdHash);
		int i = 0;
		/*
		printf("The hash for this file is: ");
		for(i = 0; i < MD5_DIGEST_LENGTH; i++){
			printf("%02x", (unsigned char) hash[i]);
		}
		printf("\n");
		*/
		char* hexhash = (char *) malloc(sizeof(char) * ((MD5_DIGEST_LENGTH * 2) + 1));
		memset(hexhash, '\0', sizeof(char) * ((MD5_DIGEST_LENGTH * 2) + 1));
		int previous = 0;
		for(i = 0; i < MD5_DIGEST_LENGTH; ++i){
			sprintf( (char*) (hexhash + previous), "%02x", (unsigned char) hash[i]); //Each characters takes up 2 bytes now (hexadecimal value)
			previous += 2;
		}
		free(hash);
		close(filefd);
		return hexhash;
	}
}

/*
	Create the directories required for the files
*/
void createDirectories(fileNode* list){
	fileNode* file = list;
	while(file != NULL){
		char* temp = strdup(file->filepath);
		char* subdirectories = dirname(temp);
		makeNestedDirectories(subdirectories);
		free(temp);
		file = file->next;
	}
}

/*
	Creates a single directory, given the directory path (does not work with nested directories)
*/
int makeDirectory(char* directoryPath){
	int success = mkdir(directoryPath, S_IRWXU);
	if(success == -1){
		printf("Warning: Directory could not be created\n");
		return 0;
	}else{
		//printf("Directory Created\n");
		return 1;
	}
}
/*
	Given the directory path will check if it exist
		Returns: 
		1 if exist
		0 if does not exist
*/
int directoryExist(char* directoryPath){
	DIR* directory = opendir(directoryPath);
	if(directory != NULL){ //Directory Exists
		closedir(directory);
		return 1;
	}else{ //Directory does not exist or does not have permission to access
		return 0;
	}
}

/*
	Given a directory path, will construct the whole directory path (including the nested directories) and will not overwrite directories that exist
*/
void makeNestedDirectories(char* path){ 
	char* parentdirectory = strdup(path);
	char* directory = strdup(path);
	char* directoryName = basename(directory);
	char* parentdirectoryName = dirname(parentdirectory);
	if(strlen(path) != 0 && strcmp(path, ".") != 0){
		makeNestedDirectories(parentdirectoryName);
	}
	mkdir(path, 0777);
	free(directory);
	free(parentdirectory);
}

/*
	Inserts the fileNode at the tail (to perverse the order)
*/
void insertLL(fileNode* node){
	if(listOfFiles == NULL){
		listOfFiles = node;
	}else{
		fileNode* temp = listOfFiles;
		while(temp->next != NULL){
			temp = temp->next;
		}
		temp->next = node;
		node->prev = temp;
	}
}

int reserveKeywords(char* word){
	char* reserveKeyword[] = {".Manifest", ".History", ".Manifest4829484936", ".serverManifestVersion", ".commitManifestVersion", ".Update", ".Commit", ".Conflict"};
	int i;
	for(i = 0; i < 8; i++){
		if(strcasecmp(word, reserveKeyword[i]) == 0){
			return 1;
		}
	}
	if(strlen(word) >= strlen(".rollback") && strncasecmp(word, ".rollback", strlen(".rollback")) == 0){
		return 1;
	}
	return 0;
}

int setupConnection(){
	char** information = getConfig();
	if(information != NULL){
   	int socketfd = createSocket();
   	if(socketfd >= 0){
	  		if(connectToServer(socketfd, information) == -1){
	  			close(socketfd);
	  			socketfd = -1;
	  		}
	  	}
		free(information[0]);
		free(information[1]);
		free(information);
	  	return socketfd;
   }
   return 0;
}

int createSocket(){
	int socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if(socketfd < 0){
		printf("Fatal Error: Could not open socket\n");
	}
	return socketfd;
}

int connectToServer(int socketfd, char** serverinfo){
	struct hostent* ip = gethostbyname(serverinfo[0]); //Not sure how to handle this, stuff isn't being freed here
	if(ip == NULL){
		printf("Fatal Error: IP address used in configure file does not work, invalid ip address\n");
		return -1;
	}
	struct sockaddr_in serverinfostruct;
	bzero((char*)&serverinfostruct, sizeof(serverinfostruct)); 
	serverinfostruct.sin_family = AF_INET;
	serverinfostruct.sin_port = htons(atoi(serverinfo[1]));
	bcopy((char*)ip->h_addr, (char*)&serverinfostruct.sin_addr.s_addr, ip->h_length);
	while(1){
		if(connect(socketfd, (struct sockaddr*)&serverinfostruct, sizeof(serverinfostruct)) < 0){
			printf("Warning: Cannot connect to server, retrying\n");
			sleep(3);
		}else{
			printf("Successful Connection to Server\n");
			return 1;
		}
	}
	return -1;
}

/*
Returns a char** that contains:
	1st Element = IP
	2nd Element = Port
*/
char** getConfig(){
	int fd = open(".configure", O_RDONLY);
	if(fd == -1){
		printf("Fatal Error: Configure file is missing or no permissions to Configure file, please call configure before running\n");
		return NULL;
	}
	int read = 0;
	char buffer[25] = {'\0'};
	int bufferPos = 0;
	int tokenpos = 0;
	int defaultSize = 10;
	char* token = (char*) malloc(sizeof(char) * (defaultSize + 1));
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	char** information = (char**) malloc(sizeof(char*) * 2);
	memset(information, '\0', sizeof(char*) * 2);
	int finished = 0;
	do{
		read = bufferFill(fd, buffer, sizeof(buffer));
		for(bufferPos = 0; bufferPos < (sizeof(buffer) / sizeof(buffer[0])); ++bufferPos){
			if(buffer[bufferPos] == ' '){
				information[0] = token;
				defaultSize = 10;
				tokenpos = 0;
				token = (char*) malloc(sizeof(char) * (defaultSize + 1));
				memset(token, '\0', sizeof(char) * (defaultSize + 1));
			}else if(buffer[bufferPos] == '\n'){
				information[1] = token;
				finished = 1;
				break;
			}else{
				if(tokenpos >= defaultSize){
					defaultSize = defaultSize * 2;
					token = doubleStringSize(token, defaultSize);
				}
				token[tokenpos] = buffer[bufferPos];
				tokenpos++;
			}
		}
	}while((buffer[0] != '\0' && finished == 0) && read != 0);
	close(fd);
	return information;
}
void printFiles(){
	fileNode* temp = listOfFiles;
	while(temp != NULL){
		printf("File: %s with path of %s and length of %llu\n", temp->filename, temp->filepath, temp->filelength);
		temp = temp->next;
	}
}
void sendLength(int socketfd, char* token){
	char str[5] = {'\0'};
	sprintf(str, "%lu", strlen(token));
	writeToFile(socketfd, str);
	writeToFile(socketfd, "$");
	writeToFile(socketfd, token);
}
void freeFileNodes(){
	while(listOfFiles != NULL){
		free(listOfFiles->filename);
		free(listOfFiles->filepath);
		fileNode* temp = listOfFiles;
		listOfFiles = listOfFiles->next;
		free(temp);
	}
	listOfFiles = NULL;
}

char* doubleStringSize(char* word, int newsize){
	char* expanded =  (char*) malloc(sizeof(char) * (newsize + 1));
	memset(expanded, '\0', (newsize+1) * sizeof(char));
	memcpy(expanded, word, strlen(word));
	free(word);
	return expanded;
}

void quickSortRecursive(mNode* startNode, mNode* endNode, int (*comparator)(void*, void*)){
    if(startNode == endNode || startNode == NULL || endNode == NULL){
        return;
    }

    mNode* prevPivot = partition(startNode, endNode, comparator);
    if(prevPivot != startNode){
        quickSortRecursive(startNode, prevPivot->prev, comparator);
    }
    if(prevPivot != endNode){
        quickSortRecursive(prevPivot->next, endNode, comparator);
    }

}

int quickSort( mNode* head, int (*comparator)(void*, void*)){
    mNode* tail = head;
    while(tail->next != NULL){
        tail = tail->next;
    }
    quickSortRecursive(head, tail, comparator);
    return 1;
}

void* partition(mNode* startNode, mNode* endNode, int (*comparator)(void*, void*)){
    mNode* pivot = startNode;

    mNode* left = pivot->next;
    mNode* end = endNode;
    mNode* storeIndex = left;
    mNode* beforeNULL = end;


    mNode* i;
    for(i = left; i != end->next; i = i->next){
        if(comparator(i, pivot) <= 0){
            beforeNULL = storeIndex;
            char* fileholder = i->filepath;
				char* filehash = i->hash;
				char* fileversion = i->version;
            i->filepath = storeIndex->filepath;
            i->hash = storeIndex->hash;
            i->version = storeIndex->version;
            storeIndex->filepath = fileholder;
            storeIndex->hash = filehash;
            storeIndex->version = fileversion;
            storeIndex = storeIndex->next;
        }
    }
    char* fileholder = pivot->filepath;
    char* filehash = pivot->hash;
    char* fileversion = pivot->version;
    if(storeIndex == NULL){
        pivot->filepath = beforeNULL->filepath;
        pivot->hash = beforeNULL->hash;
        pivot->version = beforeNULL->version;
        beforeNULL->filepath = fileholder;
        beforeNULL->hash = filehash;
        beforeNULL->version = fileversion;
        return beforeNULL;
    }else{
        pivot->filepath = storeIndex->prev->filepath;
        pivot->hash = storeIndex->prev->hash;
        pivot->version = storeIndex->prev->version;
        storeIndex->prev->filepath = fileholder;
        storeIndex->prev->hash = filehash;
        storeIndex->prev->version = fileversion;
        return storeIndex->prev;
    }
    return NULL; //ERROR IF IT REACHES HERE
}

int strcomp(void* string1, void* string2){
    mNode* s1ptr = (mNode*) string1;
    mNode* s2ptr = (mNode*) string2;

    char* s1 = s1ptr->filepath;
    char* s2 = s2ptr->filepath;
    while(*s1 != '\0' && *s2 != '\0'){
        if(*s1 == *s2){
            s1 = s1 + sizeof(char);
            s2 = s2 + sizeof(char);
        }else{
            return *s1 - *s2;
        }
    }
    return *s1 - *s2;
}
