#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef USE_MPI
#include <mpi.h>
#endif

namespace {
using Matrix = std::vector<std::int32_t>;

#ifdef USE_MPI
#ifdef _OPENMP
constexpr auto mode = "hybrid";
#else
constexpr auto mode = "mpi";
#endif
#elif defined(_OPENMP)
constexpr auto mode = "openmp";
#else
constexpr auto mode = "sequential";
#endif

int parse_int(std::string_view text, int limit, const char *name) {
    int value = 0;
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < 1 ||
        value > limit) {
        throw std::runtime_error(std::string(name) + " must be in [1, " +
                                 std::to_string(limit) + "]");
    }
    return value;
}

void initialize(Matrix &a, Matrix &bt, int n) {
    std::uint32_t state = 2026;
    const auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<std::int32_t>(state % 19) - 9;
    };

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            a[i * n + j] = next();
            bt[j * n + i] = next();
        }
    }
}

int multiply(const Matrix &a, const Matrix &bt, Matrix &c, int n, int rows) {
    int threads = 1;
#ifdef _OPENMP
#pragma omp parallel default(none) shared(a, bt, c, n, rows, threads)
#endif
    {
#ifdef _OPENMP
#pragma omp single nowait
        threads = omp_get_num_threads();
#pragma omp for schedule(static)
#endif
        for (int i = 0; i < rows; ++i) {
            const auto *left = a.data() + i * n;
            auto *output = c.data() + i * n;
            for (int j = 0; j < n; ++j) {
                const auto *right = bt.data() + j * n;
                std::int32_t sum = 0;
                for (int k = 0; k < n; ++k) {
                    sum += left[k] * right[k];
                }
                output[j] = sum;
            }
        }
    }
    return threads;
}

Matrix read_reference(const char *path, int elements) {
    Matrix reference(elements);
    std::ifstream file(path, std::ios::binary);
    if (!file.read(reinterpret_cast<char *>(reference.data()),
                   elements * sizeof(reference[0])) ||
        file.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error(
            std::string("Cannot read an exact-size reference: ") + path);
    }
    return reference;
}

void write_reference(const char *path, const Matrix &output) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(output.data()),
               output.size() * sizeof(output[0]));
    file.close();
    if (!file) {
        throw std::runtime_error(std::string("Cannot write reference: ") +
                                 path);
    }
}

void compare(const Matrix &reference, const Matrix &output, int n) {
    const auto difference =
        std::mismatch(reference.begin(), reference.end(), output.begin());
    if (difference.first != reference.end()) {
        const auto index = difference.first - reference.begin();
        throw std::runtime_error("Result mismatch at row " +
                                 std::to_string(index / n) + ", column " +
                                 std::to_string(index % n) + ": expected " +
                                 std::to_string(*difference.first) + ", got " +
                                 std::to_string(*difference.second));
    }
}

int run(int argc, char **argv, int rank, int ranks) {
    if (argc != 3 && argc != 5) {
        throw std::runtime_error("Usage: matmul N REPEATS "
                                 "[--read-reference|--write-reference FILE]");
    }
    const int n = parse_int(argv[1], 4096, "N");
    const int repeats = parse_int(argv[2], 100, "REPEATS");
    if (ranks > n) {
        throw std::runtime_error("Each MPI rank must receive at least one row");
    }
    const std::string_view reference_option = argc == 5 ? argv[3] : "";
    const bool read = reference_option == "--read-reference";
    const bool write = reference_option == "--write-reference";
    if (argc == 5 &&
        (!read && (!write || std::string_view(mode) != "sequential"))) {
        throw std::runtime_error(
            "Only the sequential program can write a reference");
    }

    const int elements = n * n;
    Matrix bt(elements);
    Matrix a, output, reference;
    if (rank == 0) {
        a.resize(elements);
        output.resize(elements);
        initialize(a, bt, n);
        if (read) {
            reference = read_reference(argv[4], elements);
        }
    }

#ifdef _OPENMP
    omp_set_dynamic(0);
#endif
#ifdef USE_MPI
    const int rows_per_rank = n / ranks;
    const int extra_rows = n % ranks;
    std::vector<int> counts(ranks), offsets(ranks);
    for (int r = 0; r < ranks; ++r) {
        counts[r] = (rows_per_rank + (r < extra_rows)) * n;
        offsets[r] = (r * rows_per_rank + std::min(r, extra_rows)) * n;
    }
    Matrix local_a(counts[rank]), local_c(counts[rank]);
    char hostname[MPI_MAX_PROCESSOR_NAME]{};
    int hostname_length = 0;
    MPI_Get_processor_name(hostname, &hostname_length);
#endif

    // The first complete execution warms up the runtime and buffers.
    for (int sample = 0; sample <= repeats; ++sample) {
        double seconds;
        int threads;
#ifdef USE_MPI
        MPI_Barrier(MPI_COMM_WORLD);
        const double start = MPI_Wtime();
        MPI_Bcast(bt.data(), elements, MPI_INT32_T, 0, MPI_COMM_WORLD);
        MPI_Scatterv(a.data(), counts.data(), offsets.data(), MPI_INT32_T,
                     local_a.data(), counts[rank], MPI_INT32_T, 0,
                     MPI_COMM_WORLD);
        threads = multiply(local_a, bt, local_c, n, counts[rank] / n);
        MPI_Gatherv(local_c.data(), counts[rank], MPI_INT32_T, output.data(),
                    counts.data(), offsets.data(), MPI_INT32_T, 0,
                    MPI_COMM_WORLD);
        const double elapsed = MPI_Wtime() - start;
        MPI_Reduce(&elapsed, &seconds, 1, MPI_DOUBLE, MPI_MAX, 0,
                   MPI_COMM_WORLD);
#else
        const auto start = std::chrono::steady_clock::now();
        threads = multiply(a, bt, output, n, n);
        seconds = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - start)
                      .count();
#endif

        if (sample == 0) {
            std::ostringstream placement;
            placement << mode << " n=" << n << " rank=" << rank << '/' << ranks
                      << " threads=" << threads;
#ifdef USE_MPI
            placement << " host="
                      << std::string_view(hostname, hostname_length);
#endif
            placement << '\n';
            std::cerr << placement.str();
        }
        if (rank != 0) {
            continue;
        }
        if (sample == 0 && !read) {
            reference = output;
        } else {
            compare(reference, output, n);
        }
        if (sample == 0) {
            if (write) {
                write_reference(argv[4], output);
            }
        } else {
            std::cout << mode << ',' << n << ',' << ranks << ',' << threads
                      << ',' << sample << ',' << std::fixed
                      << std::setprecision(9) << seconds << ','
                      << (read || write ? "exact" : "repeat") << '\n';
        }
    }
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    int rank = 0;
    int ranks = 1;
#ifdef USE_MPI
    int provided = MPI_THREAD_SINGLE;
#ifdef _OPENMP
    constexpr int required = MPI_THREAD_FUNNELED;
#else
    constexpr int required = MPI_THREAD_SINGLE;
#endif
    MPI_Init_thread(&argc, &argv, required, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
#endif
    int status = 0;
    try {
#ifdef USE_MPI
        if (provided < required) {
            throw std::runtime_error("Insufficient MPI thread support");
        }
#endif
        status = run(argc, argv, rank, ranks);
    } catch (const std::exception &error) {
        std::cerr << "Rank " << rank << ": " << error.what() << '\n';
        status = 1;
#ifdef USE_MPI
        MPI_Abort(MPI_COMM_WORLD, status);
#endif
    }
#ifdef USE_MPI
    MPI_Finalize();
#endif
    return status;
}
