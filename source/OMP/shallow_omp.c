#include "shallow_OMP.h"

double interpolate_data(const data_t *data, double x, double y) {
    int i = (int)(x / data->dx);
    int j = (int)(y / data->dy);

    // Boundary cases
    if (i < 0 || j < 0 || i >= data->nx - 1 || j >= data->ny - 1) {
        i = (i < 0) ? 0 : (i >= data->nx) ? data->nx - 1 : i;
        j = (j < 0) ? 0 : (j >= data->ny) ? data->ny - 1 : j;
        return GET(data, i, j);
    }

    // Four positions surrounding (x,y)
    double x1 = i * data->dx;
    double x2 = (i + 1) * data->dx;
    double y1 = j * data->dy;
    double y2 = (j + 1) * data->dy;

    // Four values of data surrounding (i,j)
    double Q11 = GET(data, i, j);
    double Q12 = GET(data, i, j + 1);
    double Q21 = GET(data, i + 1, j);
    double Q22 = GET(data, i + 1, j + 1);

    // Weighted coef
    double wx = (x2 - x) / (x2 - x1);
    double wy = (y2 - y) / (y2 - y1);

    double val = wx * wy * Q11 +
                 (1 - wx) * wy * Q21 +
                 wx * (1 - wy) * Q12 +
                 (1 - wx) * (1 - wy) * Q22;

    return val;
}

void update_eta(int nx, int ny, const struct parameters param, all_data_t *all_data) {
    #pragma omp parallel for collapse(2)
    for(int i = 0; i < nx; i++) {
        for(int j = 0; j < ny; j++) {
            double h_ij = GET(all_data->h_interp, i, j);
            double c1 = param.dt * h_ij;
            double eta_ij = GET(all_data->eta, i, j)
                - c1 / param.dx * (GET(all_data->u, i + 1, j) - GET(all_data->u, i, j))
                - c1 / param.dy * (GET(all_data->v, i, j + 1) - GET(all_data->v, i, j));
            SET(all_data->eta, i, j, eta_ij);
        }
    }
}

void update_velocities(int nx, int ny, const struct parameters param, all_data_t *all_data) {
    #pragma omp parallel for collapse(2)
    for(int i = 0; i < nx; i++) {
        for(int j = 0; j < ny; j++) {
            double c1 = param.dt * param.g;
            double c2 = param.dt * param.gamma;
            double eta_ij = GET(all_data->eta, i, j);
            double eta_imj = GET(all_data->eta, (i == 0) ? 0 : i - 1, j);
            double eta_ijm = GET(all_data->eta, i, (j == 0) ? 0 : j - 1);
            double u_ij = (1. - c2) * GET(all_data->u, i, j)
                - c1 / param.dx * (eta_ij - eta_imj);
            double v_ij = (1. - c2) * GET(all_data->v, i, j)
                - c1 / param.dy * (eta_ij - eta_ijm);
            SET(all_data->u, i, j, u_ij);
            SET(all_data->v, i, j, v_ij);
        }
    }
}

void boundary_condition(int n, int nx, int ny, const struct parameters param, all_data_t *all_data) {
    double t = n * param.dt;
    if(param.source_type == 1) {
        // sinusoidal velocity on top boundary
        double A = 5;
        double f = 1. / 20.;
        #pragma omp parallel for collapse(2)
        for(int i = 0; i < nx; i++) {
            for(int j = 0; j < ny; j++) {
                SET(all_data->u, 0, j, 0.);
                SET(all_data->u, nx, j, 0.);
                SET(all_data->v, i, 0, 0.);
                SET(all_data->v, i, ny, A * sin(2 * M_PI * f * t));
            }
        }
    }
    else if(param.source_type == 2) {
        // sinusoidal elevation in the middle of the domain
        double A = 5;
        double f = 1. / 20.;
        SET(all_data->eta, nx / 2, ny / 2, A * sin(2 * M_PI * f * t));

        // Apply boundary condition on (eta, u, v) for each corners of the domain
        #pragma omp parallel for 
        for(int i = 0; i < nx; i++) {
            double h_bottom = GET(all_data->h_interp, i, 0);
            double h_top = GET(all_data->h_interp, i, ny-1);
            double c_bottom = sqrt(param.g * h_bottom);
            double c_top = sqrt(param.g * h_top);

            // Bottom corner
            SET(all_data->eta, i, 0, GET(all_data->eta, i, 1) - (c_bottom * param.dt / param.dy) * 
                (GET(all_data->eta, i, 1) - GET(all_data->eta, i, 0)));
            SET(all_data->u, i, 0, GET(all_data->u, i, 1) - (c_bottom * param.dt / param.dy) * 
                (GET(all_data->u, i, 1) - GET(all_data->u, i, 0)));
            SET(all_data->v, i, 0, GET(all_data->v, i, 1) - (c_bottom * param.dt / param.dy) * 
                (GET(all_data->v, i, 1) - GET(all_data->v, i, 0)));

            // Top corner
            SET(all_data->eta, i, ny-1, GET(all_data->eta, i, ny-2) - (c_top * param.dt / param.dy) * 
                (GET(all_data->eta, i, ny-1) - GET(all_data->eta, i, ny-2)));
            SET(all_data->u, i, ny-1, GET(all_data->u, i, ny-2) - (c_top * param.dt / param.dy) * 
                (GET(all_data->u, i, ny-1) - GET(all_data->u, i, ny-2)));
            SET(all_data->v, i, ny-1, GET(all_data->v, i, ny-2) - (c_top * param.dt / param.dy) * 
                (GET(all_data->v, i, ny-1) - GET(all_data->v, i, ny-2)));
        }

        #pragma omp parallel for 
        for(int j = 0; j < ny; j++) {
            double h_left = GET(all_data->h_interp, 0, j);
            double h_right = GET(all_data->h_interp, nx-1, j);
            double c_left = sqrt(param.g * h_left);
            double c_right = sqrt(param.g * h_right);

            // Left corner
            SET(all_data->eta, 0, j, GET(all_data->eta, 1, j) - (c_left * param.dt / param.dx) * 
                (GET(all_data->eta, 1, j) - GET(all_data->eta, 0, j)));
            SET(all_data->u, 0, j, GET(all_data->u, 1, j) - (c_left * param.dt / param.dx) * 
                (GET(all_data->u, 1, j) - GET(all_data->u, 0, j)));
            SET(all_data->v, 0, j, GET(all_data->v, 1, j) - (c_left * param.dt / param.dx) * 
                (GET(all_data->v, 1, j) - GET(all_data->v, 0, j)));

            // Right corner
            SET(all_data->eta, nx-1, j, GET(all_data->eta, nx-2, j) - (c_right * param.dt / param.dx) * 
                (GET(all_data->eta, nx-1, j) - GET(all_data->eta, nx-2, j)));
            SET(all_data->u, nx-1, j, GET(all_data->u, nx-2, j) - (c_right * param.dt / param.dx) * 
                (GET(all_data->u, nx-1, j) - GET(all_data->u, nx-2, j)));
            SET(all_data->v, nx-1, j, GET(all_data->v, nx-2, j) - (c_right * param.dt / param.dx) * 
                (GET(all_data->v, nx-1, j) - GET(all_data->v, nx-2, j)));
        }
    }
    else {
        printf("Error: Unknown source type %d\n", param.source_type);
        exit(0);
    }
}

void interp_bathy(int nx, int ny, const struct parameters param, all_data_t *all_data) {
    #pragma omp parallel for collapse(2)
    for(int i = 0; i < nx; i++) {
        for(int j = 0; j < ny; j++) {
            double x = i * param.dx;
            double y = j * param.dy;
            double val = interpolate_data(all_data->h, x, y);
            SET(all_data->h_interp, i, j, val);
        }
    }
}

int main(int argc, char **argv) {
    if(argc != 2) {
        printf("Usage: %s parameter_file\n", argv[0]);
        return 1;
    }

    // Initialize parameters and h
    struct parameters param;
    if(read_parameters(&param, argv[1])) return 1;
    print_parameters(&param);

    all_data_t all_data;
    all_data.h = malloc(sizeof(data_t));
    if(read_data(all_data.h, param.input_h_filename)) return 1;

    // Infer size of domain from input bathymetric data
    double hx = all_data.h->nx * all_data.h->dx;
    double hy = all_data.h->ny * all_data.h->dy;
    int nx = floor(hx / param.dx);
    int ny = floor(hy / param.dy);
    if(nx <= 0) nx = 1;
    if(ny <= 0) ny = 1;
    int nt = floor(param.max_t / param.dt);

    printf(" - grid size: %g m x %g m (%d x %d = %d grid points)\n",
           hx, hy, nx, ny, nx * ny);
    printf(" - number of time steps: %d\n", nt);

    // Initialize variables
    all_data.eta = malloc(sizeof(data_t));
    all_data.u = malloc(sizeof(data_t));
    all_data.v = malloc(sizeof(data_t));
    all_data.h_interp = malloc(sizeof(data_t));
    
    init_data(all_data.eta, nx, ny, param.dx, param.dy, 0.);
    init_data(all_data.u, nx + 1, ny, param.dx, param.dy, 0.);
    init_data(all_data.v, nx, ny + 1, param.dx, param.dy, 0.);
    init_data(all_data.h_interp, nx, ny, param.dx, param.dy, 0.);

    // Interpolate bathymetry
    interp_bathy(nx, ny, param, &all_data);

    double start = GET_TIME();

    // Loop over timestep
    for(int n = 0; n < nt; n++) {
        if(n && (n % (nt / 10)) == 0) {
            double time_sofar = GET_TIME() - start;
            double eta = (nt - n) * time_sofar / n;
            printf("Computing step %d/%d (ETA: %g seconds)     \r", n, nt, eta);
            fflush(stdout);
        }

        // output solution
        if(param.sampling_rate && !(n % param.sampling_rate)) {
            write_data_vtk(all_data.eta, "water elevation", param.output_eta_filename, n);
        }

        // impose boundary conditions
        boundary_condition(n, nx, ny, param, &all_data);

        // Update variables
        update_eta(nx, ny, param, &all_data);
        update_velocities(nx, ny, param, &all_data);
    }

    write_manifest_vtk(param.output_eta_filename, param.dt, nt, param.sampling_rate);

    double time = GET_TIME() - start;
    printf("\nDone: %g seconds (%g MUpdates/s)\n", time,
           1e-6 * (double)all_data.eta->nx * (double)all_data.eta->ny * (double)nt / time);

    // Free memory
    free_data(all_data.h_interp);
    free_data(all_data.eta);
    free_data(all_data.u);
    free_data(all_data.v);
    free_data(all_data.h);
    
    free(all_data.h_interp);
    free(all_data.eta);
    free(all_data.u);
    free(all_data.v);
    free(all_data.h);

    return 0;
}