/*
    Connects to a mySQL database
*/
#include <mysql.h>
#include <stdio.h>

int main() {
	MYSQL mysql;
	mysql_init(&mysql);
	//mysql_options(); //maybe later

	// connect(      sql structure,  host,      user, password, database, port, unix socket, client flag)
	if(!mysql_real_connect(&mysql, "localhost", "root", "root", "mysql", 776, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(&mysql));
	}
	else {
		printf("Connection Successful\n");
		MYSQL_RES* response = mysql_list_tables(&mysql, "%");
		if(response != NULL) {

			MYSQL_ROW row;
			while(row = mysql_fetch_row(response)) {
				puts(row[0]);
			}
			mysql_free_result(response);
		}
		else {
			printf("Error listing tables");
		}
		mysql_close(&mysql);
	}
}
