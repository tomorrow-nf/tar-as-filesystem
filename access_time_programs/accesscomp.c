#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{	if(argc != 2) {
		printf("./accesscomp filepath");
	}

	struct timeval t1, t2;

	gettimeofday(&t1, NULL);

	int fi_des = open(argv[1], O_RDONLY);
	if (fi_des == -1)
		printf("ignore, error opening\n");
	else {
		void* buf = (void*) malloc(10);
		if (read(fi_des, buf, 10) == -1) printf("ignore, error reading\n");
		close(fi_des);
		free(buf);
	}

	gettimeofday(&t2, NULL);

	double elapsedtime = (t2.tv_sec - t1.tv_sec) * 1000.0;
	elapsedtime += (t2.tv_usec - t1.tv_usec) / 1000.0;

	printf("access took %f milliseconds\n", elapsedtime);

	return 0;
}
