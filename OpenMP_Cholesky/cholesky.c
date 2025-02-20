#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <omp.h>
/** 
 * File name: cholesky.c
 * File description: paralleized non-block cholesky LU factorization
 * Name:  Mingcheng Zhu
 * Email: zhumc11@gmail.com
 * Date:  Mar 18, 2018
 */
#define ARGS 3
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define uSECtoSEC 1.0E-6
#define THRESH 1e-13
#define SCALE 100.0
#define TRUE 1
#define FALSE 0

// Macro to define index into a linear array for 2D indexing. Stored 
// row by row.
#define IDX(i,j,n) ((i*n)+j)

int thread_count;
/* return a clock value with usec precision */
double get_clock() {
  struct timeval tv;
  int status;
  status = gettimeofday(&tv, (void *) 0);
  if(status<0)
    printf("gettimeofday error");
  return (tv.tv_sec * 1.0 + tv.tv_usec * 1.0E-6);
}


  void
printMatrix(double *A, int N){
  int i,j;
  for(i = 0; i < N; i++){
    for(j = 0; j < N; j++)
      printf("%lf ", A[IDX(i,j,N)]);
    printf("\n");
  }
}


/* Multiply A*A^T.  
 * double *result - result matrix
 * double * A - source matrix
 * int N - size of matrix (N x N);
 * int lowerT - A is lower triangular
 */
int multT(double *result, double *A, int N, int lowerT){
  int i,j,k;
  bzero(result, N*N*sizeof(double));

  # pragma omp parallel for num_threads(thread_count) schedule(dynamic)
  for(i = 0; i < N; i++){
    /* Result is symmetric, just compute upper triangle */
    # pragma omp parallel for num_threads(thread_count) schedule(dynamic)
    for(j = i; j < N; j++) {
      double sum = 0.0;
      /* if A is lower Triangular don't multiply zeroes */
      # pragma omp parallel for num_threads(thread_count) \
        reduction(+: sum) schedule(static)
      for(k = 0; k < (!lowerT ? N : j+1) ; k++){
        sum += A[IDX(i,k,N)] * A[IDX(j,k,N)];
      }
      result[IDX(i,j,N)] = sum;
      result[IDX(j,i,N)] = sum; /*enforce symmetry */
    }
  }
  return 0;
}


/* Validate that A ~= L*L^T 
 * double * A -  NxN symmetric positive definite matrix
 * double * L -  NxN lower triangular matrix such that L*L^T ~= A
 * int N      -  size of matrices
 * thresh     -  threshold considered to be zero (1e-14 good number)
 *
 * Returns # of elements where the residual abs(A(i,j) - LL^T(i,j)) > thresh
 */
int validate(double *A, double * L, int N, double thresh){
  double *R = malloc(N*N * sizeof(double));
  multT(R,L,N,TRUE);
  int badcount = 0;
  int i,j;
  double rdiff; /* relative difference */
  # pragma omp parallel for num_threads(thread_count) schedule(static) collapse(2)
  for (i = 0 ; i < N; i++){
    for(j = 0; j < N; j ++){
      rdiff = fabs((R[IDX(i,j,N)] - A[IDX(i,j,N)])/A[IDX(i,j,N)]);
      if (rdiff > thresh){
        /*printf("(%d,%d):R(i,j):%f,A(i,j):%f (delta: %f)\n",
          i,j,R[IDX(i,j,N)],A[IDX(i,j,N)],
          rdiff);*/
        badcount++; 
      }
    }
  }
  free(R);
  return badcount;
}


/* Initialize the N X N  array with Random numbers
 * In such a way that the resulting matrix in Symmetric
 * Positive definite (hence Cholesky factorization is valid)
 * args:
 * 	int N - size of the array
 * 	int trueRandom  - nonzero, seed with time of day, 0 don't seed
 *	double **A - N x N double array, allocated by caller
 */
void init_array(int N, int trueRandom, double *A) {
  int i,j,k;
  struct drand48_data rbuf;
  if (trueRandom)
    srand48_r((long int) time(NULL),&rbuf);
  else
    srand48_r(1L,&rbuf);

  double *B = calloc(N * N, sizeof(double));

  //printf("Random number generation\n");
  # pragma omp parallel for num_threads(thread_count) schedule(static) collapse(2)
  for(i = 0; i < N; i++){
    for(j = 0; j < N; j++){
      drand48_r(&rbuf,&B[IDX(i,j,N)]);
      B[IDX(i,j,N)] *= SCALE;
    }
  }
  //printf("done random number generation\n");
  // printMatrix(B,N);

  /* Compute B*B^T to get symmetric, positive definite*/
  multT(A,B,N,0);
  free (B);
}


/* Compute the Cholesky Decomposition of A 
 * L - NxN result matrix, Lower Triangular L*L^T = A
 * A - NxN symmetric, positive definite matrix A
 * N - size of matrices;
 */
void cholesky(double *L, double *A, int N){
  int i,j,k;
  bzero(L,N*N*sizeof(double));
  double temp;
  # pragma omp parallel for num_threads(thread_count) schedule(dynamic) 
  for (i = 0; i < N; i++){
    for (j = 0; j < (i+1); j++) {
      temp = 0;
      /* Inner product of ith row of L, jth row of L */
      for (k = 0; k < j; k++)
        temp += L[IDX(i,k,N)] * L[IDX(j,k,N)];
      if (i == j)
        L[IDX(i,j,N)] = sqrt(A[IDX(i,i,N)] - temp);
      else {
        L[IDX(i,j,N)] = (A[IDX(i,j,N)] - temp)/ L[IDX(j,j,N)];
      }
    }
  }
}


int main(int argc, char* argv[]) {

  int n;
  int i,j,k;
  double ts, te; /* Starting time and ending time */
  double *A, *L;
  double temp;
  if(argc < ARGS || argc > ARGS){
    fprintf(stderr,"Wrong # of arguments.\nUsage: %s <N> <nthreads>\n",argv[0]);
    return -1;
  }
  n = atoi(argv[1]);
  thread_count = atoi(argv[2]);
  A = (double *)malloc(n*n*sizeof(double));
  L = (double *)calloc(n*n,sizeof(double));
  //printf("Initializing \n");

  ts=get_clock();
  init_array(n,0,A);
  te=get_clock();
  // printf("Initial matrix:\n");
  // printMatrix(A,n);
  printf(" %dX%d matrix with %d threads\n", n,n,thread_count);
  printf("intialize:  %f seconds\n", te - ts);

  /*printf("A is \n");
    for(i=0; i<n; i++){
    for(j=0; j<n; j++){
    printf("%15.8f ", A[IDX(i,j,n)]);
    }
    printf("\n");
    }*/

  /*Serial decomposition*/
  ts=get_clock();
  cholesky(L,A,n);
  te=get_clock();

  /*printf("L is \n");
    for(i=0; i<n; i++){
    for(j=0; j<n; j++){
    printf("%15.8f ", L[IDX(i,j,n)]);
    }
    printf("\n");
    }*/

  //printf("Decomposed matrix:\n");
  //printMatrix(L,n);
  printf("cholesky:   %f seconds\n", te - ts);
  ts=get_clock();
  int badcount = validate(A,L,n,THRESH);
  te=get_clock();
  printf("validation: %f seconds\n", te - ts);

  if (badcount == 0)
    printf("solution validates\n");
  else
    printf("solution is invalid, %d elements above threshold\n",badcount);
  free(A);
  free(L);
  return badcount;
}
