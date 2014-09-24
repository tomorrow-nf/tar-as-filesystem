/*
    Connects to a mySQL database
*/
#include <mysql.h>
#include <stdio.h>

int main() {
	MYSQL mysql;
	mysql_init(&mysql);
	//mysql_options(); //maybe later

	if(!mysql_real_connect(&mysql, "host", "user", "password", "database", 0, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(&mysql));
	}

}
