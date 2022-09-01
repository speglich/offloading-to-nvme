#define _POSIX_C_SOURCE 200809L
#define START_TIMER(S) struct timeval start_ ## S , end_ ## S ; gettimeofday(&start_ ## S , NULL);
#define STOP_TIMER(S,T) gettimeofday(&end_ ## S, NULL); T->S += (double)(end_ ## S .tv_sec-start_ ## S.tv_sec)+(double)(end_ ## S .tv_usec-start_ ## S .tv_usec)/1000000;

#include "stdlib.h"
#include "math.h"
#include "sys/time.h"
#include "xmmintrin.h"
#include "pmmintrin.h"
#include "omp.h"
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "zfp.h"

struct dataobj
{
  void *restrict data;
  int * size;
  int * npsize;
  int * dsize;
  int * hsize;
  int * hofs;
  int * oofs;
} ;

struct profiler
{
  double section0;
  double section1;
  double section2;
} ;

void open_thread_files(int *files, int nthreads, int ndisks)
{

  for(int i=0; i < nthreads; i++)
  {
    int nvme_id = i % ndisks;
    char name[100];

    sprintf(name, "data/nvme%d/thread_%d.data", nvme_id, i);
    printf("Reading file %s\n", name);

    if ((files[i] = open(name, O_RDONLY,
        S_IRUSR)) == -1)
    {
        perror("Cannot open output file\n"); exit(1);
    }
  }

}

void open_compressed_thread_files(int *files, int nthreads, int ndisks)
{

  for(int i=0; i < nthreads; i++)
  {
    int nvme_id = i % ndisks;
    char name[100];

    sprintf(name, "data/nvme%d/compressed_thread_%d.data", nvme_id, i);
    printf("Reading file %s\n", name);

    if ((files[i] = open(name, O_RDONLY,
        S_IRUSR)) == -1)
    {
        perror("Cannot open output file\n"); exit(1);
    }
  }

}

int Gradient(struct dataobj *restrict damp_vec, const float dt, struct dataobj *restrict grad_vec, const float o_x, const float o_y, struct dataobj *restrict rec_vec, struct dataobj *restrict rec_coords_vec, struct dataobj *restrict u_vec, struct dataobj *restrict v_vec, struct dataobj *restrict vp_vec, const int x_M, const int x_m, const int y_M, const int y_m, const int p_rec_M, const int p_rec_m, const int time_M, const int time_m, const int nthreads, const int nthreads_nonaffine, struct profiler * timers)
{
  float (*restrict damp)[damp_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[damp_vec->size[1]]) damp_vec->data;
  float (*restrict grad)[grad_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[grad_vec->size[1]]) grad_vec->data;
  float (*restrict rec)[rec_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[rec_vec->size[1]]) rec_vec->data;
  float (*restrict rec_coords)[rec_coords_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[rec_coords_vec->size[1]]) rec_coords_vec->data;
  float (*restrict u)[u_vec->size[1]][u_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[u_vec->size[1]][u_vec->size[2]]) u_vec->data;
  float (*restrict v)[v_vec->size[1]][v_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[v_vec->size[1]][v_vec->size[2]]) v_vec->data;
  float (*restrict vp)[vp_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[vp_vec->size[1]]) vp_vec->data;

  /* Flush denormal numbers to zero in hardware */
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);

  float r0 = 1.0F/(dt*dt);
  float r1 = 1.0F/dt;

  double read_time = 0.0;
  double file_time = 0.0;

  struct timeval start, end;
  gettimeofday(&start, NULL);
  printf("Using nthreads %d\n", nthreads);

  int *files = malloc(nthreads * sizeof(int));

  if (files == NULL)
  {
      printf("Error to alloc\n");
      exit(1);
  }

  open_thread_files(files, nthreads, 8);

  int *counters = malloc(nthreads * sizeof(int));

  if (counters == NULL)
  {
      printf("Error to alloc\n");
      exit(1);
  }

  for(int i=0; i < nthreads; i++){
    counters[i] = 1;
  }


  int *compressed_files = malloc(nthreads * sizeof(int));

  if (files == NULL)
  {
      printf("Error to alloc\n");
      exit(1);
  }

  open_compressed_thread_files(compressed_files, nthreads, 8);

  size_t *compressed_offset = malloc(nthreads * sizeof(size_t));

  if (compressed_offset == NULL)
  {
      printf("Error to alloc\n");
      exit(1);
  }

  for(int i=0; i < nthreads; i++){
    compressed_offset[i] = 0;
  }

  gettimeofday(&end, NULL);
  file_time += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

  size_t u_size = u_vec->size[2]*sizeof(float);
  for (int time = time_M, t0 = (time)%(3), t1 = (time + 2)%(3), t2 = (time + 1)%(3); time >= time_m; time -= 1, t0 = (time)%(3), t1 = (time + 2)%(3), t2 = (time + 1)%(3))
  {
    /* Begin section0 */
    START_TIMER(section0)
    #pragma omp parallel num_threads(nthreads)
    {
      #pragma omp for collapse(1) schedule(dynamic,1)
      for (int x = x_m; x <= x_M; x += 1)
      {
        #pragma omp simd aligned(damp,v,vp:64)
        for (int y = y_m; y <= y_M; y += 1)
        {
          float r6 = 1.0F/(vp[x + 4][y + 4]*vp[x + 4][y + 4]);
          v[t1][x + 4][y + 4] = (r1*damp[x + 1][y + 1]*v[t0][x + 4][y + 4] + r6*(-r0*(-2.0F*v[t0][x + 4][y + 4]) - r0*v[t2][x + 4][y + 4]) + 8.33333315e-4F*(-v[t0][x + 2][y + 4] - v[t0][x + 4][y + 2] - v[t0][x + 4][y + 6] - v[t0][x + 6][y + 4]) + 1.3333333e-2F*(v[t0][x + 3][y + 4] + v[t0][x + 4][y + 3] + v[t0][x + 4][y + 5] + v[t0][x + 5][y + 4]) - 4.99999989e-2F*v[t0][x + 4][y + 4])/(r0*r6 + r1*damp[x + 1][y + 1]);
        }
      }
    }
    STOP_TIMER(section0,timers)
    /* End section0 */

    /* Begin section1 */
    START_TIMER(section1)
    #pragma omp parallel num_threads(nthreads_nonaffine)
    {
      int chunk_size = (int)(fmax(1, (1.0F/3.0F)*(p_rec_M - p_rec_m + 1)/nthreads_nonaffine));
      #pragma omp for collapse(1) schedule(dynamic,chunk_size)
      for (int p_rec = p_rec_m; p_rec <= p_rec_M; p_rec += 1)
      {
        float posx = -o_x + rec_coords[p_rec][0];
        float posy = -o_y + rec_coords[p_rec][1];
        int ii_rec_0 = (int)(floor(1.0e-1F*posx));
        int ii_rec_1 = (int)(floor(1.0e-1F*posy));
        int ii_rec_2 = 1 + (int)(floor(1.0e-1F*posy));
        int ii_rec_3 = 1 + (int)(floor(1.0e-1F*posx));
        float px = (float)(posx - 1.0e+1F*(int)(floor(1.0e-1F*posx)));
        float py = (float)(posy - 1.0e+1F*(int)(floor(1.0e-1F*posy)));
        if (ii_rec_0 >= x_m - 1 && ii_rec_1 >= y_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_1 <= y_M + 1)
        {
          float r2 = (dt*dt)*(vp[ii_rec_0 + 4][ii_rec_1 + 4]*vp[ii_rec_0 + 4][ii_rec_1 + 4])*(1.0e-2F*px*py - 1.0e-1F*px - 1.0e-1F*py + 1)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 4][ii_rec_1 + 4] += r2;
        }
        if (ii_rec_0 >= x_m - 1 && ii_rec_2 >= y_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_2 <= y_M + 1)
        {
          float r3 = (dt*dt)*(vp[ii_rec_0 + 4][ii_rec_2 + 4]*vp[ii_rec_0 + 4][ii_rec_2 + 4])*(-1.0e-2F*px*py + 1.0e-1F*py)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 4][ii_rec_2 + 4] += r3;
        }
        if (ii_rec_1 >= y_m - 1 && ii_rec_3 >= x_m - 1 && ii_rec_1 <= y_M + 1 && ii_rec_3 <= x_M + 1)
        {
          float r4 = (dt*dt)*(vp[ii_rec_3 + 4][ii_rec_1 + 4]*vp[ii_rec_3 + 4][ii_rec_1 + 4])*(-1.0e-2F*px*py + 1.0e-1F*px)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_3 + 4][ii_rec_1 + 4] += r4;
        }
        if (ii_rec_2 >= y_m - 1 && ii_rec_3 >= x_m - 1 && ii_rec_2 <= y_M + 1 && ii_rec_3 <= x_M + 1)
        {
          float r5 = 1.0e-2F*px*py*(dt*dt)*(vp[ii_rec_3 + 4][ii_rec_2 + 4]*vp[ii_rec_3 + 4][ii_rec_2 + 4])*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_3 + 4][ii_rec_2 + 4] += r5;
        }
      }
    }
    STOP_TIMER(section1,timers)
    /* End section1 */

    struct timeval start, end;
    gettimeofday(&start, NULL);

    #pragma omp parallel for schedule(static,1) num_threads(nthreads)
    for(int i= u_vec->size[1]-1;i>=0;i--)
    {
      int tid = i%nthreads;

      int compression = 1;

      if(compression ==  0) {
        off_t offset = counters[tid] * u_size;
        lseek(files[tid], -1 * offset, SEEK_END);

        int ret = read(files[tid], u[time][i], u_size);

        if (ret != u_size) {
            printf("%d", ret);
            perror("Cannot open output file");
            exit(1);
        }

        counters[tid]++;
      } else {

      zfp_type type = zfp_type_float;
      zfp_field* field = zfp_field_1d(u[t2][i], type, u_vec->size[2]);

      zfp_stream* zfp = zfp_stream_open(NULL);

      zfp_stream_set_rate(zfp, 0.5, type, zfp_field_dimensionality(field), zfp_false);

      size_t bufsize = zfp_stream_maximum_size(zfp, field);
      void* buffer  = malloc(bufsize);

      bitstream* stream = stream_open(buffer, bufsize);

      zfp_stream_set_bit_stream(zfp, stream);
      zfp_stream_rewind(zfp);

      compressed_offset[tid] += bufsize;
      lseek(compressed_files[tid], -1 * compressed_offset[tid], SEEK_END);

      int ret = read(compressed_files[tid], buffer, bufsize);

      if (ret != bufsize) {
          printf("%d", ret);
          perror("Cannot open output file");
          exit(1);
      }

      if (!zfp_decompress(zfp, field)) {
        printf("decompression failed\n");
        exit(1);
      }

      zfp_field_free(field);
      zfp_stream_close(zfp);
      stream_close(stream);
      free(buffer);

      }

    }

    gettimeofday(&end, NULL);
    read_time += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

    /* Begin section2 */
    START_TIMER(section2)
    #pragma omp parallel num_threads(nthreads)
    {
      #pragma omp for collapse(1) schedule(static,1)
      for (int x = x_m; x <= x_M; x += 1)
      {
        #pragma omp simd aligned(grad,u,v:64)
        for (int y = y_m; y <= y_M; y += 1)
        {
          grad[x + 1][y + 1] += -(r0*(-2.0F*v[t0][x + 4][y + 4]) + r0*v[t1][x + 4][y + 4] + r0*v[t2][x + 4][y + 4])*u[time][x + 4][y + 4];
        }
      }
    }
    STOP_TIMER(section2,timers)
    /* End section2 */
  }

  gettimeofday(&start, NULL);
  for(int i=0; i < nthreads; i++){
    close(files[i]);
  }

  for(int i=0; i < nthreads; i++){
    close(compressed_files[i]);
  }

  gettimeofday(&end, NULL);

  file_time += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

  printf("Time to read all timesteps: %f\n", read_time);
  printf("Time to manipulate all files: %f\n", file_time);

  printf("time_M %d, time_m %d\n", time_M, time_m);

  return 0;
}
/* Backdoor edit at Thu Sep  1 13:59:53 2022*/
/* Backdoor edit at Thu Sep  1 14:00:16 2022*/
/* Backdoor edit at Thu Sep  1 14:02:18 2022*/
/* Backdoor edit at Thu Sep  1 14:02:35 2022*/
/* Backdoor edit at Thu Sep  1 14:13:23 2022*/
/* Backdoor edit at Thu Sep  1 14:14:57 2022*/
/* Backdoor edit at Thu Sep  1 14:17:13 2022*/ 
