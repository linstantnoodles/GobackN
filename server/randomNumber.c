#include <stdio.h>      /* for printf() and fprintf() */
#include <stdlib.h>     /* for atoi() and exit() */

int randomNumber(int upperBound);

int main(int argc, char *argv[])
{
	int tries=20;
	int rn;
	int upperBound=10;
	while (tries > 0){  // this will generate 20 rn, just for fun
		rn = randomNumber(upperBound);  //returns a random number from 1 to 10
		printf("random number = %d\n",rn);
		tries--;
	}
     
}

int randomNumber(int upperBound){  //this is the simple rn generator
	int j;
	j=1+(rand() % upperBound);
	return j;
}