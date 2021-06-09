#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_CHAR_LENGTH 2048
#define MAX_ARGUMENTS 512

/* 
Global variables for
1. Foreground only mode
2. Signal termination number for foreground processes
*/
bool FOREGROUND_ONLY = false;
int SIGNAL_NUMBER;

/* A linked list struct to store terminal command  for user */
struct command
{
    char* value;
    struct command* next;
};

/* A linked list struct to store zombie processes that run in background */
struct zombieProcess
{
	bool killed;
	int zombie_pid;
	struct zombieProcess* next;
};

/* function to substitute string from https://www.geeksforgeeks.org/c-program-replace-word-text-another-given-word/  used to replace $$ with pid */
char* substituteString(const char* s, const char* oldW, const char* newW)
{
	char* result;
	int i, cnt = 0;
	int newWlen = strlen(newW);
	int oldWlen = strlen(oldW);

	// Counting the number of times old word occur in the string
	for (i = 0; s[i] != '\0'; i++) {
		if (strstr(&s[i], oldW) == &s[i]) {
			cnt++;

			// Jumping to index after the old word.
			i += oldWlen - 1;
		}
	}

	// Making new string of enough length
	result = (char*)malloc(i + cnt * (newWlen - oldWlen) + 1);

	i = 0;
	while (*s) {
		// compare the substring with the result
		if (strstr(s, oldW) == s) {
			strcpy(&result[i], newW);
			i += newWlen;
			s += oldWlen;
		}
		else
			result[i++] = *s++;
	}
	result[i] = '\0';
	return result;
}

// function to create zombie list
struct zombieProcess* makeZombie(void)
{
	// from heap generate enough data for a struct
	struct zombieProcess* zombie = malloc(sizeof(struct zombieProcess));
	zombie->killed = false;
	zombie->next = NULL;
	zombie->zombie_pid = 0;
	return zombie;
}

// The signal handler for SIGSTP - only for main process
void handle_SIGTSTP(int signo) {
	if (!FOREGROUND_ONLY) {
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		FOREGROUND_ONLY = true;
		// reentrant function
		write(STDOUT_FILENO, message, 50);
		fflush(stdout);
	}
	else {
		char* message = "\nExiting foreground-only mode\n";
		FOREGROUND_ONLY = false;
		// reentrant function
		write(STDOUT_FILENO, message, 30);
		fflush(stdout);
	}
}

// The signal handler for SIGINT - only for child foreground process
void handle_SIGINT(int signo) {
	char* message = "terminated by signal ";
	// reentrant function
	write(STDOUT_FILENO, message, 21);
	fflush(stdout);
}

// function to create a struct of user command
struct command* holdUserCommand(char* userInput)
{
	// bool to see if to substitute $$ with pid and variable for $$
	bool substitute = false;
	char* dollarSign = "$$";

	// from heap generate enough data for a struct
	struct command* userCommand = malloc(sizeof(struct command));

	// pointer for string token finding and token
	char* saveptr;
	char* token = strtok_r(userInput, " ", &saveptr);
	
	// head and next node to assist in referencing next node
	struct command* head = NULL;
	struct command* next = NULL;

	head = userCommand;
	
	// loop thru user command and make a linked list of each command and argument
	while (token != NULL) {
		// check if token contains $$
		char* subString;
		subString = strstr(token, dollarSign);

		// if $$ found in string, call substituteString function
		if (subString != NULL) {
			// convert pid to ascii
			int pid = getpid();
			char intToStr[10];
			sprintf(intToStr, "%d", pid);

			// call substituteString function
			char* updatedString = substituteString(token, dollarSign, intToStr);
	
			// copy into current node value
			userCommand->value = calloc(strlen(updatedString) + 1, sizeof(char));
			strcpy(userCommand->value, updatedString);

			// set boolean to true
			substitute = true;
		}
		
		// copy token directly if there is no substitution of $$
		if (!substitute) {
			// copy command into linked list 
			userCommand->value = calloc(strlen(token) + 1, sizeof(char));
			strcpy(userCommand->value, token);
		}
		
		// create next node of linked list
		next = userCommand;
		next->next = malloc(sizeof(struct command));
		userCommand = next->next;

		// find next token
		token = strtok_r(NULL, " ", &saveptr);

		// reset substitute
		substitute = false;
	}
	// return the head of the struct
	return head;
}

// function for status command
void showStatus(int status)
{
	// if status is 100, print the terminating signal of the last foreground process
	if (status == 100) {
		printf("terminated by signal %d\n", SIGNAL_NUMBER);
		fflush(stdout);
		return;
	}
	// else, print status
	printf("exit value %d\n", status);
	fflush(stdout);
}

// function for CD command
int changeDirectory(struct command* userInput)
{
	// boolean value for argument, env variable for HOME
	bool argument = false;
	char* home = getenv("HOME");

	// check if there is an argument in user command
	if (userInput->next->value != NULL) {
		argument = true;
	}

	// cd command has argument
	if (argument) {
		// check if user provided path is absolute (starts with /)
		if (userInput->next->value[0] == '/') {
			
			// change directory to absolute path
			int ret = chdir(userInput->next->value);

			// unsuccesful cd, send message and 1 status
			if (ret != 0) {
				printf("No such file or directory\n");
				fflush(stdout);
				return 1;
			}

			// return 0 if succesful
			return 0;
		}

		// else, create a relative path. concat /argument to end of current directory
		char* currentDir = getcwd(NULL, 0);
		int totalPathLength = strlen(currentDir) + strlen(userInput->next->value) + 2;
		char relativePath [totalPathLength];

		relativePath[0] = '\0';

		strcat(relativePath, currentDir);
		strcat(relativePath, "/");
		strcat(relativePath, userInput->next->value);

		// change directory to relative path
		int ret = chdir(relativePath);

		// unsuccesful cd, send message and 1 status
		if (ret != 0) {
			printf("No such file or directory\n");
			fflush(stdout);
			return 1;
		}

		// return 0 if succesful
		return 0;
	}
	else {
		chdir(home);  // go to home ENV variable
		return 0;
	}
}

// run command in foreground
int runInForeground(char* argv[], struct command* userInput, struct sigaction* SIGINT_action, struct sigaction* SIGTSTP_action)
{
	int status = 0;				// status to be returned
	pid_t childProcess = -5;  // dummy value
	int childStatus;
	pid_t childPid;

	// boolean value to see if redirection is required
	bool inputRedirection = false;
	bool outputRedirection = false;

	//  string for input and output redirection
	char changeInput[] = "<";
	char changeOutput[] = ">";

	// char pointer for input and ouput source
	char* inputSource;
	char* outputSource;

	//  pointers for output and input redirection
	int targetFD;
	int sourceFD;

	// find if redirection is necessary
	while (userInput->next != NULL)
	{
		// check for any input redirection
		if (strcmp(changeInput, userInput->value) == 0) {
			// inputRedirection to true and get source
			inputRedirection = true;
			inputSource = userInput->next->value;
		}

		// check for any output redirection
		if (strcmp(changeOutput, userInput->value) == 0) {
			// outputRedirection to true and get source
			outputRedirection = true;
			outputSource = userInput->next->value;
		}

		// go to next user command
		userInput = userInput->next;
	}
	// If fork is successful, the value of spawnpid will be 0 in the child, the child's pid in the parent
	childProcess = fork();
	switch (childProcess)
	{
	case -1:
		// fork fails send message and status of 1
		perror("Command failed! Please try again!");
		fflush(stdout);
		exit(1);
	// spawnpid is 0 in the child
	case 0:  
		SIGTSTP_action->sa_handler = SIG_IGN;   // Update SIG_IGN to ignore signal
		sigaction(SIGTSTP, SIGTSTP_action, NULL);
		sigfillset(&SIGTSTP_action->sa_mask);  // Block all catchable signals while handle_SIGTSTP is running
		SIGTSTP_action->sa_flags = 0;   // No flags set

		SIGINT_action->sa_handler = handle_SIGINT;   // set the sig handler
		sigfillset(&SIGINT_action->sa_mask);  // Block all catchable signals while handle_SIGINT is running
		SIGINT_action->sa_flags = 0;   // No flags set
		sigaction(SIGINT, SIGINT_action, NULL);  // Install the signal handler

		// redirect input if necessary
		if (inputRedirection)
		{
			// open source file
			sourceFD = open(inputSource, O_RDONLY);

			// source file cannot be opened, send status of 2
			if (sourceFD == -1) {
				printf("cannot open %s for input\n", inputSource);
				fflush(stdout);
				exit(2);
			}

			// Redirect stdin to source file
			int result = dup2(sourceFD, 0);
			if (result == -1) {
				printf("cannot open %s for input\n", inputSource);
				fflush(stdout);
				exit(2);
			}
		}

		// redirect output if necssary
		if (outputRedirection) 
		{
			// open the file and set permissions
			targetFD = open(outputSource, O_WRONLY | O_CREAT | O_TRUNC, 0640);

			// file cannot be opened, print error message and exit 1 as status
			if (targetFD == -1) {
				printf("cannot open %s for output\n", outputSource);
				fflush(stdout);
				exit(1);
			}

			// Redirect stdout to target file
			int result = dup2(targetFD, 1);
			if (result == -1) {
				printf("cannot open %s for output\n", outputSource);
				fflush(stdout);
				exit(1);
			}
		}
		// call execv function
		execvp(argv[0], argv);
		// exec only returns if there is an error
		perror(argv[0]);
		fflush(stdout);
		// send 2 as signal if error
		exit(2);
	default:
		// wait for the child process
		childPid = waitpid(childProcess, &childStatus, 0);

		// send status of 1 if command failed, else send status of 0 for normal termination
		if (WIFEXITED(childStatus)) 
		{
			// printf("Child %d exited normally with status %d\n", childProcess, WEXITSTATUS(childStatus));
			if (WEXITSTATUS(childStatus) != 0) 
			{
				status = 1;
			}
			else 
			{
				status = 0;
			}
		}
		else  // abnormal termination due to signal, send 1 as status.
		{
			SIGNAL_NUMBER = WTERMSIG(childStatus);
			printf("terminated by signal %d\n", SIGNAL_NUMBER);
			fflush(stdout);
			status = 100;
		}
	}
	return status;
}

// run command in background
void runInBackground(char* argv[], struct command* userInput, struct zombieProcess* zombieList, struct sigaction* SIGTSTP_action)
{
	pid_t childProcess = -5;  // dummy value

	// boolean value to see if redirection is required
	bool inputRedirection = false;
	bool outputRedirection = false;

	//  string for input and output redirection and null
	char null[] = "/dev/null";
	char changeInput[] = "<";
	char changeOutput[] = ">";

	// char pointer for input and ouput source
	char* inputSource;
	char* outputSource;

	//  pointers for output and input redirection
	int targetFD;
	int sourceFD;

	// find if redirection is necessary
	while (userInput->next != NULL)
	{
		// check for any input redirection
		if (strcmp(changeInput, userInput->value) == 0) {
			// inputRedirection to true and get source
			inputRedirection = true;
			inputSource = userInput->next->value;
		}

		// check for any output redirection
		if (strcmp(changeOutput, userInput->value) == 0) {
			// outputRedirection to true and get source
			outputRedirection = true;
			outputSource = userInput->next->value;
		}
		// go to next user command
		userInput = userInput->next;
	}
	// If fork is successful, the value of spawnpid will be 0 in the child, the child's pid in the parent
	childProcess = fork();
	switch (childProcess)
	{
	case -1:
		// fork fails, send message and status of 1
		perror("Command failed! Please try again!");
		fflush(stdout);
		exit(1);
	case 0:  // spawnpid is 0 in the child
		SIGTSTP_action->sa_handler = SIG_IGN;   // SIG_IGN as its signal handler
		sigaction(SIGTSTP, SIGTSTP_action, NULL);
		sigfillset(&SIGTSTP_action->sa_mask);  // Block all catchable signals while handle_SIGTSTP is running
		SIGTSTP_action->sa_flags = 0;   // No flags set

		// redirect input if necessary
		if (inputRedirection)
		{
			// open source file
			sourceFD = open(inputSource, O_RDONLY);

			// source file cannot be opened, send status of 2
			if (sourceFD == -1) {
				printf("cannot open %s for input\n", inputSource);
				fflush(stdout);
				exit(2);
			}

			// Redirect stdin to source file
			int result = dup2(sourceFD, 0);
			if (result == -1) {
				printf("cannot open %s for input\n", inputSource);
				fflush(stdout);
				exit(2);
			}
		} 
		else
		{
			// set source file to /dev/null
			sourceFD = open(null, O_RDONLY);

			// source file cannot be opened, send status of 2
			if (sourceFD == -1) {
				printf("cannot open %s for input\n", inputSource);
				fflush(stdout);
				exit(2);
			}

			// Redirect stdin to /dev/null
			int result = dup2(sourceFD, 0);
			if (result == -1) {
				printf("cannot open %s for input\n", inputSource);
				fflush(stdout);
				exit(2);
			}
		}

		// redirect output if necssary
		if (outputRedirection)
		{
			// open the file and set permissions
			targetFD = open(outputSource, O_WRONLY | O_CREAT | O_TRUNC, 0640);

			// file cannot be opened, print error message and exit 1 as status
			if (targetFD == -1) {
				printf("cannot open %s for output\n", outputSource);
				fflush(stdout);
				exit(1);
			}

			// Redirect stdout to target file
			int result = dup2(targetFD, 1);
			if (result == -1) {
				printf("cannot open %s for output\n", outputSource);
				fflush(stdout);
				exit(1);
			}
		}  
		else
		{
			// set to /dev/null
			targetFD = open(null, O_WRONLY | O_CREAT | O_TRUNC, 0640);

			// file cannot be opened, print error message and exit 1 as status
			if (targetFD == -1) {
				printf("cannot open %s for output\n", outputSource);
				fflush(stdout);
				exit(1);
			}

			// Redirect stdout to /dev/null
			int result = dup2(targetFD, 1);
			if (result == -1) {
				printf("cannot open %s for output\n", outputSource);
				fflush(stdout);
				exit(1);
			}
		}
		// call execv function
		execvp(argv[0], argv);
		// exec only returns if there is an error
		perror(argv[0]);
		fflush(stdout);
		// send 2 as signal if error
		exit(2);
	default:
		// put in process id and make next zombie
		zombieList->zombie_pid = childProcess;
		zombieList->next = makeZombie();
		// print the background pid
		printf("background pid is %d\n", zombieList->zombie_pid);
	}
}

// function to get argv of user command to be used in execvp()
void buildArgv(char* argv[], struct command* userInput)
{
	// printf("build argv\n");
	//  string for backgroundOperator, input and output
	char backgroundOperator[] = "&";
	char changeInput[] = "<";
	char changeOutput[] = ">";

	// idx for array
	int idx = 0;

	// get arguments in array format. while loop as long as command is not  > or <.
	while (userInput->next != NULL)
	{
		// exit if input direction, output redirection, or & operator
		if (strcmp(changeInput, userInput->value) == 0 || strcmp(changeOutput, userInput->value) == 0 || strcmp(backgroundOperator, userInput->value) == 0) {
			break;
		}

		// pointer for string and dereference pointer
		char* cmd;
		cmd = userInput->value;

		// put command in argv and increment idx
		argv[idx] = cmd;
		idx++;

		// go to next user command
		userInput = userInput->next;
	}
}

// function to execute other commands
int executeOtherCommands(struct command* userInput, struct zombieProcess* zombieList, int current_status, struct sigaction* SIGINT_action, struct sigaction* SIGTSTP_action)
{
	// status to be returned from executing command
	int status;

	// boolean value to see if redirection is set
	bool redirection = false;

	//  string for backgroundOperator
	char backgroundOperator[] = "&";
	char changeInput[] = "<";
	char changeOutput[] = ">";

	// prior command and head node
	struct command* head = userInput;
	struct command* tail = NULL;

	//count of arguments in user command
	int argument_count = 0;

	// while loop to last command to see if process needs to run in background
	while (userInput->next != NULL)
	{
		// check for any input or output redirection
		if (strcmp(changeInput, userInput->value) == 0 || strcmp(changeOutput, userInput->value) == 0) {
			// set redirection, inputRedirection to true
			redirection = true;
		}

		// only count argument, if no redirection and & operator
		if (!redirection && strcmp(backgroundOperator, userInput->value) != 0) {
			argument_count++;
		}

		// go to next user command
		tail = userInput;
		userInput = userInput->next;
	}

	// if argument is greater than 512, print error message to user
	if (argument_count > MAX_ARGUMENTS)
	{
		printf("Too many arguments in command line!\n");
		fflush(stdout);
		return current_status;
	}

	// build argv of user command
	char* argv[argument_count + 1];  // array of char pointers
	argv[argument_count] = NULL;  // last index is NULL
	buildArgv(argv, head);

	// run in background
	if (strcmp(backgroundOperator, tail->value) == 0 && !FOREGROUND_ONLY) {
		// invoke runInBackground
		runInBackground(argv, head, zombieList, SIGTSTP_action);
		// set status to current status since process running in background and return status
		status = current_status;
		return status;
	}
	// else, run in foreground and update status
	status = runInForeground(argv, head, SIGINT_action, SIGTSTP_action);
	return status;
}

// function to clear zombie processes
void clearZombieProcs(int current_status, struct zombieProcess* zombieList)
{
	// no pid for zombie or already killed, exit out of function
	if (!zombieList->zombie_pid || zombieList->killed)
	{
		return;
	}
	// else, clear out zombie procedures
	int   childStatus;
	pid_t zombieStatus;
	zombieStatus = waitpid(zombieList->zombie_pid, &childStatus, WNOHANG);

	// send message to user if zombieStatus is cleared
	if (zombieStatus != 0)
	{
		printf("background pid %d is done: ", zombieList->zombie_pid);
		fflush(stdout);
		// set killed to true
		zombieList->killed = true;
		if (WIFEXITED(childStatus)) {  // normal exit, send status
			showStatus(WEXITSTATUS(childStatus));

		}
		else { // abnormal exit send signal number
			printf("terminated by signal %d\n", WTERMSIG(childStatus));
			fflush(stdout);
		}
	}
}

int main(int argc, char* argv[])
{
	// exit, cd and status command
	char exit[] = "exit", cd[] = "cd", status[] = "status";

	// default status for execution
	int exec_status = 0;

	// array of chars to hold user command
	char userCommand[2100];

	// reference to head and next node of zombie list
	struct zombieProcess* zombieHead = makeZombie();
	struct zombieProcess* currentZombie = NULL;

	// Initialize SIGINT_action & SIGTSTP_action struct to be empty
	struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

	do // do-while loop until user uses exit command
	{
		// SIG_IGN as its signal handler
		SIGINT_action.sa_handler = SIG_IGN;
		// Install the signal handler
		sigaction(SIGINT, &SIGINT_action, NULL);

		// set handler for SIGTSTP
		SIGTSTP_action.sa_handler = handle_SIGTSTP;
		// Block all catchable signals while handle_SIGTSTP is running
		sigfillset(&SIGTSTP_action.sa_mask);
		// No flags set
		SIGTSTP_action.sa_flags = 0;
		sigaction(SIGTSTP, &SIGTSTP_action, NULL);
		currentZombie = zombieHead; // reset to head of zombielist

		// clear out zombie procs
		while (currentZombie->next != NULL)
		{
			clearZombieProcs(exec_status, currentZombie);
			currentZombie = currentZombie->next;
		}

		printf(": ");
		fflush(stdout);

		// store user command in array
		fgets(userCommand, 2100, stdin);

		// strip new line at end of string, default behavior of fgets
		int len = strlen(userCommand);

		// check if command exceeds 2048 characters, is blank or starts with #, reprompt user.
		// very rare edge case where SIGTSTP enters a string length of 0
		if (len > MAX_CHAR_LENGTH || len == 1 || userCommand[0] == '#' || !len) {
			continue;
		}
		
		// strip new line at end of string, default behavior of fgets
		if ((len > 0) && (userCommand[len - 1] == '\n')) {
			userCommand[len - 1] = '\0';
		}

		// create a linked list struct of user command
		struct command* linkedListOfUserCommand = holdUserCommand(userCommand);

		// only way to override strange bug, reset to 0. DONT USE MEMESET or userCommand[0] = '\0';
		char userCommand[2100] = { 0 };

		// if command is cd
		if (strcmp(cd, linkedListOfUserCommand->value) == 0) {
			// return status after running this command
			exec_status = changeDirectory(linkedListOfUserCommand);
		}
		// else if command is status
		else if (strcmp(status, linkedListOfUserCommand->value) == 0) {
			showStatus(exec_status);
		}
		// if not exit command
		else  if (strcmp(exit, linkedListOfUserCommand->value) != 0) {
			// set status equal to whatever is returned from this function
			exec_status = executeOtherCommands(linkedListOfUserCommand, currentZombie, exec_status, &SIGINT_action, &SIGTSTP_action);
		}
	} 
	while (strcmp(exit, userCommand) != 0);  // user has entered exit command
	struct zombieProcess* priorZombie = NULL;  // pointer to hold prior zombie

	// loop through zombie list, kill all processes if not killed, then free zombie list
	while (zombieHead->next != NULL)
	{
		if (!zombieHead->killed) 
		{
			kill(zombieHead->zombie_pid, SIGKILL);
		}
		priorZombie = zombieHead;
		zombieHead = zombieHead->next;
		free(priorZombie);
	}
}