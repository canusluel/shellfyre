#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "my_module_variables.h"

#define maxCommandSize 1024
#define maxFolderCharSize 256
#define maxSearchLength 128
#define maxPokeLength 64
#define pathLen 150
#define maxHistory 100
#define BUFFER_SIZE 25
#define READ_END	0
#define WRITE_END	1

static int victories, defeats, ties=0, recDirOpened = 0, modInstalled = 0;
static char currentFilePath[pathLen];
const char *sysname = "shellfyre", *fileName = "/recentDirectories.txt";

/** Project 1 shellfyre by
  Can Usluel (72754) and Halil Doruk Yıldırım (72298)
 **/


enum return_codes
{
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
	}
	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	// FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;

	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main()
{
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;


		code = process_command(command);
		if (code == EXIT)
			break;

		free_command(command);
	}

	printf("\n");
	return 0;
}
void executeFilesearch(struct command_t *command, char *starterDirectory, char *searchedString){
	//Getting current directory

	DIR *currentDirectory;
	struct dirent *dir;
	//Creating a 2D array to store subfolders' names
	char subFolders[maxFolderCharSize][maxFolderCharSize];
	int i = 0;

	currentDirectory = opendir(starterDirectory);

	if (currentDirectory) {
		//traversing current directory
		while ((dir = readdir(currentDirectory)) != NULL) {
			//adding subfolders to the array if its not the previous folder(..), 
			//the folder itself(.) or .git folder
			if(dir->d_type == 4 && !(strstr(dir->d_name, "."))){
				strcpy(subFolders[i], dir->d_name);
				i++;
			}

			//displaying the file if that file contains the given substring
			if(strstr(dir->d_name, searchedString)){
				printf("%s%s\n", starterDirectory, dir->d_name);

				//opening the file with the user's preferred application
				if(command->args[1] != NULL){
					if((strcmp(command->args[0], "-o") == 0 ||strcmp(command->args[1], "-o") == 0) &&
							(dir->d_type == 8) && strstr(dir->d_name, ".")){
						char openedFile[maxSearchLength];
						strcpy(openedFile, "xdg-open ");
						strcat(openedFile, starterDirectory);
						strcat(openedFile, dir->d_name);
						system(openedFile);
					}
				}
			}
		}
		if(command->args[1] != NULL){
			if(strcmp(command->args[0], "-r") == 0 || strcmp(command->args[1], "-r") == 0){
				int j=0;
				while(j != i){
					//setting starter directory for recursive calls
					char bufferStarterDirectory[maxFolderCharSize];
					strcpy(bufferStarterDirectory, starterDirectory);
					strcat(bufferStarterDirectory, subFolders[j]);
					strcat(bufferStarterDirectory, "/");
					executeFilesearch(command, bufferStarterDirectory, searchedString);
					j++;
				}
			}
		}
		closedir(currentDirectory);
	}

}
void initializeFilePath(){
	//this function should be called when first cd or take is executed before changing directories
	//it initializes the path of the file that records of visited directories are kept
	char currentDirectory[128];
	getcwd(currentDirectory, sizeof(currentDirectory));
	strcpy(currentFilePath, currentDirectory);
	strcat(currentFilePath, fileName);
	recDirOpened = 1;
}
void updateRecentDirectories(){
	//this function updates the visited directories whenever cd, cdh or take is called
	//appends the most recent directory to the txt file
	char currentDirectory[128];

	getcwd(currentDirectory, sizeof(currentDirectory));

	FILE *fp = fopen(currentFilePath, "a");

	if(fp){
		//saving current working directory 	
		fprintf(fp, "%s\n", currentDirectory);

	}else{

		printf("Cannot open the file");
		exit(-1);

	}

	fclose(fp);

}

void executeCdh(){
	char write_msg[BUFFER_SIZE];
	char read_msg[BUFFER_SIZE];
	pid_t pid;
	int fd[2];
	char visitHistory[maxHistory][maxFolderCharSize];
	int size = 0;
	char buffer[maxFolderCharSize];
	FILE *fp;

	//creating pipe to transfer user choice to parrent
	if (pipe(fd) == -1) {
		fprintf(stderr,"Pipe failed");
	}

	if(recDirOpened == 0){
		fp = fopen("recentDirectories.txt", "r");
		initializeFilePath();
	}else{
		fp = fopen(currentFilePath, "r");
	}

	if(!fp){

		printf("No history\n");

	}else{
		//traversing visit history and recording them to a 2D array
		while (fgets(buffer, maxFolderCharSize, fp)){
			strcpy(visitHistory[size], buffer);
			size++;
		}
		int choice;
		int index = size - 10;
		int listNumber = 10;
		char listLetter = 'j';
		if(size < 10){
			index = 0;
			listNumber = size;
			listLetter -= 10 - size;

		}
		//printing history for user to select
		for(index; index<size; index++){
			printf("%c  %d) %s\n", listLetter, listNumber, visitHistory[index]);
			listNumber--;
			listLetter--;
		}

		//creating a child procces to execute scanf, without a child procces gives segfault	
		pid = fork();
		if(pid == 0){//child

			printf("Select directory by letter or by number: ");
			scanf("%s", write_msg);

			close(fd[READ_END]);
			write(fd[WRITE_END], write_msg, strlen(write_msg)+1); 
			close(fd[WRITE_END]);
			fclose(fp);
			exit(0);

		}else{//parent

			close(fd[WRITE_END]);
			read(fd[READ_END], read_msg, BUFFER_SIZE);
			close(fd[READ_END]);

			choice = atoi(read_msg);
			if(choice == 0){
				//if user choices letter instead of number
				choice = (int)read_msg[0] - 96;

			}

			if(choice <= 10){

				//removing \n at the end of the line
				visitHistory[size - choice][strlen(visitHistory[size - choice]) - 1] = 0;
				//switching to the selected directory
				chdir(visitHistory[size - choice]);
				updateRecentDirectories();

			}else{

				printf("Invalid choice\n");

			}

		}
		fclose(fp);
	}
}


void executeTake(struct command_t *command){

	char *input = command->args[0];
	char *token;
	token= strtok(input,"/");

	while(token != NULL){
		DIR* dir = opendir(token);
		if(ENOENT == errno) {
			mkdir(token, 0777);
		} 
		chdir(token);
		token= strtok(NULL, "/");
		if(recDirOpened == 0) initializeFilePath();
		updateRecentDirectories();
	}
}


//The notify-send usage with crontab was adapted from stackoverflow, provided by the TA on the discussion board
void executeJoker(){

	FILE *fp;

	fp= fopen("cron.txt","w");
	fprintf(fp, " */15 * * * *  XDG_RUNTIME_DIR=/run/user/$(id -u) notify-send -i face-laugh \"$(curl -s https://icanhazdadjoke.com/)\" \n" );
	fclose(fp);

	char command[200];
	strcpy(command, "crontab cron.txt");
	system(command);

}

/* 
   This command displays the pixel art of given pokemon by executing curl command.
   Only 134 pokemons on the txt file are drawable.
   */
int executePokemon(struct command_t *command){

	char *filename = "pokemons.txt";
	FILE *fp = fopen(filename, "r");
	bool pokemonFound = false;
	if (fp == NULL){
		printf("Error: could not open file %s", filename);
		return 1;
	}

	char buffer[maxPokeLength];
	char searchedItem[maxPokeLength];
	strcpy(searchedItem, command->args[0]);
	strcat(searchedItem, "\n");

	char artUrl[maxSearchLength];
	char curlCommand[maxCommandSize];
	strcpy(curlCommand, "curl ");
	strcpy(artUrl, "http://www.fiikus.net/asciiart/pokemon/");

	while (fgets(buffer, maxPokeLength, fp) && !pokemonFound){

		if(strcasecmp(searchedItem, buffer+5) == 0){
			strncat(artUrl, &buffer[1], 1);
			strncat(artUrl, &buffer[2], 1);
			strncat(artUrl, &buffer[3], 1);
			strcat(artUrl, ".txt");
			strcat(curlCommand, artUrl);
			system(curlCommand);
			printf("\n");
			pokemonFound = true; 
		}

	}
	if(!pokemonFound) printf("Pokemon not found\n");
	fclose(fp);
	return 0;
}

/*
   This command randomly picks rock paper scissors and plays with the user, then displays the # of wins, losses and ties.
   Takes arguments "rock", "paper" or "scissors", case insensitive.
   */
void executeRps(struct command_t *command){
	int random = rand() % 3;
	printf("Here we go! 3\n");
	sleep(1);
	printf("2\n");
	sleep(1);
	printf("1...\n");
	sleep(1);
	if(random==0){
		printf("SHOOT!\n"
				"    _______\n"
				"---'   ____)\n"
				"      (_____)\n"
				"      (_____)\n"
				"      (____)\n"
				"---.__(___)\n"
				"\n");
		if(strcasecmp("rock", command->args[0]) == 0){
			printf("Rock? We tied.\n");
			ties++; }
		else if(strcasecmp("paper", command->args[0]) == 0){
			printf("Paper? You win...\n");
			victories++; }
		else if(strcasecmp("scissors", command->args[0]) == 0){
			printf("Scissors? Smashed you!\n");
			defeats++; }
		else{ printf("You gave an incorrect argument! Try rock, paper or scissors!\n"); }
		printf("Times you won: %d\nTimes I won: %d\nTies: %d\n", victories, defeats, ties);
	}
	else if(random==1){
		printf("SHOOT!\n"
				"     _______\n"
				"---'    ____)____\n"
				"           ______)\n"
				"          _______)\n"
				"         _______)\n"
				"---.__________)\n"
				"\n");       
		if(strcasecmp("rock", command->args[0]) == 0){
			printf("Rock? I won!\n");
			defeats++; }
		else if(strcasecmp("paper", command->args[0]) == 0){
			printf("Paper? Tied.\n");
			ties++; }
		else if(strcasecmp("scissors", command->args[0]) == 0){
			printf("Scissors? Damn, you won.\n");
			victories++; }
		else{ printf("You gave an incorrect argument! Try rock, paper or scissors!\n"); }
		printf("Times you won: %d\nTimes I won: %d\nTies: %d\n", victories, defeats, ties);
	}
	else if(random==2){
		printf("SHOOT!\n"
				"    _______"
				"---'   ____)____\n"
				"          ______)\n"
				"       __________)\n"
				"      (____)\n"
				"---.__(___)\n"
				"\n");
		if(strcasecmp("rock", command->args[0]) == 0){
			printf("Rock? Dang, you won...\n");
			victories++; }
		else if(strcasecmp("paper", command->args[0]) == 0){
			printf("Paper? Hah, sliced you!\n");
			defeats++; }
		else if(strcasecmp("scissors", command->args[0]) == 0){
			printf("Scissors? Tied.\n");
			ties++; }
		else{ printf("You gave an incorrect argument! Try rock, paper or scissors!\n"); }
		printf("Times you won: %d\nTimes I won: %d\nTies: %d\n", victories, defeats, ties);
	}

}

void executePstraverse(struct command_t *command){

	int check;
	int fd;
	char *parameters1[] = {"/usr/bin/sudo", "/usr/sbin/insmod", "./my_module.ko", NULL};
	char *parameters2[] = {"/usr/bin/sudo", "/usr/bin/chmod", "777", "/dev/my_device", NULL};

	//if mod is uninstalled, installs it in the first call
	if(modInstalled == 0){
		pid_t pid = fork();
		if(pid == 0){//child
			execv(parameters1[0], parameters1);
			exit(0);
		}else if(pid > 0){//parent
			wait(NULL);
			pid_t pid = fork();

			if(pid == 0){//child
				execv(parameters2[0], parameters2);
				exit(0);
			}else if(pid > 0){//parent
				wait(NULL);
			}

		}
		modInstalled = 1;	
	}
	fd = open("/dev/my_device", O_RDWR);
	if(fd < 0){
		printf("Failed to open device, errno = %d %s\n",errno,strerror(errno));
		exit(-1);
	}
	//writing the given PID to the device
	check = ioctl(fd, WRITE_VAL, &command->args);
	if (check == -1){
		printf("Failed to execute ioctl, errno = %d %s\n", errno, strerror(errno));
		exit(-1);
	}

	close(fd);
}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0){
		//if a kernel module is installed before, deletes it before exiting from shell
		if(modInstalled == 1){
			char *parameters3[] = {"/usr/bin/sudo", "/usr/sbin/rmmod", "./my_module.ko", NULL};
			pid_t pid = fork();
			if(pid == 0){
				execv(parameters3[0], parameters3);
				exit(0);
			}else if(pid > 0){
				wait(NULL);
			}

		}
		return EXIT;
	}
	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{	
			if(recDirOpened == 0) initializeFilePath();
			r = chdir(command->args[0]);
			updateRecentDirectories();
			if (r == -1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}

	// TODO: Implement your custom commands here
	if (strcmp(command->name, "filesearch") == 0){
		int count = command->arg_count;
		if(count > 0){
			executeFilesearch(command, "./", command->args[count-1]);
		}else{
			printf("-%s: %s: Insufficient arguments\n", sysname, command->name);
		}
		return SUCCESS;
	}

	if(strcmp(command->name, "cdh") == 0) {
		if(command->arg_count==0){ executeCdh(); }
		else{ printf("-%s: %s: Insufficient arguments\n", sysname, command->name); }
		return SUCCESS;
	}

	if(strcmp(command->name, "take") == 0) {
		if(command->arg_count==1) { executeTake(command); }
		else{ printf("-%s: %s: Insufficient arguments\n", sysname, command->name); }
		return SUCCESS;
	}

	if(strcmp(command->name, "joker") == 0) {
		if(command->arg_count==0) { executeJoker(); }
		else{ printf("-%s: %s: Insufficient arguments\n", sysname, command->name); }
		return SUCCESS;
	}
	if (strcmp(command->name, "pokemon") == 0){
		if(command->arg_count > 0){
			executePokemon(command);
		}else{
			printf("-%s: %s: Insufficient arguments\n", sysname, command->name);
		}
		return SUCCESS;
	}


	if(strcmp(command->name, "rps") == 0) {
		if(command->arg_count > 0) {
			executeRps(command); 
		}
		else{ 
			printf("-%s: %s: Insufficient arguments\n", sysname, command->name); 
		}
		return SUCCESS;
	}

	if(strcmp(command->name, "pstraverse") == 0) {
		if(command->arg_count > 0) {
			executePstraverse(command); 
		}
		else{ 
			printf("-%s: %s: Insufficient arguments\n", sysname, command->name); 
		}
		return SUCCESS;
	}


	pid_t pid = fork();

	if (pid == 0) // child
	{
		// increase args size by 2
		command->args = (char **)realloc(
				command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		/// TODO: do your own exec with path resolving using execv()

		// Getting all the paths that may contain executable.
		char *possibleCommandPaths = getenv("PATH");
		// Creating a 2D array to store the paths
		char commandPathArray[maxCommandSize][maxCommandSize];
		// Creating a string tokenizer to parse through stored paths
		char *token;

		int pathCount = 0;

		token = strtok(possibleCommandPaths, ":");
		//Placing the elements in possibleCommandPaths to 2D array
		while (token != NULL ) {
			strcpy(commandPathArray[pathCount], token);
			token = strtok(NULL, ":");
			pathCount++;
		}

		//Searching the paths for executable
		for(int i = 0; i<pathCount; i++) {
			strcat(commandPathArray[i], "/");
			strcat(commandPathArray[i], command->name);
			if (execv(commandPathArray[i], command->args) != -1) {
				exit(0);
			}
		}

		// If an executable isn't found, prints error message
		printf("-%s: %s: command not found\n", sysname, command->name);
		exit(0);
	}
	else
	{
		/// TODO: Wait for child to finish if command is not running in background
		if(!command->background) wait(NULL);
		return SUCCESS;
	}

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
