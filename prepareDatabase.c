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
	char* archivetable = "ArchiveList"; // all tables follow: name
	char* createarchivetable = ""; //                         creation string
	int archivetable_exists = 0; //                            existence flag
	char* basetar = "UncompTar";
	char* createbasetar = "";
	int basetar_exists = 0;

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
				//TODO add more tables
			}
			mysql_free_result(response);
		}
		else {
			printf("Error listing tables");
		}

		// create non-existant tables
		if(!archivetable_exists) {
			printf("archivetable does not exist\n");
		}
		if(!basetar_exists) {
			printf("basetar does not exist\n");
		}

		mysql_close(&mysql);
	}
}

// mysql_query(con, "CREATE TABLE IF NOT EXISTS ArchiveList (ArchiveName VARCHAR(50), ArchivePath VARCHAR(200), id INT)");
	//mysql_query(con, "CREATE TABLE IF NOT EXISTS UncompTar (ArchiveName VARCHAR(50), MemberName VARCHAR(50), GBoffset INT, BYTEoffset BIGINT, LinkFlag CHAR(1), LastModified TIMESTAMP)");
	//mysql_query(con, "CREATE TABLE bz2 (ArchiveName VARCHAR(50), MemberName VARCHAR(50), )");
	//mysql_query(con, "CREATE TABLE gzip (ArchiveName VARCHAR(50), MemberName VARCHAR(50), )");
	//mysql_query(con, "CREATE TABLE xz (ArchiveName VARCHAR(50), MemberName VARCHAR(50), )");
