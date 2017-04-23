#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>

#define LINE_BUFF_SIZE 512

void log(char * thread, char * message) {
	printf("%s: %s\n", thread, message);
}

/* For a more complicated task I would include the relevant semaphores in the
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

void readingThreadMethod(ThreadArgsA * args) {
	log("A", "Begins");

	char lineBuff[LINE_BUFF_SIZE];

	bool isEof = false;
	do {
		log("A", "Wait");
		sem_wait(&writeDone);
		log("A", "Signalled");

		if (fgets(lineBuff, LINE_BUFF_SIZE, args->file) != NULL) {
			int nbytes = strlen(lineBuff) + 1;
			printf("A: Writing %d bytes to pipe\n", nbytes);

			write(args->pipe.s.write, lineBuff, nbytes);
		} else {
			isEof = true;
		}

		log("A", "Signalling");
		sem_post(&readDone);
	} while (!isEof);

	close(args->pipe.s.write);
	return;
}

void passingThreadMethod(ThreadArgsB * args) {
	log("B", "Begins");

	bool isEof = false;
	do {
		log("B", "Wait");
		sem_wait(&readDone);
		log("B", "Signalled");

		if (read(args->pipe.s.read, sharedLineBuffer, LINE_BUFF_SIZE) <= 0)
			threadBHasNoMoreData = isEof = true;

		printf("B: Read %d bytes from pipe\n", strlen(sharedLineBuffer));

		log("B", "Signalling");
		sem_post(&passDone);
	} while (!isEof);
}

void writingThreadMethod(ThreadArgsC * args) {
	log("C", "Begins");
	bool isEof = false;
	bool hasPassedHeader = false;

	do {
		log("C", "Wait");
		sem_wait(&passDone);
		log("C", "Signalled");

		if (!threadBHasNoMoreData) {
			if (hasPassedHeader) {
				fputs(sharedLineBuffer, args->file);
			}	else {
				hasPassedHeader = strncmp("end_header", sharedLineBuffer, 10) == 0;

				if (hasPassedHeader)
					log("C", "Header found");
				else
					printf("Ignoring %s", sharedLineBuffer);
			}
		}

		log("C", "Signalling");
		sem_post(&writeDone);
	} while (!threadBHasNoMoreData);

	log("C", "EOF found");
	sem_post(&finished);
}

int main(void) {
	PipeDescriptor fd;

	int pipeErr;
	if ((pipeErr = pipe(fd.fd)) < 0) {
		fprintf(stderr, "Error opening pipe: %d", pipeErr);
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
		fputs("Error creating a semaphore\n", stderr);
		return -2;
	}

	FILE *input = fopen("data.txt", "r");
	if (input == NULL) {
		fputs("Error opening input file\n", stderr);
		return -3;
	}

	FILE *output = fopen("src.txt", "w");
	if (output == NULL) {
		fputs("Error opening output file\n", stderr);
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
		fputs("There was a problem when constructing the threads", stderr);
		fclose(input);
		fclose(output);

		return -5;
	}

	log("main", "Signalling");
	sem_post(&writeDone);
	sem_wait(&finished);
	fclose(input);
	fclose(output);

	log("main", "Done!");

	return 0;
}
