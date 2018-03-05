/*
 * ImageWriter performs a block level copy from one file to another, much like `dd`.
 * ImageWriter gives a nicer status than `dd`, and uses a buffer and threads to help speed things along.
 *
 * Usage: ImageWriter [-s block_size -b num_block] <in_file> <out_file>
 *
 *  block_size : size (in bytes) of each block
 *  num_block  : the number of blocks in the buffer
 *
 * total buffer size in bytes = block_size * num_block
 *
 * To compile: gcc -o ImageWriter ImageWriter.c
 *
 * Author: J. Lowell Wofford <lowell@rescompllc.com>
 */ 
 
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <unistd.h>

#define BLOCK_SIZE 1024*1024
#define NUM_BLOCK 10
#define STATUS_REFRESH 1000000/8

#define next_block_idx(curr_block) (((curr_block) + 1) % num_block)
#define usec(t) (((t.tv_sec) * 1000000) + t.tv_usec)
#define buf_idx(idx) ((idx)*(block_size))

pthread_mutex_t rw_mutex;
pthread_cond_t ready_to_write_cv;
pthread_cond_t ready_to_read_cv;

unsigned int block_size, num_block;

typedef struct block {
    size_t size;
    bool end;
} block;

typedef struct thread_data {
    block *blocks;
    char *buf;
    unsigned int numToWrite;
    int file_in;
    int file_out;
    size_t read, wrote, fsize;
    unsigned int rwaits, wwaits;
} thread_data;

void *statusWriter(void *targ) {
    thread_data *td = (thread_data *)targ;
    while(true) {
        printf("\rRead %3ld%%, Wrote %3ld%%, Waits (read/write): (%d/%d)", (td->read * 100)/td->fsize, (td->wrote * 100)/td->fsize, td->rwaits, td->wwaits);
        fflush(stdout);
        usleep(STATUS_REFRESH);
    }
    pthread_exit(NULL);
}

void *readerThread(void *targ) {
    thread_data *td = (thread_data *)targ;
    unsigned int idx = 0;
    while(true) {
        pthread_mutex_lock(&rw_mutex);
        if(td->numToWrite >= num_block) {
            td->rwaits++;
            pthread_cond_wait(&ready_to_read_cv, &rw_mutex);
        }
        pthread_mutex_unlock(&rw_mutex);

        int ret = read(td->file_in, &td->buf[buf_idx(idx)], sizeof(char)*block_size);
        switch(ret) {
            case -1:
            perror("read error: ");
            td->blocks[idx].end = true;
            td->blocks[idx].size = 0;
            pthread_exit(NULL);
            break;

            case 0:
            td->blocks[idx].end = true;

            default:
            td->blocks[idx].size = ret;
        }

        pthread_mutex_lock(&rw_mutex);
        td->numToWrite++;
        pthread_cond_signal(&ready_to_write_cv);
        pthread_mutex_unlock(&rw_mutex);

        if(td->blocks[idx].end) pthread_exit(NULL);

        td->read += td->blocks[idx].size;
        idx = next_block_idx(idx);
    }
    pthread_exit(NULL);
    return NULL;
}

void *writerThread(void *targ) {
    thread_data *td = (thread_data *)targ;
    unsigned int idx = 0;
    while(true) {
        pthread_mutex_lock(&rw_mutex);
        if(td->numToWrite < 1) {
            td->wwaits++;
            pthread_cond_wait(&ready_to_write_cv, &rw_mutex);
        }
        pthread_mutex_unlock(&rw_mutex);

        int ret = write(td->file_out, &td->buf[buf_idx(idx)], td->blocks[idx].size);
        if(ret == -1) perror("write error: ");
        if(ret != td->blocks[idx].size) printf("we didn't write everything!");

        if(td->blocks[idx].end) pthread_exit(NULL);

        pthread_mutex_lock(&rw_mutex);
        td->numToWrite--;
        pthread_cond_signal(&ready_to_read_cv);
        pthread_mutex_unlock(&rw_mutex);
        td->wrote += td->blocks[idx].size;
        idx = next_block_idx(idx);
    }
    pthread_exit(NULL);
    return NULL;
}

void usage(const char *name) {
    fprintf(stderr, 
"\n"
"Usage: %s [-s block_size -b num_block] <in_file> <out_file>\n" 
"\n"
"\tblock_size : size (in bytes) of each block\n"
"\tnum_block  : the number of blocks in the buffer\n"
"\n"
"total buffer size in bytes = block_size * num_block\n"
"\n",
name);
}

int main(int argc, char *argv[]) {
    struct timeval t_start, t_end;
    block_size = BLOCK_SIZE;
    num_block = NUM_BLOCK;

    char *name = argv[0];
    int ch, fd;
    while((ch = getopt(argc, argv, "b:s:")) != -1) {
        switch(ch) {
            case 'b':
            num_block = atoi(optarg);
            break;
            case 's':
            block_size = atoi(optarg);
            break;
            default:
            usage(name);
            return -1;
        }
    }
    argc -= optind;
    argv = &argv[optind];

    if(argc != 2) {
        usage(name);
        return -1;
    }
    thread_data td;
    td.file_in = open(argv[0], O_RDONLY);
    if(td.file_in == -1) {
        perror("couldn't open input file: ");
        return -1;
    }
    td.fsize = lseek(td.file_in, 0, SEEK_END);
    lseek(td.file_in, 0, SEEK_SET);
    td.file_out = open(argv[1], O_WRONLY|O_CREAT);
    if(td.file_out == -1) {
        perror("couldn't open output file: ");
        return -1;
    }

    td.blocks = calloc(num_block, sizeof(block));
    if(td.blocks == NULL) {
        perror("failed to allocate buffer: ");
        return -1;
    }
    td.buf = malloc(num_block*sizeof(char)*block_size);
    if(td.buf == NULL) {
        perror("failed to allocate buffer: ");
        return -1;
    }
    td.numToWrite = 0;
    td.read = 0;
    td.wrote = 0;
    td.rwaits = 0;
    td.wwaits = 0;

    printf("Starting block-level copy from \"%s\" to \"%s\" with buffer: %d (blocks), block: %d (bytes)\n", argv[0], argv[1], num_block, block_size);

    gettimeofday(&t_start, NULL);

    pthread_mutex_init(&rw_mutex, NULL);
    pthread_cond_init(&ready_to_write_cv, NULL);
    pthread_cond_init(&ready_to_read_cv, NULL);

    pthread_t threads[3];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    pthread_create(&threads[2], NULL, statusWriter, (void *)&td);
    pthread_create(&threads[0], &attr, readerThread, (void *)&td);
    pthread_create(&threads[1], &attr, writerThread, (void *)&td);
    pthread_attr_destroy(&attr);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    pthread_cancel(threads[2]);
    gettimeofday(&t_end, NULL);

    printf("\nCompleted in %0.2f seconds\n",  (float)(usec(t_end) - usec(t_start))/1000000);

    pthread_cond_destroy(&ready_to_read_cv);
    pthread_cond_destroy(&ready_to_write_cv);
    pthread_mutex_destroy(&rw_mutex);

    close(td.file_in);
    close(td.file_out);
    free(td.blocks);
    free(td.buf);
    pthread_exit(NULL);
    return 0;

    return 0;
}
