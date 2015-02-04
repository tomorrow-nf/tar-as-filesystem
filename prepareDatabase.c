/*
    Connects to a mySQL database
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include "sqloptions.h"

int main() {
	MYSQL mysql;
	mysql_init(&mysql);
	//read options from file
	mysql_options(&mysql, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	// connect
	if(!mysql_real_connect(&mysql, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(&mysql));
		printf("\nA database called 'Tarfiledb' must exist.\n");
		
		mysql_close(&mysql);
		return 1;
	}

	// table strings
	char* archivetable = "ArchiveList"; // all tables follow: name, creation string, existence flag
	char* createarchivetable = "CREATE TABLE ArchiveList (ArchiveID INT AUTO_INCREMENT, ArchiveName VARCHAR(255), ArchivePath VARCHAR(5000), Timestamp VARCHAR(100), PRIMARY KEY(ArchiveID), UNIQUE(ArchiveName)) ENGINE=InnoDB";
	int archivetable_exists = 0;

	char* basetar = "UncompTar";
	char* createbasetar = "CREATE TABLE UncompTar (FileID INT AUTO_INCREMENT, ArchiveID INT, ArchiveName VARCHAR(255), MemberName VARCHAR(300), MemberPath VARCHAR(5000), GBoffset INT, BYTEoffset BIGINT, MemberLength VARCHAR(12), LinkFlag CHAR(1), DirFlag CHAR(1), Mode INT, Uid INT, Gid INT, PRIMARY KEY (FileID), FOREIGN KEY(ArchiveID) REFERENCES ArchiveList(ArchiveID)) ENGINE=InnoDB";
	int basetar_exists = 0;

	char* bzip2_files = "Bzip2_files";
	char* create_bzip2_files = "CREATE TABLE Bzip2_files (FileID INT AUTO_INCREMENT, ArchiveID INT, ArchiveName VARCHAR(255), MemberName VARCHAR(300), MemberPath VARCHAR(5000), Blocknumber INT, BlockOffset BIGINT, InsideOffset BIGINT, MemberLength VARCHAR(12), LinkFlag CHAR(1), DirFlag CHAR(1), Mode INT, Uid INT, Gid INT, PRIMARY KEY (FileID), FOREIGN KEY(ArchiveID) REFERENCES ArchiveList(ArchiveID)) ENGINE=InnoDB";
	int bzip2_files_exists = 0;

	char* bzip2_blocks = "Bzip2_blocks";
	char* create_bzip2_blocks = "CREATE TABLE Bzip2_blocks (ArchiveID INT, ArchiveName VARCHAR(255), Blocknumber INT, BlockOffset BIGINT, BlockSize BIGINT, PRIMARY KEY (ArchiveID, Blocknumber), FOREIGN KEY(ArchiveID) REFERENCES ArchiveList(ArchiveID)) ENGINE=InnoDB";
	int bzip2_blocks_exists = 0;

	char* compxz = "CompXZ";
	char* createcompxz = "CREATE TABLE CompXZ (FileID INT AUTO_INCREMENT, ArchiveID INT, ArchiveName VARCHAR(255), MemberName VARCHAR(300), MemberPath VARCHAR(5000), Blocknumber INT, BlockOffset BIGINT, InsideOffset BIGINT, MemberLength VARCHAR(12), LinkFlag CHAR(1), DirFlag CHAR(1), Mode INT, Uid INT, Gid INT, PRIMARY KEY (FileID), FOREIGN KEY(ArchiveID) REFERENCES ArchiveList(ArchiveID)) ENGINE=InnoDB";
	int compxz_exists = 0;

	char* compxz_blocks = "CompXZ_blocks";
	char* create_compxz_blocks = "CREATE TABLE CompXZ_blocks (ArchiveID INT, ArchiveName VARCHAR(255), Blocknumber INT, BlockOffset BIGINT, BlockSize BIGINT, PRIMARY KEY (ArchiveID, Blocknumber), FOREIGN KEY(ArchiveID) REFERENCES ArchiveList(ArchiveID)) ENGINE=InnoDB";
	int compxz_blocks_exists = 0;

	int connection = 2; //2=connected to Tarfiledb, 0=no connection

	// if connection is not 0 "mysql" is now connected to Tarfiledb
	if(connection) {
		
		printf("Connection Successful to Tarfiledb\n");

		// check that required tables exist
		MYSQL_RES* response = mysql_list_tables(&mysql, "%");
		if(response != NULL) {

			MYSQL_ROW row;
			while(row = mysql_fetch_row(response)) {
				if(strcmp(row[0], archivetable) == 0) archivetable_exists = 1;
				if(strcmp(row[0], basetar) == 0) basetar_exists = 1;
				if(strcmp(row[0], bzip2_files) == 0) bzip2_files_exists = 1;
				if(strcmp(row[0], bzip2_blocks) == 0) bzip2_blocks_exists = 1;
				if(strcmp(row[0], compxz) == 0) compxz_exists = 1;
				if(strcmp(row[0], compxz_blocks) == 0) compxz_blocks_exists = 1;
			}
			mysql_free_result(response);
		}
		else {
			printf("Error listing tables");
		}

		// create non-existant tables
		if(!archivetable_exists) {
			printf("ArchiveList does not exist, creating\n");
			if(mysql_query(&mysql, createarchivetable)) {
				printf("Error: %s\n", mysql_error(&mysql));
			}
		}
		if(!basetar_exists) {
			printf("UncompTar does not exist, creating\n");
			if(mysql_query(&mysql, createbasetar)) {
				printf("Error: %s\n", mysql_error(&mysql));
			}
		}
		if(!bzip2_files_exists) {
			printf("Bzip2_files does not exist, creating\n");
			if(mysql_query(&mysql, create_bzip2_files)) {
				printf("Error: %s\n", mysql_error(&mysql));
			}
		}
		if(!bzip2_blocks_exists) {
			printf("Bzip2_blocks does not exist, creating\n");
			if(mysql_query(&mysql, create_bzip2_blocks)) {
				printf("Error: %s\n", mysql_error(&mysql));
			}
		}
		if(!compxz_exists) {
			printf("CompXZ does not exist, creating\n");
			if(mysql_query(&mysql, createcompxz)) {
				printf("Error: %s\n", mysql_error(&mysql));
			}
		}
		if(!compxz_blocks_exists) {
			printf("Compxz_blocks does not exist, creating\n");
			if(mysql_query(&mysql, create_compxz_blocks)) {
				printf("Error: %s\n", mysql_error(&mysql));
			}
		}

		mysql_close(&mysql);
	}
}
