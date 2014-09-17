analyze_tar: analyze_tar.o
	gcc analyze_tar.o -o analyze_tar

analyze_tar.o: analyze_tar.c
	gcc -c analyze_tar.c

clean:
	rm -f *.o analyze_tar
