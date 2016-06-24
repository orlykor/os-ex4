/*
 * CachingFileSystem.cpp
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <fstream>
#include <ctime>
#include <errno.h>
#include <string.h>
#include <linux/limits.h>
#include <iostream>
#include <unistd.h>
#include <dirent.h>
#include <cmath>
#include "Cache.h"
using namespace std;
struct fuse_operations caching_oper;
static cacheStruct cache;

/**
 * Check if the result of the functions is correct
 */
int checkResult(int res){
	if(res < 0 ){
		return -errno;
	}
	return res;
}

/**
 * Convert the path relative to mountdir to the absolute path (located in
 * rootdir)
 */
void caching_fullPath(char fullPath[PATH_MAX], const char* path) {
	strcpy(fullPath, cache.rootDir);
	if (path[0] == '/') {
		strncat(fullPath, path + 1, PATH_MAX);
	}
	else {
		strncat(fullPath, path, PATH_MAX);
	}
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int caching_getattr(const char *path, struct stat *statbuf){

	char fullPath[PATH_MAX];

	cache.logFile << time(NULL) << " getattr\n";
	cache.logFile.flush();
	if(strcmp(path, "/.filesystem.log") == 0){
		return -ENOENT;
	}
	caching_fullPath(fullPath,path);
	return checkResult(lstat(fullPath, statbuf));
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
int caching_fgetattr(const char *path, struct stat *statbuf,
					 struct fuse_file_info *fi){
	cache.logFile << time(NULL) << " fgetattr\n";
	cache.logFile.flush();
	if(strcmp(path, "/.filesystem.log") == 0){
		return -ENOENT;
	}
	if(!strcmp(path, "/")){
		caching_getattr(path, statbuf);
	}
	return checkResult(fstat((int)fi->fh, statbuf));
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
int caching_access(const char *path, int mask)
{
	char fullPath[PATH_MAX];
	cache.logFile << time(NULL) << " access\n";
	cache.logFile.flush();
	if(strcmp(path, "/.filesystem.log") == 0){
		return -ENOENT;
	}
	caching_fullPath(fullPath,path);
	return checkResult(access(fullPath, mask));
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * initialize an arbitrary filehandle (fh) in the fuse_file_info 
 * structure, which will be passed to all file operations.

 * pay attention that the max allowed path is PATH_MAX (in limits.h).
 * if the path is longer, return error.

 * Changed in version 2.2
 */
int caching_open(const char *path, struct fuse_file_info *fi){
	char fullPath[PATH_MAX];
	cache.logFile << time(NULL) << " open\n";
	cache.logFile.flush();

	if(strcmp(path, "/.filesystem.log") == 0){
		return -ENOENT;
	}
	if((fi->flags & (O_RDWR|O_WRONLY)) > 0){
		return  -EACCES;
	}
	fi->direct_io = 1;
	caching_fullPath(fullPath,path);

	int fd = open(fullPath, O_RDONLY|O_DIRECT|O_SYNC);
	if(fd >= 0){
		fi->fh = (uint64_t) fd;

		return 	0;
	}
	return -errno;
}


/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error. For example, if you receive size=100, offest=0,
 * but the size of the file is 10, you will init only the first 
   ten bytes in the buff and return the number 10.
   
   In order to read a file from the disk, 
   we strongly advise you to use "pread" rather than "read".
   Pay attention, in pread the offset is valid as long it is 
   a multipication of the block size.
   More specifically, pread returns 0 for negative offset 
   and an offset after the end of the file
   (as long as the the rest of the requirements are fulfiiled).
   You are suppose to preserve this behavior also in your implementation.

 * Changed in version 2.2
 */
int caching_read(const char *path, char *buf, size_t size,
				 off_t offset, struct fuse_file_info *fi){

	char fullPath[PATH_MAX];
	cache.logFile << time(NULL) << " read\n";
	cache.logFile.flush();

	if(strcmp(path, "/.filesystem.log") == 0){
		return -ENOENT;
	}
	caching_fullPath(fullPath,path);

	unsigned int firstBlock = (int)offset/cache.blksize;
	size_t leftToRead = size;
	unsigned int i = 0;
	unsigned int sum = 0;
	unsigned int startIndex = (unsigned int)(offset - firstBlock*cache.blksize);
	while(leftToRead > 0){
		int blockIndex = findBlock(cache, (firstBlock+i) * cache.blksize,
								   fullPath);
		if(blockIndex < 0){
			char * blockBuff = (char*)aligned_alloc((size_t)cache.blksize,
													(size_t)cache.blksize);
			//returns how many bytes it had read
			ssize_t bytesRead = pread((int)fi->fh, blockBuff,
									  (size_t)cache.blksize,(firstBlock+i) *
															cache.blksize);

			if (bytesRead == 0) {
				break;
			}

			addNewBlock(cache, fullPath, blockBuff, (size_t) bytesRead,
						(firstBlock + i) * cache.blksize);
		}
		else{
			moveBlockToTop(cache, blockIndex);
		}
		size_t left = min(cache.cacheVec[0]->len - startIndex, leftToRead);
		if(left <= 0)
		{
			break;
		}
		memcpy(buf + sum, cache.cacheVec[0]->buff + startIndex, left);

		sum += left;
		if (cache.cacheVec[0]->len < cache.blksize) {
			break;
		}
		leftToRead -= left;
		startIndex = 0;
		i++;
	}
	return (int)sum;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
int caching_flush(const char *path, struct fuse_file_info *fi)
{
	cache.logFile << time(NULL) << " flush\n";
	cache.logFile.flush();
	return 0;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int caching_release(const char *path, struct fuse_file_info *fi){
	cache.logFile << time(NULL) << " release\n";
	cache.logFile.flush();
	return checkResult(close((int)fi->fh));
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int caching_opendir(const char *path, struct fuse_file_info *fi){
	DIR* dp;
	char fullPath[PATH_MAX];

	if(strcmp(path, "/.filesystem.log") == 0){
		return -ENOENT;
	}
	cache.logFile << time(NULL) << " opendir\n";
	cache.logFile.flush();
	caching_fullPath(fullPath,path);
	dp = opendir(fullPath);
	if(dp == NULL){
		return -errno;
	}
	fi->fh = (uint64_t) dp;
	return 0;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * Introduced in version 2.3
 */
int caching_readdir(const char *path, void *buf,
					fuse_fill_dir_t filler,
					off_t offset, struct fuse_file_info *fi){

	cache.logFile << time(NULL) << " readdir\n";
	DIR* dp = (DIR*) (uintptr_t) fi->fh;
	struct dirent *de = readdir(dp);
	if (de == 0){
		return -errno;
	}
	do{
		if (strcmp(de->d_name, ".filesystem.log") == 0 && strcmp(path, "/") == 0) {
			continue;
		}
		if(filler(buf, de->d_name, NULL, 0) != 0){
			return -ENOMEM;
		}
	}while((de = readdir(dp)) != NULL);

	return 0;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int caching_releasedir(const char *path, struct fuse_file_info *fi){
	cache.logFile << time(NULL) << " releasedir\n";
	cache.logFile.flush();
	return checkResult(closedir((DIR*) (uintptr_t) fi->fh));
}

/** Rename a file */
int caching_rename(const char *path, const char *newpath){
	cache.logFile << time(NULL) << " rename\n";
	char fullPath[PATH_MAX];
	caching_fullPath(fullPath,path);
	char nFullPath[PATH_MAX];
	caching_fullPath(nFullPath,newpath);
	struct stat s;
	if(stat(fullPath, &s) == 0) {
		if(s.st_mode & S_IFDIR)
		{
			if(fullPath[strlen(fullPath)-1] != '/'){
				size_t len = strlen(fullPath);
				fullPath[len] = '/';
				fullPath[len+1] = '\0';
			}
			if(nFullPath[strlen(nFullPath)-1] != '/'){
				size_t len = strlen(nFullPath);
				nFullPath[len] = '/';
				nFullPath[len+1] = '\0';
			}
			for(auto it = cache.cacheVec.begin(); it != cache.cacheVec.end();
				++it){
				if(strncmp(fullPath, (*it)->path, strlen(fullPath)) == 0){
					string sPath((*it)->path);
					sPath.replace(0,strlen(fullPath), nFullPath);
					memcpy((*it)->path, sPath.c_str(), strlen(sPath.c_str())+1);
				}
			}
		}
		else
		{
			for(auto it = cache.cacheVec.begin(); it != cache.cacheVec.end();
				++it){
				if(strncmp(fullPath, (*it)->path, strlen(fullPath)) == 0){
					memcpy((*it)->path, nFullPath, strlen(nFullPath)+1);
				}
			}
		}
	}
	else {
		return -errno;
	}
	return 	checkResult(rename(fullPath,nFullPath));
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *

If a failure occurs in this function, do nothing (absorb the failure
and don't report it).
For your task, the function needs to return NULL always
(if you do something else, be sure to use the fuse_context correctly).
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *caching_init(struct fuse_conn_info *conn){
	cache.logFile << time(NULL) << " init\n";
	cache.logFile.flush();
	return NULL;
}


/**
 * Clean up filesystem
 *
 * Called on filesystem exit.

If a failure occurs in this function, do nothing
(absorb the failure and don't report it).

 * Introduced in version 2.3
 */
void caching_destroy(void *userdata){
	cache.logFile << time(NULL) << " destroy\n";
	cache.logFile.flush();
	for(auto it = cache.cacheVec.begin(); it!= cache.cacheVec.end(); ++it){
		free((*it)->buff);
		delete (*it);
	}
	cache.cacheVec.clear();
	cache.logFile.close();
}

/**
 * Ioctl from the FUSE sepc:
 * flags will have FUSE_IOCTL_COMPAT set for 32bit ioctls in
 * 64bit environment.  The size and direction of data is
 * determined by _IOC_*() decoding of cmd.  For _IOC_NONE,
 * data will be NULL, for _IOC_WRITE data is out area, for
 * _IOC_READ in area and if both are set in/out area.  In all
 * non-NULL cases, the area is of _IOC_SIZE(cmd) bytes.
 *
 * However, in our case, this function only needs to print
 cache table to the log file .
 *
 * Introduced in version 2.8
 */
int caching_ioctl (const char *, int cmd, void *arg,
				   struct fuse_file_info *, unsigned int flags, void *data){

	cache.logFile << time(NULL) << " ioctl\n";
	cache.logFile.flush();
	for(auto it = cache.cacheVec.rbegin(); it != cache.cacheVec.rend(); ++it){
		string str((*it)->path);
		size_t found = str.find(cache.rootDir);
		str.erase(0, found+strlen(cache.rootDir));
		cache.logFile << str << " " << (*it)->offSet/cache.blksize + 1 << " " <<
		(*it)->refCount << "\n";
	}
	return 0;
}


// Initialise the operations.
// You are not supposed to change this function.
void init_caching_oper()
{
	caching_oper.getattr = caching_getattr;
	caching_oper.access = caching_access;
	caching_oper.open = caching_open;
	caching_oper.read = caching_read;
	caching_oper.flush = caching_flush;
	caching_oper.release = caching_release;
	caching_oper.opendir = caching_opendir;
	caching_oper.readdir = caching_readdir;
	caching_oper.releasedir = caching_releasedir;
	caching_oper.rename = caching_rename;
	caching_oper.init = caching_init;
	caching_oper.destroy = caching_destroy;
	caching_oper.ioctl = caching_ioctl;
	caching_oper.fgetattr = caching_fgetattr;


	caching_oper.readlink = NULL;
	caching_oper.getdir = NULL;
	caching_oper.mknod = NULL;
	caching_oper.mkdir = NULL;
	caching_oper.unlink = NULL;
	caching_oper.rmdir = NULL;
	caching_oper.symlink = NULL;
	caching_oper.link = NULL;
	caching_oper.chmod = NULL;
	caching_oper.chown = NULL;
	caching_oper.truncate = NULL;
	caching_oper.utime = NULL;
	caching_oper.write = NULL;
	caching_oper.statfs = NULL;
	caching_oper.fsync = NULL;
	caching_oper.setxattr = NULL;
	caching_oper.getxattr = NULL;
	caching_oper.listxattr = NULL;
	caching_oper.removexattr = NULL;
	caching_oper.fsyncdir = NULL;
	caching_oper.create = NULL;
	caching_oper.ftruncate = NULL;
}

int caching_checkErrors(int argc, char* argv[]){
	struct stat st1;
	struct stat st2;
	if(argc != 6){
		return -1;
	}
	if(stat(argv[1], &st1) < 0){
		return -1;
	}
	if(!S_ISDIR(st1.st_mode)){
		return -1;
	}
	if(stat(argv[2], &st2) < 0){
		return -1;
	}
	if(!S_ISDIR(st2.st_mode)){
		return -1;
	}
	if(atoi(argv[3]) <= 0){
		return -1;
	}
	if(atof(argv[4]) > 1 || atof(argv[4]) < 0 ||
	   floor(atof(argv[4])*atoi(argv[3])) == 0){
		return -1;
	}
	if(atof(argv[5]) > 1 || atof(argv[5]) < 0 ||
	   floor(atof(argv[5])*atoi(argv[3])) == 0){
		return -1;
	}
	if((atof(argv[4]) + atof(argv[5])) > 1){
		return -1;
	}
	return 0;
}



//the function main
int main(int argc, char* argv[]){

	if(caching_checkErrors(argc, argv) < 0){
		cout << "Usage: CachingFileSystem rootdir mountdir numberOfBlocks "
				"fOld fNew\n";
		exit(1);
	}
	char* fullRootDir = realpath(argv[1], NULL);
	init_cache(cache, atoi(argv[3]), atof(argv[4]), atof(argv[5]), fullRootDir);

	init_caching_oper();
	argv[1] = argv[2];
	for (int i = 2; i< argc; i++){
		argv[i] = NULL;
	}
	argv[2] = (char*) "-s";
//	argv[3] = (char*) "-f";

	argc = 3;
	int fuse_stat = fuse_main(argc, argv, &caching_oper, NULL);
	free(fullRootDir);

	return fuse_stat;
}
