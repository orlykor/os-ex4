#include <sys/stat.h>
#include <cmath>
#include <string.h>
#include <iostream>
#include "Cache.h"

/**
 * initializes the Cache struct.
 */
void init_cache(cacheStruct& cache, int numOfBlocks, double fOld, double fNew,
                char rootDir[PATH_MAX]){
    struct stat fi;
    stat("/tmp", &fi);
    cache.blksize = (unsigned int) fi.st_blksize;
    cache.numOfBlocks = (unsigned int)numOfBlocks;
    cache.firstOld = (unsigned int)(numOfBlocks - floor(numOfBlocks * fOld));
    cache.lastNew = (unsigned int)floor(numOfBlocks * fNew);
    strcpy(cache.rootDir, rootDir);
    if (cache.rootDir[strlen(cache.rootDir) - 1] != '/') {
        size_t len = strlen(cache.rootDir);
        cache.rootDir[len] = '/';
        cache.rootDir[len + 1] = '\0';
    }
    cache.countFreeBlocks = numOfBlocks;
    try{
        char logPath[PATH_MAX];
        strcpy(logPath, cache.rootDir);
        strcat(logPath, "/.filesystem.log");
        cache.logFile.open(logPath, fstream::app | fstream::out);
    }
    catch (exception& e){
        cout << "System Error: " << e.what();
        exit(1);
    }
}

/**
 * finds the block in the blocks's vector.
 *
 * return the index of the block in the vector if exists.
 * return -1 if block not exists.
 *
 */
int findBlock(cacheStruct& cache, off_t offSet, const char * path){
    for(unsigned int i = 0; i < cache.cacheVec.size(); i++){
        if((cache.cacheVec[i]->offSet == offSet )&& (strcmp(cache.cacheVec[i]->
                path, path) == 0)){
            return i;
        }
    }
    return -1;
}

/**
 * move the given block to the top of the cache.
 *
 */
void moveBlockToTop(cacheStruct& cache, unsigned int index){
    //checks if the block is not new in order to add 1 to the refcount
    if(index >= cache.lastNew){
        cache.cacheVec[index]->refCount++;
    }
    //add the block to the beginning of the vector.
    block *tmp = cache.cacheVec[index];
    cache.cacheVec.erase(cache.cacheVec.begin() + index);
    cache.cacheVec.insert(cache.cacheVec.begin(),tmp);
}

/**
 *  on a cache miss delete a selected block according to the FBR algorithm.
 */
void removeBlock(cacheStruct& cache){
    unsigned int toDelete = cache.firstOld;
    for(unsigned int i = toDelete + 1; i < cache.cacheVec.size(); i++){
        if(cache.cacheVec[i]->refCount <= cache.cacheVec[toDelete]->refCount){
            toDelete = i;
        }
    }
    //release all the allocated data
    free(cache.cacheVec[toDelete]->buff);
    delete cache.cacheVec[toDelete];
    cache.cacheVec.erase(cache.cacheVec.begin() + toDelete);
}

/**
 * if the block does not exists, add a new block to the beginning of the
 * cache.
 */
void addNewBlock(cacheStruct& cache, const char path[PATH_MAX], char * buff,
                 size_t len, off_t offSet){
    block * nBlock = new block;
    nBlock->buff = buff;
    nBlock->len = len;
    nBlock->offSet = offSet;
    nBlock->refCount = 1;
    memcpy(nBlock->path, path, strlen(path));
    cache.cacheVec.insert(cache.cacheVec.begin(), nBlock);
    if(cache.cacheVec.size() > cache.numOfBlocks){
        removeBlock(cache);
    }
}