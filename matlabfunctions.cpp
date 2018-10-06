#include <math.h>
void sign(double *x, int xLen, double *y){
	for(int i = 0; i < xLen; i ++){
		if(x[i] > 0){
			y[i] = 1;
		} else if(x[i] == 0){
			y[i] = 0;
		} else if(x[i] < 0){
			y[i] = -1;
		}
	}
}