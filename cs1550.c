/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING..
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3
#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

//this is where the block of files start
#define FILE_START 15360
//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;



struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE)

#define MAX_BLOCK_FOR_FILE (10210-20)
/*Each char represents a block in the .disk file
 *this map will keep track of whether a block is
 *in use or not
 *There are total 10240 blocks in 5mb .disk
 *root + max_dirs = 1+29 = 30. 10240-30 = 10210 blocks
 *left for files. Although the last block 20 blocks will keep the map.
 */
struct map{
	 //bit map
	 unsigned char blockmap[MAX_BLOCK_FOR_FILE];
};

typedef struct map map;
struct cs1550_disk_block
{
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*This method opens the .disk file, and reads
 *in a the root block and returns it.
 */
static cs1550_root_directory* readRoot(){

		cs1550_root_directory* root = (cs1550_root_directory *)malloc(sizeof(cs1550_root_directory));

		FILE * fp = fopen(".disk", "rb+");
		fseek(fp, 0, SEEK_SET);
		fread(root, sizeof(cs1550_root_directory), 1, fp);
		fprintf(stderr, "RootNum %d\n", root->nDirectories);
		fclose(fp);


	return root;
}

/*
 *This reads the directory entry at the offset
 */
static cs1550_directory_entry* readDir(long offset){
		cs1550_directory_entry *dir = (cs1550_directory_entry *)malloc(sizeof(cs1550_directory_entry));

		FILE * fp = fopen(".disk", "rb+");
		fseek(fp, offset, SEEK_SET);
		fread(dir, sizeof(cs1550_directory_entry), 1, fp);
		fclose(fp);
		return dir;
}

/*
 *Writes the rootNode to the .disk after update
 */
static int writeRoot(cs1550_root_directory * root){


	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, 0, SEEK_SET);
	fwrite(root, sizeof(cs1550_root_directory), 1, fp);
	fclose(fp);

	return 1;
}

/*Writes the directory entry to .disk
 *only called when new dirs are created.
 */
static int writeDir(long offset){
	cs1550_directory_entry* newEntry = (cs1550_directory_entry*)malloc(sizeof(cs1550_directory_entry));
	newEntry->nFiles = 0;
	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, offset, SEEK_SET);
	fwrite(newEntry, sizeof(cs1550_directory_entry), 1, fp);
	fclose(fp);
	return 1;
}

/*
 *Updates the directory entry after new files are created.
 */
static int updateDir(long offset, cs1550_directory_entry * entry){
	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, offset, SEEK_SET);
	fwrite(entry, sizeof(cs1550_directory_entry), 1, fp);
	fclose(fp);
	return 1;
}

/*
 *Checks if the path contains two slashes(whether is sub dir or /)
 */
static int checkAccess(char * path){
	int i;
	int counter = 0;
	for(i = 0; i<strlen(path); i++){
		char x = path[i];
		if(x=='/'){
			counter++;
			if(counter>1){
				return 0;
			}
		}
	}
	return 1;
}

/*
 *Writes block of data to certain offset.
 */
static int writeFile(long offset, cs1550_disk_block * data){
	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, offset, SEEK_SET);
	fwrite(data, sizeof(cs1550_directory_entry), 1, fp);
	fclose(fp);
	return 1;
}

/*
 *Looks for a file, and returns the directory entry if it exists.
 *else returns NULL
 */
static cs1550_directory_entry* fileExist(char *path){
	fprintf(stderr, "path from write: %s\n", path);
	char directory[8];
	char  filename[8];
	char extension[3];
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	int i;
	cs1550_root_directory *root = readRoot();
	int numOfDirs = root->nDirectories;
	for(i=0; i<numOfDirs; i++){
		char *dirName = root->directories[i].dname;
		if(strcmp(dirName, directory)==0){
			int startBlock = root->directories[i].nStartBlock;
			cs1550_directory_entry *dir = readDir(startBlock);
			int j;
			for(j=0; j<dir->nFiles; j++){
			char *file = dir->files[j].fname;
			if(strcmp(file, filename)==0){
					return dir;
			}
			}
		}
	}
	return NULL;
}

/*
 *Finds a directory's offset, from the root directory.
 */
static int findOffset(char * directory){
	int i;

	cs1550_root_directory *root = readRoot();
	int numOfDirs = root->nDirectories;
	for(i=0; i<numOfDirs; i++){
		char *dirName = root->directories[i].dname;
		if(strcmp(dirName, directory)==0){
			int startBlock = root->directories[i].nStartBlock;
			return startBlock;
		}
	}
	return -1;
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{

	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
  //counter for for loops
	int i = 0;
	cs1550_root_directory* root;
	char directory[8];
	char  filename[8];
	char extension[3];
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	fprintf(stderr, "Path = %s\n", path);
	fprintf(stderr, "Direc = %s\n", directory);
	fprintf(stderr, "Fielname = %s\n",filename);
	fprintf(stderr, "Extension = %s\n",extension);
	root = readRoot();
	int counter = 0;

	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	} else {
	//return that path doesn't exist if
	//none of the other conditions under
	//work
	res = -ENOENT;
	//Check if name is subdirectory
	//go through the array of directories checking for each name
	for(i = 0; i<MAX_DIRS_IN_ROOT; i++){
		char *dir_path = root->directories[i].dname;

		if(strcmp(directory, dir_path)==0){
			int startBlock = root->directories[i].nStartBlock;
			cs1550_directory_entry * dir = readDir(startBlock);
			fprintf(stderr, "FOUND DIR, Files in Dir = %d\n", dir->nFiles);
				//If there are two slashes in in the path,
				//and there are no files in the directory
				//user is probably trying to make a directory in a directory.
				if(checkAccess(path)==0&&((dir->nFiles)==0)){
					return -ENOENT;
				}
				if(checkAccess(path)==0&&(dir->nFiles)>0){

							int j;
							for(j = 0; j<MAX_FILES_IN_DIR; j++){
								char *name_path =  dir->files[j].fname;
								//Check if name is a regular file

								if(strcmp(filename, name_path)==0){
									//regular file, probably want to be read and write
									stbuf->st_mode = S_IFREG | 0666;
									stbuf->st_nlink = 1; //file links
									stbuf->st_size = dir->files[j].fsize; //file size - make sure you replace with real size!
									fprintf(stderr, "FOUND FILE : %s\n", name_path);
									return 0;
								}

							}
							fprintf(stderr, "NOT A FILE\n");
							return -ENOENT;
						}
						else{
							//Might want to return a structure with these fields
							stbuf->st_mode = S_IFDIR | 0755;
							stbuf->st_nlink = 2;
							return 0;
						}
				}

		}
		fprintf(stderr, "NOT A DIRECTORY\n");
		return -ENOENT;
	}

}



/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

	cs1550_root_directory * root  = readRoot();
	char directory[9];

	sscanf(path, "/%[^/]", directory);

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	if (strcmp(path, "/") == 0){

		if(root->nDirectories>0){
			int i = 0;
			//if this was the root directory, and fill with all the sub dirs.
			for(i = 0; i<root->nDirectories; i++){
					filler(buf, root->directories[i].dname, NULL, 0);
				}
			}
			return 0;
	}
	else{
		int i;
		for(i=0; i<MAX_DIRS_IN_ROOT; i++){
			char *dirName = root->directories[i].dname;
			//if you found the sub dir, fill with all the files in the  sub dir.
			if(strcmp(dirName, directory)==0){
					int start = root->directories[i].nStartBlock;
					cs1550_directory_entry *dir  = readDir(start);
					if(dir->nFiles>0){
					for(i = 0; i<dir->nFiles; i++){
							char name[12];
							strcpy(name, dir->files[i].fname);
							fprintf(stderr, "EXTENSION : %s\n", dir->files[i].fext);
							if(strcmp(dir->files[i].fext,"")!=0){
								strcat(name, ".");
								strcat(name, dir->files[i].fext);
						  }
							filler(buf, name, NULL, 0);
						}
					}
					return 0;
			}
		}

	}
	return -ENOENT;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	int res;
	(void) path;
	(void) mode;
	char name[9];

	int i;
	int counter = 0;
	//if this is not being created in root dir
	//return

	if(checkAccess(path)==0){
		return -EPERM;
	}

	//if dir_name too long, return error
	if((strlen(path)-1)>8){
		res = -ENAMETOOLONG;
		return res;
	}
	//scan the name of the dir the user wants to create
	sscanf(path, "/%s", name);

	//read teh root from the .disk file
	cs1550_root_directory* root = readRoot();

	//get the number of directories already existing
	int directoryNum = root->nDirectories;
	int startBlock;

	//if the directory already exist.
	for(i = 0; i<directoryNum; i++){
		if(strcmp(root->directories[i].dname, name)==0){
			return -EEXIST;
		}
	}
	//copy the name of this directory the user wants to create
	//in the dirName in the root
	strcpy((root->directories[directoryNum]).dname, name);

	//get the directories start BLOCK_SIZE
	//by adding the root block and all the blocks already created.
	(root->directories[directoryNum]).nStartBlock = BLOCK_SIZE+(directoryNum*BLOCK_SIZE);

	//save the start block
	startBlock = BLOCK_SIZE+(directoryNum*BLOCK_SIZE);
	//add one to the number of directoreis
	root->nDirectories = root->nDirectories+1;
	//write the root back to disk, and directory entry to disk
	writeRoot(root);
	writeDir(startBlock);
	return 0;
}

/*
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

//This is total number of files there can exist in the system.
#define MAX_FILES_SYSTEM ((MAX_DIRS_IN_ROOT)*(MAX_FILES_IN_DIR))

/*
 *Find where to write the file on disk. Starting at the begnning of map.
 */
 static long findFreeSpace(){
	 map * data = (map *)malloc(sizeof(map));
	 FILE * fp = fopen(".disk", "rb+");
	 fseek(fp, -sizeof(map), SEEK_END);
	 fread(data, sizeof(map), 1, fp);

	 long i = 0;
	 //go through the .disk file looking for a free spot
	 //to write our file.

	 for(i = 0; i<MAX_BLOCK_FOR_FILE; i++){
		 if(data->blockmap[i]==0){
				fprintf(stderr, "FREE SPACE POINT INDEX = %d\n", i);
				fclose(fp);
		 		return i;
	 		}

 	 }
	 fclose(fp);
 	 return -1;

 }


/*
 *This function updates the bitmap, with whether the block is taken or not.
 */
 static int updateMap(int index, char cond){
	 	map * data = (map *)malloc(sizeof(map));
		FILE * fp = fopen(".disk", "rb+");
		fseek(fp, -sizeof(map), SEEK_END);
		fread(data, sizeof(map), 1, fp);
		data->blockmap[index] = cond;
		fseek(fp, -sizeof(map), SEEK_END);
		fwrite(data,1, sizeof(map), fp);
		fclose(fp);
 }

/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;

	if(checkAccess(path)!=0){
		return -EPERM;
	}
	int i;
	cs1550_root_directory* root;
	char directory[8] = "0";
	char  filename[8] = "0";
	char extension[3] = "";
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	fprintf(stderr, "PATH : %s\n", path);
	fprintf(stderr, "DIR NAME: %s\n", directory);
	fprintf(stderr, "FILE NAME: %s\n", filename);
	fprintf(stderr, "EXT NAME: %s\n", extension);
	if(strlen(filename)>8||strlen(extension)>3){
		return -ENAMETOOLONG;
	}
	//make sure user is not trying to create file in root


	root = readRoot();


	for(i = 0; i<MAX_DIRS_IN_ROOT; i++){

		char *dirName = root->directories[i].dname;

		if(strcmp(dirName, directory)==0){
			int startBlock = root->directories[i].nStartBlock;
			cs1550_directory_entry * dir = readDir(startBlock);

			//if there are no files in the directory
			//write the first file
			if(dir->nFiles==MAX_FILES_IN_DIR){
				fprintf(stderr, "CANOT CREATE MORE FILES IN THIS DIR\n");
				return -EPERM;
			}
			if(dir->nFiles==0){
				fprintf(stderr, "Making first file : %s\n", filename);
				dir->nFiles = dir->nFiles+1;
				strcpy(dir->files[0].fname, filename);
				strcpy(dir->files[0].fext, extension);
				dir->files[0].fsize = 0;

				long start = findFreeSpace();
				updateMap(start, 1);
				start = (start*512)+FILE_START;
				fprintf(stderr,"Start : %d\n", start);
				dir->files[0].nStartBlock = start;
				cs1550_disk_block *data = (cs1550_disk_block *)malloc(sizeof(cs1550_disk_block));
				writeFile(start, data);
				updateDir(startBlock, dir);
			}
			//else find next free space and write it there
			else{
				fprintf(stderr, "Making not first file: %s\n", filename);
				int numOfFiles = dir->nFiles;
				strcpy(dir->files[numOfFiles].fname, filename);
				strcpy(dir->files[numOfFiles].fext, extension);
				dir->files[numOfFiles].fsize = 0;
				long start = findFreeSpace();
				fprintf(stderr,"Writing file at index %d\n", start);
				updateMap(start, 1);
				start = (start*512)+FILE_START;
				fprintf(stderr,"Start : %d\n", start);
				dir->files[numOfFiles].nStartBlock = start;
				cs1550_disk_block *data = (cs1550_disk_block *)malloc(sizeof(cs1550_disk_block));
				dir->nFiles = dir->nFiles+1;
				writeFile(start, data);
				updateDir(startBlock, dir);
			}
		}
	}



	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/*
 *This function takes a block, some position(in buffer), and writes the block to the buffer.
 */
static int writeBlockToBuf(char *buf, cs1550_disk_block * block, int pos){
	int i = 0;
	for(i = 0; i<BLOCK_SIZE; i++){
		buf[pos+i] = block->data[i];
	}
	pos = pos+BLOCK_SIZE;
	return pos;
}
/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;
	char directory[8];
	char  filename[8];
	char extension[3];
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	fprintf(stderr, "SIZE OF SIZE_T = %d\n", size);

	if(checkAccess(path)!=0){
		return -EISDIR;
	}
	//check to make sure path exists
	struct cs1550_directory_entry* dir = fileExist(path);
	int i;
	//check to make sure path exists
	if(dir==NULL){
		return -1;
	}
	//check that size is > 0
	if(size<1){
		return -1;
	}

	if(dir!=NULL){
		for(i = 0; i<dir->nFiles; i++){
				char *name = dir->files[i].fname;
				if(strcmp(name, filename)==0){
					int fileSize = dir->files[i].fsize;
					int fileStart = dir->files[i].nStartBlock;
					fprintf(stderr, "THE FILE SIZE IS FROM FILE READ %d\n", fileSize);
					//check that offset is <= to the file size
					if(offset<=fileSize){
						int numOfBlocks = (size/BLOCK_SIZE);
						FILE * fp = fopen(".disk", "rb+");
						int seekPoint = fileStart + offset;
						fprintf(stderr, "FILESTART %d\n", fileStart);
						fprintf(stderr, "SEEKPOINT %d\n", seekPoint);
						cs1550_disk_block *block = (cs1550_disk_block*)malloc(sizeof(cs1550_disk_block));
						int pos = 0;
						int retPos = 0;

						for(i = 0; i<numOfBlocks; i++){
							//read in data
							fprintf(stderr, "READING FROM : %d\n", pos);
							fseek(fp, seekPoint+(BLOCK_SIZE*i), SEEK_SET);
							fread(block, sizeof(cs1550_disk_block), 1, fp);
							retPos = writeBlockToBuf(buf, block, pos);
							pos = retPos;
						}
						free(block);
						fclose(fp);
						return size;
					}
					else{
						return -1;
					}
				}
		}
	}




	//set size and return, or error

	return -1;
}

/*
 *Writes data to file, if the data is less than 512 bytes.
 */
static int writeDataToFile(const char* buf, size_t size, long startBlock, off_t offset){

	FILE * fp = fopen(".disk", "rb+");
	int seekPoint = startBlock + offset;
	fseek(fp, seekPoint, SEEK_SET);
	fwrite(buf, size, 1, fp);
	fclose(fp);
	}

/*
 *Writes individual blocks to file.
 */
static int writeBlockToFile(cs1550_disk_block * block, long offset){
	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, offset, SEEK_SET);
	fwrite(block, sizeof(cs1550_disk_block), 1, fp);
	fclose(fp);
	return 1;
}

/*
 *This function read the data from the file to a block and returns it.
 */
static cs1550_disk_block * readDataToBlock(long offset){
	cs1550_disk_block *block = (cs1550_disk_block*)malloc(sizeof(cs1550_disk_block));
	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, offset, SEEK_SET);
	fread(block, sizeof(cs1550_disk_block), 1, fp);
	fclose(fp);
	return block;
}

/*
 *This function starts at the end of the bit map, and finds the end
 *where a free block exists.
 */
static int findEnd(){
	map * data = (map *)malloc(sizeof(map));
	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, -sizeof(map), SEEK_END);
	fread(data, sizeof(map), 1, fp);
	int i = 0;
	for(i = MAX_BLOCK_FOR_FILE; i>-1; i--){
		if(data->blockmap[i]==1){
			fclose(fp);
			return i+1;
		}
	}
	fclose(fp);
	return -1;
}

/*
 *This function moves file to the end, that is blocking some file that is trying to apend.
 */
static long moveFiles(long location, long fileSize){
		cs1550_root_directory * root = readRoot();
		int i = 0;
		int j = 0;
		int totalDirs = root->nDirectories;
		for(i = 0; i<totalDirs; i++){
			char * dirName = root->directories[i].dname;
			int dirOff = findOffset(dirName);
			cs1550_directory_entry *dir = readDir(dirOff);
			for(j = 0; j<dir->nFiles; j++){
				long startBlock = dir->files[j].nStartBlock;
				fprintf(stderr, "NAME : %s\n",dir->files[j].fname);
				fprintf(stderr, "START : %d\n",dir->files[j].nStartBlock);
				if(startBlock==location){
						long retSize = dir->files[j].fsize;

						fprintf(stderr, "MOVING BLOCKING FILE\n");
						fprintf(stderr, "MOVING FILE: %s\n", dir->files[j].fname);
						int freePoint = findEnd();
						long newLocation = FILE_START+(freePoint*BLOCK_SIZE);
						int blocks = (int)(retSize/BLOCK_SIZE);

						int startToLook = ((startBlock-FILE_START)/BLOCK_SIZE);
						//update the map with free spaces got from moving this file
						for(i = -1; i<blocks; i++){
							updateMap(startToLook+(i+1), 0);
							fprintf(stderr, "MAP UPDATE : INDEX FREED: %d\n", startToLook+(i+1));
						}
						dir->files[j].nStartBlock = newLocation;
						fprintf(stderr, "NEW LOCATION: %d\n", newLocation);
						for(i = -1; i<blocks; i++){

							cs1550_disk_block * block = readDataToBlock(startBlock);
							writeBlockToFile(block, newLocation);
							updateMap(freePoint+(i+1), 1);
							fprintf(stderr, "MAP UPDATED AT WITH 1 :  %d\n", freePoint+(i+1));
							startBlock = startBlock + BLOCK_SIZE;
							newLocation = newLocation + BLOCK_SIZE;

						}
						updateDir(dirOff, dir);
						return 1;


				}
			}
		}
		return -1;
}

/*
 *This function writes buffer data to a block.
 */
static int writeBufToBlock(int bufPos, cs1550_disk_block * block, const char * buf){
	int i;
	for(i = 0; i<512; i++){
		block->data[i] = buf[bufPos];
		bufPos++;
	}

	return bufPos;
}

/*
 *This function finds contiguous blocks from some index, for writing.
 */
static int findContigBlocks(int index){
	map * data = (map *)malloc(sizeof(map));
	FILE * fp = fopen(".disk", "rb+");
	fseek(fp, -sizeof(map), SEEK_END);
	fread(data, sizeof(map), 1, fp);
	if(data->blockmap[index]==0){
		fclose(fp);
		return 1;
	}

	return -1;
	fclose(fp);
}

/*
 *
 */
 static int printBitMap(){
	 map * data = (map *)malloc(sizeof(map));
	 FILE * fp = fopen(".disk", "rb+");
	 fseek(fp, -sizeof(map), SEEK_END);
	 fread(data, sizeof(map), 1, fp);
	 int i;
	 for(i = 0; i<50; i++){
		 fprintf(stderr,"BIT[%d] = %d\n", i, data->blockmap[i]);
	 }

	 fclose(fp);
 }
/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	fprintf(stderr,"OFSSET IN BEGINNING %d\n", offset);
	char directory[8];
	char  filename[8];
	char extension[3];
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	struct cs1550_directory_entry* dir = fileExist(path);
	int i;
	//check to make sure path exists
	if(dir==NULL){
		fprintf(stderr, "NULL DIR: %s\n", path);
		return -1;
	}
	//check that size is > 0
	if(size<1){
		fprintf(stderr, "SIZE LESS THAN 0s: %s\n", path);
		return -1;
	}

	if(dir!=NULL){
		for(i = 0; i<dir->nFiles; i++){
				char *name = dir->files[i].fname;
				int fileFound;
				if(strcmp(name, filename)==0){
					int fileSize = dir->files[i].fsize;
					int fileStart = dir->files[i].nStartBlock;
					//check that offset is <= to the file size
					fileFound =  i;
					fprintf("FILE SIZE = %d\n", fileSize);
					if(offset<=fileSize){

						//append
						if((offset!=0)&&offset==fileSize){
							fprintf(stderr,"THIS AN APEND\n");
							fprintf(stderr, "SIZE = %d\n", size);
							if(size>512){
								fprintf(stderr, "MAY NEED NEW BLOCK : %d\n", size);
								int blocksOccu;
								int blocksNeededNow;
								int bufRet = 0;
								int bufPos = 0;
								if((fileSize%BLOCK_SIZE)!=0){
									blocksOccu = (fileSize/BLOCK_SIZE)+1;
									blocksNeededNow = (size/BLOCK_SIZE)+1;
									fprintf(stderr, "NOT JUST MAKING A BIG FILE!\n");
								}
								else{
									blocksNeededNow = (size/BLOCK_SIZE);
									blocksOccu = (fileSize/BLOCK_SIZE);
									fprintf(stderr, "JUST MAKING A BIG FILE, THIS APENDING 4096+ BYTES AT THE END\n");
								}

								int blocksNeeded = blocksNeededNow;
								int blocksFound = 0;
								int blockTaken = 0;
								int spaceOccu = blocksOccu*BLOCK_SIZE;
								int startToLook = (((fileStart + spaceOccu)-FILE_START)/BLOCK_SIZE);

								fprintf(stderr, "START LOOKING FOR FREE BLOCKS FROM HERE = %d\n", startToLook);
								int j = 0;
								for(j = 0; j<blocksNeeded; j++){
									if((startToLook+j)==MAX_FILES_SYSTEM){
										return -EFBIG;
									}
									if(findContigBlocks(startToLook+j)==1){
										blocksFound++;
									}
									else{
										blockTaken = j+startToLook;
										break;
									}
								}
								//If I was able to find, the number of blocks needed
								//to write this size bytes to file.
								if(blocksFound==blocksNeeded){
									fprintf(stderr, "NO MOVING REQUIRED\n");
									int bufPos = 0;
									int bufRet = 0;
									int filePos = fileStart+spaceOccu;
									int blocksWrite = blocksNeeded;
									//since I can only write total of 8 blocks 4096 bytes every write!
									if(blocksWrite>8){
										blocksWrite = 8;
									}
									fprintf(stderr, "BLOCKS NEEDED %d\n", blocksNeeded);
									cs1550_disk_block *block = (cs1550_disk_block * )malloc(sizeof(cs1550_disk_block));
									int startIndex = (((fileStart+spaceOccu)-FILE_START)/BLOCK_SIZE);

									for(j = 0; j<blocksWrite; j++){
										fprintf(stderr, "WRITING BLOCK NO: %d \n", j);
										fprintf(stderr, "WRITING AT BUF POS: %d \n", bufRet);
										bufRet = writeBufToBlock(bufPos, block, buf);
										bufPos = bufRet;
										writeBlockToFile(block, filePos+(j*BLOCK_SIZE));
										updateMap((startIndex+j), 1);
										fprintf(stderr, "MAP UPDATED AT %d\n", (startIndex+j));

									}

									int dirStart = findOffset(directory);
									cs1550_directory_entry *entry = readDir(dirStart);
									entry->files[fileFound].fsize = size+fileSize;
									entry->files[fileFound].nStartBlock = fileStart;
									updateDir(dirStart, entry);
									free(block);
									printBitMap();
									return size;
								}
								else{
									int filePos = fileStart+spaceOccu;
									fprintf(stderr, "MOVING REQUIRED");
									int ret;
									blocksFound = 0;
									while(blocksNeeded!=blocksFound){
										fprintf(stderr, "BLOCKS NEEDED : %d\n", blocksNeeded);
										fprintf(stderr, "BLOCKS FOUND: %d\n", blocksFound);

										fprintf(stderr, "INDEX AT WHICH BLOCK IS TAKEN : %d\n", blockTaken);
										long loc = FILE_START+(blockTaken*BLOCK_SIZE);
										ret = moveFiles(loc, fileSize);
										//if ret==-1 it means that I already occupy that block(no file exists there)
										//so I go ahead and write it.
										if(ret==-1){
											cs1550_disk_block * block = (cs1550_disk_block*)malloc(sizeof(cs1550_disk_block));

											fprintf(stderr, "I OCCUPY! INDEX = %d\n", startToLook);
											bufRet = writeBufToBlock(bufPos, block, buf);
											bufPos = bufRet;
											writeBlockToFile(block, filePos);
											updateMap((startToLook), 1);
											fprintf(stderr, "BIT MAP UPDATED AT INDEX %d WTIH 1\n", startToLook);
											spaceOccu = spaceOccu+512;
											blocksNeeded--;
											startToLook++;
											free(block);
										}
										blocksFound = 0;
										for(j = 0; j<blocksNeeded; j++){
											if((startToLook+j)==MAX_FILES_SYSTEM){
												return -EFBIG;
											}
											if(findContigBlocks(startToLook+j)==1){
												blocksFound++;
											}
											else{
												blockTaken = j+startToLook;
												break;
											}
										}
									}
									//if blocks needed ==0 means that I've written all data that I needed to, this write.
									if(blocksNeeded==0){
										fprintf(stderr, "WROTE ALL BLOCKS I NEEDED TO\n");
										int dirStart = findOffset(directory);
										cs1550_directory_entry *entry = readDir(dirStart);
										entry->files[fileFound].fsize = size+fileSize;
										entry->files[fileFound].nStartBlock = fileStart;
										updateDir(dirStart, entry);
										printBitMap();
										return size;
									}
									//I moved enough blocks to be able to write.
									if(ret==1){
										fprintf(stderr, "MOVED ENOUGH BLOCKS TO WRITE MY BLOCK\n");
										int k;
										int blocksWrite = blocksNeeded;
										if(blocksWrite>8){
											blocksWrite = 8;
										}
										int filePos = fileStart+spaceOccu;
										bufPos = 0;
										bufRet = 0;
										int startIndex = (((fileStart+spaceOccu)-FILE_START)/BLOCK_SIZE);


										fprintf(stderr,"START INDEX = %d\n", startIndex);
										fprintf(stderr, "BLOCKS TO WRITE = %d\n", blocksWrite);
										cs1550_disk_block * block = (cs1550_disk_block *)malloc(sizeof(cs1550_disk_block));
										for(k = 0; k<blocksWrite; k++){
											bufRet = writeBufToBlock(bufPos, block, buf);
											bufPos = bufRet;
											writeBlockToFile(block, filePos+(k*BLOCK_SIZE));
											updateMap((startIndex+k), 1);
											fprintf(stderr, "BIT MAP UPDATED AT INDEX %d WITH 1\n", startIndex+k);

										}
										int dirStart = findOffset(directory);
										cs1550_directory_entry *entry = readDir(dirStart);
										entry->files[fileFound].fsize = size+fileSize;
										entry->files[fileFound].nStartBlock = fileStart;
										updateDir(dirStart, entry);
										free(block);
										printBitMap();
										return size;
									}

								}

							}
							//I'm apending a file that is smaller, than a block. And adding data that will
							//keep it smaller than a block. No moving required.
							else if(fileSize<512&&((fileSize+size)<512)){
									cs1550_disk_block *block = (cs1550_disk_block*)malloc(sizeof(cs1550_disk_block));
									fprintf(stderr, "WILL NOT NEED NEW BLOCK, SINCE WRITING SMALL FILE: %d\n", (fileSize+size));
									fprintf(stderr, "OFFSET : %d\n", offset);
									//writeBufToBlock(0, block, buf);
								//	writeBlockToFile(block, fileStart+offset);
								  writeDataToFile(buf, size, fileStart, offset);
									int sizeIncrease = fileSize+size;
									fprintf(stderr, "FILE SIZE BEFORE WRITE : %d\n", dir->files[i].fsize);
									dir->files[i].fsize = sizeIncrease;
									fprintf(stderr, "FILE SIZE AFTER WRITE : %d\n", dir->files[i].fsize);
									int dirStart = findOffset(directory);
									updateDir(dirStart, dir);
									free(block);
									printBitMap();
									return size;
							}
							//writing the smallest possible part of an append. Probably last block of a file.
							else if(size<512&&fileSize>512){
								cs1550_disk_block *block = (cs1550_disk_block*)malloc(sizeof(cs1550_disk_block));
								int blocksBet = (fileStart-FILE_START)/512;
								int blocksOccu = fileSize/BLOCK_SIZE;
								int indexToWrite = blocksBet+blocksOccu;
								fprintf(stderr, "INDEX TO WRITE = %d", indexToWrite);
								if((indexToWrite)==MAX_FILES_SYSTEM){
									return -EFBIG;
								}
								if(findContigBlocks(indexToWrite)==1){
									fprintf(stderr, "WRITING SMALLEST BLOCK OF APEND\n");
									fprintf(stderr, "OFFSET : %d\n", offset);
									writeBufToBlock(0, block, buf);
									writeBlockToFile(block, fileStart+offset);
									int sizeIncrease = fileSize+size;
									fprintf(stderr, "FILE SIZE BEFORE WRITE : %d\n", dir->files[i].fsize);
									dir->files[i].fsize = sizeIncrease;
									fprintf(stderr, "FILE SIZE AFTER WRITE : %d\n", dir->files[i].fsize);
									updateMap(indexToWrite,1);
									int dirStart = findOffset(directory);
									cs1550_directory_entry *entry = readDir(dirStart);
									entry->files[fileFound].fsize = size+fileSize;
									entry->files[fileFound].nStartBlock = fileStart;
									updateDir(dirStart, entry);
									free(block);
									printBitMap();
									return size;
								}
								else{
									int loc = (indexToWrite*BLOCK_SIZE)+FILE_START;
									int ret = moveFiles(loc, 0);
									if(ret==-1){
										fprintf(stderr, "I OCCUPY THIS BLOCK\n");
										writeDataToFile(buf, size, fileStart, offset);
										int dirStart = findOffset(directory);
										cs1550_directory_entry *entry = readDir(dirStart);
										entry->files[fileFound].fsize = size+fileSize;
										entry->files[fileFound].nStartBlock = fileStart;
										updateDir(dirStart, entry);
										free(block);
										printBitMap();
										return size;
									}
									else{
											fprintf(stderr, "I GUESS THIS IS NOT THE PROBELM\n");
									}
									fprintf(stderr, "WRITING SMALLEST BLOCK OF APEND\n");
									fprintf(stderr, "OFFSET : %d\n", offset);
									writeBufToBlock(0, block, buf);
									writeBlockToFile(block, loc);
									updateMap(indexToWrite,1);
									int sizeIncrease = fileSize+size;
									fprintf(stderr, "FILE SIZE BEFORE WRITE : %d\n", dir->files[i].fsize);
									dir->files[i].fsize = sizeIncrease;
									fprintf(stderr, "FILE SIZE AFTER WRITE : %d\n", dir->files[i].fsize);
									int dirStart = findOffset(directory);
									cs1550_directory_entry *entry = readDir(dirStart);
									entry->files[fileFound].fsize = size+fileSize;
									entry->files[fileFound].nStartBlock = fileStart;
									updateDir(dirStart, entry);
									free(block);
									printBitMap();
									return size;

								}

							}
						}
						//This is not an apend. Meaning either I am writing to a new file, or I opened this in nano
						//and am writing new stuff from there.
						else{
							fprintf(stderr, "NOT APEND");
							fprintf(stderr, "SIZE = %d\n", size);
							if(size>512){
								int blocksOccu;
								int blocksNeeded;
								if(size%512==0){
										blocksNeeded = ((size/BLOCK_SIZE));
								}
								else{
										blocksNeeded = ((size/BLOCK_SIZE))+1;
								}
								if(fileSize%512==0){
										blocksOccu = (fileSize/BLOCK_SIZE);
								}
								else{
										blocksOccu = (fileSize/BLOCK_SIZE)+1;
								}
								if(fileSize==0){
									fprintf(stderr, "NEW WRITE : \n");
									blocksOccu = 1;
								}
								blocksNeeded = ((size/BLOCK_SIZE)-blocksOccu)+1;
								//if blocks needed come out to be negative, I probably don't needed
								//new blocks. Nanoing a file that is already big enough.
								if(blocksNeeded<0){
									blocksNeeded = 0;
								}
								int blocksFound = 0;
								int blockTaken = 0;
								int spaceOccu = blocksOccu*BLOCK_SIZE;
								int startToLook = (((fileStart+spaceOccu)-FILE_START)/512);

								fprintf(stderr, "START TO LOOK AT INDEX: %d\n", startToLook);
								fprintf(stderr, "SPACE OCCUPIED BY FILE: %d\n", spaceOccu);
								//check if there are contiguous blocks
								int j = 0;
								for(j = 0; j<blocksNeeded; j++){
									if((startToLook)==MAX_FILES_SYSTEM){
										return -EFBIG;
									}
									if(findContigBlocks(startToLook+j)==1){
										blocksFound++;
									}
									else{
										blockTaken = j+startToLook;
										break;
									}
								}
								//if I found enough blocks to write, write. No moving of blocks is required.
								if(blocksFound==blocksNeeded){
									fprintf(stderr, "NO MOVING REQUIRED\n");
									int bufPos = 0;
									int bufRet = 0;
									int filePos = fileStart;
									int blocksWrite = blocksNeeded+blocksOccu;
									if(blocksWrite>8){
										blocksWrite = 8;
									}
									cs1550_disk_block *block = (cs1550_disk_block * )malloc(sizeof(cs1550_disk_block));
									int startIndex = ((fileStart-FILE_START)/BLOCK_SIZE);
									if((fileStart-FILE_START)==0){
										startIndex = 0;
									}
									for(j = 0; j<blocksWrite; j++){
										bufRet = writeBufToBlock(bufPos, block, buf);
										bufPos = bufRet;
										writeBlockToFile(block, filePos+(j*BLOCK_SIZE));
										updateMap((startIndex+j), 1);
										fprintf(stderr, "MAP UPDATED AT %d\n", (startIndex+j));
									}

									dir->files[fileFound].fsize = size;
									dir->files[fileFound].nStartBlock = fileStart;
									int dirStart = findOffset(directory);
									updateDir(dirStart, dir);
									free(block);
									fprintf(stderr, "SIZE when apending! %d\n", size);
									printBitMap();
									return size;
								}
								//otherwise moving is requred. Move stuff, so file can fit.
								else{
									fprintf(stderr, "MOVING REQUIRED");
									int ret;
									blocksFound = 0;
									while(blocksNeeded!=blocksFound){
										fprintf(stderr, "BLOCKS NEEDED : %d\n", blocksNeeded);
										fprintf(stderr, "BLOCKS FOUND : %d\n", blocksFound);
										fprintf(stderr, "BLOCK TAKE AT INDEX : %d\n", blockTaken);

										long loc = FILE_START+(blockTaken*BLOCK_SIZE);
										ret = moveFiles(loc, fileSize);
										blocksFound = 0;
										for(j = 0; j<blocksNeeded; j++){
											if((startToLook+j)==MAX_FILES_SYSTEM){
												return -EFBIG;
											}
											if(findContigBlocks(startToLook+j)==1){
												blocksFound++;
											}
											else{
												blockTaken = j+startToLook;
												break;
											}
										}
									}
									//
									if(ret==1){
										fprintf(stderr, "MOVED ENOUGH BLOCKS TO WRITE MY BLOCK\n");
										int k;
										int blocksWrite = blocksNeeded+blocksOccu;
										if(blocksWrite>8){
											blocksWrite = 8;
										}
										int filePos = fileStart;
										int bufPos = 0;
										int bufRet = 0;
										int startIndex = ((fileStart-FILE_START)/BLOCK_SIZE);
										if((fileStart-FILE_START)==0){
											startIndex = 0;
										}
										fprintf(stderr,"START WRTING FROM BEGINING OF FILE = %d\n", startIndex);
										fprintf(stderr, "BLOCKS TO WRITE = %d\n", blocksWrite);
										cs1550_disk_block * block = (cs1550_disk_block *)malloc(sizeof(cs1550_disk_block));
										for(k = 0; k<blocksWrite; k++){
											bufRet = writeBufToBlock(bufPos, block, buf);
											bufPos = bufRet;
											writeBlockToFile(block, filePos+(k*BLOCK_SIZE));
											updateMap((startIndex+k), 1);
											fprintf(stderr, "MAP UPDATED AT: %d\n", startIndex+k);

										}
										int dirStart = findOffset(directory);
										cs1550_directory_entry *entry = readDir(dirStart);
										entry->files[fileFound].fsize = size;
										entry->files[fileFound].nStartBlock = fileStart;
										updateDir(dirStart, entry);
										free(block);
										printBitMap();
										return size;
									}

								}
							}
							//the file was small enough that we don't need to worry about moving things.
							else if(fileSize<=512&&((fileSize+size)<=512)){
								cs1550_disk_block *block = (cs1550_disk_block*)malloc(sizeof(cs1550_disk_block));
								fprintf(stderr, "WILL NOT NEED NEW BLOCK: %d\n", (fileSize+size));
								writeBufToBlock(0, block, buf);
								writeBlockToFile(block, fileStart);
								//writeDataToFile(buf, size, fileStart, offset);
								fprintf(stderr, "OFFSET : %d\n", offset);
								dir->files[fileFound].fsize = size;
								int dirStart = findOffset(directory);
								updateDir(dirStart, dir);
								printBitMap();
								return size;
							}

						}

					}
					else{
						return -EFBIG;
					}
				}
		}
	}
	else{
		return -1;
	}


}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/*
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
