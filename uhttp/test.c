#include <stdio.h>
#include <string.h>

int main() {
	char string[30];
	bzero(string, 30);
	strcpy(string, "This is a test\r\nstring\r\n");
	
	char *test;
	test = strtok(string, " \r\n");
	while(test!=NULL) {
		printf("%s\n", test);
		test = strtok(NULL, " \r\n");
	}
	
	return 0;
}
