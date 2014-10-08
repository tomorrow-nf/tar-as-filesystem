/*
    Connects to a mySQL database
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>

int main() {
	MYSQL mysql;
	mysql_init(&mysql);
	//mysql_options(); //maybe later

	// database strings
	char* database = "Tarfiledb";
	char* createdatabase = "CREATE DATABASE Tarfiledb";

	// table strings
	char* archivetable = "ArchiveList"; // all tables follow: name, creation string, existence flag
	char* createarchivetable = "CREATE TABLE ArchiveList (ArchiveName VARCHAR(255) PRIMARY KEY, ArchivePath VARCHAR(5000), Timestamp VARCHAR(40)) ENGINE=InnoDB";
	int archivetable_exists = 0;

	char* basetar = "UncompTar";
	char* createbasetar = "CREATE TABLE UncompTar (ArchiveName VARCHAR(255), MemberName VARCHAR(255), GBoffset INT, BYTEoffset BIGINT, MemberLength VARCHAR(12), LinkFlag CHAR(1), PRIMARY KEY (ArchiveName, MemberName)) ENGINE=InnoDB";
	int basetar_exists = 0;

	char* bzip2_files = "Bzip2_files";
	char* create_bzip2_files = "CREATE TABLE Bzip2_files (ArchiveName VARCHAR(255), MemberName VARCHAR(255), Blocknumber INT, Blockoffset BIGINT, MemberLength VARCHAR(12), LinkFlag CHAR(1), PRIMARY KEY (ArchiveName, MemberName)) ENGINE=InnoDB";
	int bzip2_files_exists = 0;

	char* bzip2_blocks = "Bzip2_blocks";
	char* create_bzip2_blocks = "CREATE TABLE Bzip2_blocks (ArchiveName VARCHAR(255), Blocknumber INT, GBoffset INT, BYTEoffset INT, BIToffset INT, PRIMARY KEY (ArchiveName, Blocknumber)) ENGINE=InnoDB";
	int bzip2_blocks_exists = 0;

/*	
	char* basetar = "CompGzip";
	char* createcompgzip = "CREATE TABLE CompGzip (ArchiveName VARCHAR(255), MemberName VARCHAR(255), GBoffset INT, BYTEoffset BIGINT, MemberLength VARCHAR(12), LinkFlag CHAR(1), PRIMARY KEY (ArchiveName, MemberName)) ENGINE=InnoDB";
	int compgzip_exists = 0;

	char* basetar = "CompXZ";
	char* createcompxz = "CREATE TABLE CompXZ (ArchiveName VARCHAR(255), MemberName VARCHAR(255), GBoffset INT, BYTEoffset BIGINT, MemberLength VARCHAR(12), LinkFlag CHAR(1), PRIMARY KEY (ArchiveName, MemberName)) ENGINE=InnoDB";
	int compxz_exists = 0;
	*/

	int connection = 2; //2 = connected to Tarfiledb, 1 = Tarfiledb successfully created, 0 = no connection

	// connect(      sql structure,  host,      user, password, database, port, unix socket, client flag)
	if(!mysql_real_connect(&mysql, "localhost", "root", "root", "Tarfiledb", 0, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(&mysql));
		connection = 0;

		if(strcmp(mysql_error(&mysql), "Unknown database 'Tarfiledb'") == 0) {
			printf("Database does not exist, trying to create\n");

			mysql_close(&mysql);
			mysql_init(&mysql);
			if(!mysql_real_connect(&mysql, "localhost", "root", "root", "mysql", 0, NULL, 0)) {
				printf("Connection Failure to root database: %s\n", mysql_error(&mysql));
			}
			else {
				connection = 1; //connected to root database server
				if(mysql_query(&mysql, createdatabase)) {
					printf("Failed to create database: %s\n", mysql_error(&mysql));
					connection = 0; //database not created, connection essentially failed
				}
			}
			mysql_close(&mysql);
		}
	}

	// we try to reconnect now that the database has been created
	if(connection == 1) {
		mysql_init(&mysql);
		if(!mysql_real_connect(&mysql, "localhost", "root", "root", "Tarfiledb", 0, NULL, 0)) {
			printf("Connection Failure: %s\n", mysql_error(&mysql));
			connection = 0;
			mysql_close(&mysql);
		}
	}

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
				//TODO add more tables
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
		//TODO add more tables

		mysql_close(&mysql);
	}
}
