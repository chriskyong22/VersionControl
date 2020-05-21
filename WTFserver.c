#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <errno.h>
#include <signal.h>
#include <openssl/md5.h>
#include <libgen.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct _fileNode_{
	char* filename;
	char* filepath;
	long long filelength;
	struct _fileNode_* next;
	struct _fileNode_* prev;
}fileNode;

typedef struct _userNode_{
	pthread_t thread;
	struct _userNode_* next;
}userNode;

typedef struct _commitNode_{
	char* pName;
	char* clientid;
	char* commit;
	struct _commitNode_* next;
	struct _commitNode_* prev;
}commitNode;

typedef struct _manifestNodes_{
	char* filepath;
	char* version;
	char* hash;
	struct _manifestNodes_* next;
	struct _manifestNodes_* prev;
}mNode;

//Setup Server Methods
int setupServer(int socketfd, int port);

//Clean up Methods
void sighandler(int sig);
void unloadMemory();
void freeFileNodes();
void freeCommitNode(commitNode* node);
void freeMNode(mNode* node);
void freeMLL(mNode* head);
void freeCommitLL(commitNode* head);

//File Recieving Methods:
void* metadataParser(void* clientfdptr);
int bufferFill(int fd, char* buffer, int bytesToRead);
void readNbytes(int fd, int length,char* mode ,char** placeholder);
void writeToFileFromSocket(int socketfd, int filelength, char* filepath);

// File Sending Methods:
void writeToFile(int fd, char* data);
void sendFile(int clientfd, char* fileName);
void setupFileMetadata(int clientfd, fileNode* files, int numOfFiles);
void sendFilesToClient(int clientfd, fileNode* files, int numOfFiles);
void sendManifest(char* projectName, int clientfd);
void sendFileBytes(char* filepath, int socketfd);
void sendLength(int socketfd, char* token);

// File Helper Methods
long long calculateFileBytes(char* fileName);
fileNode* createFileNode(char* filepath, char* filename);
void printFiles();
void insertLL(fileNode* node);
int getLength(int socketfd);

//Manifest Methods
void createManifest(int fd, char* directorypath);
void readManifestFiles(char* projectName, int mode, int clientfd);
void modifyManifest(char* projectName, char* filepath, int mode, char* replace);
char* createManifestLine(char* version, char* filepath, char* hashcode, int local, int mode);
char* getManifestVersionString(char* projectName);
char* generateManifestPath(char* projectName);
void getManifestVersion(char* projectName, int clientfd);
int getFileVersion(mNode* manifest, char* filepath);
mNode* insertMLL(mNode* newNode, mNode* head);
void printMLL(mNode* head);

//General Helper Methods
char* doubleStringSize(char* word, int newsize);
char* pathCreator(char* path, char* name);
char* generatePath(char* projectName, char* pathToAppend);
int makeDirectory(char* directoryName);
void makeNestedDirectories(char* path);
int directoryTraverse(char* path, int mode, int fd);
int directoryExist(char* directoryPath);
char* generateHashCode(char* filepath);
void insertThreadLL(pthread_t node);
void printActiveCommits();
char* convertIntToString(int value);
void removeEmptyDirectory(char* path, char* projectName);

// Command Methods
void createProject(char* directoryName, int clientfd);
void getProjectVersion(char* directoryName, int clientfd);
void destroyProject(char* directoryName, int clientfd);
void update(char* projectName, int clientfd);
void upgrade(char* projectName, int clientfd, int numOfFiles);
void commit(char* projectName, int clientfd);
void insertCommit(char* clientid, char* projectName, char* commit, int clientfd);
void push(char* projectName, int clientfd);
void getHistory(int clientfd, char* projectName);
int rollbackProject(char* projectPath, char* rollbackfolder);

// Command Helper Methods
char* generateRollbackVersionfp(char* projectName);
int generateBackup(char* projectName);
void rollbackVersion(int clientfd, char* projectName);
void updateHistory(char* projectName, char* commit, int manifestVersion);
int getCommit(char* commit, mNode** head);
void removeGreaterVersions(char* projectName, int version);

userNode* pthreadHead = NULL;
fileNode* listOfFiles = NULL;
commitNode* activeCommit = NULL;
int numOfFiles = 0;

pthread_mutex_t lockRepo;
pthread_mutex_t pthreadLock;
int main(int argc, char** argv){
	signal(SIGINT, sighandler);
	atexit(unloadMemory);
	if(argc != 2){
		printf("Fatal Error: invalid number of arguments\n");
	}else{
		//Assuming argv[1] is always a number currently
		int socketfd = socket(AF_INET, SOCK_STREAM, 0);
		if(socketfd == -1){
			printf("Fatal Error: Creating server socket failed\n");
		}else{
			if(setupServer(socketfd, atoi(argv[1])) == -1){
				printf("Fatal Error: Could not bind the server socket to the ip and port\n");
				//printf("Error code: %d\n", errno);
			}else{
				struct sockaddr_in client;
				socklen_t clientSize = sizeof(struct sockaddr_in);
				pthread_mutex_init(&pthreadLock, NULL);
				pthread_mutex_init(&lockRepo, NULL);
				while(1){
					int clientfd = accept(socketfd, (struct sockaddr*) &client, &clientSize);
					if(clientfd == -1){
						printf("Error: Refused Connection\n");
					}else{
						printf("Successfully accepted the connection and creating thread to run this client on\n");
						//printf("Creating the thread to run this connection\n");
						pthread_t client = NULL;
						pthread_create(&client, NULL, metadataParser, &clientfd);
						insertThreadLL(client);
					}
				}
			}
		}
	}
	return 0;
}

void insertThreadLL(pthread_t node){
	userNode* newNode = malloc(sizeof(userNode) * 1);
	newNode->thread = node;
	newNode->next = NULL;
	pthread_mutex_lock(&pthreadLock);
	if(pthreadHead == NULL){
		pthreadHead = newNode;
	}else{
		newNode->next = pthreadHead;
		pthreadHead = newNode;
	}
	pthread_mutex_unlock(&pthreadLock);
}
/*
	General metadata parser and the function the thread will run on
*/
void* metadataParser(void* clientfdptr){
	int clientfd = *((int*) clientfdptr);
	char buffer[1] = {'\0'}; 
	int defaultSize = 15;
	char* token = malloc(sizeof(char) * (defaultSize + 1));
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	int read = 0;
	int bufferPos = 0;
	int tokenpos = 0;
	int fileLength = 0;
	int filesRead = 0;
	int fileName = 0;
	listOfFiles = NULL;
	fileNode* file = NULL;
	char* mode = NULL;
	do{
		read = bufferFill(clientfd, buffer, sizeof(buffer));
		if(buffer[0] == '$'){
			if(mode == NULL){
				mode = token;
				//printf("%s\n", mode);
				pthread_mutex_lock(&lockRepo);
			}else if(strcmp(mode, "rollback") == 0){
				//printf("Getting the project name to rollback\n");
				fileLength = atoi(token);
				free(token);
				char* projectName = NULL;
				readNbytes(clientfd, fileLength, NULL, &projectName);
				rollbackVersion(clientfd, projectName);
				
				free(projectName);
				break;
			}else if(strcmp(mode, "history") == 0){
				//printf("Getting the Project name to retrieve the history\n");
				fileLength = atoi(token);
				free(token);
				char* projectName = NULL;
				readNbytes(clientfd, fileLength, NULL, &projectName);
				getHistory(clientfd, projectName);
				free(projectName);
				break;
			}else if(strcmp(mode, "push") == 0){
				//printf("Getting the project name to retrieve the manifest version for commit\n");
				fileLength = atoi(token);
				free(token);
				char* projectName = NULL;
				readNbytes(clientfd, fileLength, NULL, &projectName);
				push(projectName, clientfd);
			
				free(projectName);
				break;
			}else if(strcmp(mode, "commit") == 0){
				//printf("Getting the project name to retrieve the manifest version for commit\n");
				fileLength = atoi(token);
				free(token);
				char* projectName = NULL;
				readNbytes(clientfd, fileLength, NULL, &projectName);
				commit(projectName, clientfd);
				
				
				free(projectName);
				break;
			}else if(strcmp(mode, "getManifestVersion") == 0){
				//printf("Getting the project name to retrieve the manifest version\n");
				fileLength = atoi(token);
				free(token);
				char* temp = NULL;
				readNbytes(clientfd, fileLength, NULL, &temp);
				getManifestVersion(temp, clientfd);
				free(temp);
				break;
			}else if(strcmp(mode, "upgrade") == 0){
				fileLength = atoi(token);
				free(token);
				char* projectName = NULL;
				readNbytes(clientfd, fileLength, NULL, &projectName);
				getManifestVersion(projectName, clientfd);
				fileLength = getLength(clientfd);
				if(fileLength == 0){
					writeToFile(clientfd, "SUCCESS");
					printf("Warning: Successful upgrade, the client is up to date to the server\n");
					getManifestVersion(projectName, clientfd);
				}else if(fileLength == -1){
					writeToFile(clientfd, "SUCCESS");
					printf("Warning: Successful upgrade, the client's update project version did not match the server\n");
				}else{ 
					//printf("Reading and sending %d files\n", fileLength);
					upgrade(projectName, clientfd, fileLength);
				}
				free(projectName);
				//printf("Finished Upgrade\n");
				break;
			}else if(strcmp(mode, "update") == 0){
				//printf("Getting the project to update\n");
				fileLength = atoi(token);
				free(token);
				char* temp = NULL;
				readNbytes(clientfd, fileLength, NULL, &temp);
				update(temp, clientfd);
				free(temp);
				//printf("Finished with update\n");
				break;
			}else if(strcmp(mode, "destroy") == 0){
				//printf("Getting the project to destroy\n");
				fileLength = atoi(token);
				free(token);
				char* temp = NULL;
				readNbytes(clientfd, fileLength, NULL, &temp);
				destroyProject(temp, clientfd);
				free(temp);
				break;
			}else if(strcmp(mode, "checkout") == 0){
				//printf("Getting the project to check out\n");
				fileLength = atoi(token);
				free(token);
				char* temp = NULL;
				readNbytes(clientfd, fileLength, NULL, &temp);
				readManifestFiles(temp, 0, clientfd);
				printFiles();
				sendFilesToClient(clientfd, listOfFiles, numOfFiles);
				free(temp);
				break;
			}else if(strcmp(mode, "create") == 0) {
				//printf("Getting the filename to create\n");
				fileLength = atoi(token);
				free(token);
				char* temp = NULL;
				readNbytes(clientfd, fileLength, NULL, &temp);
				createProject(temp, clientfd);
				free(temp);
				break;
         }else if(strcmp(mode, "currentversion") == 0){
             //printf("Getting the Project\n");
             fileLength = atoi(token);
             free(token);
             char* temp = NULL;
             readNbytes(clientfd, fileLength, NULL, &temp);
             getProjectVersion(temp, clientfd);
             free(temp);
             break;
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
			token[tokenpos] = buffer[0];
			tokenpos++;
		}
	}while(buffer[0] != '\0' && read != 0);
	if(mode != NULL){
		free(mode);
	}
	close(clientfd);
	printf("Server: Terminated Connection with Client\n");
	freeFileNodes();
	numOfFiles = 0;
	pthread_mutex_unlock(&lockRepo);
	pthread_exit(NULL);
}

void printFiles(){
	fileNode* temp = listOfFiles;
	while(temp != NULL){
		printf("File: %s with path of %s and length of %llu\n", temp->filename, temp->filepath, temp->filelength);
		temp = temp->next;
	}
}

/*
	Reads N bytes specified by length
		IF MODE = NULL: creates a token and stores into placeholder
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
		if(mode == NULL){
			for(bufferPos = 0; bufferPos < read; ++bufferPos){
				if(tokenpos >= defaultSize){
					defaultSize = defaultSize * 2;
					token = doubleStringSize(token, defaultSize);
				}
				token[tokenpos] = buffer[bufferPos];
				tokenpos++;
			}
		}
		length = length - read;
	}while(buffer[0] != '\0' && read != 0);
	if(mode == NULL){
		(*placeholder) = token;
	}
}

void createProject(char* projectName, int clientfd){
	//printf("Attempting to create the directory\n");
	int success = makeDirectory(projectName);
	if(success){
		char* manifest = generateManifestPath(projectName);
		int fd = open(manifest, O_WRONLY | O_TRUNC | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
		createManifest(fd, projectName);
		close(fd);
		free(manifest);
		char* historyfile = generatePath(projectName, "/.history");  //CHANGE LATER (HISTORY)
		int historyfd = open(historyfile, O_CREAT, S_IRUSR | S_IWUSR);
		free(historyfile);
		close(historyfd);
		//printf("Sending the Manifest to Client\n");
		writeToFile(clientfd, "SUCCESS");
		//printf("projectName: %s\n", projectName);
		sendManifest(projectName, clientfd);
	}else{
		printf("Error: Directory Failed to Create: Sending Error to Client\n");
		writeToFile(clientfd, "FAILURE");
	}
}

void getHistory(int clientfd, char* projectName){
	int projectExist = directoryExist(projectName);
	if(projectExist == 0){
		writeToFile(clientfd, "FAILURE");
		printf("Error: The Project was not found for history\n");
		return;
	}
	char* historyfp = generatePath(projectName, "/.history"); //CHANGE LATER
	int historyfd = open(historyfp, O_RDONLY);
	if(historyfd == -1){
		writeToFile(clientfd, "FAILURE");
		printf("Error: History file was not found\n");	
	}else{
		writeToFile(clientfd, "SUCCESS");
		//printf("Sending history file\n");
		close(historyfd);
		sendFileBytes(historyfp, clientfd);
	}
	free(historyfp);
}

void destroyProject(char* directoryName, int clientfd){
	int success = directoryTraverse(directoryName, 3, -1);
	int curDirectory = remove(directoryName);
	if(success == 1 && curDirectory != -1){
		//printf("Server destroyed the project, sending client success message\n");
		//printf("Destroying all active commits\n");
		commitNode* curNode = activeCommit;
		while(curNode != NULL){
			if(strcmp(directoryName, curNode->pName) == 0){
				commitNode* temp = curNode;
				curNode = curNode->next;
				if(activeCommit == temp){
					activeCommit = temp->next;
				}
				if(temp->prev != NULL){
					temp->prev->next = temp->next;
				}
				if(temp->next != NULL){
					temp->next->prev = temp->prev;
				}
				freeCommitNode(temp); 
			}else{
				curNode = curNode->next;
			}
		}
		writeToFile(clientfd, "SUCCESS");
	}else{
		printf("Error: Server failed to destroy the whole project, sending error to client\n");
		writeToFile(clientfd, "FAILURE");
	}
}

void update(char* projectName, int clientfd){
	char* manifest = generateManifestPath(projectName);
	int fd = open(manifest, O_RDONLY);
	free(manifest);
	if(fd == -1){
		printf("Error: Project's Manifest does not exist or could not open, sending error to client\n");
		writeToFile(clientfd, "FAILURE");
	}else{
		//printf("Project's Manifest found, sending to client\n");
		writeToFile(clientfd, "SUCCESS");
		close(fd);
		sendManifest(projectName, clientfd);
	}
}

void push(char* projectName, int clientfd){
	int projectExist = directoryExist(projectName);
	if(projectExist == 0){
		writeToFile(clientfd, "FAILURE");
		printf("Error: The Project was not found for push\n");
		return;
	}
	writeToFile(clientfd, "SUCCESS");
	//printf("The project %s was successfully found for push, awaiting the clientid, commit\n", projectName);
	int tokenlength = getLength(clientfd);
	char* clientid = NULL;
	readNbytes(clientfd, tokenlength, NULL, &clientid);
	tokenlength = getLength(clientfd);
	char* clientcommit = NULL;
	readNbytes(clientfd, tokenlength, NULL, &clientcommit);
	commitNode* curNode = activeCommit;
	commitNode* commitFound = NULL;
	//printf("Succesfully recieved the client's clientid(%s) and commit(%s)\n", clientid, clientcommit);
	while(curNode != NULL){
		if(strcmp(clientcommit, curNode->commit) == 0 && strcmp(projectName, curNode->pName) == 0 && strcmp(clientid, curNode->clientid) == 0){
			//printf("The commit has been found\n");
			commitFound = curNode;
			if(activeCommit == commitFound){
				activeCommit = commitFound->next;
			}
			if(commitFound->prev != NULL){
				commitFound->prev->next = commitFound->next;
			}
			if(commitFound->next != NULL){
				commitFound->next->prev = commitFound->prev;
			}
			commitFound->next = NULL;
			commitFound->prev = NULL;
			break;
		}
		curNode = curNode->next;
	}
	free(clientid);
	free(clientcommit);
	if(commitFound != NULL){
		//printf("Removing all active commits for the project\n");
		curNode = activeCommit;
		while(curNode != NULL){
			if(strcmp(commitFound->pName, curNode->pName) == 0){
				commitNode* temp = curNode;
				curNode = curNode->next;
				if(activeCommit == temp){
					activeCommit = temp->next;
				}
				if(temp->prev != NULL){
					temp->prev->next = temp->next;
				}
				if(temp->next != NULL){
					temp->next->prev = temp->prev;
				}
				freeCommitNode(temp); 
			}else{
				curNode = curNode->next;
			}
		}
		//printf("Succesfully removed all active commits for the project\n");
		//printf("\n\n\nActive Commits:\n\n\n");
		//printActiveCommits();
		writeToFile(clientfd, "SUCCESS");	
		//printf("The Active Commit Selected is:\nProject: %s\nClientid: %s\nCommit %s\n", commitFound->pName, commitFound->clientid, commitFound->commit);
				
		char* rollbackpath = generatePath(projectName, "/.rollback");
		if(directoryExist(rollbackpath) == 0){
			//printf("The rollback folder did not exist in the server, creating one\n");
			makeDirectory(rollbackpath);
		}
		free(rollbackpath);
		
		int rollbackcreated = generateBackup(projectName);
		if(rollbackcreated == -1){
			printf("Error: Server could not back up the project, sending error to client and stopping with the push\n");
			writeToFile(clientfd, "FAILURE");
			return;
		}
		writeToFile(clientfd, "SUCCESS");
		//printf("Successfully duplicated the project into history folder and created all files, now awaiting the files\n");
		mNode* listOfCommits = NULL;
		getCommit(commitFound->commit, &listOfCommits);
		//printf("The list of commits in order is:\n");
		//printMLL(listOfCommits);
		
		char* manifest = generateManifestPath(projectName);
		int manifestfd = open(manifest, O_RDONLY);
		free(manifest);
		mNode* manifestHead = NULL;
		int manifestversion = readManifest(projectName, manifestfd, -1, &manifestHead);
		close(manifestfd);
		//printf("The server's current manifest:\n");
		//printMLL(manifestHead);
		mNode* curFile = listOfCommits;
		while(curFile != NULL){
			if(strcmp(curFile->version, "D") == 0){
				modifyManifest(projectName, curFile->filepath, 0, NULL);
				remove(curFile->filepath);
				char* directoryPath = dirname(curFile->filepath);
				removeEmptyDirectory(directoryPath, projectName);
			}else{
				int filefd = open(curFile->filepath, O_RDONLY);
				if(filefd == -1){
					char* filepathTEMP = strdup(curFile->filepath);
					char* subdirectories = dirname(filepathTEMP);
					makeNestedDirectories(subdirectories);
					free(filepathTEMP);
				}else{
					close(filefd);
					remove(curFile->filepath);
				}
				int filelength = getLength(clientfd);
				writeToFileFromSocket(clientfd, filelength, curFile->filepath); // updating the file content
				char* replace = NULL;
				if(strcmp(curFile->version, "A") == 0){
					replace = createManifestLine("0", curFile->filepath, curFile->hash, 0, 0);
				}else{
					int version = getFileVersion(manifestHead, curFile->filepath);
					version = version + 1;
					char* newversion = convertIntToString(version);
					replace = createManifestLine(newversion, curFile->filepath, curFile->hash, 0, 0);
					free(newversion);
				}
				modifyManifest(projectName, curFile->filepath, 3, replace);
				free(replace);
			}
			curFile = curFile->next;
		}
		freeMLL(listOfCommits);
		freeMLL(manifestHead);
		//printf("The server has fully updated all the file entries and files of the project\n");
		char* newManifestVersion = convertIntToString(manifestversion + 1);
		//printf("The new manifest version is now: %s\n", newManifestVersion);
		char* manifestVersionLine = malloc(sizeof(char) * (strlen(newManifestVersion) + 2));
		memset(manifestVersionLine, '\0', sizeof(char) * (strlen(newManifestVersion) + 2));
		memcpy(manifestVersionLine, newManifestVersion, strlen(newManifestVersion));
		strcat(manifestVersionLine, "\n");
		free(newManifestVersion);
		//printf("The manifest line entry is now %s", manifestVersionLine);
		modifyManifest(projectName, NULL, 2, manifestVersionLine);
		free(manifestVersionLine);
		//printf("Succesfully updated the Manifest Version\n");
		//printf("Now updating the history file\n");
		updateHistory(projectName, commitFound->commit, (manifestversion + 1));
		writeToFile(clientfd, "SUCCESS");
		//printf("Sending the updated manifest to client to replace\n");
		manifest = generateManifestPath(projectName);
		sendFileBytes(manifest, clientfd);
		free(manifest);
	}else{
		printf("Error: Server was not able to find the commit for this push, sending error to client\n");
		writeToFile(clientfd, "FAILURE");
	}
	freeCommitNode(commitFound);
	//printf("PUSH IS FINISHED\n");
}

void updateHistory(char* projectName, char* commit, int manifestVersion){
	char* historypath = generatePath(projectName, "/.history"); //CHANGE TO HIDDEN LATER
	int historyfd = open(historypath, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
	free(historypath);
	if(historyfd == -1){
		printf("Error: History file was not able to be created\n");
		return;
	}else{
		char* stringVersion = convertIntToString(manifestVersion);
		writeToFile(historyfd, stringVersion);
		writeToFile(historyfd, "\n");
		free(stringVersion);
		writeToFile(historyfd, commit);
		close(historyfd);
	}	
	//printf("Succesfully inserted the commit into the history file\n");
}

void rollbackVersion(int clientfd, char* projectName){
	char* project = generatePath("", projectName);
	char* version = NULL;
	int versionlength = getLength(clientfd);
	readNbytes(clientfd, versionlength, NULL, &version);
	if(directoryExist(project) == 0){
		printf("Error: The Project does not exist for the rollback, sending error to client\n");
		writeToFile(clientfd, "FAILURE");
	}else{
		//printf("Server found the project to rollback, %s\n", project);
		writeToFile(clientfd, "SUCCESS");
		char* versionfilepath = malloc(sizeof(char) * (strlen(version) + strlen(project) + strlen("/.rollback/") + 2)); //CHANGE LATER TO HIDDEN
		memset(versionfilepath, '\0', sizeof(char) * (strlen(version) + strlen(project) + strlen("/.rollback/") + 2));
		strcat(versionfilepath, project);
		strcat(versionfilepath, "/.rollback/");
		strcat(versionfilepath, version);
		int tarfd = open(versionfilepath, O_RDONLY);
		if(tarfd != -1){
			close(tarfd);
			//printf("Server found version rollback folder, %s\n", versionfilepath);
			writeToFile(clientfd, "SUCCESS");
			//printf("Clearing the project folder except for rollback\n");
			directoryTraverse(project, 4, -1);
			//printf("Performing rollback\n");
			rollbackProject(project, versionfilepath);
			//printf("Finished Rollback, now deleting all directories above the version number\n");
			removeGreaterVersions(projectName, atoi(version));
			//printf("Finished deleting all directories above the version number, sending success to client\n");
			writeToFile(clientfd, "SUCCESS");
		}else{
			printf("Error: The version the client wants to rollback does not exist, sending client an error\n");
			writeToFile(clientfd, "FAILURE");
		}
		free(versionfilepath);
	}
	free(project);
	free(version);
}

void removeGreaterVersions(char* projectName, int version){
	char* rollbackfp = generatePath(projectName, "/.rollback");
	DIR* dirPath = opendir(rollbackfp);
	struct dirent* curFile = readdir(dirPath);
	while(curFile != NULL){
		if(strcmp(curFile->d_name, ".") == 0 || strcmp(curFile->d_name, "..") == 0){
			curFile = readdir(dirPath);
			continue;
		}
		if(curFile->d_type == DT_REG){
			//printf("File Found: %s\n", curFile->d_name);
			char* directorypath = pathCreator(rollbackfp, curFile->d_name);
			//printf("File path: %s\n", directorypath);
			int foundVersion = atoi(curFile->d_name);
			if(foundVersion > version){
				remove(directorypath);
			}
			free(directorypath);
		}
		curFile = readdir(dirPath);
	}
	closedir(dirPath);
	free(rollbackfp);
}

int rollbackProject(char* projectPath, char* rollbackfolder){
	//printf("The folder to insert is %s\n", projectPath);
	//printf("The rollback folder is %s\n", rollbackfolder);
	char* systemCall = (char*) malloc(sizeof(char) * (strlen(rollbackfolder) + strlen("tar -xf ") + 1));
	memset(systemCall, '\0', sizeof(char) * (strlen(rollbackfolder) + strlen("tar -xf ") + 1));
	strcat(systemCall, "tar -xf ");
	strcat(systemCall, rollbackfolder);
	int success = system(systemCall);
	free(systemCall);
	return success;
}

int directoryExist(char* directoryPath){
	DIR* directory = opendir(directoryPath);
	if(directory != NULL){ //Directory Exists
		closedir(directory);
		return 1;
	}else{ //Directory does not exist or does not have permission to access
		return 0;
	}
}

int makeDirectory(char* directoryPath){
	int success = mkdir(directoryPath, S_IRWXU);
	if(success == -1){
		printf("Fatal Error: Directory could not be created\n");
		return 0;
	}else{
		//printf("Directory Created\n");
		return 1;
	}
}

int getFileVersion(mNode* manifest, char* filepath){
	mNode* curEntry = manifest;
	while(curEntry != NULL){
		if(strcmp(filepath, curEntry->filepath) == 0){
			//printf("Successfully found the entry in the manifest\n");
			return atoi(curEntry->version);
		}
		curEntry = curEntry->next;
	}
	return -1;
}

char* convertIntToString(int value){
	int length = snprintf(NULL, 0, "%d", value);
	char* stringversion = malloc(sizeof(char) * (length + 1));
	memset(stringversion, '\0', sizeof(char) * (length + 1));
	sprintf(stringversion, "%d", value);
	//printf("The string value of %d is %s\n", value, stringversion);
	return stringversion;
}

void writeToFileFromSocket(int socketfd, int filelength, char* filepath){
	char buffer[101] = {'\0'};
	int read = 0;
	int filefd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC,  S_IRUSR | S_IWUSR);
	if(filefd == -1){
		printf("Error: Could not write to File from the Socket because File did not exist or no permissions\n");
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


int getCommit(char* commit, mNode** head){
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
	int length = strlen(commit);
	while(length != 0){
		if(commit[bufferPos] == '\n'){
			curFile->hash = token;
			(*head) = insertMLL(curFile, (*head));
			numberOfFiles = numberOfFiles + 1;
			curFile = (mNode*) malloc(sizeof(mNode) * 1);
			mode = 0;
			numOfSpace = 0;
			tokenpos = 0;
			defaultSize = 15;
			token = malloc(sizeof(char) * (defaultSize + 1));
			memset(token, '\0', sizeof(char) * (defaultSize + 1));
		}else if(commit[bufferPos] == ' '){
			numOfSpace++;
			if(numOfSpace == 1){
				if(strcmp("D", token) == 0){
					mode = 1;
					curFile->version = token;
				}else if(strcmp("A", token) == 0){
					mode = 2;
					curFile->version = token;
				}else if(strcmp("M", token) == 0){
					mode = 3;
					curFile->version = token;
				}else{
					printf("Warning: The commit file is not properly formatted, %s\n", token);
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
			token[tokenpos] = commit[bufferPos];
			tokenpos++;
		}		
		length = length - 1;
		bufferPos++;
	}
	
	free(curFile);
	free(token);
	return numberOfFiles;
}



int generateBackup(char* projectName){
	char* historyfile = generateRollbackVersionfp(projectName);
	//printf("The rollback filepath is %s\n", historyfile);
	char* projectPath = generatePath("", projectName);
	//printf("The project is %s\n", projectPath);
	char* rollbackfolder = generatePath(projectName, "/.rollback"); //Change later
	//printf("The rollback folder is: %d\n", rollbackfolder);
	char* systemCall = (char*) malloc(sizeof(char) * (strlen(projectPath) + strlen(rollbackfolder) + strlen(historyfile) + strlen("tar -czf  ./ --exclude=''") + 3));
	memset(systemCall, '\0', sizeof(char) * (strlen(projectPath) + strlen(rollbackfolder) + strlen(historyfile) + strlen("tar -czf  ./ --exclude=''") + 3));
	strcat(systemCall, "tar -czf ");
	strcat(systemCall, historyfile);
	strcat(systemCall, " ");
	strcat(systemCall, projectPath);
	strcat(systemCall, " --exclude='");
	strcat(systemCall, rollbackfolder);
	strcat(systemCall, "'");
	//printf("The system call is %s\n", systemCall);
	int success = system(systemCall);
	free(systemCall);
	free(projectPath);
	free(rollbackfolder);
	free(historyfile);
	return success;
}

char* generateRollbackVersionfp(char* projectName){
	char* currentVersion = getManifestVersionString(projectName);
	char* historyVersion = malloc(sizeof(char) * (strlen(currentVersion) + 1 +  strlen("/.rollback/") + 1));
	memset(historyVersion, '\0', sizeof(char) * (strlen(currentVersion) + 1 + strlen("/.rollback/") + 1));
	strcat(historyVersion, "/.rollback/"); // CHANGE LATER
	strcat(historyVersion, currentVersion);
	free(currentVersion);
	char* historyFolder = generatePath(projectName, historyVersion);
	free(historyVersion);
	return historyFolder;
}

void commit(char* projectName, int clientfd){
	char* manifest = generateManifestPath(projectName);
	int fd = open(manifest, O_RDONLY);
	free(manifest);
	if(fd == -1){
		printf("Error: Project's Manifest does not exist or could not open, sending error to client\n");
		writeToFile(clientfd, "FAILURE");
	}else{
		//printf("Project's Manifest found, sending to client\n");
		writeToFile(clientfd, "SUCCESS");
		close(fd);
		sendManifest(projectName, clientfd);
		char* sameVersion = NULL;
		readNbytes(clientfd, strlen("FAILURE"), NULL, &sameVersion);
		if(strcmp(sameVersion, "SUCCESS") == 0){
			free(sameVersion);
			sameVersion = NULL;
			readNbytes(clientfd, strlen("FAILURE"), NULL, &sameVersion);
			if(strcmp(sameVersion, "SUCCESS") == 0){
				free(sameVersion);
				//printf("Client successfully completed commit and is sending the .commit file\n");
				int tokenlength = getLength(clientfd);
				char* clientid = NULL;
				readNbytes(clientfd, tokenlength, NULL, &clientid);
				tokenlength = getLength(clientfd);
				char* commitProjectName = NULL;
				readNbytes(clientfd, tokenlength, NULL, &commitProjectName);
				tokenlength = getLength(clientfd);
				char* commitToken = NULL;
				readNbytes(clientfd, tokenlength, NULL, &commitToken);
				insertCommit(clientid, commitProjectName, commitToken, clientfd);
			}else if(strcmp(sameVersion, "FAILURE") == 0){
				printf("Warning: Client did not successfully create commit, either the client could not create commit or client had an outdated version, succesfully completed commit\n");
				free(sameVersion);
			}else if(strcmp(sameVersion, "UPDATED") == 0){
				printf("Warning: The Client and Server have no file entries in their Manifest or there were no new changes, succesfully completed commit\n");
				free(sameVersion);
			}else{
				printf("Error: Not sure what the Client send %s\n", sameVersion);
				free(sameVersion);
			}
		}else if(strcmp(sameVersion, "FAILURE") == 0){
			printf("Warning: The Client had a different manifest version than the server, successfully completed the commit\n");
			free(sameVersion);
		}else{
			printf("Error: Not sure what the Client Sent %s\n", sameVersion);
			free(sameVersion);
		}
	}
	//printf("Finished commit\n");
}

void insertCommit(char* clientid, char* projectName, char* commit, int clientfd){
	commitNode* newNode = malloc(sizeof(commitNode) * 1);
	//printf("Clientid: %s", clientid);
	
	newNode->clientid = clientid;
	newNode->commit = commit;
	newNode->pName = projectName;
	newNode->next = NULL;
	newNode->prev = NULL;
	int found = 0;
	if(activeCommit == NULL){
		activeCommit = newNode;
	}else{
		commitNode* temp = activeCommit;
		commitNode* prevNode = temp;
		while(temp != NULL){
			prevNode = temp;
			if(strcmp(temp->pName, newNode->pName) == 0 && strcmp(temp->clientid, newNode->clientid) == 0){
				
				//printf("Found a duplicate commit for the same project for the same client, replacing\n");
				if(temp->prev != NULL){
					temp->prev->next = newNode;
					newNode->prev = temp->prev;
				}
				if(temp->next != NULL){
					temp->next->prev = newNode;
					newNode->next = temp->next;
				}
				if(temp == activeCommit){
					activeCommit = newNode;
				}
				freeCommitNode(temp);
				found = 1;
				break;
			}
			temp = temp->next;
		}
		if(found == 0){
			prevNode->next = newNode;
			newNode->prev = prevNode;
		}
	}
	writeToFile(clientfd, "SUCCESS");
	//printf("Successfully stored: %s\nbelongs to %s for %s\n", newNode->commit, newNode->clientid, newNode->pName);
	//printf("\n\n\nActive Commits:\n\n\n");
	//printActiveCommits();
}

void printActiveCommits(){
	commitNode* curNode = activeCommit;
	while(curNode != NULL){
		printf("Currently stored: %s\nbelongs to client%s for Project %s\n", curNode->commit, curNode->clientid, curNode->pName);
		curNode = curNode->next;
	}
}

void freeCommitLL(commitNode* head){
	while(head != NULL){
		commitNode* temp = head;
		head = head->next;
		freeCommitNode(temp);	
	}
}

void freeCommitNode(commitNode* node){
	free(node->clientid);
	free(node->pName);
	free(node->commit);
	free(node);
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

void printMLL(mNode* head){
	mNode* temp = head;
	while(temp != NULL){
		printf("Version:%s\tFilepath:%s\tHashcode:%s\n", temp->version, temp->filepath, temp->hash);
		temp = temp->next;
	}
}

void sendFileBytes(char* filepath, int socketfd){
	long long fileBytes = calculateFileBytes(filepath);
	char filebytes[256] = {'\0'};
	sprintf(filebytes, "%lld", fileBytes);
	writeToFile(socketfd, filebytes);
	writeToFile(socketfd, "$");
	sendFile(socketfd, filepath);
}


void upgrade(char* projectName, int clientfd, int numOfFiles){
	char buffer[2] = {'\0'}; 
	int defaultSize = 15;
	char* token = malloc(sizeof(char) * (defaultSize + 1));
	memset(token, '\0', sizeof(char) * (defaultSize + 1));
	int read = 0;
	int bufferPos = 0;
	int tokenpos = 0;
	int fileLength = 0;
	int filesRead = 0;
	listOfFiles = NULL;
	do{ 
		read = bufferFill(clientfd, buffer, 1);
		if(buffer[0] == '$'){

			char* temp = NULL;
			fileLength = atoi(token);
			free(token);
			readNbytes(clientfd, fileLength, NULL, &temp);
			
			char* name = (char*) malloc(sizeof(char) * strlen(basename(temp)) + 1);
			memset(name, '\0', sizeof(char) * strlen(basename(temp)) + 1);
			memcpy(name, basename(temp), strlen(basename(temp)));

			fileNode* file = createFileNode(temp, name);
			insertLL(file);
			
			filesRead++;
			
			if(numOfFiles == filesRead){
				fileNode* temp = listOfFiles;
				while(temp != NULL){
					//printf("File: %s\n", temp->filename);
					temp = temp->next;
				}
				break;
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
			token[tokenpos] = buffer[0];
			tokenpos++;
		}
	}while(read != 0 && buffer[0] != '\0');
	sendFilesToClient(clientfd, listOfFiles, numOfFiles);
	char* manifest = generateManifestPath(projectName);
	int manifestfd = open(manifest, O_RDONLY);
	if(manifestfd != -1){
		writeToFile(clientfd, "SUCCESS");
		//printf("Successfully Located the Project's Manifest for upgrade\n");
		close(manifestfd);
		sendFileBytes(manifest, clientfd);
	}else{
		printf("Error: Failed to Locate the Project's Manifest for upgrade\n");
		writeToFile(clientfd, "FAILURE");
	}
	free(manifest);
}

/*
	Mode 0: Remove
	Mode 1: Add
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
		printf("Error: The Manifest does not exist!\n");
		return;
	}
	int tempmanifestfd = open(manifestTemp, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if(tempmanifestfd == -1){
		printf("Error: The temporarily Manifest File already exists somehow\n");
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
			printf("Warning: The file is not listed in the Manifest to remove\n");
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


void createManifest(int fd, char* directorypath){
	writeToFile(fd, "0");
	writeToFile(fd, "\n");
	directoryTraverse(directorypath, 0, fd);
}

void sendManifest(char* projectName, int clientfd){
	char* manifest = generateManifestPath(projectName);
	fileNode* manifestNode = createFileNode(manifest, strdup(".Manifest"));
	insertLL(manifestNode);
	sendFilesToClient(clientfd, manifestNode, 1);
}

char* generateManifestPath(char* projectName){
	char* manifest = malloc(sizeof(char) * strlen(projectName) + 3 + strlen("/.Manifest"));
	memset(manifest, '\0', sizeof(char) * strlen(projectName) + 3 + strlen("/.Manifest"));
	manifest[0] = '.';
	manifest[1] = '/';
	memcpy(manifest + 2, projectName, strlen(projectName));
	strcat(manifest, "/.Manifest");
	return manifest;
}

void getManifestVersion(char* projectName, int clientfd){
	char* manifest = generateManifestPath(projectName);
	int manifestfd = open(manifest, O_RDONLY);
	if(manifestfd == -1){
		printf("Error: Server does not contain the project or the Manifest is missing or no permissions, sending error to client\n");
		writeToFile(clientfd, "FAILURE");
	}else{
		writeToFile(clientfd, "SUCCESS");
		char buffer[100] = {'\0'};
    	int defaultSize = 25;
    	char *token = malloc(sizeof(char) * (defaultSize + 1));
    	memset(token, '\0', sizeof(char) * (defaultSize + 1));
    	int tokenpos = 0;
    	int bufferPos = 0;
    	int read = 0;
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
					sendLength(clientfd, token);
					read = 0;
					break;
				}
    		}
    	}while(read != 0 && buffer[0] != '\0');
    	free(token);
		close(manifestfd);
	}	
	free(manifest);
}

void sendLength(int socketfd, char* token){
	char str[5] = {'\0'};
	sprintf(str, "%lu", strlen(token));
	writeToFile(socketfd, str);
	writeToFile(socketfd, "$");
	writeToFile(socketfd, token);
}

char* generatePath(char* projectName, char* pathToAppend){
	char* filepath = malloc(sizeof(char) * (strlen(projectName) + strlen(pathToAppend) + 3));
	memset(filepath, '\0', (sizeof(char) * (strlen(projectName) + 3 + strlen(pathToAppend))));
	filepath[0] = '.';
	filepath[1] = '/';
	memcpy(filepath + 2, projectName, strlen(projectName));
	strcat(filepath, pathToAppend);
	return filepath;
}

/*
	Mode: 0 Read the Manifest and Record all the files as file objects and then read all the files. (Discard file version and hashcode)
*/
void readManifestFiles(char* projectName, int mode, int clientfd){
	char* manifest = generateManifestPath(projectName);
	numOfFiles = 0;
	int manifestfd = open(manifest, O_RDONLY);
	if(manifestfd == -1){
		printf("Error: Server does not contain the project or the Manifest is missing or no permissions, sending error to client\n");
		writeToFile(clientfd, "FAILURE");
	}else{
		writeToFile(clientfd, "SUCCESS");
		//printf("Searching the Manifest for the list of all files\n");
		numOfFiles++;
		fileNode* ManifestNode = createFileNode(manifest, strdup(".Manifest"));
		insertLL(ManifestNode);
		
		char buffer[100] = {'\0'};
    	int defaultSize = 25;
    	char *token = malloc(sizeof(char) * (defaultSize + 1));
    	memset(token, '\0', sizeof(char) * (defaultSize + 1));
    
    	int read = 0;
    	int tokenpos = 0;
    	int bufferPos = 0;
    	int numOfSpaces = 0;
    
    	int manifestVersion = 0;
    
    	do{
        read = bufferFill(manifestfd, buffer, sizeof(buffer));
        for(bufferPos = 0; bufferPos < (sizeof(buffer)/sizeof(buffer[0])); ++bufferPos){
			  if(buffer[bufferPos] == '\n'){ 
		     		if(manifestVersion == 0){ //This is for version of the Manifest 
		     			manifestVersion = 1; 
		     		}else{
		     			defaultSize = 25;
    					token = malloc(sizeof(char) * (defaultSize + 1));
   				 	memset(token, '\0', sizeof(char) * (defaultSize + 1));
   				 	tokenpos = 0;
		     			numOfSpaces = 0;
		     		}
		     }else if(buffer[bufferPos] == ' '){
		    		numOfSpaces++;
		    		if(numOfSpaces == 2){
		    			char* temp = strdup(token);
		    			char* filename = strdup(basename(temp));
		    			//printf("The filepath is:%s\n",token);
		    			fileNode* newFile = createFileNode(token, filename);
		    			insertLL(newFile);
		    			numOfFiles++;
		    			free(temp);
		    		}
			  }else{
				if(manifestVersion != 0 && numOfSpaces == 1){
		    		if(tokenpos >= defaultSize){
						defaultSize = defaultSize * 2;
						token = doubleStringSize(token, defaultSize);
					}
					token[tokenpos] = buffer[bufferPos];
					tokenpos++;
		    	}
		    }
        }
		}while (buffer[0] != '\0' && read != 0);
		free(token);
		close(manifestfd);
		//printf("The List of files in project is:\n");
	}
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


char* getManifestVersionString(char* projectName){
	char* manifest = generateManifestPath(projectName);
	int manifestfd = open(manifest, O_RDONLY);
	free(manifest);
	if(manifestfd == -1){
		printf("Error: Project's Manifest does not exist\n");
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
	The Client will recieve 
		SUCCESS if the Manifest is found
		FAILURE if the Manifest is not found
	If SUCCESS: the server will create a long token that is the format of 
		NumberOfBytesOfToken$FileVersion\sFilepath\n
		Note the pattern will repeat for all files in the project and it will be one long continous token
*/
void getProjectVersion(char* directoryName, int clientfd) {
    //printf("Attempting to get project Version\n");
    char* manifest = generateManifestPath(directoryName);
    int fdManifest = open(manifest, O_RDONLY);
    free(manifest);
    if(fdManifest == -1){
    	printf("Error: Project's Manifest Requested does not exist or does not have permissions to access on the server side, sending error to client\n");
    	writeToFile(clientfd, "FAILURE");
    	return;
    }else{
    	//printf("Successfully found the project requested\n");
    	writeToFile(clientfd, "SUCCESS");
    }
    char buffer[50] = {'\0'};
    int defaultSize = 25;
    char *token = malloc(sizeof(char) * (defaultSize + 1));
    memset(token, '\0', sizeof(char) * (defaultSize + 1));
    
    int read = 0;
    int tokenpos = 0;
    int bufferPos = 0;
    int numOfSpaces = 0;
    
    int manifestVersion = 0;
    
    do {
        read = bufferFill(fdManifest, buffer, sizeof(buffer));
        for(bufferPos = 0; bufferPos < (sizeof(buffer)/sizeof(buffer[0])); ++bufferPos){
		     if(buffer[bufferPos] == '\n') { 
		     		if(manifestVersion == 0){ //This is for version of the Manifest (we don't want to send this to client)
		     			manifestVersion = 1; 
		     			free(token);
		     			
		     			tokenpos = 0;
		     			defaultSize = 25;
		     			token = malloc(sizeof(char) * (defaultSize + 1));
   					memset(token, '\0', sizeof(char) * (defaultSize + 1));
		     		}else{
		     			if(tokenpos >= defaultSize){ 
							defaultSize = defaultSize * 2;
							token = doubleStringSize(token, defaultSize);
						}
						token[tokenpos] = buffer[bufferPos];
						tokenpos++;
						numOfSpaces = 0;
		     		}
		     }else{
		     		if(buffer[bufferPos] == ' '){
		     			numOfSpaces++;
		     		}
		     		if(numOfSpaces < 2){
		     			if(tokenpos >= defaultSize){
							defaultSize = defaultSize * 2;
							token = doubleStringSize(token, defaultSize);
						}
						token[tokenpos] = buffer[bufferPos];
						tokenpos++;
		     		}
		     }
        }
    }while (buffer[0] != '\0' && read != 0);
    if(token[0] == '\0'){
    	printf("Warning: The project has no files\n");
    }
    close(fdManifest);
    //printf("Sending the Client: %s\n", token);
    writeToFile(clientfd, "output$");
	 char str[3] = {'\0'};
	 sprintf(str, "%d", strlen(token));
	 writeToFile(clientfd, str);
	 writeToFile(clientfd, "$");
	 writeToFile(clientfd, token);
    free(token);
}

/*
Purpose: Setup the metadata of files
Format: numOfFiles$lengthofpathname$pathname$filesize
*/
void setupFileMetadata(int clientfd, fileNode* files, int numOfFiles){
	fileNode* file = files;
	char str[5] = {'\0'};
	sprintf(str, "%d", numOfFiles);
	writeToFile(clientfd, str);
	writeToFile(clientfd, "$");
	while(file != NULL){
		memset(str, '\0', sizeof(char) * 5);
		sprintf(str, "%lu", strlen(file->filepath));
		writeToFile(clientfd, str);
		writeToFile(clientfd, "$");
		writeToFile(clientfd, file->filepath);
		char filebytes[256] = {'\0'};
		sprintf(filebytes, "%lld", file->filelength);
		writeToFile(clientfd, filebytes);
		writeToFile(clientfd, "$");
		file = file->next;
	}
}

/*
Purpose: Send files to the Client
files must be a linked list of fileNodes
*/
void sendFilesToClient(int clientfd, fileNode* files, int numOfFiles){
	writeToFile(clientfd, "sendFile$");
	setupFileMetadata(clientfd, files, numOfFiles);
	fileNode* file = files;
	while(file != NULL){
		//printf("File path: %s\n", file->filepath);
		if(file->filepath[0] == '.' && file->filepath[1] == '/'){
			sendFile(clientfd, file->filepath);
		}else{
			char* temp = (char*) malloc(sizeof(char) * strlen(file->filepath) + 3);
			memset(temp, '\0', sizeof(char) * strlen(file->filepath) + 3);
			temp[0] = '.';
			temp[1] = '/';
			memcpy(temp+2, file->filepath, strlen(file->filepath));
			//printf("The new path is %s\n", temp);
			sendFile(clientfd, temp);
			free(temp);
		}
		
		file = file->next;
	}
}

/*
Purpose: Send one file to the Client
*/
void sendFile(int clientfd, char* filepath){
	int filefd = open(filepath, O_RDONLY);
	int read = 0;
	if(filefd == -1){
		printf("Fatal Error: Could not send file because file did not exist\n");
		return;
	}
	char buffer[101] = {'\0'}; // Buffer is sized to 101 because we need a null terminated char array for writeToFile method since it performs a strlen
	do{
		read = bufferFill(filefd, buffer, (sizeof(buffer) - 1)); 
		//printf("The buffer has %s\n", buffer);
		writeToFile(clientfd, buffer);
	}while(buffer[0] != '\0' && read != 0);
	//printf("Finished sending file: %s to client\n", filepath);
	close(filefd);
}

/*
Mode 0: Traverse through and add to manifest (the fd)
Mode 1: Traverse through the directories and create fileNodes (to be implemented)
Mode 2: Traverse through the directories and for each file, send to Client (Note* you should run MODE 1 to setup the Metadata of all these files) (to be implemented)
Mode 3: Traverse through the directories and delete all directories and files
Mode 4: Traverse through all the directories and delete all files and directories except for the "rollback" folder
*/
int directoryTraverse(char* path, int mode, int fd){ 
	DIR* dirPath = opendir(path);
	if(!dirPath){
		printf("Error: Directory path does not exist or no valid permissions!\n");
		return -1;
	}
	struct dirent* curFile = readdir(dirPath);
	while(curFile != NULL){
		if(strcmp(curFile->d_name, ".") == 0 || strcmp(curFile->d_name, "..") == 0){
			curFile = readdir(dirPath);
			continue;
		}
		if(curFile->d_type == DT_REG){
			//printf("File Found: %s\n", curFile->d_name);
			char* filepath = pathCreator(path, curFile->d_name);
			//printf("File path: %s\n", filepath);
			if(mode == 0 && strcmp(curFile->d_name, ".Manifest") != 0){
				char* hashcode = generateHashCode(filepath);
				char* temp = createManifestLine("1", filepath, hashcode, 0, 0);
				writeToFile(fd, temp);
				free(temp);
				free(hashcode);
			}else if(mode == 1 && strcmp(curFile->d_name, ".Manifest") != 0){
				
			}else if(mode == 3 || mode == 4){
				int success = remove(filepath);
				if(success == -1){
					printf("Error: File %s could not be deleted\n", filepath);
				}
			}
			free(filepath);
		}else if(curFile->d_type == DT_DIR){
			char* directorypath = pathCreator(path, curFile->d_name);
			if(mode == 3){
				directoryTraverse(directorypath, mode, fd);
				remove(directorypath);
			}else if(mode == 4){
				if(strcmp(curFile->d_name, ".rollback") != 0){ //CHANGE LATER
					directoryTraverse(directorypath, mode, fd);
					int success = remove(directorypath);
					if(success == -1){
						printf("Error: Directory %s could not be deleted\n", directorypath);
					}
				}
			}else{
				directoryTraverse(directorypath, mode, fd);
			}
			free(directorypath);
		}else{
			
		}
		curFile = readdir(dirPath);
	}
	closedir(dirPath);
	return 1;
}


char* generateHashCode(char* filepath){
	int filefd = open(filepath, O_RDONLY);
	if(filefd == -1){
		//printf("Error: File does not exist to generate a hashcode\n");
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
		//printf("The hash for this file is: ");
		//for(i = 0; i < MD5_DIGEST_LENGTH; i++){
		//	printf("%02x", (unsigned char) hash[i]);
		//}
		//printf("\n");
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

void writeToFile(int fd, char* data){
	int bytesToWrite = strlen(data);
	int bytesWritten = 0;
	int status = 0;
	while(bytesWritten < bytesToWrite){
		status = write(fd, (data + bytesWritten), (bytesToWrite - bytesWritten));
		if(status == -1){
			printf("Warning: write encountered an error\n");
			return;
		}
		bytesWritten += status;
	}
}

int bufferFill(int fd, char* buffer, int bytesToRead){
    int position = 0;
    int bytesRead = 0;
    int status = 0;
    memset(buffer, '\0', bytesToRead);
    do{
        status = read(fd, (buffer + bytesRead), bytesToRead-bytesRead);
        if(status == 0){
            //printf("Finished reading the File or Buffer is filled\n");
            break;
        }else if(status == -1){
            printf("Warning: Error when reading the file reading\n");
            return bytesRead;
        }
        bytesRead += status;
    }while(bytesRead < bytesToRead);
    	return bytesRead;
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

fileNode* createFileNode(char* filepath, char* filename){
	fileNode* newNode = (fileNode*) malloc(sizeof(fileNode) * 1);
	newNode->filepath = filepath;
	newNode->filename = filename;
	newNode->filelength = calculateFileBytes(filepath);
	newNode->next = NULL;
	newNode->prev = NULL;
	return newNode;
}


/*
	Given a path: it will generate all subdirectories for the given path (make sure the path ends with the directory and not a file)
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

void removeEmptyDirectory(char* path, char* projectName){
	char* parentdirectory = strdup(path);
	char* directory = strdup(path);
	char* directoryName = basename(directory);
	char* parentdirectoryName = dirname(parentdirectory);
	//printf("The directory path is %s\n", path);
	if(strcmp(directoryName, projectName) != 0 && strcmp(path, ".") != 0 && strlen(path) != 0){
		int success = remove(path);
		if(success != -1){
			//printf("Succesfully removed: %s\n", path);
			removeEmptyDirectory(parentdirectoryName, projectName);
		}
	}
	free(directory);
	free(parentdirectory);
}

char* pathCreator(char* path, char* name){
	char* newpath = (char *) malloc(sizeof(char) * (strlen(path) + strlen(name) + 2));
	memcpy(newpath, path, strlen(path));
	newpath[strlen(path)] = '/';
	memcpy((newpath + strlen(path) + 1), name, (strlen(name)));
	newpath[strlen(name) + strlen(path) + 1] = '\0';
	return newpath;
}

char* doubleStringSize(char* word, int newsize){
	char* expanded =  (char*) malloc(sizeof(char) * (newsize + 1));
	memset(expanded, '\0', (newsize+1) * sizeof(char));
	memcpy(expanded, word, strlen(word));
	free(word);
	return expanded;
}

int setupServer(int socketfd, int port){
	struct sockaddr_in serverinfo;
	bzero((char*)&serverinfo, sizeof(serverinfo));
	serverinfo.sin_family = AF_INET;
	serverinfo.sin_port = htons(port);
	serverinfo.sin_addr.s_addr = INADDR_ANY;
	int success = bind(socketfd, (struct sockaddr*) &serverinfo, sizeof(struct sockaddr_in));
	if(success == -1){
		return -1;
	}else{
		listen(socketfd, 10);
		return 1;
	}
}

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


void sighandler(int sig){
	exit(0);
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

void unloadMemory(){
	printf("Server is shutting down: Deallocing structures, closing file descriptors, joining all threads\n");
	while(listOfFiles != NULL){
		free(listOfFiles->filename);
		free(listOfFiles->filepath);
		fileNode* temp = listOfFiles;
		listOfFiles = listOfFiles->next;
		free(temp);
	}
	freeCommitLL(activeCommit);
	while(pthreadHead != NULL){
		pthread_join(pthreadHead->thread, NULL);
		userNode* temp = pthreadHead;
		pthreadHead = pthreadHead->next;
		free(temp);
	}
}
