#include<stdio.h>
#include<math.h>
#include<stdlib.h>
#include<string.h>
#include<sys/wait.h>
#include<unistd.h>		/* write(2) close(2) */
#include<sys/types.h>	/* open(2) */
#include<sys/stat.h>	/* open(2) */
#include<fcntl.h>		/* open(2) */
#include<error.h>		/* perror() */

#define IBM_SIZE 4
#define DBM_SIZE 463
#define FL_SIZE 100
#define DR_SIZE 10
#define BUFF 1024
#define MAX 20 
#define MAX_FD 10
#define INODE_COUNT 32

#define EXIT "exit"

/* Structure for SuperBlock*/
typedef struct{
	long FSSize; 		/* size of the file system */
	int root_inode_num; 	/* root inode number (always put 0) */
	int block_size; 	/* size of each block */
	int ib_size; 		/* size of each inode structure */
	int ib_start_loc; 	/* starting location of inode (after superblock node ends) */
	int ib_cnt;		/* count of inodes */
	int free_inode_cnt;	/* count of free inodes */
	int db_start_loc;	/* starting location of data block (after inodes end) */
	int db_cnt;		/* data block count */
	int free_db_count;	/* count of free data blocks */
	unsigned char ib_bitmap[IBM_SIZE];	/* inode block bitmap containing info. about empty inode blocks */
	unsigned char db_bitmap[DBM_SIZE];	/* data block bitmap containing info. about empty data blocks */
}superblock;

/* Structure storing inode */
typedef struct{
	char filename[20];
	char type;    /* type of data block */
	long size;     /* size of file or folder in bytes */
	int db_count; /* current count of (used) data blocks */
	int db_start_loc; /* starting location of the data blocks */
}inode;

/* for mounting purposes, each filename corresponds to  a drive name */ 
typedef struct{
	char filesystem_name[FL_SIZE];
	char drive_name[DR_SIZE];
}mount_table;
mount_table fd_map[MAX_FD];

void parse(char cmd[]);
void implement_sys_call(char cmd_tokens[][10]);
int get_free_inode(superblock snode);
int get_free_db(superblock snode,int db_req);
void set_ib(superblock *snode,int in_used, int flag);
void set_db(superblock *snode,int db_start, int db_req, int flag);
int check_file(char *fs_name, char file[20]);

void create_fs(char *path,int size_B,long size_FS);
void mount_fs(char *fs_name, char *dr_name);
void copy_file(char *copy_from, char *dr_name, char copy_to[20]);
void copy_file_drives(char *dr_from,char *copy_from, char *dr_to, char copy_to[20]);
void readfile(char *dr_name, char file[20]);
void listfiles(char *dr_name); 
void rmfile(char *dr_name, char file[20]);
void mvfile(char *dr_from,char *copy_from, char *dr_to, char copy_to[20]);

int main()
{	
	/* initialize mount_table structure */
	for(int i=0;i<MAX_FD;i++){
		strcpy(fd_map[i].filesystem_name,"\0");
		strcpy(fd_map[i].drive_name,"\0");
	}
	
	char cmd[BUFF];
	printf("Welcome to MYFS program. Enter 'exit' to exit.\n");
	while(1)
	{
		/* Prompt Display*/
		printf("myfs> ");
		/* input the command */
		fgets(cmd,BUFF,stdin);
		cmd[strlen(cmd) - 1] = '\0';			/*Replace \n with \0*/
		/* if user entered exit, then EXIT */
		if(strcmp(cmd, EXIT) == 0)	break;
		/* Parse the command and parse it */
		else  parse(cmd);
	}
	return 0;
}

/* Function to create a filesystem */	
void create_fs(char *path, int size_B, long size_FS)
{
	if(size_B<sizeof(superblock)||size_B<sizeof(inode)){
		printf("Invalid Block Size! Blocksize >= %ld\n",sizeof(superblock));
		return;
	}
	/* Initializing Superblock */
	superblock snode;
	snode.FSSize = size_FS;
	snode.root_inode_num = 0;
	snode.block_size = size_B;
	snode.ib_size = sizeof(inode);
	snode.ib_start_loc = size_B;
	snode.ib_cnt = INODE_COUNT;
	snode.free_inode_cnt = snode.ib_cnt;
	snode.db_start_loc = size_B + INODE_COUNT * size_B;
	snode.db_cnt = (size_FS - snode.db_start_loc)/size_B;
	snode.free_db_count = snode.db_cnt;
	if(snode.db_cnt>DBM_SIZE*8){
		printf("File System Size not supported. Either increase block size or decrease Filesystem Size!\n");
		return;
	}
	int i;
	for(i = 0; i < IBM_SIZE; i++)
		snode.ib_bitmap[i] = 255; // setting all inode blocks to free
	for(i = 0; i < DBM_SIZE; i++)
		snode.db_bitmap[i] = 255; // setting all data blocks to free


	/*int open(const char *pathname, int flags);*/
	int fd;						/*File Descriptor*/
	fd = open(path,O_RDWR|O_CREAT|O_EXCL,0777);
	ftruncate(fd,snode.FSSize);
	if(fd == -1)
	{
		fprintf(stderr,"Make Filesystem Failed! A file system already exists with the given name.\n");
		return;
	}

	/*write superblock to file*/
	/*ssize_t write(int fd, const void *buf, size_t count);*/
	int status;
	status = write(fd, (void *)&snode, sizeof(snode));
	if(status == sizeof(snode))
		printf("Superblock Written Successfully!\n");
	else if(status >= 0){
		fprintf(stderr,"Superblock Write Operation Incomplete!\n");
		return;
	}
	else{
		fprintf(stderr,"Superblock Write Operation Failed!\n");
		return;
	}

	printf("File System Created Successfully!\n");
	close(fd);
}

/* function to mount a filesystem */ 
void mount_fs(char *fs_name, char *dr_name){
	int i;	
	for(i=0;i<MAX_FD;i++){
		if(strcmp(fd_map[i].filesystem_name,fs_name)==0){
			fprintf(stderr,"Filesystem has already been assigned a drive: %s\n",fd_map[i].drive_name);
			return;
		}
		if(strcmp(fd_map[i].drive_name,dr_name)==0){
			fprintf(stderr,"Drive has already been assigned to a filesystem: %s\n",fd_map[i].filesystem_name);
			return;
		}
	}
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].filesystem_name,"\0")==0){
			strcpy(fd_map[i].filesystem_name, fs_name);
			strcpy(fd_map[i].drive_name, dr_name);
			printf("Filesystem successfully been assigned a drive!\n");
			break;
		}
	if(i==MAX_FD){
		fprintf(stderr,"Maximum number of drives have been created!\n");
		return;
	}
}

/* function to copy file from OS to a drive */
void copy_file(char *copy_from, char *dr_name, char copy_to[20]){	
	struct stat buffer;
	int status,i;
	/* getting file size */
	long file_size;
	status = stat(copy_from, &buffer);
	if(status == 0) file_size = buffer.st_size;
	else{ 
		fprintf(stderr,"Couldn't get file Size. Maybe Invalid path!\n");
		return;
	}
	/* reading superblock*/
	superblock snode;
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].drive_name,dr_name)==0)
			break;
	if(i==MAX_FD){
		printf("FS not mounted!\n");
		return;
	}
	// checking if filename does not already exists
	status = check_file(fd_map[i].filesystem_name,copy_to); 
	if(status!=INODE_COUNT||status==-1) {
		printf("Filename Already exists or error reading superblock!\n");
		return;
	}
	// opening file system and reading superblock
	int fd;	
	fd = open(fd_map[i].filesystem_name,O_RDONLY);
	status = read(fd, (void *)&snode, sizeof(snode));
	if(status!=sizeof(snode)) { printf("Operation Failed! Couldn't read Superblock!\n");  return;}
	close(fd);			
	int db_req = (int)ceil((double)file_size/snode.block_size); // calculating required data blocks
	int ib_free = get_free_inode(snode); // getting the ith free inode block
	if(ib_free==-1) { 
		printf("No free Inode Blocks. Make some space! Current Free Inode Block Count: %d\n",snode.free_inode_cnt);  
		return; 
	}
	int db_free = get_free_db(snode,db_req); // getting the ith free data block large enough to store file
	if(db_free==-1) { 
		printf("No free Data Blocks. Make some space! Current Free Data Space: %d\n",snode.free_db_count*snode.block_size); 
		return; 
	}
	snode.free_inode_cnt--; //decreasing free inode blocks count
	snode.free_db_count-=db_req; // decreasing free data blocks count
	// initializing inode block of the file	
	inode in;
	strcpy(in.filename,copy_to);	
	in.type = 'f';
	in.size = file_size;	
	in.db_count = db_req;
	in.db_start_loc = snode.db_start_loc + db_free*snode.block_size; // setting current Data block start location
	set_ib(&snode,ib_free,0); // setting the inode block as used
	set_db(&snode,db_free,db_req,0); //setting the data blocks as used
	int fd_copy;	
	fd = open(fd_map[i].filesystem_name,O_WRONLY);
	fd_copy = open(copy_from,O_RDONLY);
	/* writing updated superblock of filesystem to which the file will be copied */
	status = write(fd, (void *)&snode, sizeof(snode));
	if(status!=sizeof(snode)) {
		printf("Operation Failed! Couldn't write superblock!\n");  
		return;
	}	
	/* writing inode of the file to be copied*/
	lseek(fd,snode.ib_start_loc+ib_free*snode.block_size,SEEK_SET);
	status = write(fd, (void *)&in, sizeof(inode));
	if(status!=sizeof(inode)) { 
		printf("Operation Failed! Couldn't write inode block!\n");  
		return;
	}
	/* writing file to the filesystem */
	lseek(fd,in.db_start_loc,SEEK_SET);
	printf("File Size: %ld & Data Blocks required: %d\n",in.size,db_req);	
	printf("Start Inode: %d & DB: %d\n",ib_free,db_free);
	char c;
	i=0;
	do{
		read(fd_copy,(void *)&c,sizeof(c));
		write(fd,(void *)&c,sizeof(c));
		i++;
	}while(i!=file_size);
	printf("Copy Operation Successful!\n");
	close(fd);
	close(fd_copy);
}

/* function to copy file from one drive to another */
void copy_file_drives(char *dr_from,char *copy_from, char *dr_to, char copy_to[20]){	
	int i,ib,status;	
	superblock snode;
	/* finding fs name for the corresponding drive to copy from */
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].drive_name,dr_from)==0)
			break;
	if(i==MAX_FD){
		printf("FS not mounted!\n");
		return;
	}
	ib = check_file(fd_map[i].filesystem_name,copy_from);
	if(ib==INODE_COUNT||ib==-1){
		printf("File not found or error reading superblock!\n");
		return;
	}
	int fd_from;
	inode in_from;
	fd_from = open(fd_map[i].filesystem_name,O_RDONLY);
	/* reading snode of the filesystem from which the file will be copied */
	status = read(fd_from, (void *)&snode, sizeof(snode));
	if(status!=sizeof(snode)) { printf("Operation Failed! Couldn't read Superblock!\n");  return;}	
	lseek(fd_from,snode.ib_start_loc+ib*snode.block_size,SEEK_SET); // setting seek to the inode of the file to be copied
	status = read(fd_from, (void *)&in_from, sizeof(inode));
	if(status!=sizeof(inode)){ printf("Operation Failed! Couldn't read Inode!\n"); return;}

	/* finding fs name for the corresponding drive to copy to */
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].drive_name,dr_to)==0)
			break;
	if(i==MAX_FD){
		printf("FS not mounted!\n");
		return;
	}
	ib = check_file(fd_map[i].filesystem_name,copy_to);
	if(ib!=INODE_COUNT||ib==-1){
		printf("File name Already exists or error reading superblock!\n");
		return;
	}

	/* getting free inode block and data blocks*/
	int fd_to;
	fd_to = open(fd_map[i].filesystem_name,O_RDWR);
	status = read(fd_to, (void *)&snode, sizeof(snode));
	if(status!=sizeof(snode)) { printf("Operation Failed! Couldn't read Superblock!\n");  return;}
	int db_req = (int)ceil((double)in_from.size/snode.block_size); // calculating required data blocks
	int ib_free = get_free_inode(snode); // getting the ith free inode block
	if(ib_free==-1) { 
		printf("No free Inode Blocks. Make some space! Current Free Inode Block Count: %d\n",snode.free_inode_cnt);  
		return; 
	}
	int db_free = get_free_db(snode,db_req); // getting the ith free data block large enough to store file
	if(db_free==-1) { 
		printf("No free Data Blocks. Make some space! Current Free Data Space: %d\n",snode.free_db_count*snode.block_size); 
		return; 
	}
	snode.free_inode_cnt--; //decreasing free inode blocks count
	snode.free_db_count-=db_req; // decreasing free data blocks count
	// initializing inode block of the file	
	inode in_to;
	strcpy(in_to.filename,copy_to);	
	in_to.type = 'f';
	in_to.size = in_from.size;	
	in_to.db_count = db_req;
	in_to.db_start_loc = snode.db_start_loc + db_free*snode.block_size; // setting current Data block start location
	set_ib(&snode,ib_free,0); // setting the inode block as used
	set_db(&snode,db_free,db_req,0); //setting the data blocks as used
	lseek(fd_to,0,SEEK_SET);	

	// writing inode, superblock and copying file to the file system
	status = write(fd_to, (void *)&snode, sizeof(snode));
	if(status!=sizeof(snode)){
		printf("Operation Failed! Couldn't write superblock!\n");  
		return;
	}	
	lseek(fd_to,snode.ib_start_loc+ib_free*snode.block_size,SEEK_SET);
	status = write(fd_to, (void *)&in_to, sizeof(inode));
	if(status!=sizeof(inode)) { 
		printf("Operation Failed! Couldn't write inode block!\n");  
		return;
	}
	lseek(fd_from,in_from.db_start_loc,SEEK_SET);	
	lseek(fd_to,in_to.db_start_loc,SEEK_SET);
	printf("File Size: %ld & Data Blocks required: %d\n",in_from.size,db_req);	
	printf("Start Inode: %d & DB: %d\n",ib_free,db_free);
	char c;
	i=0;
	do{
		read(fd_from,(void *)&c,sizeof(c));
		write(fd_to,(void *)&c,sizeof(c));
		i++;
	}while(i!=in_from.size);
	printf("Copy Operation Successful!\n");
	close(fd_to);
	close(fd_from);
}

/* function to list all the files in a drive */
void listfiles(char *dr_name){
	int status,i;
	superblock snode;
	inode in;	
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].drive_name,dr_name)==0)
			break;
	if(i==MAX_FD){
		printf("FS not mounted!\n");
		return;
	}
	int fd;
	fd = open(fd_map[i].filesystem_name,O_RDONLY);
	status = read(fd, (void *)&snode, sizeof(snode));	
	if(status!=sizeof(snode)) { printf("Operation Failed! Couldn't Read Superblock!\n");  return;}
	lseek(fd,snode.ib_start_loc,SEEK_SET);	
	printf("Filename\tFileSize\n");
	int ib = 0;
	do{
		ib++;
		status = read(fd, (void *)&in, sizeof(inode));
		if(status==sizeof(inode)&&in.size>0) printf("%s\t\t%ld\n",in.filename,in.size);
		lseek(fd,snode.block_size-sizeof(in),SEEK_CUR);
		
	}while(ib<INODE_COUNT);
	close(fd);
}

/* function to read a particular file in a drive */
void readfile(char *dr_name, char file[20]){
	int status,i;
	superblock snode;
	inode in;	
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].drive_name,dr_name)==0)
			break;
	if(i==MAX_FD){
		printf("FS not mounted!\n");
		return;
	}
	int fd;
	fd = open(fd_map[i].filesystem_name,O_RDONLY);
	status = read(fd, (void *)&snode, sizeof(snode));	
	if(status!=sizeof(snode)) { printf("Operation Failed! Couldn't Read Superblock!\n");  return;}
	lseek(fd,snode.ib_start_loc,SEEK_SET);	
	int ib=0;
	do{
		status = read(fd, (void *)&in, sizeof(inode));
		if(strcmp(in.filename,file)==0) break;	
		ib++;
		lseek(fd,snode.block_size-sizeof(in),SEEK_CUR);
	}while(ib<INODE_COUNT);
	if(ib==INODE_COUNT) {
		printf("File Not Found!\n");
		return;
	}
	lseek(fd,in.db_start_loc,SEEK_SET);
	char c;
	i=0;
	do{
		read(fd,(void *)&c,sizeof(c));
		printf("%c",c);
		i++;
	}while(i!=in.size);
	close(fd);
}

/* function to move a file from one drive to another/same drive */
void mvfile(char *dr_from, char *file_from, char *dr_to, char file_to[20]){
	copy_file_drives(dr_from,file_from,dr_to,file_to);
	int status,i;
	inode in;	
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].drive_name,dr_to)==0)
			break;
	if(i==MAX_FD){
		printf("FS not mounted!\n");
		return;
	}
	status = check_file(fd_map[i].filesystem_name,file_to);
	if(status==INODE_COUNT||status==-1){
		printf("Operation Failed!\n");
		return;
	} 
	rmfile(dr_from,file_from);
}

/* function to remove file from a drive */
void rmfile(char *dr_name, char file[20]){
	int status,i;
	superblock snode;
	inode in;	
	for(i=0;i<MAX_FD;i++)
		if(strcmp(fd_map[i].drive_name,dr_name)==0)
			break;
	if(i==MAX_FD){
		printf("FS not mounted!\n");
		return;
	}
	int fd;
	fd = open(fd_map[i].filesystem_name,O_RDWR);
	status = read(fd, (void *)&snode, sizeof(snode));	
	if(status!=sizeof(snode)) { 
		printf("Operation Failed! Couldn't Read Superblock!\n");  
		return;
	}
	lseek(fd,snode.ib_start_loc,SEEK_SET);	
	int ib = 0;
	do{
		status = read(fd, (void *)&in, sizeof(inode));
		if(strcmp(in.filename,file)==0) break;	
		ib++;
		lseek(fd,snode.block_size-sizeof(in),SEEK_CUR);
	}while(ib<INODE_COUNT);
	if(ib==INODE_COUNT) {
		printf("File Not Found!\n");
		return;
	}	
	int db_start = (in.db_start_loc - snode.db_start_loc)/snode.block_size;
	strcpy(in.filename,"\0");
	in.size = 0;
	set_ib(&snode,ib,1); // setting the inode block as free
	set_db(&snode,db_start,in.db_count,1); //setting the data blocks as free
	snode.free_inode_cnt+=1;
	snode.free_db_count+=in.db_count;
	printf("Inode Found: %d\n",ib);	
	lseek(fd,0,SEEK_SET);
	status = write(fd, (void *)&snode, sizeof(snode));
	if(status!=sizeof(snode)) {
		printf("Operation Failed! Couldn't write superblock!\n");  
		return;
	}	
	lseek(fd,snode.ib_start_loc+ib*snode.block_size,SEEK_SET);
	status = write(fd, (void *)&in, sizeof(inode));
	if(status!=sizeof(inode)) { 
		printf("Operation Failed! Couldn't write inode block!\n");  
		return;
	}
	printf("Removed Successfully!\n");
	close(fd);
}

/* function to check if the file already exists or not*/
int check_file(char *fs_name, char file[20]){
	int status;
	superblock snode;
	inode in;
	int fd;
	fd = open(fs_name,O_RDONLY);
	status = read(fd, (void *)&snode, sizeof(snode));	
	if(status!=sizeof(snode)) {return -1;}
	lseek(fd,snode.ib_start_loc,SEEK_SET);	
	int ib=0;
	do{
		status = read(fd, (void *)&in, sizeof(inode));
		if(strcmp(in.filename,file)==0) return ib;	
		ib++;
		lseek(fd,snode.block_size-sizeof(in),SEEK_CUR);
	}while(ib<INODE_COUNT);
	return ib;
}

/* function to get the ith free inode block */
int get_free_inode(superblock snode){
	int i,j,bit;
	for(j=0;j<IBM_SIZE;j++)
	{
		for (i = 0 ; snode.ib_bitmap[j]!=0 && i < 8 ; i++) {
    			bit = (snode.ib_bitmap[j] & (1 << i)) != 0;
			if(bit==1) break;
		}
		if(bit==1) break;
	}
	if(j==IBM_SIZE) return -1;
	return j*8+i;		
}

/* function to get the ith free data block after which the file will be copied */
int get_free_db(superblock snode,int db_req){
	int i,j,bit;
	int temp = db_req;
	int start_byte = -1,start_bit = -1;
	int db_limit = ceil(snode.db_cnt/8);
	for(j = 0; j<db_limit;j++)
	{
		for (i = 0 ; snode.db_bitmap[j]!=0 && i < 8 ; i++) {
    			bit = (snode.db_bitmap[j] & (1 << i)) != 0;
			if(bit==1){
				if(start_byte==-1){
					start_byte = j;
					start_bit = i;
				}
				temp--;
			}
			else {
				temp = db_req;
				start_byte = -1;
			}
			if(temp==0) break;
		}
		if(temp==0) break;
	}
	if(j==db_limit) return -1;
	return start_byte*8+start_bit;		
}

/* function to set an inode block as used/free based on the flag value */
void set_ib(superblock *snode,int in_used, int flag){
	int block = 0;
	if(in_used>=8){
		block = in_used/8;
		in_used = in_used%8;
	}
	snode->ib_bitmap[block] = (snode->ib_bitmap[block] & (~(1 << in_used))) | (flag << in_used);
}

/* function to set data blocks as used/free based on the flag value */
void set_db(superblock *snode,int db_start, int db_req, int flag){
	int i,j,byte = 0;
	if(db_start>=8){
		byte = db_start/8;
		db_start = db_start%8;
	}
	for(i=byte;i<DBM_SIZE && db_req!=0;i++){
		for(j=db_start;j<8 && db_req!=0;j++){
			snode->db_bitmap[i] = (snode->db_bitmap[i] & (~(1 << j))) | (flag << j);
			db_req--;
		}
		if(j==8) db_start = 0;
	}
}

/* Parses and executes a command*/
void parse(char cmd[])
{
	/* Store each command token in a separate string */	
	char cmd_tokens[10][10];
	int i, j, k;
	j = 0;
	k = 0;
	for(i = 0; i <= strlen(cmd); i++)
	{
		if (cmd[i] == '\0')	cmd_tokens[j][k] = '\0';
		else if(cmd[i] == ' ')
		{
			if(cmd[i + 1] != ' ' && cmd[i + 1] != '\0'){
				cmd_tokens[j][k] = '\0';
				j++;
				k = 0;
				bzero(cmd_tokens[j], 10);
			}
		}
		else
		{
			cmd_tokens[j][k] = cmd[i];
			k++;
		}
	}
	/* At the end of command put NULL string */
	strcpy(cmd_tokens[j+1],"\0");
	/* implement required system calls */
	implement_sys_call(cmd_tokens);
}

/*Function to implement required system_call for a command*/
void implement_sys_call(char cmd_tokens[][10])
{ 	 
	if(strcmp(cmd_tokens[0],"mkfs") == 0)
	{
		/* checking if the command format is correct */
		int i;
		for(i = 0; strcmp(cmd_tokens[i],"\0") != 0; i++);
		if(i != 4){
			fprintf(stderr,"Incorrect Command Format! Usage : mkfs [file_system_name] [block size (bytes)] [file system size (MB)] .\n");
			return;
		}
		long fssize = atoi(cmd_tokens[3]) * 1024 * 1024;
		int bsize = atoi(cmd_tokens[2]);
		create_fs(cmd_tokens[1],bsize,fssize);
	}
	else if(strcmp(cmd_tokens[0], "use") == 0)
	{
		/* checking if the command format is correct */
		int i;
		for(i = 0; strcmp(cmd_tokens[i],"\0") != 0; i++);
		if(i != 4 || strcmp(cmd_tokens[2], "as") != 0){
			fprintf(stderr,"Incorrect Command Format! Usage : use [file_system_name] as [drive_name] .\n");
			return;
		}
		mount_fs(cmd_tokens[1],cmd_tokens[3]);
	}
	else if(strcmp(cmd_tokens[0], "cp") == 0)
	{
		/* checking if the command format is correct */
		int i;
		for(i = 0; strcmp(cmd_tokens[i],"\0") != 0; i++);
		if(i != 4 && i != 5){
			fprintf(stderr,"Incorrect Command Format!\nUsage : cp <[drive_to_name]> [copy_from_file_name] [drive_to_name] [copy_to_file_name].\n or cp [copy_from_file_name] [drive_to_name] [copy_to_file_name].\n");
			return;
		}
		if(i==4) copy_file(cmd_tokens[1],cmd_tokens[2],cmd_tokens[3]);
		if(i==5) copy_file_drives(cmd_tokens[1],cmd_tokens[2],cmd_tokens[3],cmd_tokens[4]);
	}
	else if(strcmp(cmd_tokens[0], "read") == 0)
	{
		/* checking if the command format is correct */
		int i;
		for(i = 0; strcmp(cmd_tokens[i],"\0") != 0; i++);
		if(i != 3){
			fprintf(stderr,"Incorrect Command Format! Usage : read [drive_name] [file_name]  .\n");
			return;
		}
		readfile(cmd_tokens[1],cmd_tokens[2]);
	}
	else if(strcmp(cmd_tokens[0], "rm") == 0)
	{
		/* checking if the command format is correct */
		int i;
		for(i = 0; strcmp(cmd_tokens[i],"\0") != 0; i++);
		if(i != 3){
			fprintf(stderr,"Incorrect Command Format! Usage : rm [drive_name] [file_name]  .\n");
			return;
		}
		rmfile(cmd_tokens[1],cmd_tokens[2]);
	}
	else if(strcmp(cmd_tokens[0], "ls") == 0)
	{
		/* checking if the command format is correct */
		int i;
		for(i = 0; strcmp(cmd_tokens[i],"\0") != 0; i++);
		if(i != 2){
			fprintf(stderr,"Incorrect Command Format! Usage : ls [drive_name].\n");
			return;
		}
		listfiles(cmd_tokens[1]);
	}
	else if(strcmp(cmd_tokens[0],"mv") == 0)
	{
		/* checking if the command format is correct */
		int i;
		for(i = 0; strcmp(cmd_tokens[i],"\0") != 0; i++);
		if(i != 5){
			fprintf(stderr,"Incorrect Command Format! Usage : mv [drive_from_name] [move_from_file_name] [drive_to_name] [move_to_file_name].\n");
			return;
		}
		mvfile(cmd_tokens[1],cmd_tokens[2],cmd_tokens[3],cmd_tokens[4]);
	}
	else printf("INVALID COMMAND!\n");
}
