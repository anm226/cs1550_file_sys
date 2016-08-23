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

    int file_size;
    memset(&buf, '\0', strlen(buf));

        //check to make sure path exists
    char DIR_name[MAX_FILENAME + 1];
    char FILE_name[MAX_FILENAME + 1];
    char FILE_extension[MAX_EXTENSION + 1];
    get_filepath(path, DIR_name, FILE_name, FILE_extension);

    // check for root
    if (strcmp(path, "/") == 0) return -EISDIR;

    // check if path is a directory, not a file
    if (strlen(FILE_name) < 1) return -EISDIR;

    // check if file exists
    int directory_offset = get_subdirectory_starting_block(DIR_name);
    long *file_info = get_file_starting_block(FILE_name, FILE_extension, directory_offset);
    // check if file not found
    if (file_info[0] == -1) {
       printf("CS1550_READ:: FILE NOT FOUND, file size will be 0");
       file_size = 0;
    } else {
        file_size = file_info[1];
    }

        //check that size is > 0
    if ( size < 1) {
        return -EISDIR;

    }
        //check that offset is <= to the file size
    // NOTE:  Looks like this call is used recursively, in some way, and this is the end condition.
    //        It must ALWAYS be true at the END of a READ
    if (!(offset <= file_size)) {
        printf("CS1550_READ:: Offset is > file size. Offset=%d, FileSize=%d\n\n", (int)offset, file_size );
        return -1;
    }

        //read in data
    // open the directory up, read in the file's data
    //cs1550_disk_block *disk_block = get_disk_block(file_info[0]);

    cs1550_disk_block disk_block;
    memset(&disk_block, '\0', sizeof(cs1550_disk_block));

/* OPEN DISK FILE AND GET ITS DATA */
    // Open the .disk file, get a pointer to it
    FILE *disk_file_ptr = fopen(".disk", "rb");
    // handle error
    if (disk_file_ptr == NULL) {
        perror("GET DISK BLOCK: Could not open .disk file. Please ensure it is in the current directory");
    }

    // seek to the location in .disk where our directory is stored, using the subdir_offset
    long offset_index = BLOCK_SIZE * file_info[0]; // how many blocks to offfset by
    fseek(disk_file_ptr, offset_index, SEEK_SET);

    // read in the directory's data from our opened file, and handle error if needed
    cs1550_disk_block DISK_block;
    // check that it equals block size to ensure there wasn't an error reading the data,
    // there was actually something there, and we got all we expected
    /*
    if (BLOCK_SIZE != fread(&DISK_block, 1, BLOCK_SIZE, disk_file_ptr)) {
        perror("DISK BLOCK: Could not read in DISK BLOCK entry from the .disk file");
    }
    */

    if (BLOCK_SIZE != fread(&DISK_block, 1, BLOCK_SIZE, disk_file_ptr)) {
        perror("DISK BLOCK: Could not read in DISK BLOCK entry from the .disk file");
    }
    // get our ptr to return
    //cs1550_disk_block *disk_block_ptr = malloc(sizeof(DISK_block));
    //disk_block_ptr = &DISK_block;

    disk_block = DISK_block;

// close the file
    fclose(disk_file_ptr);
    /* END GRAB DATA FROM .DISK */

    // TRY MEMCPY
    /*int index;
    printf("CS1550_READ:: Iterating and filling buffer. Size=%d, File_Size=%d\n, offset=%d", (int)size, file_size, (int)offset);
    for (index=0; index < file_size; index++) {
        buf[index] = disk_block.data[index];
        printf("CS1550_READ: buf[%d]=%c\n", index, buf[index]);
    }*/
    memcpy(buf, disk_block.data, size);

    // null terminate our buffer
    //buf[index] = '\0';
    printf("CS1550_READ:: BUFFER IS FILLED AND SAYS: %s\n", buf);
    printf("CS1550_READ:: LENGTH OF BUFFER IS: %d \n", (int)strlen(buf));

//set size and return, or error
        //size = file_info[1];

        return size;
}
disk_block = DISK_block;

// close the file
fclose(disk_file_ptr);
/* END GRAB DATA FROM .DISK */

// TRY MEMCPY
/*int index;
printf("CS1550_READ:: Iterating and filling buffer. Size=%d, File_Size=%d\n, offset=%d", (int)size, file_size, (int)offset);
for (index=0; index < file_size; index++) {
    buf[index] = disk_block.data[index];
    printf("CS1550_READ: buf[%d]=%c\n", index, buf[index]);
}*/
memcpy(buf, disk_block.data, size);

// null terminate our buffer
//buf[index] = '\0';
printf("CS1550_READ:: BUFFER IS FILLED AND SAYS: %s\n", buf);
printf("CS1550_READ:: LENGTH OF BUFFER IS: %d \n", (int)strlen(buf));

//set size and return, or error
    //size = file_info[1];

    return size;
}
