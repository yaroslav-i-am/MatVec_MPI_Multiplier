#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_FILENAME_LENGTH 128
#define MAIN_PROCESS 0
#define STR_DEFAULT_LENGTH 128
#define SUBMATR_TAG 15
#define SUBVEC_TAG 25
#define N_DIVIDERS 2

void process_error(int err){
    if (err != MPI_SUCCESS){
        printf("Error %d\n", err);

        char* str = malloc(STR_DEFAULT_LENGTH * sizeof(char));
        int str_len; 

        MPI_Error_string(err, str, &str_len);

        printf("Error: %s, length: %d\n", str, str_len);

        free(str);
    }
}


void get_2_most_closest_dividers(long int number, int* dividers){
    int sroot = (int) sqrt((double) number);
    for (int cur_div = sroot; cur_div > 0; --cur_div){
        if (number % cur_div == 0){
            dividers[0] = cur_div;
            dividers[1] = number / cur_div;

            return;
        }
    }
    return;
}


void build_matrix_filename(long int n_rows, long int n_cols, char* filename){
    sprintf(filename, "matrix_%ld_%ld.txt", n_rows, n_cols);
    return;
}

void build_vector_filename(long int n_elems, char* filename){
    sprintf(filename, "vector_%ld.txt", n_elems);
    return;
}


void print_vec(double* vec, long int n_elems, int proc_num){
    printf("%d\tVECTOR:\n", proc_num);
    for (long int i = 0; i < n_elems; ++i){
        printf("%d\t%lf\n", proc_num, vec[i]);
    }
    return;
}


void print_matr(double* matr, long int n_rows, long int n_cols, int proc_num){
    printf("%d\tMATRIX (%ld x %ld):\n", proc_num, n_rows, n_cols);
    for (long int i = 0; i < n_rows; ++i){
        for (long int j = 0; j < n_cols; ++j){
            printf("%lf ", matr[i * n_cols + j]);
        }   
        printf("\n");
    }
    return;
}


int load_matr(long int n_rows, long int n_cols, double* matrix){
    char* body = (char*) malloc(MAX_FILENAME_LENGTH * sizeof(char));
    build_matrix_filename(n_rows, n_cols, body);
    char filename[MAX_FILENAME_LENGTH] = "./data/";
    strcat(filename, body);
    free(body);
    printf("Reading matrix from file '%s'...\n", filename);

    FILE *fp = fopen(filename, "r");
    if (fp == NULL){
        return -1;
    }

    for (long int i = 0; i < n_rows; ++i){
        for (long int j = 0; j < n_cols; ++j){
            fscanf(fp, "%lf", &matrix[i * n_cols + j]);
        }   
    }

    return 0;
}


int load_vec(long int n_rows,  double* vector){
    char* body = (char*) malloc(MAX_FILENAME_LENGTH * sizeof(char));
    build_vector_filename(n_rows, body);
    char filename[MAX_FILENAME_LENGTH] = "./data/";
    strcat(filename, body);
    // printf("Before free\n");
    free(body);
    printf("Reading vector from file '%s'...\n", filename);

    FILE *fp = fopen(filename, "r");
    if (fp == NULL){ 
        return -1;
    }

    for (long int i = 0; i < n_rows; ++i){
        fscanf(fp, "%lf", &vector[i]); 
    }

    return 0;
}


void multiply_rowwise(double* matrix, double* vector, long int n_rows, long int n_cols, double* result){
    for (long int i = 0; i < n_rows; ++i){
        double sum = 0;
        for (long int j = 0; j < n_cols; ++j){
            sum += matrix[i * n_cols + j] * vector[j]; 
        }
        result[i] = sum;
    }

    return;
}



void distribute_data(double* matrix, double* vector, int comm_sz_rows, int comm_sz_cols, long int n_rows, long int n_cols, int my_rank, MPI_Comm comm, double* local_matr, double* local_vec){

    int local_n_rows = n_rows / comm_sz_rows;
    int local_n_cols = n_cols / comm_sz_cols;

    MPI_Datatype mpi_vec;

    MPI_Type_vector(
        local_n_rows,
        local_n_cols,
        n_cols,
        MPI_DOUBLE,
        &mpi_vec
    );

    MPI_Type_commit(&mpi_vec);

    int error;
    
    // printf("BEFORE TRANSPORTATION\n");

    if (my_rank == MAIN_PROCESS){

        // double* packed_matrix = (double*) malloc(n_rows * local_n * sizeof(double));

        int position;

        int pack_size;
        MPI_Pack_size(1, mpi_vec, MPI_COMM_WORLD, &pack_size);

        for (int i = 0; i < comm_sz_rows; ++i){
            for (int j = 0; j < comm_sz_cols; ++j){
                if (i == 0 && j == 0)
                    continue;

                position = 0;

                // printf("Packing (%d)...\n", i);
                error = MPI_Pack(
                    &matrix[i * local_n_rows * n_cols + j * local_n_cols],
                    1,
                    mpi_vec,
                    local_matr,
                    pack_size,
                    &position,
                    MPI_COMM_WORLD
                );
                process_error(error);

                // printf("Sending (%d)...\n", i);
                error = MPI_Send(
                    local_matr,
                    local_n_rows * local_n_cols,
                    MPI_DOUBLE,
                    i * comm_sz_cols + j,
                    SUBMATR_TAG,
                    MPI_COMM_WORLD
                );
                process_error(error);


                error = MPI_Send(
                    &vector[j * local_n_cols],
                    local_n_cols,
                    MPI_DOUBLE,
                    i * comm_sz_cols + j,
                    SUBVEC_TAG,
                    MPI_COMM_WORLD
                );
                process_error(error);


            }
        }

        position = 0;

        error = MPI_Pack(
            &matrix[0],
            1,
            mpi_vec,
            local_matr,
            pack_size,
            &position,
            MPI_COMM_WORLD
        );
        process_error(error);

        // print_matr(local_matr, local_n_rows, local_n_cols, my_rank);

        memcpy(local_vec, &vector[0], local_n_cols * sizeof(double));

    }
    else {
        error = MPI_Recv( 
            &local_matr[0],
            local_n_rows * local_n_cols, 
            MPI_DOUBLE,
            MAIN_PROCESS,
            SUBMATR_TAG, 
            MPI_COMM_WORLD, 
            MPI_STATUS_IGNORE
        );
        process_error(error);

        error = MPI_Recv( 
            &local_vec[0],
            local_n_cols, 
            MPI_DOUBLE,
            MAIN_PROCESS,
            SUBVEC_TAG, 
            MPI_COMM_WORLD, 
            MPI_STATUS_IGNORE
        );
        process_error(error);
    }

    // printf("AFTER TRANSPORTATION\n");
    

    error = MPI_Type_free(&mpi_vec);
    process_error(error);

    return;
}


void gather_local_results(double* local_res, int comm_sz_rows, int comm_sz_cols, long int n_rows, long int n_cols, int my_rank, MPI_Comm comm, double* result){
    int local_n_rows = n_rows / comm_sz_rows;
    // int local_n_cols = n_cols / comm_sz_cols;
    int comm_sz = comm_sz_rows * comm_sz_cols;


    if (my_rank == MAIN_PROCESS){
        for (long int i = 0; i < n_rows; ++i){
            result[i] = 0.0;
        }
    }

    int error;

    // MPI_Barrier(MPI_COMM_WORLD);
    // printf("All here!");
    
    if (my_rank != MAIN_PROCESS){

        error = MPI_Send(
            local_res,
            local_n_rows,
            MPI_DOUBLE,
            MAIN_PROCESS,
            SUBVEC_TAG,
            MPI_COMM_WORLD
        );
        process_error(error);

    }
    else{
        MPI_Status status;

        double* recv_buf = (double*) malloc(local_n_rows * sizeof(double));

        for (int i = 0; i < comm_sz; ++i){
            int src;

            if (i != 0){
                error = MPI_Recv( 
                    &recv_buf[0],
                    local_n_rows, 
                    MPI_DOUBLE,
                    MPI_ANY_SOURCE,
                    SUBVEC_TAG, 
                    MPI_COMM_WORLD, 
                    &status
                );
                process_error(error);

                src = status.MPI_SOURCE;
            }
            else {
                memcpy(&recv_buf[0], local_res, local_n_rows * sizeof(double));
                src = 0;
            }

            // printf("src = %d\n", src);

            for (long int j = 0; j < local_n_rows; ++j){
                // printf("j = %ld\n", j);
                // printf("cur_index = %ld\n", (src / comm_sz_cols) * local_n_rows + j);
                result[(src / comm_sz_cols) * local_n_rows + j] += recv_buf[j];
            }
        }
    }
}



void multiply_blockwise(double* local_matr, double* nums, long int n_rows, long int local_n, int my_rank, int comm_sz, double* result){
    
    for (long int j = 0; j < local_n; ++j){
        for (long int i = 0; i < n_rows; ++i){
            local_matr[i * local_n + j] *= nums[j]; 
        }
    }

    long int n_cols = local_n * comm_sz;

    double* columns = (double*) malloc(n_cols * sizeof(double));
    for (long int i = 0; i < n_rows; ++i){
        double sum = 0.0;
        for (long int j = 0; j < local_n; ++j){
            sum += local_matr[i * local_n + j];
        }
        columns[i] = sum;
    } 
 

    MPI_Reduce(         // Должны вызывать все процессы
        columns,        // Буфер, который отсылаем
        result,         // Буфер ответа. Важен только тот, который идёт во 0-й поток
        n_rows,         // Количество чисел
        MPI_DOUBLE,     // 
        MPI_SUM,        // Как агрегируем
        MAIN_PROCESS,              // Куда отправляем результат
        MPI_COMM_WORLD
    );

    free(columns);


    // double* matrix = (double*) malloc(n_rows * n_cols * sizeof(double));

    // MPI_Gather(local_matr, n_rows * local_n, MPI_DOUBLE, matrix, n_rows * local_n, MPI_DOUBLE, MAIN_PROCESS, MPI_COMM_WORLD);

    // if (my_rank == MAIN_PROCESS)
    //     print_matr(matrix, n_rows, n_cols, -1);

    return;
}



int main(int argc, char** argv){
    long int n_rows = strtol(argv[1], NULL, 10);
    long int n_cols = strtol(argv[2], NULL, 10);

    int comm_sz, comm_sz_rows, comm_sz_cols;
    int my_rank;
    int error;

    double start, finish, elapsed = 0;

    MPI_Init(NULL, NULL);

    MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    unsigned long long all_count = n_rows * n_cols;

    if (my_rank == MAIN_PROCESS){
        if (all_count % comm_sz != 0){
            printf("\nERROR!!!\n%lld mod %d = %lld. Unable to parallellize task.\n", all_count, comm_sz, all_count % comm_sz);
            return 0;
        }        
    }

    int* layout = malloc(N_DIVIDERS * sizeof(long int));
    get_2_most_closest_dividers(comm_sz, layout);
    comm_sz_rows = layout[0];
    comm_sz_cols = layout[1];
    free(layout);

    int local_n_rows = n_rows / comm_sz_rows;
    int local_n_cols = n_cols / comm_sz_cols;

    double* matrix;
    double* vector;
    double* result;


    if (my_rank == MAIN_PROCESS){
        printf("n_rows = %ld\n", n_rows);
        printf("n_cols = %ld\n", n_cols);
        printf("comm_sz = %d\n", comm_sz);
        printf("my_rank = %d\n", my_rank);
        printf("comm_sz_rows = %d\n", comm_sz_rows);
        printf("comm_sz_cols = %d\n", comm_sz_cols);
        printf("local_n_rows = %d\n", local_n_rows);
        printf("local_n_cols = %d\n", local_n_cols);

        matrix = (double*) malloc(n_rows * n_cols * sizeof(double));
        vector = (double*) malloc(n_cols * sizeof(double));
        result = (double*) malloc(n_rows * sizeof(double));


        if (my_rank == MAIN_PROCESS){
            error = load_matr(n_rows, n_cols, matrix);
            if (error == -1){
                char* filename = (char*) malloc(MAX_FILENAME_LENGTH * sizeof(char));
                build_matrix_filename(n_rows, n_cols, filename);
                printf("Unable to locate matrix file '%s'\n", filename);
                free(filename);
                return 0;
            }
            
            // print_matr(matrix, n_rows, n_cols, -1);
        }

        if (my_rank == MAIN_PROCESS){
            error = load_vec(n_cols, vector);
            if (error == -1){
                char* filename = (char*) malloc(MAX_FILENAME_LENGTH * sizeof(char));
                build_vector_filename(n_cols, filename);
                printf("Unable to locate vector file '%s'\n", filename);
                free(filename);
                return 0;
            }
            
            // print_vec(vector, n_cols, -1);
        }
    }


    double* local_matr = (double*) malloc(local_n_rows * local_n_cols * sizeof(double));
    double* local_vec = (double*) malloc(local_n_cols * sizeof(double));
    double* local_res = (double*) malloc(local_n_rows * sizeof(double));  


    MPI_Barrier(MPI_COMM_WORLD);
    start = MPI_Wtime();
    
    distribute_data(matrix, vector, comm_sz_rows, comm_sz_cols, n_rows, 
                    n_cols, my_rank, MPI_COMM_WORLD, local_matr, local_vec);
    multiply_rowwise(local_matr, local_vec, local_n_rows, local_n_cols, local_res);
    gather_local_results(local_res, comm_sz_rows, comm_sz_cols, n_rows, n_cols, my_rank, MPI_COMM_WORLD, result);

    MPI_Barrier(MPI_COMM_WORLD);
    finish = MPI_Wtime();
    double local_elapsed = finish - start;
    MPI_Reduce(&local_elapsed, &elapsed, 1, MPI_DOUBLE, MPI_MAX, MAIN_PROCESS, MPI_COMM_WORLD);

    if (my_rank == MAIN_PROCESS){
        printf("RESULT:\n");
        print_vec(result, n_rows, -1);
        printf("Elapsed time: %f s\n", elapsed);
    }


    free(local_matr);
    free(local_vec);
    free(local_res);
    if (my_rank == MAIN_PROCESS){
        free(result);
        free(vector);
        free(matrix);
    }

    MPI_Finalize();

    return 0;
}
