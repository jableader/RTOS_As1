#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>

#define LINE_BUFF_SIZE 200

/* For a more complicated task I would include the relevant semaphores in the
 * relevant thread args struct, but for this trivial case a global will suffice
 */
sem_t readDone, passDone, writeDone, finished;
char sharedLineBuffer[LINE_BUFF_SIZE];

typedef struct {
	int fd[2];
	FILE *file;
} ThreadArgsA;

typedef struct {
	int fd[2];
} ThreadArgsB;

typedef struct {
	FILE * file;
} ThreadArgsC;

void readingThreadMethod(ThreadArgsA * args) {
	close(args->fd[0]);

	char lineBuff[LINE_BUFF_SIZE];

	bool isEof = false;
	do {
		sem_wait(&writeDone);

		if (fgets(lineBuff, LINE_BUFF_SIZE, args->file) != NULL) {
			write(args->fd[1], lineBuff, strlen(lineBuff));
		} else {
			isEof = true;
		}

		sem_post(&readDone);
	} while (!isEof);

	close(args->fd[1]);
	return;
}

void passingThreadMethod(ThreadArgsB * args) {
	close(args->fd[1]);

	bool isEof = false;
	do {
		sem_wait(&readDone);

		if (read(args->fd[0], sharedLineBuffer, LINE_BUFF_SIZE) == 0) {
			sharedLineBuffer[0] = '\0'; // Empty string for EOF
			isEof = true;
		}

		sem_post(&passDone);
	} while (!isEof);
}

void writingThreadMethod(ThreadArgsC * args) {
	bool isEof = false;
	bool isInHeader = false;

	do {
		sem_wait(&passDone);

		isEof = sharedLineBuffer[0] == '\0';
		if (isInHeader) {
			isInHeader = strcmp("end_header\n", sharedLineBuffer) == 0;
		}

		if (!isEof && !isInHeader) {
			fputs(sharedLineBuffer, args->file);
		}

		sem_post(&writeDone);
	} while (!isEof);

	sem_post(&finished);
}

int main(void) {
	int pipeErr, fd[2];
	if ((pipeErr = pipe(fd)) < 0) {
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

	ThreadArgsA argsA = { fd, input };
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

	sem_post(&writeDone);
	sem_wait(&finished);
	fclose(input);
	fclose(output);

	fputs("Done!", stdout);

	return 0;
}
