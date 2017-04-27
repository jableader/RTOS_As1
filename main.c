#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>

/* We were not given any instruction on how large a line could be, however from
 * inspecting the sample file I can see that the only lines we don't want cut
 * are within 10 and 25 characters long. I think that a buffer twenty times
 * that size should suffice.
 */
#define LINE_BUFF_SIZE 512

/* For a more complicated task I would include the relevant data in the
 * thread args structs, but for this trivial case a global will suffice
 */
sem_t readDone, passDone, writeDone;
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
	pthread_t thread;
	char id;
	sem_t* start;
	sem_t* end;
	bool(*threadMethod)(void *);
	void* otherArgs;
} thread_descriptor;

void threadRunner(thread_descriptor * td) {
	printf("%c: Beginning.\n", td->id);

	bool shouldContinue;
	do {
		printf("%c: Waiting for signal.\n", td->id);
		sem_wait(td->start);
		printf("%c: Recieved signal, running method.\n", td->id);

		shouldContinue = td->threadMethod(td->otherArgs);

		printf("%c: Signalling next thread.\n", td->id);
		sem_post(td->end);
	} while (shouldContinue);

	printf("%c: Exit condition reached, ending now.\n", td->id);
}

bool createThread(thread_descriptor * thread, char id, sem_t* start, sem_t* end, bool(*threadMethod)(void *), void * otherArgs) {
	thread->id = id;
	thread->start = start;
	thread->end = end;
	thread->threadMethod = threadMethod;
	thread->otherArgs = otherArgs;

	return pthread_create(&(thread->thread), NULL, (void *)threadRunner, (void *)thread) == 0;
}

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

bool readingThreadMethod(ThreadArgsA* args) {
	static char lineBuff[LINE_BUFF_SIZE];

	if (fgets(lineBuff, LINE_BUFF_SIZE, args->file) != NULL) {
		trimAfterNewline(lineBuff);

		int nbytes = strlen(lineBuff) + 1;
		printf("A: Writing %d bytes to pipe\n", nbytes);

		write(args->pipe.s.write, lineBuff, nbytes);

		return true;
	} else {
		close(args->pipe.s.write);

		return false;
	}
}

bool passingThreadMethod(ThreadArgsB* args) {
	if (read(args->pipe.s.read, sharedLineBuffer, LINE_BUFF_SIZE) <= 0) {
		threadBHasNoMoreData = true;
		return false;
	}

	return true;
}

bool writingThreadMethod(ThreadArgsC* args) {
	static bool hasPassedHeader = false;

	if (threadBHasNoMoreData) {
		return false;
	}

	printf("C: Recieved \"%s\" from B\n", sharedLineBuffer);

	if (hasPassedHeader) {
		fputs(sharedLineBuffer, args->file);
		fputc('\n', args->file);
	} else if (strncmp("end_header", sharedLineBuffer, 10) == 0) {
		printf("C: Header found\n");
		hasPassedHeader = true;
	}

	return true;
}

bool initialiseSemaphores() {
	/* Init all our semaphores to zero, they should all block and we can begin by
	* signalling A through writeDone
	*/
	return sem_init(&readDone, true, 0) != -1 ||
					sem_init(&passDone, true, 0) != -1 ||
					sem_init(&writeDone, true, 0) != -1;
}

bool destroySemaphores() {
	/* Use a single pipe char, we would like to attempt to destroy subsequent
	* semaphores, regardless of whether the previous ones succeeded
	*/
	return
			sem_destroy(&readDone) != -1 |
			sem_destroy(&passDone) != -1 |
			sem_destroy(&writeDone) != -1;
}

int main(void) {
	int result = -1;

	PipeDescriptor fd;
	int pipeErr;
	if ((pipeErr = pipe(fd.fd)) < 0) {
		perror("Error opening pipe: %d");
		goto done;
	}

	if (!initialiseSemaphores()) {
		perror("Error creating a semaphore");
		goto cleanSemaphores;
	}

	FILE* input = fopen("data.txt", "r");
	if (input == NULL) {
		perror("Error opening input file");
		goto cleanSemaphores;
	}

	FILE* output = fopen("src.txt", "w");
	if (output == NULL) {
		perror("Error opening output file");
		goto cleanInputFile;
	}

	thread_descriptor threadA, threadB, threadC;

	ThreadArgsA argsA = { input, fd };
	bool threadsCreated = createThread(&threadA, 'A', &writeDone, &readDone, (bool(*)(void*))readingThreadMethod, &argsA);

	ThreadArgsB argsB = { fd };
	threadsCreated &= createThread(&threadB, 'B', &readDone, &passDone, (bool(*)(void*))passingThreadMethod, &argsB);

	ThreadArgsC argsC = { output };
	threadsCreated &= createThread(&threadC, 'C', &passDone, &writeDone, (bool(*)(void*))writingThreadMethod, &argsC);

	if (!threadsCreated) {
		perror("There was a problem when constructing the threads");
		goto cleanInputFile;
	}

	printf("main: Signalling\n");

	result = 0;

	sem_post(&writeDone);
	pthread_join(threadC.thread, NULL);

	cleanInputFile:
	if (fclose(input) != 0) {
		printf("main: (Warning) There was an error closing the input file\n");
	}

	cleanOutputFile:
	if (fclose(output) != 0) {
		printf("main: (Warning) There was an error closing the output file\n");
	}

	cleanSemaphores:
	if (!destroySemaphores()) {
		printf("main: (Warning) Some or all semaphores were not destroyed\n");
	}

	done:
	printf("main: Done!\n");
	return result;
}
