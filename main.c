#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>

#define LINE_BUFF_SIZE 512

/* For a more complicated task I would include the relevant data in the
* relevant thread args struct, but for this trivial case a global will suffice
*/
sem_t readDone, passDone, writeDone, finished;
char sharedLineBuffer[LINE_BUFF_SIZE];
volatile bool threadBHasNoMoreData = false;

typedef union {
	int fd[2];
	struct {
		int read;
		int write;
	} s;
} PipeDescriptor;

typedef struct {
	FILE * file;
	PipeDescriptor pipe;
} ThreadArgsA;

typedef struct {
	PipeDescriptor pipe;
} ThreadArgsB;

typedef struct {
	FILE * file;
} ThreadArgsC;

void trimAfterNewline(char* s) {
	while (*s != '\n' && *s != '\r' && *s != '\0')
		s++;
	*s = '\0';
}

void readingThreadMethod(ThreadArgsA * args) {
	printf("A: Begins\n");

	char lineBuff[LINE_BUFF_SIZE];

	bool isEof = false;
	do {
		printf("A: Wait\n");
		sem_wait(&writeDone);
		printf("A: Signalled\n");

		if (fgets(lineBuff, LINE_BUFF_SIZE, args->file) != NULL) {
			trimAfterNewline(lineBuff);

			int nbytes = strlen(lineBuff) + 1;
			printf("A: Writing %d bytes to pipe\n", nbytes);

			write(args->pipe.s.write, lineBuff, nbytes);
		} else {
			isEof = true;
		}

		printf("A: Signalling\n");
		sem_post(&readDone);
	} while (!isEof);

	close(args->pipe.s.write);
	return;
}

void passingThreadMethod(ThreadArgsB * args) {
	printf("B: Begins\n");

	bool isEof = false;
	do {
		printf("B: Wait\n");
		sem_wait(&readDone);
		printf("B: Signalled\n");

		if (read(args->pipe.s.read, sharedLineBuffer, LINE_BUFF_SIZE) <= 0) {
			threadBHasNoMoreData = isEof = true;
		}

		printf("B: Read %d bytes from pipe\n", (int)strlen(sharedLineBuffer));

		printf("B: Signalling\n");
		sem_post(&passDone);
	} while (!isEof);
}

void writingThreadMethod(ThreadArgsC * args) {
	printf("C: Begins\n");
	bool isEof = false;
	bool hasPassedHeader = false;

	do {
		printf("C: Wait\n");
		sem_wait(&passDone);
		printf("C: Signalled\n");

		if (!threadBHasNoMoreData) {
			if (hasPassedHeader) {
				printf("C: Writing \"%s\"\n", sharedLineBuffer);
				fputs(sharedLineBuffer, args->file);
				fputc('\n', args->file);
			} else {
				hasPassedHeader = strncmp("end_header", sharedLineBuffer, 10) == 0;

				if (hasPassedHeader) {
					printf("C: Header found\n");
				} else{
					printf("C: Ignoring \"%s\"\n", sharedLineBuffer);
				}
			}
		}

		printf("C: Signalling\n");
		sem_post(&writeDone);
	} while (!threadBHasNoMoreData);

	printf("C: EOF found\n");
	sem_post(&finished);
}

int main(void) {
	PipeDescriptor fd;

	int pipeErr;
	if ((pipeErr = pipe(fd.fd)) < 0) {
		perror("Error opening pipe: %d");
		return -1;
	}

	/* Init all our semaphores to zero, they should all block and we can begin by
	* signalling A through writeDone
	*/
	bool semaphoresWereInitialised =
			sem_init(&readDone, true, 0) != -1 ||
			sem_init(&passDone, true, 0) != -1 ||
			sem_init(&writeDone, true, 0) != -1 ||
			sem_init(&finished, true, 0)  != -1;

	if (!semaphoresWereInitialised) {
		perror("Error creating a semaphore");
		return -2;
	}

	FILE *input = fopen("data.txt", "r");
	if (input == NULL) {
		perror("Error opening input file");
		return -3;
	}

	FILE *output = fopen("src.txt", "w");
	if (output == NULL) {
		perror("Error opening output file");
		fclose(input);
		return -4;
	}

	pthread_t threadA, threadB, threadC;

	ThreadArgsA argsA = { input, fd };
	bool threadsCreated = pthread_create(&threadA, NULL, (void *)readingThreadMethod, (void *)(&argsA)) == 0;

	ThreadArgsB argsB = { fd };
	threadsCreated &= pthread_create(&threadB, NULL, (void *)passingThreadMethod, (void *)(&argsB)) == 0;

	ThreadArgsC argsC = { output };
	threadsCreated &= pthread_create(&threadC, NULL, (void *)writingThreadMethod, (void *)(&argsC)) == 0;

	if (!threadsCreated) {
		perror("There was a problem when constructing the threads");
		fclose(input);
		fclose(output);

		return -5;
	}

	printf("main: Signalling\n");
	sem_post(&writeDone);
	sem_wait(&finished);

	fclose(input);
	fclose(output);

	/* Use a single pipe char, we would like to attempt to destroy subsequent
	* semaphores, regardless of whether the previous ones succeeded
	*/
	bool semaphoresWereDestroyed =
			sem_destroy(&readDone) != -1 |
			sem_destroy(&passDone) != -1 |
			sem_destroy(&writeDone) != -1 |
			sem_destroy(&finished) != -1;

	if (!semaphoresWereDestroyed) {
		printf("main: Some or all semaphores were not destroyed (warning)\n");
	}

	printf("main: Done!\n");

	return 0;
}
