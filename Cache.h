//
// Created by orlykor12 on 5/23/16.
//

#ifndef EX4_CACHE_H
#define EX4_CACHE_H

#include <vector>
#include <linux/limits.h>
#include <fstream>

using namespace std;


typedef struct {
    char * buff;
    size_t len;
    int refCount;
    off_t offSet;
    char path[PATH_MAX] = {0};
} block;


typedef struct {
    unsigned int numOfBlocks;
    unsigned int firstOld;
    unsigned int lastNew;
    vector <block*> cacheVec;
    char rootDir[PATH_MAX];
    fstream logFile;
    unsigned int blksize;
    int countFreeBlocks;

} cacheStruct;

void init_cache(cacheStruct& cache, int numOfBlocks, double fOld, double fNew,
                char rootDir[PATH_MAX]);


int findBlock(cacheStruct& cache, off_t offSet, const char * path);


void moveBlockToTop(cacheStruct& cache, unsigned int index);

void addNewBlock(cacheStruct& cache, const char path[PATH_MAX], char * buff,
                 size_t len, off_t offSet);

void removeBlock(cacheStruct& cache);

#endif //EX4_CACHE_H
